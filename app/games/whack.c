/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Whack-a-mole on the keys. Moles pop up from the keys, lit, in time with a groove; whack one before it ducks and it
 * plays its key's note. They only come up on the keys of a pentatonic scale (C D E G A), so a good run of whacks is a
 * tune. A gold one (flashing) is worth three; a whack on an empty hole is a clunk and costs one. 45 seconds a round.
 *
 * Standing: K1 the level (how fast they come and go); PLAY starts. Part of the unity build. */
#include "game.h"

#define WM_LEVELS 5
#define WM_ROUND 45000                   /* ms */
#define WM_BPM 112

enum { WS_STAND, WS_PLAY, WS_OVER };
static struct {
    int state, score, hits, missed, stray, new_best;
    volatile uint32_t t0;
    volatile int tick_step;
    volatile uint32_t up_until[NKEYS];   /* a mole up until then (0: none) */
    volatile uint8_t gold[NKEYS];
    volatile uint32_t whacked_ms[NKEYS]; /* for the screen: a whack (the mole's dazed face), or a clunk */
    volatile uint8_t clunk[NKEYS];
} wm;

static int pentatonic(int k) { int pc = (53 + k) % 12; return pc == 0 || pc == 2 || pc == 4 || pc == 7 || pc == 9; }

static int32_t wm_step(void) { return 60000 / WM_BPM / 2; }   /* ms an eighth */

static void whack_go(void)
{
    int k;
    rnd_state ^= plat_ms() * 2654435761u;
    for (k = 0; k < NKEYS; k++) {
        wm.up_until[k] = 0;
        wm.whacked_ms[k] = 0;
    }
    wm.score = wm.hits = wm.missed = wm.stray = 0;
    wm.tick_step = 0;
    wm.t0 = plat_ms() + 500u;
    wm.state = WS_PLAY;
    app_say("WHACK THEM", "");
}

/* the tick: the groove, and moles coming up on the eighths */
static void whack_tick(uint32_t ms)
{
    int32_t t = (int32_t)(ms - wm.t0);
    int k;
    if (wm.state != WS_PLAY || t < 0)
        return;
    for (k = 0; k < NKEYS; k++)          /* moles that got away */
        if (wm.up_until[k] && (int32_t)(ms - wm.up_until[k]) >= 0) {
            wm.up_until[k] = 0;
            wm.missed++;
        }
    while (wm.tick_step * wm_step() <= t) {
        int st = wm.tick_step % 8, lv = clamp(sav.whack.level, 1, WM_LEVELS);
        if (st % 2 == 0)
            bw_drum(BW_HAT, st == 0 ? 80 : 50);
        if (st == 0 || st == 4)
            bw_drum(BW_KICK, 90);
        if (t < WM_ROUND - 1500 && (int)rnd(10) < 3 + lv) {   /* a mole, on a free pentatonic key */
            int tries = 8;
            do
                k = (int)rnd(NKEYS);
            while ((!pentatonic(k) || wm.up_until[k]) && --tries);
            if (tries) {
                wm.up_until[k] = ms + (uint32_t)(1500 - lv * 180);
                wm.gold[k] = rnd(12) == 0;
                wm.whacked_ms[k] = 0;
            }
        }
        wm.tick_step++;
    }
}

static void whack_key(int k, int down, uint32_t ms)   /* in the tick */
{
    if (!down)
        return;
    if (wm.state != WS_PLAY) {
        bw_note(53 + k, 100, BW_PLUCK);
        return;
    }
    if (wm.up_until[k]) {
        wm.score += wm.gold[k] ? 3 : 1;
        wm.hits++;
        wm.up_until[k] = 0;
        wm.clunk[k] = 0;
        bw_note(53 + k, 110, wm.gold[k] ? BW_CHIME : BW_PLUCK);
    } else {
        wm.stray++;
        wm.score -= wm.score > 0;
        wm.clunk[k] = 1;
        bw_note(41, 80, BW_CLUNK);
    }
    wm.whacked_ms[k] = ms ? ms : 1;
}

static void whack_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (wm.state == WS_PLAY) {
        wm.state = WS_STAND;
        app_say("STOPPED", "");
    } else {
        whack_go();
    }
}

static void whack_enc(int role, int steps)
{
    char t[4];
    if (role != EN_K1 || wm.state == WS_PLAY)
        return;
    sav.whack.level = (uint8_t)clamp(sav.whack.level + steps, 1, WM_LEVELS);
    int_text(sav.whack.level, t);
    app_readout("LEVEL", t);
    wm.state = WS_STAND;
    save_dirty();
}

static void whack_enter(void) { wm.state = WS_STAND; }
static void whack_leave(void) { wm.state = WS_STAND; }

static void mole(int32_t x, int32_t y, int gold, int dazed)   /* a mole's head, its middle at (x, y) */
{
    fill_circle(x, y, 9, gold ? RGB(240, 190, 40) : RGB(120, 84, 60));
    fill_circle(x, y + 4, 4, RGB(240, 200, 180));   /* the snout */
    fill_circle(x, y + 2, 1, G_INK);
    if (dazed) {
        cv_line(x - 5, y - 5, x - 2, y - 2, G_INK);
        cv_line(x - 5, y - 2, x - 2, y - 5, G_INK);
        cv_line(x + 2, y - 5, x + 5, y - 2, G_INK);
        cv_line(x + 2, y - 2, x + 5, y - 5, G_INK);
    } else {
        cv_rect(x - 4, y - 4, 2, 2, G_INK);
        cv_rect(x + 2, y - 4, 2, 2, G_INK);
    }
}

static void whack_draw(uint32_t now)
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
    int32_t y0, i, t = (int32_t)(now - wm.t0);
    char s[24];
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, RGB(120, 170, 90));     /* the lawn */
        cv_rect(0, 0, 240, 18, RGB(120, 84, 60));
        cv_text(6, 3, &FONT_XS, "WHACK-A-MOLE", G_PAPER);
        if (wm.state == WS_STAND) {
            round_rect(14, 30, 212, 96, 8, G_PAPER);
            text_cat((s[0] = 0, s), "K1 LEVEL ");
            int_text(sav.whack.level, s + 9);
            cv_text(24, 38, &FONT_B, s, G_INK);
            cv_text(24, 62, &FONT_XS, "MOLES POP UP ON THE KEYS (THEY", G_PLANK2);
            cv_text(24, 76, &FONT_XS, "LIGHT): WHACK THEM BEFORE THEY GO.", G_PLANK2);
            cv_text(24, 90, &FONT_XS, "GOLD ONES ARE WORTH THREE.", G_PLANK2);
            if (sav.whack.best) {
                text_cat((s[0] = 0, s), "BEST ");
                int_text(sav.whack.best, s + 5);
                cv_text(24, 106, &FONT_XS, s, G_INK);
            }
        } else {
            int32_t left = clamp(WM_ROUND - t, 0, WM_ROUND);
            int_text(wm.score, s);
            cv_text(10, 26, &FONT_L, s, G_PAPER);
            cv_rect(120, 34, 110 * left / WM_ROUND, 6, G_PAPER);   /* the time left */
            if (wm.state == WS_OVER)
                text_mid(120, 64, &FONT_B, wm.new_best ? "NEW BEST!" : "TIME'S UP", G_PAPER);
        }
        /* the holes, laid out as the keys: the black keys' row above the white keys' */
        for (i = 0; i < 27; i++) {
            int black = i >= 16, k = black ? BK[i - 16] : WK[i];
            int32_t x = key_x(k), y = black ? 150 : 186;
            uint32_t w = wm.whacked_ms[k];
            round_rect(x - 5, y + 5, 11, 7, 3, RGB(70, 50, 36));   /* the hole */
            if (!pentatonic(k))
                continue;
            if (wm.up_until[k])
                mole(x, y, wm.gold[k] && (now / 120u & 1u), 0);
            else if (w && now - w < 300u && !wm.clunk[k])
                mole(x, y + (int32_t)(now - w) / 30, 0, 1);   /* whacked: dazed, going down */
        }
        cv_rect(0, 220, 240, 20, G_LILAC);
        cv_text(232 - text_w(&FONT_XS, wm.state == WS_PLAY ? "PLAY: STOP" : "PLAY: START"), 224, &FONT_XS,
                wm.state == WS_PLAY ? "PLAY: STOP" : "PLAY: START", G_INK);
        band_send(y0);
    }
}

static void whack_frame(uint32_t now)
{
    if (wm.state == WS_PLAY && (int32_t)(now - wm.t0) > WM_ROUND) {
        char t[8];
        wm.state = WS_OVER;
        wm.new_best = wm.score > sav.whack.best;
        if (wm.new_best) {
            sav.whack.best = (uint16_t)wm.score;
            save_dirty();
        }
        int_text(wm.score, t);
        app_say(wm.new_best ? "TIME'S UP. NEW BEST, SCORE" : "TIME'S UP, SCORE", t);
    }
    whack_draw(now);
}

static void whack_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    int i, up = (int)(ms / 500u % 5u);
    for (i = 0; i < 5; i++) {
        int32_t cx = x + w * (2 * i + 1) / 10, cy = y + h * 2 / 3;
        cv_rect(cx - 4, cy + 2, 9, 3, RGB(70, 50, 36));
        if (i == up)
            fill_circle(cx, cy - 2, 4, RGB(120, 84, 60));
    }
}

static void whack_best(char *t)
{
    t[0] = 0;
    if (sav.whack.best) {
        text_cat(t, "BEST ");
        int_text(sav.whack.best, t + 5);
    }
}

static uint32_t whack_keys_lit(uint32_t *mid)   /* the moles up: lit (a gold one flashing) */
{
    uint32_t keys = 0, now = plat_ms();
    int k;
    *mid = 0;
    if (wm.state != WS_PLAY)
        return 0;
    for (k = 0; k < NKEYS; k++)
        if (wm.up_until[k] && (!wm.gold[k] || (now / 120u & 1u)))
            keys |= 1u << k;
    return keys;
}

static void whack_where(char *t)
{
    t[0] = 0;
    if (wm.state == WS_STAND) {
        text_cat(t, "WHACK-A-MOLE. KNOB 1, LEVEL ");
        sp_num(t, sav.whack.level);
        text_cat(t, ". MOLES POP UP ON THE KEYS, LIT, WHACK THEM BEFORE THEY GO. ");
    } else {
        text_cat(t, wm.state == WS_OVER ? "WHACK-A-MOLE. TIME'S UP. SCORE " : "WHACK-A-MOLE. SCORE ");
        sp_num(t, wm.score);
        text_cat(t, ". ");
    }
    if (sav.whack.best) {
        text_cat(t, "BEST ");
        sp_num(t, sav.whack.best);
        text_cat(t, ". ");
    }
    text_cat(t, wm.state == WS_PLAY ? "PLAY STOPS." : "PLAY STARTS.");
}

static int whack_playing(void) { return wm.state == WS_PLAY; }

static const uint8_t WHACK_TUNE[] = {60, 0, 67, 64, 0, 69, 67, 0, 72, 0, 0, 0};

static const game_t GAME_WHACK = {"WHACK", RGB(120, 84, 60), whack_enter, whack_leave, whack_frame, whack_key,
                                  whack_button, whack_enc, whack_tick, whack_attract, whack_best, whack_keys_lit,
                                  WHACK_TUNE, sizeof WHACK_TUNE, 200, BW_PLUCK, whack_where, 1, whack_playing};
