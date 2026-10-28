/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Claw: the claw machine. The glass case sits over the keys, a column for each: press a key and the claw glides
 * over it, PLAY drops it. The case is piled with toys, each singing a note: the claw hums a toy's note as it comes
 * over it. The machine wishes for one (it plays its note, again now and then); grab that one for 10 tickets, any
 * other for 3. Big toys are chords, three keys wide: centre the claw on the middle note, and they are worth twice
 * as much. Dead on a toy's key and the grip is good; a key off and it may slip on the way up. Ten coins a round.
 *
 * Levels (K1): 1 the toys and the wish show their notes; 2 by ear; 3 the wishes are chords (big toys). PLAY starts,
 * then drops. The keys never light the answer; the one under the claw glows. Part of the unity build. */
#include "game.h"

#define CL_TOYS 7
#define CL_COINS 10
#define CL_LEVELS 3
#define CL_RAIL 34                       /* the claw's rail's y */
#define CL_FLOOR 182                     /* the pile's floor: the toys' middles */
#define CL_CHUTE 14                      /* the prize chute's x */

enum { CW_STAND, CW_READY, CW_DROP, CW_OVER };
enum { DP_DOWN, DP_CLOSE, DP_UP, DP_OVER_CHUTE, DP_RELEASE };
typedef struct {
    uint8_t on, key, big, kind, minor;   /* key: its column (a big toy's middle note) */
} toy_t;

static struct {
    int state, coins, tickets, new_best, level;
    toy_t toy[CL_TOYS];
    int wish;                            /* the toy wished for */
    uint32_t wish_ms;                    /* the wish last played */
    float cx;                            /* the claw's x */
    int col;                             /* the key it goes to */
    int over;                            /* the toy it is over (hummed once), -1 none */
    /* a drop */
    int phase, held, grab, slip, slipped;   /* held: the toy in its prongs now; grab: one it will hold; slip: one it
                                          * will lose on the way up (-1 none) */
    uint32_t phase_ms, last_ms;
    float cy;                            /* the claw's height */
    int won;                             /* the last prize's tickets (for the screen) */
    uint32_t won_ms;
    uint8_t shelf[16];                   /* toys won this round: kinds, for the shelf */
    int nshelf;
} cw;

static const char *const CL_LEVEL_NAME[CL_LEVELS] = {"NOTES SHOWN", "BY EAR", "CHORDS"};

static int toy_note(const toy_t *t) { return 53 + t->key; }

static void toy_chord(const toy_t *t, int *n)   /* a big toy's three notes: the middle one is its key */
{
    n[0] = toy_note(t) - (t->minor ? 3 : 4);
    n[1] = toy_note(t);
    n[2] = toy_note(t) + (t->minor ? 4 : 3);
}

static void toy_sound(const toy_t *t, int vel, int timbre)
{
    snd_lock();
    if (t->big) {
        int n[3], i;
        toy_chord(t, n);
        for (i = 0; i < 3; i++)
            bw_note(n[i], vel, timbre);
    } else {
        bw_note(toy_note(t), vel, timbre);
    }
    snd_unlock();
}

static int toy_free(int key, int big)    /* room for a toy on this key: clear of the others */
{
    int i, x = key_x(key);
    for (i = 0; i < CL_TOYS; i++) {
        int d;
        if (!cw.toy[i].on)
            continue;
        d = key_x(cw.toy[i].key) - x;
        d = d < 0 ? -d : d;
        if (d < (big || cw.toy[i].big ? 30 : 17))
            return 0;
    }
    return 1;
}

static void toy_add(int big)
{
    int i, tries;
    for (i = 0; i < CL_TOYS && cw.toy[i].on; i++)
        ;
    if (i == CL_TOYS)
        return;
    for (tries = 0; tries < 30; tries++) {
        int k = big ? 4 + (int)rnd(19) : 2 + (int)rnd(NKEYS - 2);   /* a chord keeps its notes on the keyboard;
                                                                       * none behind the chute */
        if (toy_free(k, big)) {
            cw.toy[i].on = 1;
            cw.toy[i].key = (uint8_t)k;
            cw.toy[i].big = (uint8_t)big;
            cw.toy[i].kind = (uint8_t)rnd(4);
            cw.toy[i].minor = (uint8_t)rnd(2);
            return;
        }
    }
}

static int toys_on(int big_only)
{
    int i, n = 0;
    for (i = 0; i < CL_TOYS; i++)
        n += cw.toy[i].on && (!big_only || cw.toy[i].big);
    return n;
}

static void new_wish(uint32_t now)       /* the machine's wish: a toy in the case (a chord, on level 3) */
{
    int i, n = toys_on(cw.level == 3), pick;
    cw.wish = -1;
    if (!n)
        return;
    pick = (int)rnd((uint32_t)n);
    for (i = 0; i < CL_TOYS; i++)
        if (cw.toy[i].on && (cw.level != 3 || cw.toy[i].big) && pick-- == 0)
            cw.wish = i;
    cw.wish_ms = now;
    snd_lock();
    bw_note(96, 50, BW_CHIME);           /* the sparkle, then the wish */
    snd_unlock();
    toy_sound(&cw.toy[cw.wish], 100, BW_CHIME);
}

static void fill_case(void)
{
    while (toys_on(0) < CL_TOYS - 1) {
        int bigs = toys_on(1);
        toy_add(cw.level == 3 ? bigs < 3 : rnd(5) == 0 && bigs < 1);
        if (toys_on(0) >= CL_TOYS)
            break;
    }
}

static void claw_go(void)
{
    int i;
    uint32_t now = plat_ms();
    rnd_state ^= now * 2654435761u;
    cw.level = clamp(sav.claw.level, 1, CL_LEVELS);
    for (i = 0; i < CL_TOYS; i++)
        cw.toy[i].on = 0;
    fill_case();
    cw.coins = CL_COINS;
    cw.tickets = 0;
    cw.nshelf = 0;
    cw.held = -1;
    cw.cy = CL_RAIL + 12;
    cw.state = CW_READY;
    cw.over = -1;
    cw.won_ms = 0;
    cw.last_ms = now;
    new_wish(now);
    app_say("TEN COINS", "FIND THE ONE IT WISHES FOR");
}

static int toy_under(int *off)           /* the toy the claw is over: off, the keys it is off by (0 dead on) */
{
    int i, best = -1, bd = 999;
    for (i = 0; i < CL_TOYS; i++) {
        int d;
        if (!cw.toy[i].on)
            continue;
        d = cw.col - cw.toy[i].key;
        d = d < 0 ? -d : d;
        if (d < bd && d <= 1) {
            bd = d;
            best = i;
        }
    }
    *off = bd;
    return best;
}

static void claw_key(int k, int down, uint32_t ms)
{
    (void)ms;
    if (!down)
        return;
    if (cw.state != CW_READY) {          /* standing: the keys play */
        if (cw.state != CW_DROP) {
            snd_lock();
            bw_note(53 + k, 90, BW_PLUCK);
            snd_unlock();
        }
        return;
    }
    cw.col = k;                          /* the claw glides over it, a soft whirr at its pitch */
    snd_lock();
    bw_note(53 + k, 40, BW_SINE);
    snd_unlock();
}

static void claw_drop(uint32_t now)
{
    int off, t = toy_under(&off);
    cw.state = CW_DROP;
    cw.phase = DP_DOWN;
    cw.phase_ms = now;
    cw.coins--;
    cw.held = cw.grab = cw.slip = -1;
    cw.slipped = 0;
    if (t >= 0) {                        /* the grip: dead on, firm; a key off, a chance; big toys are harder */
        int firm = cw.toy[t].big ? (off == 0 ? 75 : 25) : (off == 0 ? 92 : 45);
        if ((int)rnd(100) < firm)
            cw.grab = t;
        else
            cw.slip = t;                 /* it rises with it, then drops it */
    }
    snd_lock();
    bw_note(67, 60, BW_PLUCK);
    bw_note(64, 55, BW_PLUCK);
    snd_unlock();
}

static void claw_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (cw.state == CW_STAND || cw.state == CW_OVER)
        claw_go();
    else if (cw.state == CW_READY && cw.coins > 0)
        claw_drop(plat_ms());
}

static void claw_enc(int role, int steps)
{
    char t[24];
    if (role != EN_K1 || cw.state == CW_READY || cw.state == CW_DROP)
        return;
    sav.claw.level = (uint8_t)clamp(sav.claw.level + steps, 1, CL_LEVELS);
    int_text(sav.claw.level, t);
    text_cat(t, ", ");
    text_cat(t, CL_LEVEL_NAME[sav.claw.level - 1]);
    app_readout("LEVEL", t);
    cw.state = CW_STAND;
    save_dirty();
}

static void claw_enter(void)
{
    cw.state = CW_STAND;
    cw.cx = 120;
    cw.cy = CL_RAIL + 12;
    cw.held = -1;
}
static void claw_leave(void) { cw.state = CW_STAND; }

static void claw_win(uint32_t now)       /* a toy down the chute */
{
    toy_t *t = &cw.toy[cw.held];
    int wished = cw.held == cw.wish, tk = (wished ? 10 : 3) * (t->big ? 2 : 1);
    char s[24];
    cw.tickets += tk;
    cw.won = tk;
    cw.won_ms = now;
    if (cw.nshelf < 16)
        cw.shelf[cw.nshelf++] = (uint8_t)(t->kind | (t->big ? 4 : 0));
    t->on = 0;
    cw.held = -1;
    snd_lock();                          /* tickets: a run up */
    bw_note(72, 80, BW_CHIME);
    bw_note(wished ? 79 : 76, 80, BW_CHIME);
    if (wished)
        bw_note(84, 80, BW_CHIME);
    snd_unlock();
    s[0] = 0;
    int_text(tk, s);
    text_cat(s, " TICKETS");
    app_say(wished ? "THE ONE IT WANTED" : "A PRIZE", s);
    fill_case();
    new_wish(now);
}

static void claw_end(void)
{
    char t[24] = "";
    cw.state = CW_OVER;
    cw.new_best = cw.tickets > sav.claw.best;
    if (cw.new_best) {
        sav.claw.best = (uint16_t)cw.tickets;
        save_dirty();
    }
    int_text(cw.tickets, t);
    text_cat(t, " TICKETS");
    app_say(cw.new_best ? "OUT OF COINS. NEW BEST" : "OUT OF COINS", t);
}

static void claw_step(uint32_t now)
{
    float dt = (float)(now - cw.last_ms) * 0.001f, tx = (float)key_x(cw.col);
    uint32_t pt = now - cw.phase_ms;
    cw.last_ms = now;
    if (dt > 0.05f)
        dt = 0.05f;
    if (cw.state == CW_READY) {          /* the glide, and a hum over a toy */
        int off, t;
        cw.cx += (tx - cw.cx) * (dt * 6.0f > 1.0f ? 1.0f : dt * 6.0f);
        t = cw.cx - tx < 2.0f && tx - cw.cx < 2.0f ? toy_under(&off) : -1;
        if (t >= 0 && off == 0 && t != cw.over)
            toy_sound(&cw.toy[t], 35, BW_SINE);
        cw.over = t >= 0 && off == 0 ? t : -1;
        if (cw.wish >= 0 && (int32_t)(now - cw.wish_ms) > 6000) {   /* the wish, again */
            cw.wish_ms = now;
            toy_sound(&cw.toy[cw.wish], 80, BW_CHIME);
        }
        return;
    }
    if (cw.state != CW_DROP)
        return;
    switch (cw.phase) {
    case DP_DOWN:                        /* down to the pile */
        cw.cy = CL_RAIL + 12 + (float)(CL_FLOOR - 14 - CL_RAIL - 12) * (pt > 700u ? 1.0f : (float)pt / 700.0f);
        if (pt > 700u) {
            cw.phase = DP_CLOSE;
            cw.phase_ms = now;
            snd_lock();
            bw_drum(BW_BLOCK_LO, 70);    /* the prongs close */
            snd_unlock();
        }
        break;
    case DP_CLOSE:
        if (pt > 250u) {
            cw.phase = DP_UP;
            cw.phase_ms = now;
            cw.held = cw.grab >= 0 ? cw.grab : cw.slip;   /* it has it (or thinks it has) */
        }
        break;
    case DP_UP:                          /* up; a slipping toy falls halfway */
        cw.cy = (float)(CL_FLOOR - 14) - (float)(CL_FLOOR - 14 - CL_RAIL - 12) * (pt > 800u ? 1.0f : (float)pt / 800.0f);
        if (cw.slip >= 0 && !cw.slipped && pt > 450u) {
            cw.slipped = 1;
            cw.held = -1;
            snd_lock();                  /* the sad slide down */
            bw_note(64, 60, BW_PLUCK);
            bw_note(60, 55, BW_PLUCK);
            bw_note(55, 50, BW_PLUCK);
            snd_unlock();
            app_say("IT SLIPPED", "");
        }
        if (pt > 800u) {
            cw.phase = DP_OVER_CHUTE;
            cw.phase_ms = now;
        }
        break;
    case DP_OVER_CHUTE:                  /* over to the chute (or back, empty) */
        cw.cx += ((float)CL_CHUTE + 6.0f - cw.cx) * (dt * 5.0f > 1.0f ? 1.0f : dt * 5.0f);
        if (pt > 900u) {
            cw.phase = DP_RELEASE;
            cw.phase_ms = now;
        }
        break;
    default:
        if (pt > 300u) {
            if (cw.held >= 0)
                claw_win(now);
            else if (!cw.slipped)
                app_say("NOTHING", "");
            cw.col = clamp(cw.col, 0, NKEYS - 1);
            cw.state = cw.coins > 0 ? CW_READY : CW_OVER;
            if (cw.state == CW_OVER)
                claw_end();
        }
    }
}

static void toy_draw(const toy_t *t, int32_t x, int32_t y, int names)
{
    static const uint16_t BODY[4] = {RGB(170, 120, 80), RGB(250, 210, 60), RGB(245, 245, 245), RGB(232, 88, 128)};
    int32_t r = t->big ? 12 : 7;
    char s[6];
    fill_circle(x, y, r, BODY[t->kind]);
    if (t->kind == 0) {                  /* a bear: ears */
        fill_circle(x - r + 2, y - r + 2, r / 3 + 1, BODY[0]);
        fill_circle(x + r - 2, y - r + 2, r / 3 + 1, BODY[0]);
    } else if (t->kind == 1 || t->kind == 2) {   /* a duck, a gull: a beak */
        cv_rect(x + r - 2, y - 1, r / 2 + 2, 3, G_ORANGE);
    } else {                             /* a ball: a stripe */
        cv_rect(x - r + 1, y - 1, 2 * r - 1, 3, C_WHITE);
    }
    cv_rect(x - r / 3 - 1, y - r / 3, 2, 2, G_INK);
    cv_rect(x + r / 3, y - r / 3, 2, 2, G_INK);
    if (names) {
        note_text(toy_note(t), s);
        text_mid(x, y - r - 15, &FONT_XS, s, G_PAPER);   /* its note, over it */
    }
}

static uint16_t claw_key_col(int k)      /* the key under the claw */
{
    return (cw.state == CW_READY || cw.state == CW_DROP) && k == cw.col ? RGB(232, 88, 128) : 0;
}

static void claw_draw(uint32_t now)
{
    int32_t y0, i, cx = (int32_t)cw.cx, cy = (int32_t)cw.cy;
    int names = cw.level == 1 || cw.state == CW_STAND;
    char s[32];
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, RGB(40, 30, 60));       /* the booth's dark inside */
        cv_rect(0, 0, 240, 18, RGB(232, 88, 128));
        cv_text(6, 3, &FONT_XS, "CLAW", G_PAPER);
        if (cw.state != CW_STAND) {
            text_cat((s[0] = 0, s), "COINS ");
            int_text(cw.coins, s + 6);
            text_cat(s, "  TICKETS ");
            int_text(cw.tickets, s + text_len(s));
            cv_text(234 - text_w(&FONT_XS, s), 3, &FONT_XS, s, G_PAPER);
        }
        cv_rect(0, 18, 240, 178, RGB(60, 50, 90));   /* the glass case */
        for (i = 0; i < cw.nshelf; i++)  /* the shelf of prizes won */
            fill_circle(30 + i * 13, 25, 4, (cw.shelf[i] & 3) == 1 ? RGB(250, 210, 60) : (cw.shelf[i] & 3) == 2 ?
                                                             C_WHITE : (cw.shelf[i] & 3) == 3 ? RGB(232, 88, 128) :
                                                                                               RGB(170, 120, 80));
        cv_rect(0, CL_RAIL - 2, 240, 3, G_PLANK);   /* the rail */
        cv_rect(2, 150, 24, 46, RGB(30, 22, 44));   /* the chute */
        cv_text(4, 138, &FONT_XS, "WIN", G_PLANK);
        cv_rect(0, CL_FLOOR + 12, 240, 2, G_PLANK2);
        for (i = 0; i < CL_TOYS; i++)    /* the pile */
            if (cw.toy[i].on && i != cw.held)
                toy_draw(&cw.toy[i], key_x(cw.toy[i].key), CL_FLOOR - (cw.toy[i].big ? 5 : 0),
                         names && cw.state != CW_STAND);
        if (cw.state == CW_STAND) {
            round_rect(30, 46, 180, 112, 8, G_PAPER);
            cv_text(40, 54, &FONT_XS, "K1 LEVEL", G_PLANK2);
            int_text(sav.claw.level, s);
            text_cat(s, " ");
            text_cat(s, CL_LEVEL_NAME[clamp(sav.claw.level, 1, CL_LEVELS) - 1]);
            cv_text(98, 52, &FONT_B, s, G_INK);
            cv_text(40, 78, &FONT_XS, "A KEY MOVES THE CLAW OVER IT.", G_PLANK2);
            cv_text(40, 92, &FONT_XS, "PLAY DROPS IT. GRAB THE TOY", G_PLANK2);
            cv_text(40, 106, &FONT_XS, "THAT SINGS THE MACHINE'S WISH.", G_PLANK2);
            text_cat((s[0] = 0, s), "10 COINS");
            if (sav.claw.best) {
                text_cat(s, ". BEST ");
                int_text(sav.claw.best, s + text_len(s));
            }
            cv_text(40, 132, &FONT_XS, s, G_INK);
        } else {
            /* the claw: its cable, its box, its prongs (open, or closed on a toy) */
            int open = cw.state == CW_READY || (cw.state == CW_DROP && (cw.phase == DP_DOWN || cw.phase == DP_RELEASE));
            cv_rect(cx, CL_RAIL, 1, cy - CL_RAIL, G_PLANK);
            cv_rect(cx - 6, cy - 4, 13, 6, G_PLANK2);
            cv_line(cx - 4, cy + 2, cx - (open ? 11 : 6), cy + 12, G_PLANK);
            cv_line(cx + 4, cy + 2, cx + (open ? 11 : 6), cy + 12, G_PLANK);
            if (cw.held >= 0)
                toy_draw(&cw.toy[cw.held], cx, cy + 14 + (cw.toy[cw.held].big ? 6 : 0), 0);
            /* the wish: in a bubble at the top (its note shown on level 1) */
            if (cw.wish >= 0 && cw.state != CW_OVER) {
                round_rect(150, 38, 84, 20, 6, G_PAPER);
                s[0] = 0;
                text_cat(s, "WISH: ");
                if (cw.level == 1)
                    note_text(toy_note(&cw.toy[cw.wish]), s + 6);
                else
                    text_cat(s, "LISTEN");
                text_mid(192, 41, &FONT_XS, s, G_INK);
            }
            if (cw.won_ms && now - cw.won_ms < 1500u) {
                text_cat((s[0] = 0, s), "+");
                int_text(cw.won, s + 1);
                text_mid(120, 70, &FONT_L, s, G_ORANGE);
            }
            if (cw.state == CW_OVER) {
                round_rect(40, 64, 160, 70, 8, G_PAPER);
                text_mid(120, 70, &FONT_B, "OUT OF COINS", G_RED);
                text_cat((s[0] = 0, s), "TICKETS ");
                int_text(cw.tickets, s + 8);
                text_mid(120, 92, &FONT_B, s, G_INK);
                text_mid(120, 114, &FONT_XS, cw.new_best ? "NEW BEST!" : "", G_RED);
            }
        }
        keyboard(198, 20, claw_key_col);
        cv_rect(0, 220, 240, 20, G_LILAC);
        {
            const char *f = cw.state == CW_READY ? "PLAY: DROP" : cw.state == CW_OVER ? "PLAY: AGAIN" :
                            cw.state == CW_STAND ? "PLAY: START" : "";
            cv_text(232 - text_w(&FONT_XS, f), 224, &FONT_XS, f, G_INK);
        }
        band_send(y0);
    }
}

static void claw_frame(uint32_t now)
{
    if (cw.state == CW_READY || cw.state == CW_DROP)
        claw_step(now);
    claw_draw(now);
}

static void claw_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    int32_t px = x + 6 + (int32_t)(ms / 30u % (uint32_t)(w - 12)), down = (int32_t)(ms / 600u % 2u) * (h / 3);
    cv_rect(x, y + 4, w, 1, G_PLANK);
    cv_rect(px, y + 4, 1, 6 + down, G_PLANK);
    cv_line(px - 3, y + 10 + down, px, y + 7 + down, G_PLANK);
    cv_line(px + 3, y + 10 + down, px, y + 7 + down, G_PLANK);
    fill_circle(x + w / 4, y + h - 8, 4, RGB(170, 120, 80));
    fill_circle(x + w / 2, y + h - 8, 4, RGB(250, 210, 60));
    fill_circle(x + 3 * w / 4, y + h - 8, 4, RGB(232, 88, 128));
}

static void claw_best(char *t)
{
    t[0] = 0;
    if (sav.claw.best) {
        text_cat(t, "BEST ");
        int_text(sav.claw.best, t + 5);
        text_cat(t, " TICKETS");
    }
}

static uint32_t claw_keys_lit(uint32_t *mid)   /* the key under the claw glows (where it is, not where to go) */
{
    *mid = cw.state == CW_READY ? 1u << cw.col : 0u;
    return 0;
}

static void claw_where(char *t)
{
    t[0] = 0;
    if (cw.state == CW_STAND) {
        text_cat(t, "CLAW. KNOB 1, LEVEL ");
        sp_num(t, sav.claw.level);
        text_cat(t, ", ");
        text_cat(t, CL_LEVEL_NAME[clamp(sav.claw.level, 1, CL_LEVELS) - 1]);
        text_cat(t, ". A KEY MOVES THE CLAW OVER IT, PLAY DROPS IT. GRAB THE TOY THAT SINGS THE MACHINE'S WISH. ");
        if (sav.claw.best) {
            text_cat(t, "BEST ");
            sp_num(t, sav.claw.best);
            text_cat(t, ". ");
        }
        text_cat(t, "PLAY STARTS.");
        return;
    }
    text_cat(t, cw.state == CW_OVER ? "CLAW. OUT OF COINS. " : "CLAW. ");
    sp_num(t, cw.tickets);
    text_cat(t, " TICKETS, ");
    sp_num(t, cw.coins);
    text_cat(t, cw.state == CW_OVER ? " COINS. PLAY STARTS AGAIN." : " COINS LEFT. PLAY DROPS.");
}

static int claw_playing(void) { return cw.state == CW_READY || cw.state == CW_DROP; }

static const uint8_t CLAW_TUNE[] = {76, 79, 84, 0, 83, 79, 76, 0, 74, 77, 81, 0, 79, 0, 0, 0};

static const game_t GAME_CLAW = {"CLAW", RGB(232, 88, 128), claw_enter, claw_leave, claw_frame, claw_key, claw_button,
                                 claw_enc, 0, claw_attract, claw_best, claw_keys_lit, CLAW_TUNE, sizeof CLAW_TUNE, 180,
                                 BW_CHIME, claw_where, 0, claw_playing, "GRAB THE PRIZE"};
