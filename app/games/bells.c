/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Bells: change ringing. The tower rings a method's plain course, from rounds back to rounds; you take one bell on
 * the keys (any key) and are scored on your striking, how evenly you keep your place among the others, as real bands
 * are judged. Or you listen (bell LISTEN), the blue line drawn as it rings.
 *
 * The screen (mockups/ring-screen-v2.html, layout A): the bells hang in their frame across the top, mouth up, and
 * swing a full circle as they ring, one way at handstroke and back at backstroke; under yours a countdown of pips to
 * your next blow, then NOW; the blue line scrolls underneath, your bell's path drawn through it.
 *
 * Knobs: K1 the method, K2 your bell, K3 the pace. PLAY starts and stands the bells. Part of the unity build. */
#include "game.h"

#define ROUNDS_BEFORE 4                  /* rows of rounds before "go" */
#define ROUNDS_AFTER 2                   /* after "that's all" */
#define ROWS_MAX (ROUNDS_BEFORE + 232 + ROUNDS_AFTER)
#define PACE_MIN 16                      /* the gap between blows, x 10 ms */
#define PACE_MAX 32
#define VEL_TOWER 90                     /* the tower's bells; yours louder (~6 dB) and brighter: you hear it */
#define VEL_MINE 127

static struct {
    uint8_t rows[ROWS_MAX][RING_MAX];    /* the whole touch, worked out at the start */
    int nrows, bells, stage;
    volatile int ringing;                /* 1: the tower is ringing */
    volatile uint32_t t0;                /* plat_ms of the first blow */
    volatile int next;                   /* the tick's next blow: row * bells + place */
    int32_t gap;                         /* ms */
    int mine;                            /* your bell, 0-based; -1 listening */
    /* your striking */
    int my_next;                         /* your next blow to be judged: row index */
    int32_t err_sum, blows, clashes;     /* clashes: pulls far too soon */
    int score;                           /* the last touch's, % (-1 none yet) */
    uint32_t pressed_ms;                 /* the last time you pulled */
    volatile uint32_t struck_ms[RING_MAX];   /* each bell's last blow (for the swing), and on which stroke */
    volatile uint8_t struck_back[RING_MAX];
    uint32_t done_ms;                    /* the touch ended (the score shown) */
} bl;

static int bell_note(int b, int bells)   /* the ring's notes: the top of a major scale, the tenor its tonic */
{
    static const int8_t SIX[6] = {9, 7, 5, 4, 2, 0}, EIGHT[8] = {12, 11, 9, 7, 5, 4, 2, 0};
    return bells > 6 ? 60 + EIGHT[b] : 62 + SIX[b];
}

static const ring_method_t *method(void) { return &RING_METHODS[sav.bells.method % RING_NMETHODS]; }

static void method_name(char *t)         /* "PLAIN BOB MINOR" */
{
    static const char *const STAGE[] = {"", "", "", "SINGLES", "MINIMUS", "DOUBLES", "MINOR", "TRIPLES", "MAJOR"};
    const ring_method_t *m = method();
    t[0] = 0;
    text_cat(t, m->name);
    text_cat(t, m->stage == 6 && m->name[0] == 'C' ? " S " : m->stage == 8 && m->name[0] == 'C' ? " S " :
                m->name[0] == 'K' ? " TB " : " ");
    text_cat(t, STAGE[m->stage]);
}

static void touch_build(void)            /* rounds, the plain course, rounds; your bell */
{
    ring_t r;
    int i;
    ring_start(&r, method());
    bl.bells = r.bells;
    bl.stage = r.stage;
    bl.mine = sav.bells.bell && sav.bells.bell <= bl.bells ? sav.bells.bell - 1 : -1;
    bl.nrows = 0;
    for (i = 0; i < ROUNDS_BEFORE; i++)
        memcpy(bl.rows[bl.nrows++], r.row, RING_MAX);
    do {
        ring_next(&r);
        memcpy(bl.rows[bl.nrows++], r.row, RING_MAX);
    } while (!r.round && bl.nrows < ROWS_MAX - ROUNDS_AFTER);
    for (i = 0; i < ROUNDS_AFTER; i++)
        memcpy(bl.rows[bl.nrows++], r.row, RING_MAX);
}

static int32_t blow_time(int row, int place) { return ring_time(row, place, bl.bells, bl.gap); }

static int my_place(int row)
{
    int p;
    for (p = 0; p < bl.bells; p++)
        if (bl.rows[row][p] == bl.mine)
            return p;
    return 0;
}

static void bells_stand(void)
{
    bl.ringing = 0;
    if (bl.blows) {
        bl.score = (int)(100 - (100 * bl.err_sum + bl.blows * (bl.gap / 4)) / (bl.blows * (bl.gap / 2)));
        bl.score = bl.score < 0 ? 0 : bl.score;
        if (bl.score > sav.bells.best[sav.bells.method % NMETHODS_MAX] || !sav.bells.best[sav.bells.method % NMETHODS_MAX]) {
            sav.bells.best[sav.bells.method % NMETHODS_MAX] = (uint8_t)(bl.score ? bl.score : 1);
            save_dirty();
        }
        bl.done_ms = plat_ms();
    }
}

static void bells_go(void)
{
    int i;
    touch_build();
    bl.gap = (int32_t)clamp(sav.bells.pace, PACE_MIN, PACE_MAX) * 10;
    bl.my_next = 0;
    bl.err_sum = bl.blows = bl.clashes = 0;
    bl.done_ms = 0;
    snd_lock();
    for (i = 0; i < RING_MAX; i++) {
        bl.struck_ms[i] = 0;
        bl.struck_back[i] = 0;
        bw_bell_pan(i, i == bl.mine ? 0 : -48 + 96 * i / (bl.bells - 1));   /* the treble on the left, yours
                                                                         * in the middle */
    }
    snd_unlock();
    bl.next = 0;
    bl.t0 = plat_ms() + 600u;
    bl.ringing = 1;
    app_say("LOOK TO", "TREBLE'S GOING");
}

/* the tick: the tower's bells, on time (yours too, while listening) */
static void bells_tick(uint32_t ms)
{
    int32_t t;
    if (!bl.ringing || (int32_t)(ms - bl.t0) < 0)
        return;
    t = (int32_t)(ms - bl.t0);
    while (bl.ringing && bl.next < bl.nrows * bl.bells) {
        int row = bl.next / bl.bells, place = bl.next % bl.bells, b;
        if (blow_time(row, place) > t)
            break;
        b = bl.rows[row][place];
        if (b != bl.mine) {
            bw_bell(b, bell_note(b, bl.bells), VEL_TOWER);
            bl.struck_ms[b] = ms;
            bl.struck_back[b] = (uint8_t)(row & 1);
        }
        bl.next++;
    }
}

/* your blows: a pull is judged against your next blow that is due; a blow passed by a gap with no pull is a miss */
static void judge_misses(uint32_t now)
{
    int32_t t = (int32_t)(now - bl.t0);
    while (bl.mine >= 0 && bl.my_next < bl.nrows && blow_time(bl.my_next, my_place(bl.my_next)) + bl.gap < t) {
        bl.err_sum += bl.gap / 2;        /* a miss: as bad as it gets */
        bl.blows++;
        bl.my_next++;
    }
}

static void ring_mine(uint32_t ms, int back)   /* your bell sounds, and swings */
{
    int b = bl.mine >= 0 ? bl.mine : bl.bells - 1;   /* listening: the keys ring the tenor */
    snd_lock();
    bw_bell_bright(b, bell_note(b, bl.bells), VEL_MINE);
    snd_unlock();
    bl.struck_ms[b] = ms;
    bl.struck_back[b] = (uint8_t)back;
    bl.pressed_ms = ms;
}

/* a pull: your bell always sounds. Near your blow (within a gap) it is that blow, judged on how far off it was; far
 * too soon it is a clash, struck in the wrong place: a blow as bad as a miss, and the one due is still to come */
static void bells_key(int k, int down, uint32_t ms)
{
    int32_t t, d;
    (void)k;
    if (!down)
        return;
    if (!bl.ringing) {                   /* standing: hear your bell (the tenor, listening) */
        ring_mine(ms, bl.struck_back[bl.mine >= 0 ? bl.mine : bl.bells - 1] ^ 1);
        return;
    }
    if (bl.mine < 0)                     /* listening while the tower rings: the keys are quiet */
        return;
    judge_misses(ms);
    if (bl.my_next >= bl.nrows) {
        ring_mine(ms, 0);
        return;
    }
    t = (int32_t)(ms - bl.t0);
    d = t - blow_time(bl.my_next, my_place(bl.my_next));
    ring_mine(ms, bl.my_next & 1);
    if (d < -bl.gap) {
        bl.err_sum += bl.gap / 2;
        bl.blows++;
        bl.clashes++;
        return;
    }
    d = d < 0 ? -d : d;
    bl.err_sum += d > bl.gap / 2 ? bl.gap / 2 : d;
    bl.blows++;
    bl.my_next++;
}

static void bells_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (bl.ringing) {
        bells_stand();
        snd_lock();
        bw_bells_damp();
        snd_unlock();
        if (bl.score >= 0 && bl.blows) {
            char t[16] = "STRUCK ";
            sp_pct(t, bl.score);
            app_say("STAND", t);
        } else {
            app_say("STAND", "");
        }
    } else {
        bells_go();
    }
}

static void bells_enc(int role, int steps)
{
    char t[24];
    if (bl.ringing)
        return;
    if (role == EN_K1) {
        sav.bells.method = (uint8_t)clamp((int32_t)sav.bells.method + steps, 0, RING_NMETHODS - 1);
        if (sav.bells.bell > method()->stage + (method()->stage & 1))
            sav.bells.bell = (uint8_t)(method()->stage + (method()->stage & 1));
        method_name(t);
        app_readout("METHOD", t);
    } else if (role == EN_K2) {
        sav.bells.bell = (uint8_t)clamp((int32_t)sav.bells.bell + steps, 0, method()->stage);
        t[0] = 0;
        if (sav.bells.bell)
            int_text(sav.bells.bell, t);   /* LISTENING alone when 0 */
        app_readout(sav.bells.bell ? "BELL" : "LISTENING", t);
    } else if (role == EN_K3) {
        sav.bells.pace = (uint8_t)clamp((int32_t)sav.bells.pace + steps, PACE_MIN, PACE_MAX);
        int_text(sav.bells.pace * 10, t);
        text_cat(t, " MS");
        app_readout("PACE", t);
    } else {
        return;
    }
    bl.score = -1;
    touch_build();
    save_dirty();
}

static void bells_enter(void)
{
    bl.ringing = 0;
    bl.score = -1;
    touch_build();
    bl.gap = (int32_t)clamp(sav.bells.pace, PACE_MIN, PACE_MAX) * 10;
}

static void bells_leave(void)
{
    if (bl.ringing)
        bells_stand();
    snd_lock();
    bw_bells_damp();
    snd_unlock();
}

/* the swing: from the stroke's balance (mouth up, just past the top), through the bottom, to the other balance */
static float swing(int b, uint32_t now)
{
    const float BAL = 2.97f;             /* 170 degrees */
    float side = bl.struck_back[b] ? -1.0f : 1.0f, p, e;
    if (!bl.struck_ms[b])
        return BAL;
    p = (float)(now - bl.struck_ms[b]) / ((float)bl.gap * 3.2f);
    p = p > 1.0f ? 1.0f : p;
    e = p < 0.5f ? 2.0f * p * p : 1.0f - 2.0f * (1.0f - p) * (1.0f - p);
    return side * (-BAL + 2.0f * BAL * e);
}

static void bells_draw(uint32_t now)
{
    int32_t y0, cx0 = 22, dx = bl.bells > 6 ? 28 : 39, t = bl.ringing ? (int32_t)(now - bl.t0) : -1;
    int cur = 0, i, b, row = 0, place = 0, n_until = 0;
    char s[32];
    if (bl.ringing && t >= 0) {          /* where the ringing is: the last blow struck */
        cur = bl.next > 0 ? bl.next - 1 : 0;
        row = cur / bl.bells;
        place = cur % bl.bells;
        if (bl.mine >= 0) {
            int k = cur + 1;
            for (; k < bl.nrows * bl.bells && bl.rows[k / bl.bells][k % bl.bells] != bl.mine; k++)
                n_until++;
            n_until++;
        }
    }
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, G_PAPER);
        /* the header: the method, and the stroke */
        cv_rect(0, 0, 240, 18, G_SKY0);
        method_name(s);
        cv_text(6, 3, &FONT_XS, s, G_INK);
        if (bl.ringing)
            cv_text(234 - text_w(&FONT_XS, row & 1 ? "BACK" : "HAND"), 3, &FONT_XS, row & 1 ? "BACK" : "HAND", G_INK);
        /* the frame and its bells */
        cv_rect(0, 18, 240, 70, G_PAPER2);
        cv_rect(10, 60, 220, 3, G_WHEEL);
        for (b = 0; b < bl.bells; b++) {
            int32_t cx = cx0 + b * dx;
            int me = b == bl.mine, hit = bl.struck_ms[b] && now - bl.struck_ms[b] < 150u;
            s[0] = (char)('1' + b);
            s[1] = 0;
            draw_bell(cx, 60, swing(b, now), bl.bells > 6 ? 0.8f : 1.0f, me ? G_RED : G_PLANK, me ? G_RED2 : G_PLANK2, hit);
            text_mid(cx, 70, &FONT_XS, s, me ? G_RED : G_PLANK2);
        }
        /* your countdown */
        if (bl.ringing && bl.mine >= 0) {
            int32_t mx = cx0 + bl.mine * dx;
            if ((!bl.pressed_ms || now - bl.pressed_ms >= 180u) && n_until <= 1 && t >= 0)
                text_mid(mx, 90, &FONT_XS, "NOW", G_RED);
            else if (!bl.pressed_ms || now - bl.pressed_ms >= 180u) {
                int np = n_until > 6 ? 6 : n_until;
                for (i = 0; i < np; i++)
                    fill_circle(mx - (np - 1) * 3 + i * 6, 96, 2, i == 0 && n_until <= 2 ? G_RED : G_PLANK);
            }
        }
        /* the blue line: rows scroll up, the row ringing at y 134. The rows go in pairs, handstroke and backstroke,
         * a space and a dotted rule between pairs where the handstroke gap falls; the scrolling keeps time with the
         * ringing, gap and all */
        if (bl.ringing || bl.score >= 0) {
            const int32_t rh = 13, gp = 6, bx0 = 120 - (bl.bells - 1) * 23 / 2, yc = 134;   /* two rows past, six to
                                                                                          * come */
            int32_t r, scroll, lastx = -1, lasty = 0;
#define ROW_Y(r) ((r) * rh + ((r) / 2) * gp)
            {                            /* how far the line has scrolled: from this row toward the next, by time */
                int32_t ta = blow_time(row, 0), tb = blow_time(row + 1, 0), f = 0;
                if (bl.ringing && t >= 0 && tb > ta)
                    f = clamp((t - ta) * 256 / (tb - ta), 0, 256);
                else if (!bl.ringing)
                    f = 256 * place / bl.bells;
                scroll = ROW_Y(row) + (ROW_Y(row + 1) - ROW_Y(row)) * f / 256;
            }
            for (r = row - 5; r <= row + 6; r++) {
                int32_t y = yc + ROW_Y(r) - scroll, p;
                if (r < 0 || r >= bl.nrows || y < 104 || y > 214)
                    continue;
                if (!(r & 1) && r > 0)    /* the handstroke gap, before this pair */
                    for (p = bx0 - 10; p < bx0 + (bl.bells - 1) * 23 + 10; p += 4)
                        cv_rect(p, y - 2 - gp / 2 - 1, 2, 1, G_MUTE);
                if (r == row)
                    cv_rect(bx0 - 10, y - 2, (bl.bells - 1) * 23 + 20, rh, G_BLUSH);
                for (p = 0; p < bl.bells; p++) {
                    int bb = bl.rows[r][p], done = r < row || (r == row && p <= place);
                    int32_t x = bx0 + p * 23;
                    s[0] = (char)('1' + bb);
                    s[1] = 0;
                    if (bb == bl.mine || (bl.mine < 0 && bb == 0)) {   /* the line: yours, or the treble's */
                        if (lastx >= 0)
                            cv_line(lastx, lasty, x, y + 5, G_TEAL);
                        lastx = x;
                        lasty = y + 5;
                    }
                    text_mid(x, y, &FONT_XS, s, bb == bl.mine ? G_RED : done ? G_INK : G_MUTE);
                }
            }
#undef ROW_Y
        } else {                         /* standing: the settings */
            static const char *const LAB[3] = {"K1 METHOD", "K2 BELL", "K3 PACE"};
            for (i = 0; i < 3; i++) {
                cv_text(14, 112 + i * 26, &FONT_XS, LAB[i], G_PLANK2);
                s[0] = 0;
                if (i == 0)
                    method_name(s);
                else if (i == 1) {
                    if (sav.bells.bell)
                        int_text(sav.bells.bell, s);
                    else
                        text_cat(s, "LISTEN");
                } else {
                    int_text(sav.bells.pace * 10, s);
                    text_cat(s, " MS");
                }
                cv_text(94, 110 + i * 26, &FONT_B, s, G_INK);
            }
            cv_text(14, 190, &FONT_XS, "PICK A BELL WITH K2, THEN PRESS ANY", G_PLANK2);
            cv_text(14, 204, &FONT_XS, "KEY EACH TIME IT IS DUE (THE PIPS).", G_PLANK2);
        }
        /* the foot: striking */
        cv_rect(0, 220, 240, 20, G_LILAC);
        if (bl.ringing && bl.mine >= 0 && bl.blows) {
            int sc = (int)(100 - (100 * bl.err_sum + bl.blows * (bl.gap / 4)) / (bl.blows * (bl.gap / 2)));
            sc = sc < 0 ? 0 : sc;
            cv_text(8, 224, &FONT_XS, "STRIKING", G_INK);
            round_rect(84, 227, 70, 6, 3, G_PAPER);
            cv_rect(84, 227, 70 * sc / 100, 6, G_TEAL);
            int_text(sc, s);
            text_cat(s, "%");
            cv_text(232 - text_w(&FONT_XS, s), 224, &FONT_XS, s, G_INK);
        } else if (bl.ringing) {
            cv_text(8, 224, &FONT_XS, bl.mine >= 0 ? "ANY KEY: YOUR BELL" : "LISTENING", G_INK);
            cv_text(232 - text_w(&FONT_XS, "PLAY: STAND"), 224, &FONT_XS, "PLAY: STAND", G_INK);
        } else {
            if (bl.score >= 0) {
                text_cat((s[0] = 0, s), "STRUCK ");
                int_text(bl.score, s + 7);
                text_cat(s, "%");
                cv_text(8, 224, &FONT_XS, s, G_RED);
            } else if (sav.bells.best[sav.bells.method % NMETHODS_MAX]) {
                text_cat((s[0] = 0, s), "BEST ");
                int_text(sav.bells.best[sav.bells.method % NMETHODS_MAX], s + 5);
                text_cat(s, "%");
                cv_text(8, 224, &FONT_XS, s, G_INK);
            }
            cv_text(232 - text_w(&FONT_XS, "PLAY: RING"), 224, &FONT_XS, "PLAY: RING", G_INK);
        }
        band_send(y0);
    }
}

static void bells_frame(uint32_t now)
{
    if (bl.ringing) {
        judge_misses(now);
        if (bl.next >= bl.nrows * bl.bells && (int32_t)(now - bl.t0) > blow_time(bl.nrows, 0)) {
            bells_stand();               /* that's all */
            if (bl.blows) {
                char t[16] = "STRUCK ";
                sp_pct(t, bl.score);
                app_say("THAT'S ALL", t);
            } else {
                app_say("THAT'S ALL", "");
            }
        }
    }
    bells_draw(now);
}

static void bells_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    static const uint16_t COL[6] = {RGB(242, 120, 72), RGB(38, 166, 154), RGB(232, 88, 128), RGB(242, 177, 52),
                                    RGB(126, 160, 232), RGB(184, 224, 126)};
    int i;
    for (i = 0; i < 6; i++) {            /* six bells hopping in turn: rounds */
        uint32_t ph = (ms + (uint32_t)(5 - i) * 400u) % 2400u;
        int32_t up = ph < 1200u ? (int32_t)ph * (h / 4) / 1200 : (int32_t)(2400u - ph) * (h / 4) / 1200;
        fill_circle(x + w * (2 * i + 1) / 12, y + h / 2 + h / 8 - up, w > 50 ? 3 : 2, COL[i]);
    }
}

static void bells_best(char *t)
{
    int i, best = 0;
    for (i = 0; i < RING_NMETHODS && i < NMETHODS_MAX; i++)
        best = sav.bells.best[i] > best ? sav.bells.best[i] : best;
    t[0] = 0;
    if (best) {
        text_cat(t, "BEST ");
        int_text(best, t + 5);
        text_cat(t, "%");
    }
}

/* the keys: the black ones are the places in a row, 1 to 6 (or 8), left to right; your place in the row you ring
 * next is lit, in the row after it at the mid level: your path, on the keys. On your blow every white key lights */
static uint32_t bells_keys_lit(uint32_t *mid)
{
    static const uint8_t BK[11] = PLAT_BLACK_KEYS, WK[16] = PLAT_WHITE_KEYS;
    uint32_t now = plat_ms(), keys = 0;
    int32_t t = (int32_t)(now - bl.t0);
    int i;
    *mid = 0;
    if (!bl.ringing || bl.mine < 0 || bl.my_next >= bl.nrows || t < -600)
        return 0;
    keys = 1u << BK[my_place(bl.my_next)];
    if (bl.my_next + 1 < bl.nrows)
        *mid = 1u << BK[my_place(bl.my_next + 1)];
    if (blow_time(bl.my_next, my_place(bl.my_next)) - t < 80)
        for (i = 0; i < 16; i++)
            keys |= 1u << WK[i];
    return keys;
}

static void bells_where(char *t)         /* the screen, for a screen reader */
{
    char m[32];
    method_name(m);
    t[0] = 0;
    if (bl.ringing) {
        text_cat(t, "BELLS. RINGING ");
        text_cat(t, m);
        if (bl.mine >= 0) {
            text_cat(t, ". YOUR BELL ");
            sp_num(t, bl.mine + 1);
            text_cat(t, ", ANY KEY RINGS IT");
            if (bl.blows) {
                int sc = (int)(100 - (100 * bl.err_sum + bl.blows * (bl.gap / 4)) / (bl.blows * (bl.gap / 2)));
                text_cat(t, ". STRIKING ");
                sp_pct(t, sc < 0 ? 0 : sc);
            }
        } else {
            text_cat(t, ". LISTENING");
        }
        text_cat(t, ". PLAY STANDS.");
        return;
    }
    text_cat(t, "BELLS. KNOB 1, METHOD ");
    text_cat(t, m);
    text_cat(t, ". KNOB 2, ");
    if (sav.bells.bell) {
        text_cat(t, "BELL ");
        sp_num(t, sav.bells.bell);
    } else {
        text_cat(t, "LISTENING");
    }
    text_cat(t, ". KNOB 3, PACE ");
    sp_num(t, sav.bells.pace * 10);
    text_cat(t, " MS. ");
    if (bl.score >= 0) {
        text_cat(t, "STRUCK ");
        sp_pct(t, bl.score);
        text_cat(t, ". ");
    } else if (sav.bells.best[sav.bells.method % NMETHODS_MAX]) {
        text_cat(t, "BEST ");
        sp_pct(t, sav.bells.best[sav.bells.method % NMETHODS_MAX]);
        text_cat(t, ". ");
    }
    text_cat(t, sav.bells.bell ? "ANY KEY RINGS YOUR BELL ON ITS TURN. PLAY RINGS." : "PLAY RINGS.");
}

/* the attract tune: rounds on six, then a pull of Plain Bob (214365), on chimes */
static const uint8_t BELLS_TUNE[] = {83, 81, 79, 77, 76, 74, 0, 83, 81, 79, 77, 76, 74, 0,
                                     81, 83, 77, 79, 74, 76, 0, 81, 77, 83, 74, 79, 76, 0};

static const game_t GAME_BELLS = {"BELLS", RGB(232, 80, 91), bells_enter, bells_leave, bells_frame, bells_key,
                                  bells_button, bells_enc, bells_tick, bells_attract, bells_best, bells_keys_lit,
                                  BELLS_TUNE, sizeof BELLS_TUNE, 260, BW_CHIME, bells_where, 1};
