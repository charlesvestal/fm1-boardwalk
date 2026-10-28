/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Bells: change ringing, three ways (K4):
 *
 * TOWER: the tower rings a method's plain course, from rounds back to rounds; you take one bell on the keys (any key)
 * and are scored on your striking, how evenly you keep your place among the others, as real bands are judged. Or you
 * listen (no bell), the blue line drawn as it rings.
 *
 * HANDBELLS: you ring a pair, as handbell ringers do: the left half of the keyboard the lower bell, the right half
 * the higher.
 *
 * CONDUCTING: the tower rings Plain Bob and you call it: BOB (the left half of the keys) or SINGLE (the right half)
 * at a lead end changes the bells' order, and the touch must come round at the length you are set ("round in 3
 * leads"). A call made during a lead is heard at its end; the bells are worked out a few rows ahead, so call in time.
 *
 * The screen (mockups/ring-screen-v2.html, layout A): the bells hang in their frame across the top, mouth up, and
 * swing a full circle as they ring, one way at handstroke and back at backstroke; under yours a countdown of pips to
 * your next blow, then NOW; the blue line scrolls underneath in handstroke and backstroke pairs, your bell's path
 * drawn through it.
 *
 * Knobs: K1 the method, K2 your bell (or pair), K3 the pace, K4 the way. PLAY starts and stands the bells. Part of
 * the unity build. */
#include "game.h"

#define ROUNDS_BEFORE 4                  /* rows of rounds before "go" */
#define ROUNDS_AFTER 2                   /* after "that's all" */
#define ROWS_MAX (ROUNDS_BEFORE + 232 + ROUNDS_AFTER)
#define PACE_MIN 16                      /* the gap between blows, x 10 ms */
#define PACE_MAX 32
#define VEL_TOWER 90                     /* the tower's bells; yours louder (~6 dB) and brighter: you hear it */
#define VEL_MINE 127
#define CALL_AHEAD 3                     /* rows worked out ahead of the ringing, conducting */
enum { BM_TOWER, BM_HAND, BM_CONDUCT, BM_N };
enum { BR_NONE, BR_ROUND, BR_EARLY, BR_LOST };   /* how a conducted touch ended */

static struct {
    uint8_t rows[ROWS_MAX][RING_MAX];    /* the touch: all of it at the start, or as it is called */
    volatile int nrows;
    int bells, stage, mode;
    volatile int ringing;                /* 1: the tower is ringing */
    volatile uint32_t t0;                /* plat_ms of the first blow */
    volatile int next;                   /* the tick's next blow: row * bells + place */
    int32_t gap;                         /* ms */
    int mine[2];                         /* your bells, 0-based (a pair with handbells); -1 none */
    /* your striking */
    int my_next[2];                      /* each bell's next blow to be judged: row index */
    int32_t err_sum, blows, clashes;     /* clashes: pulls far too soon */
    int score;                           /* the last touch's, % (-1 none yet) */
    uint32_t pressed_ms[2];              /* the last time you pulled each */
    volatile uint32_t struck_ms[RING_MAX];   /* each bell's last blow (for the swing), and on which stroke */
    volatile uint8_t struck_back[RING_MAX];
    uint32_t done_ms;                    /* the touch ended (the score shown) */
    /* conducting */
    ring_t ring;                         /* the method as rung so far */
    ring_change_t calls[3];              /* the lead end: plain, bob, single */
    volatile int call, call_said;        /* the call made for this lead's end; said yet */
    int leads, target, complete, result;
    uint8_t hint[RING_TOUCH_MAX];        /* a way to the target: the calls, lead by lead */
} bl;

static int bell_note(int b, int bells)   /* the ring's notes: the top of a major scale, the tenor its tonic */
{
    static const int8_t SIX[6] = {9, 7, 5, 4, 2, 0}, EIGHT[8] = {12, 11, 9, 7, 5, 4, 2, 0};
    return bells > 6 ? 60 + EIGHT[b] : 62 + SIX[b];
}

static const ring_method_t *method(void) { return &RING_METHODS[sav.bells.method % RING_NMETHODS]; }
static int is_mine(int b) { return b >= 0 && (b == bl.mine[0] || b == bl.mine[1]); }
static const char *const MODE_NAME[BM_N] = {"TOWER", "HANDBELLS", "CONDUCTING"};
static const char *const CALL_NAME[3] = {"PLAIN", "BOB", "SINGLE"};

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

static int bells_of(const ring_method_t *m) { return m->stage + (m->stage & 1); }

static void pair_text(int pair, char *t) /* "3 AND 4" */
{
    t[0] = 0;
    int_text(2 * pair - 1, t);
    text_cat(t, " AND ");
    int_text(2 * pair, t + text_len(t));
}

static void touch_build(void)            /* rounds, the plain course, rounds; your bells (conducting: rounds, then as
                                          * it is called) */
{
    ring_t r;
    int i;
    ring_start(&r, method());
    bl.bells = r.bells;
    bl.stage = r.stage;
    bl.mode = sav.bells.mode % BM_N;
    bl.mine[0] = bl.mine[1] = -1;
    if (bl.mode == BM_TOWER && sav.bells.bell && sav.bells.bell <= bl.bells)
        bl.mine[0] = sav.bells.bell - 1;
    else if (bl.mode == BM_HAND && sav.bells.bell && 2 * sav.bells.bell <= bl.bells) {
        bl.mine[0] = 2 * sav.bells.bell - 2;
        bl.mine[1] = 2 * sav.bells.bell - 1;
    }
    bl.nrows = 0;
    for (i = 0; i < ROUNDS_BEFORE; i++)
        memcpy(bl.rows[bl.nrows++], r.row, RING_MAX);
    if (bl.mode == BM_CONDUCT) {
        bl.ring = r;
        ring_pb_calls(method(), bl.calls);
        bl.call = RING_PLAIN;
        bl.call_said = 1;
        bl.leads = 0;
        bl.complete = 0;
        bl.result = BR_NONE;
        return;
    }
    do {
        ring_next(&r);
        memcpy(bl.rows[bl.nrows++], r.row, RING_MAX);
    } while (!r.round && bl.nrows < ROWS_MAX - ROUNDS_AFTER);
    for (i = 0; i < ROUNDS_AFTER; i++)
        memcpy(bl.rows[bl.nrows++], r.row, RING_MAX);
    bl.complete = 1;
}

/* conducting: the rows worked out up to row upto, the call made going into each lead end; the touch ends when it
 * comes round (at its length, or not), or when it passes its length without */
static void conduct_extend(int upto)
{
    ring_t *r = &bl.ring;
    int i;
    while (!bl.complete && bl.nrows <= upto && bl.nrows < ROWS_MAX - ROUNDS_AFTER) {
        if (r->at % r->nlead == r->nlead - 1) {   /* the lead end: the call goes in */
            ring_change_t keep = r->lead[r->nlead - 1];
            r->lead[r->nlead - 1] = bl.calls[bl.call];
            ring_next(r);
            r->lead[r->nlead - 1] = keep;
            bl.call = RING_PLAIN;
            bl.leads++;
            memcpy(bl.rows[bl.nrows++], r->row, RING_MAX);
            if (r->round || bl.leads >= bl.target + 2) {
                bl.result = !r->round ? BR_LOST : bl.leads == bl.target ? BR_ROUND : BR_EARLY;
                if (r->round)
                    for (i = 0; i < ROUNDS_AFTER; i++)
                        memcpy(bl.rows[bl.nrows++], r->row, RING_MAX);
                bl.complete = 1;
            }
        } else {
            ring_next(r);
            memcpy(bl.rows[bl.nrows++], r->row, RING_MAX);
        }
    }
}

static int32_t blow_time(int row, int place) { return ring_time(row, place, bl.bells, bl.gap); }

static int row_place(int row, int b)
{
    int p;
    for (p = 0; p < bl.bells; p++)
        if (bl.rows[row][p] == b)
            return p;
    return 0;
}

static int32_t striking(void)
{
    int sc = bl.blows ? (int)(100 - (100 * bl.err_sum + bl.blows * (bl.gap / 4)) / (bl.blows * (bl.gap / 2))) : 0;
    return sc < 0 ? 0 : sc;
}

static void bells_stand(void)
{
    bl.ringing = 0;
    if (bl.blows) {
        bl.score = striking();
        if (bl.score > sav.bells.best[sav.bells.method % NMETHODS_MAX] || !sav.bells.best[sav.bells.method % NMETHODS_MAX]) {
            sav.bells.best[sav.bells.method % NMETHODS_MAX] = (uint8_t)(bl.score ? bl.score : 1);
            save_dirty();
        }
        bl.done_ms = plat_ms();
    }
}

static void conduct_target(void)         /* a length to bring it round at, and a way there (the hint after) */
{
    uint8_t found[RING_TOUCH_MAX + 1], calls[RING_TOUCH_MAX + 1][RING_TOUCH_MAX];
    int l, n = 0, pick;
    ring_pb_touches(method(), found, calls);
    for (l = 1; l <= RING_TOUCH_MAX; l++)
        n += found[l];
    pick = n ? (int)rnd((uint32_t)n) : 0;
    for (l = 1; l <= RING_TOUCH_MAX; l++)
        if (found[l] && pick-- == 0) {
            bl.target = l;
            memcpy(bl.hint, calls[l], RING_TOUCH_MAX);
        }
}

static void bells_go(void)
{
    int i;
    char t[32];
    rnd_state ^= plat_ms() * 2654435761u;
    touch_build();
    bl.gap = (int32_t)clamp(sav.bells.pace, PACE_MIN, PACE_MAX) * 10;
    bl.my_next[0] = bl.my_next[1] = 0;
    bl.err_sum = bl.blows = bl.clashes = 0;
    bl.done_ms = 0;
    if (bl.mode == BM_CONDUCT) {
        conduct_target();
        conduct_extend(CALL_AHEAD);
    }
    snd_lock();
    for (i = 0; i < RING_MAX; i++) {
        bl.struck_ms[i] = 0;
        bl.struck_back[i] = 0;
        bw_bell_pan(i, is_mine(i) && bl.mine[1] < 0 ? 0 : -48 + 96 * i / (bl.bells - 1));   /* the treble on the
                                                                                     * left; one bell of yours in
                                                                                     * the middle */
    }
    snd_unlock();
    bl.next = 0;
    bl.t0 = plat_ms() + 600u;
    bl.ringing = 1;
    if (bl.mode == BM_CONDUCT) {
        t[0] = 0;
        int_text(bl.target, t);
        text_cat(t, bl.target == 1 ? " LEAD" : " LEADS");
        app_say("LOOK TO. BRING THEM ROUND IN", t);
    } else {
        app_say("LOOK TO", "TREBLE'S GOING");
    }
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
        if (!is_mine(b)) {
            bw_bell(b, bell_note(b, bl.bells), VEL_TOWER);
            bl.struck_ms[b] = ms;
            bl.struck_back[b] = (uint8_t)(row & 1);
        }
        bl.next++;
    }
}

/* your blows: a pull is judged against that bell's next blow that is due; a blow passed by a gap with no pull is a
 * miss */
static void judge_misses(uint32_t now)
{
    int32_t t = (int32_t)(now - bl.t0);
    int h;
    for (h = 0; h < 2; h++)
        while (bl.mine[h] >= 0 && bl.my_next[h] < bl.nrows &&
               blow_time(bl.my_next[h], row_place(bl.my_next[h], bl.mine[h])) + bl.gap < t) {
            bl.err_sum += bl.gap / 2;    /* a miss: as bad as it gets */
            bl.blows++;
            bl.my_next[h]++;
        }
}

static void ring_mine(int h, uint32_t ms, int back)   /* your bell sounds, and swings */
{
    int b = bl.mine[h] >= 0 ? bl.mine[h] : bl.bells - 1;   /* listening: the keys ring the tenor */
    snd_lock();
    bw_bell_bright(b, bell_note(b, bl.bells), VEL_MINE);
    snd_unlock();
    bl.struck_ms[b] = ms;
    bl.struck_back[b] = (uint8_t)back;
    bl.pressed_ms[h] = ms;
}

/* a pull: your bell always sounds (with handbells, the left half of the keys the first of the pair, the right half
 * the second). Near its blow (within a gap) it is that blow, judged on how far off it was; far too soon it is a
 * clash, struck in the wrong place: a blow as bad as a miss, and the one due is still to come. Conducting, the keys
 * call: the left half BOB, the right half SINGLE (again: taken back) */
static void bells_key(int k, int down, uint32_t ms)
{
    int32_t t, d;
    int h = bl.mode == BM_HAND && k >= 13 ? 1 : 0;
    if (!down)
        return;
    if (bl.mode == BM_CONDUCT) {
        if (bl.ringing && !bl.complete) {
            int c = k < 13 ? RING_BOB : RING_SINGLE;
            bl.call = bl.call == c ? RING_PLAIN : c;
            bl.call_said = 0;
        }
        return;
    }
    if (!bl.ringing) {                   /* standing: hear your bell (the tenor, listening) */
        int b = bl.mine[h] >= 0 ? bl.mine[h] : bl.bells - 1;
        ring_mine(h, ms, bl.struck_back[b] ^ 1);
        return;
    }
    if (bl.mine[h] < 0)                  /* listening while the tower rings: the keys are quiet */
        return;
    judge_misses(ms);
    if (bl.my_next[h] >= bl.nrows) {
        ring_mine(h, ms, 0);
        return;
    }
    t = (int32_t)(ms - bl.t0);
    d = t - blow_time(bl.my_next[h], row_place(bl.my_next[h], bl.mine[h]));
    ring_mine(h, ms, bl.my_next[h] & 1);
    if (d < -bl.gap) {
        bl.err_sum += bl.gap / 2;
        bl.blows++;
        bl.clashes++;
        return;
    }
    d = d < 0 ? -d : d;
    bl.err_sum += d > bl.gap / 2 ? bl.gap / 2 : d;
    bl.blows++;
    bl.my_next[h]++;
}

static void conduct_said(char *t)        /* how a conducted touch ended, and the way it could have gone */
{
    int i;
    t[0] = 0;
    if (bl.result == BR_ROUND) {
        text_cat(t, "ROUND IN ");
        sp_num(t, bl.leads);
        text_cat(t, bl.leads == 1 ? " LEAD. WELL CALLED." : " LEADS. WELL CALLED.");
        return;
    }
    if (bl.result == BR_EARLY) {
        text_cat(t, "ROUND IN ");
        sp_num(t, bl.leads);
        text_cat(t, " LEADS, NOT ");
        sp_num(t, bl.target);
        text_cat(t, ". ");
    } else {
        text_cat(t, "NOT ROUND IN ");
        sp_num(t, bl.target);
        text_cat(t, bl.target == 1 ? " LEAD. " : " LEADS. ");
    }
    for (i = 0; i < bl.target; i++) {    /* the hint */
        text_cat(t, CALL_NAME[bl.hint[i]]);
        text_cat(t, i + 1 < bl.target ? ", " : " WOULD DO IT.");
    }
}

static void bells_end(int stood)
{
    char t[96];
    bells_stand();
    if (bl.mode == BM_CONDUCT && bl.result != BR_NONE) {
        if (bl.result == BR_ROUND) {
            sav.touches++;
            save_dirty();
        }
        conduct_said(t);
        app_say(stood ? "STAND" : "THAT'S ALL", t);
    } else if (bl.blows) {
        text_cat((t[0] = 0, t), "STRUCK ");
        sp_pct(t, bl.score);
        app_say(stood ? "STAND" : "THAT'S ALL", t);
    } else {
        app_say(stood ? "STAND" : "THAT'S ALL", "");
    }
}

static void bells_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (bl.ringing) {
        if (bl.mode == BM_CONDUCT && bl.result == BR_NONE)
            bl.result = BR_LOST;
        bells_end(1);
        snd_lock();
        bw_bells_damp();
        snd_unlock();
    } else {
        bells_go();
    }
}

static int pb_method(int from, int dir)   /* conducting: the nearest Plain Bob method that way (or this one) */
{
    ring_change_t c[3];
    int i;
    for (i = from; i >= 0 && i < RING_NMETHODS; i += dir)
        if (ring_pb_calls(&RING_METHODS[i], c))
            return i;
    for (i = from; i >= 0 && i < RING_NMETHODS; i -= dir)
        if (ring_pb_calls(&RING_METHODS[i], c))
            return i;
    return 4;
}

static void bells_enc(int role, int steps)
{
    char t[24];
    int max;
    if (bl.ringing)
        return;
    if (role == EN_K1) {
        int m = clamp((int32_t)sav.bells.method + steps, 0, RING_NMETHODS - 1);
        if (sav.bells.mode == BM_CONDUCT)
            m = pb_method(m, steps > 0 ? 1 : -1);
        sav.bells.method = (uint8_t)m;
        method_name(t);
        app_readout("METHOD", t);
    } else if (role == EN_K2 && sav.bells.mode != BM_CONDUCT) {
        max = sav.bells.mode == BM_HAND ? bells_of(method()) / 2 : method()->stage;
        sav.bells.bell = (uint8_t)clamp((int32_t)sav.bells.bell + steps, 0, max);
        t[0] = 0;
        if (sav.bells.bell && sav.bells.mode == BM_HAND)
            pair_text(sav.bells.bell, t);
        else if (sav.bells.bell)
            int_text(sav.bells.bell, t);   /* LISTENING alone when 0 */
        app_readout(sav.bells.bell ? (sav.bells.mode == BM_HAND ? "BELLS" : "BELL") : "LISTENING", t);
    } else if (role == EN_K3) {
        sav.bells.pace = (uint8_t)clamp((int32_t)sav.bells.pace + steps, PACE_MIN, PACE_MAX);
        int_text(sav.bells.pace * 10, t);
        text_cat(t, " MS");
        app_readout("PACE", t);
    } else if (role == EN_K4) {
        sav.bells.mode = (uint8_t)clamp((int32_t)sav.bells.mode + steps, 0, BM_N - 1);
        if (sav.bells.mode == BM_CONDUCT)
            sav.bells.method = (uint8_t)pb_method(sav.bells.method, 1);
        app_readout("MODE", MODE_NAME[sav.bells.mode]);
    } else {
        return;
    }
    max = sav.bells.mode == BM_HAND ? bells_of(method()) / 2 : method()->stage;
    if (sav.bells.bell > max)
        sav.bells.bell = (uint8_t)max;
    bl.score = -1;
    touch_build();
    save_dirty();
}

static void bells_enter(void)
{
    bl.ringing = 0;
    bl.score = -1;
    if (sav.bells.mode == BM_CONDUCT)
        sav.bells.method = (uint8_t)pb_method(sav.bells.method, 1);
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

static int blows_until(int cur, int b)   /* blows from cur to bell b's next */
{
    int k = cur + 1, n = 1;
    for (; k < bl.nrows * bl.bells && bl.rows[k / bl.bells][k % bl.bells] != b; k++)
        n++;
    return n;
}

static void bells_draw(uint32_t now)
{
    int32_t y0, cx0 = 22, dx = bl.bells > 6 ? 28 : 39, t = bl.ringing ? (int32_t)(now - bl.t0) : -1;
    int cur = 0, i, b, h, row = 0, place = 0, line_bell = bl.mine[0] >= 0 ? bl.mine[0] : 0;
    char s[40];
    if (bl.ringing && t >= 0) {          /* where the ringing is: the last blow struck */
        cur = bl.next > 0 ? bl.next - 1 : 0;
        row = cur / bl.bells;
        place = cur % bl.bells;
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
            int me = is_mine(b), hit = bl.struck_ms[b] && now - bl.struck_ms[b] < 150u;
            s[0] = (char)('1' + b);
            s[1] = 0;
            draw_bell(cx, 60, swing(b, now), bl.bells > 6 ? 0.8f : 1.0f, me ? G_RED : G_PLANK, me ? G_RED2 : G_PLANK2, hit);
            text_mid(cx, 70, &FONT_XS, s, me ? G_RED : G_PLANK2);
        }
        /* your countdowns; conducting, the call and the lead */
        if (bl.ringing && bl.mode == BM_CONDUCT) {
            int left = bl.ring.nlead - 1 - bl.ring.at % bl.ring.nlead;
            s[0] = 0;
            text_cat(s, "LEAD ");
            sp_num(s, bl.leads + 1);
            text_cat(s, " OF ");
            sp_num(s, bl.target);
            cv_text(8, 90, &FONT_XS, s, G_INK);
            if (bl.call != RING_PLAIN)
                text_mid(150, 88, &FONT_B, bl.call == RING_BOB ? "BOB!" : "SINGLE!", G_RED);
            else if (!bl.complete && left <= 4)
                text_mid(150, 90, &FONT_XS, "CALL NOW?", G_PLANK2);
        } else if (bl.ringing && t >= 0) {
            for (h = 0; h < 2; h++) {
                int32_t mx = cx0 + bl.mine[h] * dx;
                int n_until;
                if (bl.mine[h] < 0 || (bl.pressed_ms[h] && now - bl.pressed_ms[h] < 180u))
                    continue;
                n_until = blows_until(cur, bl.mine[h]);
                if (n_until <= 1)
                    text_mid(mx, 90, &FONT_XS, "NOW", G_RED);
                else {
                    int np = n_until > 6 ? 6 : n_until;
                    for (i = 0; i < np; i++)
                        fill_circle(mx - (np - 1) * 3 + i * 6, 96, 2, i == 0 && n_until <= 2 ? G_RED : G_PLANK);
                }
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
                    if (bb == line_bell) {    /* the line: your (first) bell's, or the treble's */
                        if (lastx >= 0)
                            cv_line(lastx, lasty, x, y + 5, G_TEAL);
                        lastx = x;
                        lasty = y + 5;
                    }
                    text_mid(x, y, &FONT_XS, s, is_mine(bb) ? G_RED : done ? G_INK : G_MUTE);
                }
            }
#undef ROW_Y
        } else {                         /* standing: the settings */
            static const char *const HOW[BM_N][2] = {
                {"PICK A BELL WITH K2, THEN PRESS ANY", "KEY EACH TIME IT IS DUE (THE PIPS)."},
                {"TWO BELLS: THE LEFT HALF OF THE KEYS", "RINGS THE FIRST, THE RIGHT HALF THE OTHER."},
                {"CALL: LEFT HALF BOB, RIGHT HALF SINGLE.", "BRING THE BELLS ROUND AT THE LENGTH SET."}};
            const char *lab[4] = {"K1 METHOD", sav.bells.mode == BM_HAND ? "K2 BELLS" : "K2 BELL", "K3 PACE", "K4 MODE"};
            for (i = 0; i < 4; i++) {
                cv_text(14, 108 + i * 20, &FONT_XS, lab[i], G_PLANK2);
                s[0] = 0;
                if (i == 0)
                    method_name(s);
                else if (i == 1) {
                    if (sav.bells.mode == BM_CONDUCT)
                        text_cat(s, "THE TOWER RINGS");
                    else if (!sav.bells.bell)
                        text_cat(s, "LISTEN");
                    else if (sav.bells.mode == BM_HAND)
                        pair_text(sav.bells.bell, s);
                    else
                        int_text(sav.bells.bell, s);
                } else if (i == 2) {
                    int_text(sav.bells.pace * 10, s);
                    text_cat(s, " MS");
                } else {
                    text_cat(s, MODE_NAME[sav.bells.mode % BM_N]);
                }
                cv_text(94, 106 + i * 20, &FONT_B, s, G_INK);
            }
            cv_text(14, 190, &FONT_XS, HOW[sav.bells.mode % BM_N][0], G_PLANK2);
            cv_text(14, 204, &FONT_XS, HOW[sav.bells.mode % BM_N][1], G_PLANK2);
        }
        /* the foot: striking, or the touch */
        cv_rect(0, 220, 240, 20, G_LILAC);
        if (bl.ringing && bl.mine[0] >= 0 && bl.blows) {
            int sc = striking();
            cv_text(8, 224, &FONT_XS, "STRIKING", G_INK);
            round_rect(84, 227, 70, 6, 3, G_PAPER);
            cv_rect(84, 227, 70 * sc / 100, 6, G_TEAL);
            int_text(sc, s);
            text_cat(s, "%");
            cv_text(232 - text_w(&FONT_XS, s), 224, &FONT_XS, s, G_INK);
        } else if (bl.ringing) {
            cv_text(8, 224, &FONT_XS, bl.mode == BM_CONDUCT ? "LEFT: BOB  RIGHT: SINGLE" :
                                      bl.mine[0] >= 0 ? "ANY KEY: YOUR BELL" : "LISTENING", G_INK);
            cv_text(232 - text_w(&FONT_XS, "PLAY: STAND"), 224, &FONT_XS, "PLAY: STAND", G_INK);
        } else {
            if (bl.mode == BM_CONDUCT && bl.result != BR_NONE) {
                cv_text(8, 224, &FONT_XS, bl.result == BR_ROUND ? "WELL CALLED" : "NOT THIS TIME",
                        bl.result == BR_ROUND ? G_TEAL : G_RED);
            } else if (bl.score >= 0) {
                text_cat((s[0] = 0, s), "STRUCK ");
                int_text(bl.score, s + 7);
                text_cat(s, "%");
                cv_text(8, 224, &FONT_XS, s, G_RED);
            } else if (bl.mode == BM_CONDUCT && sav.touches) {
                text_cat((s[0] = 0, s), "TOUCHES ");
                int_text(sav.touches, s + 8);
                cv_text(8, 224, &FONT_XS, s, G_INK);
            } else if (bl.mode != BM_CONDUCT && sav.bells.best[sav.bells.method % NMETHODS_MAX]) {
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
        if (bl.mode == BM_CONDUCT) {
            conduct_extend(bl.next / bl.bells + CALL_AHEAD);
            if (!bl.call_said) {         /* the call, said (the key came in the tick) */
                bl.call_said = 1;
                app_say(CALL_NAME[bl.call], "");
            }
        }
        judge_misses(now);
        if (bl.complete && bl.next >= bl.nrows * bl.bells && (int32_t)(now - bl.t0) > blow_time(bl.nrows, 0))
            bells_end(0);
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

/* the keys: the black ones are the places in a row, 1 to 6 (or 8), left to right. Your bell's place in the row you
 * ring next is lit, in the row after at the mid level (handbells: the first bell's lit, the second's mid). On your
 * blow its half of the white keys lights. Conducting: the half of your call lit, both halves mid near a lead end */
static uint32_t bells_keys_lit(uint32_t *mid)
{
    static const uint8_t BK[11] = PLAT_BLACK_KEYS, WK[16] = PLAT_WHITE_KEYS;
    uint32_t now = plat_ms(), keys = 0;
    int32_t t = (int32_t)(now - bl.t0);
    int i, h;
    *mid = 0;
    if (!bl.ringing || t < -600)
        return 0;
    if (bl.mode == BM_CONDUCT) {
        int left = bl.ring.nlead - 1 - bl.ring.at % bl.ring.nlead;
        for (i = 0; i < 16; i++) {
            if ((bl.call == RING_BOB && i < 8) || (bl.call == RING_SINGLE && i >= 8))
                keys |= 1u << WK[i];
            else if (!bl.complete && left <= 4)
                *mid |= 1u << WK[i];
        }
        return keys;
    }
    for (h = 0; h < 2; h++) {
        int row = bl.my_next[h], p;
        if (bl.mine[h] < 0 || row >= bl.nrows)
            continue;
        p = row_place(row, bl.mine[h]);
        if (h == 0 || bl.mine[1] < 0)
            keys |= 1u << BK[p];
        else
            *mid |= 1u << BK[p];
        if (bl.mine[1] < 0 && row + 1 < bl.nrows)
            *mid |= 1u << BK[row_place(row + 1, bl.mine[h])];
        if (blow_time(row, p) - t < 80)  /* its blow: its keys */
            for (i = 0; i < 16; i++)
                if (bl.mine[1] < 0 || (h == 0) == (i < 8))
                    keys |= 1u << WK[i];
    }
    return keys;
}

static void bells_where(char *t)         /* the screen, for a screen reader */
{
    char m[32], p[16];
    method_name(m);
    t[0] = 0;
    if (bl.ringing) {
        text_cat(t, "BELLS. RINGING ");
        text_cat(t, m);
        if (bl.mode == BM_CONDUCT) {
            text_cat(t, ". LEAD ");
            sp_num(t, bl.leads + 1);
            text_cat(t, " OF ");
            sp_num(t, bl.target);
            text_cat(t, bl.call == RING_PLAIN ? ". LEFT HALF CALLS BOB, RIGHT HALF SINGLE" :
                        bl.call == RING_BOB ? ". BOB CALLED" : ". SINGLE CALLED");
        } else if (bl.mine[1] >= 0) {
            pair_text(sav.bells.bell, p);
            text_cat(t, ". YOUR BELLS ");
            text_cat(t, p);
            text_cat(t, ", LEFT HALF AND RIGHT HALF");
        } else if (bl.mine[0] >= 0) {
            text_cat(t, ". YOUR BELL ");
            sp_num(t, bl.mine[0] + 1);
            text_cat(t, ", ANY KEY RINGS IT");
        } else {
            text_cat(t, ". LISTENING");
        }
        if (bl.mine[0] >= 0 && bl.blows) {
            text_cat(t, ". STRIKING ");
            sp_pct(t, striking());
        }
        text_cat(t, ". PLAY STANDS.");
        return;
    }
    text_cat(t, "BELLS, ");
    text_cat(t, MODE_NAME[sav.bells.mode % BM_N]);
    text_cat(t, ". KNOB 1, METHOD ");
    text_cat(t, m);
    if (sav.bells.mode != BM_CONDUCT) {
        text_cat(t, ". KNOB 2, ");
        if (sav.bells.bell && sav.bells.mode == BM_HAND) {
            pair_text(sav.bells.bell, p);
            text_cat(t, "BELLS ");
            text_cat(t, p);
        } else if (sav.bells.bell) {
            text_cat(t, "BELL ");
            sp_num(t, sav.bells.bell);
        } else {
            text_cat(t, "LISTENING");
        }
    }
    text_cat(t, ". KNOB 3, PACE ");
    sp_num(t, sav.bells.pace * 10);
    text_cat(t, " MS. KNOB 4 MODE. ");
    if (bl.mode == BM_CONDUCT && bl.result != BR_NONE) {
        conduct_said(t + text_len(t));
        text_cat(t, " ");
    } else if (bl.score >= 0) {
        text_cat(t, "STRUCK ");
        sp_pct(t, bl.score);
        text_cat(t, ". ");
    }
    text_cat(t, "PLAY RINGS.");
}

static int bells_playing(void) { return bl.ringing; }

/* the attract tune: rounds on six, then a pull of Plain Bob (214365), on chimes */
static const uint8_t BELLS_TUNE[] = {83, 81, 79, 77, 76, 74, 0, 83, 81, 79, 77, 76, 74, 0,
                                     81, 83, 77, 79, 74, 76, 0, 81, 77, 83, 74, 79, 76, 0};

static const game_t GAME_BELLS = {"BELLS", RGB(232, 80, 91), bells_enter, bells_leave, bells_frame, bells_key,
                                  bells_button, bells_enc, bells_tick, bells_attract, bells_best, bells_keys_lit,
                                  BELLS_TUNE, sizeof BELLS_TUNE, 260, BW_CHIME, bells_where, 1, bells_playing};
