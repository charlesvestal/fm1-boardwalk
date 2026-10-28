/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Balloons: the balloon pump at the fairground stall. Hold any key (the lit middle ones are the pump's pad) and the
 * balloon on the nozzle blows up; let go to tie it off. A dashed ring shows the size wanted, and its note plays as each balloon
 * goes on the nozzle; the balloon's stretch sings higher as it grows, reaching that note at the ring. Scored on how
 * close it came (10 dead on, then 6, 3, 1). Past the ring is a pop point you cannot see, a different one each time: as
 * it nears, the balloon wobbles and squeaks. Ten balloons a round; the tied ones float up to the string along the top.
 * The skill is the moment you let go: the size is worked out from the key's own time, to the millisecond.
 *
 * Levels (K1): 1 a steady, slow pump; 2 it surges with each stroke; 3 faster, tighter scoring, and it pops sooner.
 * PLAY starts and stops. Best: points in a round, sav.more[SV_BALLOON]. Part of the unity build. */
#include "game.h"

#define BLN_COL RGB(230, 100, 150)       /* the cabinet */
#define BLN_N 10                         /* balloons a round */
#define BLN_LEVELS 3
#define BLN_KNOT_Y 188                   /* the knot, on the nozzle */
#define BLN_X 150                        /* the nozzle's x: right of middle, so the result card fits beside it */
#define BLN_EMPTY 6.0f                   /* a balloon on the nozzle, flat */
#define BLN_TIE_MIN 13.0f                /* let go below this: still on the nozzle, nothing tied */
#define BLN_GARLAND_Y 60                 /* the string along the top */
#define BLN_PAD_LO 7                     /* the pad: the white keys C4..C5 */
#define BLN_PAD_HI 19
#define BLN_TIED_MS 1400u                /* a result on screen before the next balloon */
#define BLN_HOLD_MS 550u                 /* a tied one kept on the nozzle, by its ring, before it floats up */

enum { BLN_STAND, BLN_PLAY, BLN_OVER };
enum { BLN_WAIT, BLN_PUMP, BLN_TIED, BLN_POPPED };

static struct {
    int state, phase, n, level;          /* n: the balloon on the nozzle, 0..BLN_N-1 */
    int front;                           /* the cabinet in front (entered, not left) */
    int score, tied, popped, perfect, new_best;
    float r, target, pop_at;             /* the size (px), the ring's, the hidden pop point's */
    int strokes;                         /* the pump's strokes heard this balloon */
    int last_pts;                        /* the last balloon's points (-1: popped) */
    int too_big;                         /* the last one, past the ring */
    uint32_t keys;                       /* keys held */
    uint32_t phase_ms, squeak_ms, ref_ms, confetti_ms;
    int fan;                             /* the round's closing notes played so far */
    float from_x, from_y, from_r;        /* a tied balloon on its way up */
    uint8_t col0;                        /* the round's first colour */
    uint8_t slot[BLN_N];                 /* the string: 0 empty, 1 tied, 2 a scrap (popped) */
} bln;

static const uint16_t BLN_COLS[BLN_N] = {RGB(232, 80, 91), RGB(242, 163, 58), RGB(250, 210, 60), RGB(110, 190, 110),
                                         RGB(62, 154, 176), RGB(110, 143, 196), RGB(150, 100, 190), RGB(232, 88, 128),
                                         RGB(255, 140, 110), RGB(120, 200, 200)};
static const char *const BLN_LEVEL_NAME[BLN_LEVELS] = {"STEADY", "SURGING", "TIGHT"};

static uint16_t bln_col(int i) { return BLN_COLS[(i + bln.col0) % BLN_N]; }

static float bln_ry(float r) { return r * 1.14f; }   /* a balloon is a little taller than wide */

static int bln_cents(float r) { return 4500 + (int)(r * 62.0f); }   /* the stretch's pitch at a size */

static int bln_pad(int k)               /* a pad key: a white key from C4 to C5 */
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS;
    int i;
    for (i = 0; i < 16; i++)
        if (WK[i] == k)
            return k >= BLN_PAD_LO && k <= BLN_PAD_HI;
    return 0;
}

#define BLN_STROKE 0.6f                  /* s a stroke of the pump */

static float bln_size(uint32_t held_ms)  /* the size after the key has been held so long: a steady rate, and on level
                                          * 2 a surge each stroke (the rate's integral, so a release is exact) */
{
    static const float RATE[BLN_LEVELS] = {13.0f, 15.0f, 19.0f};
    float t = (float)held_ms * 0.001f, r = BLN_EMPTY + RATE[bln.level - 1] * t;
    if (bln.level == 2)
        r += 13.0f * BLN_STROKE / 6.2831853f * (1.0f - fm_cosf(t * (6.2831853f / BLN_STROKE)));
    return r;
}

static void bln_tone_off(void)
{
    snd_lock();
    bw_tone(0, bln_cents(bln.r), 0);
    snd_unlock();
}

static void bln_next(uint32_t now)       /* the next balloon on the nozzle: its ring and its pop point */
{
    static const uint8_t GAP_LO[BLN_LEVELS] = {7, 6, 4}, GAP_N[BLN_LEVELS] = {10, 9, 6};
    bln.phase = BLN_WAIT;
    bln.phase_ms = now;
    bln.r = BLN_EMPTY;
    bln.target = 22.0f + (float)rnd(21);
    bln.pop_at = bln.target + (float)(GAP_LO[bln.level - 1] + rnd(GAP_N[bln.level - 1])) + (float)rnd(10) * 0.1f;
    bln.strokes = 0;
    bln.ref_ms = now;                    /* the ring's note: the pitch the stretch reaches at it */
    snd_lock();
    bw_tone(1, bln_cents(bln.target), 22);
    snd_unlock();
}

static void balloon_go(void)
{
    uint32_t now = plat_ms();
    int i;
    rnd_state ^= now * 2654435761u;
    bln.level = clamp(sav.more[SV_BALLOON].level, 1, BLN_LEVELS);
    bln.n = 0;
    bln.score = bln.tied = bln.popped = bln.perfect = bln.new_best = 0;
    bln.col0 = (uint8_t)rnd(BLN_N);
    bln.confetti_ms = 0;
    for (i = 0; i < BLN_N; i++)
        bln.slot[i] = 0;
    bln.state = BLN_PLAY;
    bln_next(now);
    app_say("TEN BALLOONS", "HOLD ANY KEY TO PUMP");
}

static void bln_end(void)
{
    char t[16] = "SCORE ";
    bln.state = BLN_OVER;
    bln.phase_ms = plat_ms();            /* now the end's: the closing notes, the confetti */
    bln.fan = 0;
    bln.new_best = bln.score > sav.more[SV_BALLOON].best;
    if (bln.new_best) {
        sav.more[SV_BALLOON].best = (uint16_t)bln.score;
        save_dirty();
        bln.from_x = 127.0f;             /* confetti from behind the card's top */
        bln.from_y = 80.0f;
        bln.confetti_ms = bln.phase_ms;
    }
    int_text(bln.score, t + 6);
    app_say(bln.new_best ? "ROUND OVER. NEW BEST" : "ROUND OVER", t);
}

static void bln_stop(void)               /* everything quiet */
{
    snd_lock();
    bw_tone(0, 6000, 0);
    bw_tone(1, 6000, 0);
    snd_unlock();
}

static int bln_points(float err, int *kind)   /* how close: points, and kind 0 perfect .. 3 far, 4 too far */
{
    static const float L12[4] = {1.5f, 3.0f, 5.0f, 8.0f}, L3[4] = {1.0f, 2.0f, 3.5f, 5.5f};
    static const uint8_t PTS[4] = {10, 6, 3, 1};
    const float *w = bln.level == 3 ? L3 : L12;
    int i;
    err = err < 0.0f ? -err : err;
    for (i = 0; i < 4; i++)
        if (err <= w[i]) {
            *kind = i;
            return PTS[i];
        }
    *kind = 4;
    return 0;
}

static const char *const BLN_KIND[4] = {"PERFECT", "GOOD", "NEAR", "FAR"};

static const char *bln_result(void)      /* the last balloon, in a word or two */
{
    int kind;
    if (bln.last_pts < 0)
        return "POP";
    bln_points(bln.r - bln.target, &kind);
    return kind < 4 ? BLN_KIND[kind] : bln.too_big ? "TOO BIG" : "TOO SMALL";
}

static void bln_tie(uint32_t now)        /* let go: tied off, scored */
{
    int kind, pts = bln_points(bln.r - bln.target, &kind);
    char t[16];
    bln.phase = BLN_TIED;
    bln.phase_ms = now;
    bln.last_pts = pts;
    bln.too_big = bln.r > bln.target;
    bln.score += pts;
    bln.tied++;
    bln.slot[bln.n] = 1;
    bln.from_x = BLN_X;
    bln.from_r = bln.r;
    bln.from_y = (float)BLN_KNOT_Y - bln_ry(bln.r);
    snd_lock();
    bw_tone(0, bln_cents(bln.r), 0);
    bw_note(50, kind < 4 ? 60 : 85, BW_BASS);   /* the tie's boing: kept under the chimes when it scores */
    bw_note(62, 40, BW_PLUCK);
    if (kind == 0) {                     /* dead on: a sparkle */
        bw_note(79, 95, BW_CHIME);
        bw_note(84, 85, BW_CHIME);
        bw_note(88, 80, BW_CHIME);
    } else if (kind == 1) {
        bw_note(79, 85, BW_CHIME);
        bw_note(84, 75, BW_CHIME);
    } else if (kind < 4) {
        bw_note(kind == 2 ? 76 : 72, 75, BW_CHIME);
    } else {
        bw_note(48, 80, BW_DUD);
    }
    snd_unlock();
    if (kind == 0) {
        bln.perfect++;
        bln.confetti_ms = now;
    }
    t[0] = 0;
    if (pts) {
        int_text(pts, t);
        text_cat(t, pts == 1 ? " POINT" : " POINTS");
    } else {
        text_cat(t, "NO POINTS");
    }
    app_say(bln_result(), t);
}

static void bln_pop(uint32_t now)
{
    bln.phase = BLN_POPPED;
    bln.phase_ms = now;
    bln.last_pts = -1;
    bln.popped++;
    bln.slot[bln.n] = 2;
    snd_lock();                          /* bang */
    bw_tone(0, bln_cents(bln.r), 0);
    bw_drum(BW_CLAP, 127);
    bw_drum(BW_KICK, 120);
    bw_drum(BW_HAT, 127);
    bw_note(41, 90, BW_DUD);
    snd_unlock();
    app_say("POP", "NO POINTS");
}

static void balloon_key(int k, int down, uint32_t ms)
{
    if (down)
        bln.keys |= 1u << k;
    else
        bln.keys &= ~(1u << k);
    if (bln.state != BLN_PLAY) {         /* standing: the keys play */
        if (down) {
            snd_lock();
            bw_note(53 + k, 80, BW_PLUCK);
            snd_unlock();
        }
        return;
    }
    if (down && bln.phase == BLN_WAIT) { /* on the pump */
        bln.phase = BLN_PUMP;
        bln.phase_ms = ms;
        bln.r = BLN_EMPTY;
        bln.strokes = 0;
        snd_lock();
        bw_drum(BW_HAT, 40);
        bw_tone(1, bln_cents(bln.target), 0);
        snd_unlock();
    } else if (!bln.keys && bln.phase == BLN_PUMP) {   /* let go of the pump: the size at that very moment */
        bln.r = bln_size(ms - bln.phase_ms);
        if (bln.r >= bln.pop_at) {
            bln_pop(ms);
        } else if (bln.r >= BLN_TIE_MIN) {
            bln_tie(ms);
        } else {                         /* hardly any air: it stays on the nozzle */
            bln.phase = BLN_WAIT;
            bln_tone_off();
        }
    }
}

static void balloon_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (bln.state == BLN_PLAY) {
        bln.state = BLN_STAND;
        bln_stop();
        app_say("STOPPED", "");
    } else {
        balloon_go();
    }
}

static void balloon_enc(int role, int steps)
{
    char t[24];
    if (role != EN_K1 || bln.state == BLN_PLAY)
        return;
    sav.more[SV_BALLOON].level = (uint8_t)clamp(sav.more[SV_BALLOON].level + steps, 1, BLN_LEVELS);
    int_text(sav.more[SV_BALLOON].level, t);
    text_cat(t, ", ");
    text_cat(t, BLN_LEVEL_NAME[sav.more[SV_BALLOON].level - 1]);
    app_readout("LEVEL", t);
    bln.state = BLN_STAND;
    save_dirty();
}

static void balloon_enter(void)
{
    bln.front = 1;
    bln.state = BLN_STAND;
    bln.keys = 0;
    bln.r = 30.0f;
    bln.target = 30.0f;
}
static void balloon_leave(void)
{
    bln.front = 0;
    bln.state = BLN_STAND;
    bln_stop();
}

static void bln_step(uint32_t now)
{
    if (bln.phase == BLN_WAIT && bln.ref_ms && now - bln.ref_ms > 600u) {   /* the ring's note fades */
        bln.ref_ms = 0;
        snd_lock();
        bw_tone(1, bln_cents(bln.target), 0);
        snd_unlock();
    }
    if (bln.phase == BLN_PUMP) {
        float left;
        int cents, level, st;
        bln.r = bln_size(now - bln.phase_ms);
        st = (int)((float)(now - bln.phase_ms) * (0.001f / BLN_STROKE));
        if (st > bln.strokes) {          /* a stroke of the pump: a puff, now and then the rubber's creak */
            bln.strokes = st;
            snd_lock();
            bw_drum(BW_HAT, 16);
            if (rnd(3) == 0)
                bw_note(43 + (int)rnd(5), 26, BW_CLUNK);
            snd_unlock();
        }
        if (bln.r >= bln.pop_at) {
            bln_pop(now);
            return;
        }
        /* the stretch: quiet, its pitch the size; near the pop it strains (a shake in the pitch, louder) */
        left = bln.pop_at - bln.r;
        cents = bln_cents(bln.r);
        level = 13;
        if (left < 6.0f) {
            float near = (6.0f - left) / 6.0f;
            cents += (int)(fm_sinf((float)now * 0.045f) * 40.0f * near);
            level += (int)(near * 10.0f);
            if (now - bln.squeak_ms > 140u + rnd(260) && rnd(2)) {
                bln.squeak_ms = now;
                snd_lock();
                bw_note(86 + (int)rnd(6), 18 + (int)(near * 22.0f), BW_SINE);
                snd_unlock();
            }
        }
        snd_lock();
        bw_tone(0, cents, level);
        snd_unlock();
        return;
    }
    if ((bln.phase == BLN_TIED || bln.phase == BLN_POPPED) && now - bln.phase_ms > BLN_TIED_MS) {
        if (++bln.n >= BLN_N) {
            bln.n = BLN_N - 1;
            bln_end();
        } else {
            bln_next(now);
        }
    }
}

/* ------------------------------------------------------------------- the look --- */
static void bln_shape(int32_t cx, int32_t cy, float r, uint16_t c)   /* a balloon: an egg, narrower at the knot */
{
    float ry = bln_ry(r);
    int32_t y, h = (int32_t)ry;
    for (y = -h; y <= h; y++) {
        float f = (float)y / ry, w = 1.0f - f * f;
        w = w > 0.0f ? r * fm_sqrtf(w) * (1.0f - 0.16f * (f > 0.0f ? f : 0.0f)) : 0.0f;
        cv_rect(cx - (int32_t)w, cy + y, 2 * (int32_t)w + 1, 1, c);
    }
}

static void bln_balloon(int32_t cx, int32_t cy, float r, uint16_t c)   /* filled, shaded, its knot below */
{
    int32_t ry = (int32_t)bln_ry(r);
    bln_shape(cx, cy, r, mix(c, G_INK, 50));
    bln_shape(cx - (int32_t)(r * 0.06f), cy - (int32_t)(r * 0.06f), r * 0.93f, c);
    bln_shape(cx - (int32_t)(r * 0.38f), cy - (int32_t)(r * 0.45f), r * 0.2f + 1.0f, mix(c, C_WHITE, 150));
    cv_rect(cx - 2, cy + ry - 1, 5, 2, mix(c, G_INK, 60));   /* the knot */
    cv_rect(cx - 1, cy + ry + 1, 3, 2, mix(c, G_INK, 60));
}

static void bln_ring(int32_t cx, int32_t cy, float r, uint16_t c)   /* the dashed ring: the size wanted */
{
    float ry = bln_ry(r);
    int i;
    for (i = 0; i < 160; i++) {
        float a = (float)i * (6.2831853f / 160.0f), s = fm_sinf(a), co = fm_cosf(a), w;
        int32_t x, y;
        if ((i / 5) & 1)
            continue;
        w = r * (1.0f - 0.16f * (s > 0.0f ? s : 0.0f));
        x = cx + (int32_t)(co * w);
        y = cy + (int32_t)(s * ry);
        cv_rect(x - 1, y - 1, 2, 2, c);
    }
}

static void bln_garland(uint32_t now)    /* the string along the top, and the balloons on it */
{
    int32_t i, x;
    for (x = 0; x < 240; x += 4) {       /* sagging between pegs every 48 px */
        int32_t u = x % 48, s = u * (48 - u) / 96;
        cv_rect(x, BLN_GARLAND_Y + s, 4, 1, G_INK);
    }
    for (i = 0; i < BLN_N; i++) {
        int32_t sx = 12 + i * 24, u = sx % 48, sy = BLN_GARLAND_Y + u * (48 - u) / 96;
        int32_t bob = (int32_t)(fm_sinf((float)now * 0.002f + (float)i) * 1.5f);
        if (bln.slot[i] == 1 && !(bln.state == BLN_PLAY && i == bln.n && bln.phase == BLN_TIED)) {
            cv_line(sx, sy, sx + bob, sy - 8, G_INK);
            bln_balloon(sx + bob, sy - 18, 8.0f, bln_col(i));
        } else if (bln.slot[i] == 2) {   /* a scrap of rubber on its knot, hanging */
            uint16_t c = mix(bln_col(i), G_INK, 50);
            cv_line(sx, sy, sx, sy + 6, G_INK);
            cv_rect(sx - 2, sy + 6, 5, 3, c);
            cv_line(sx - 2, sy + 9, sx - 4, sy + 14, c);
            cv_line(sx + 2, sy + 9, sx + 3, sy + 15, c);
            cv_line(sx, sy + 9, sx, sy + 13, c);
        } else {
            cv_rect(sx - 1, sy - 1, 3, 3, G_PLANK2);   /* a peg waiting */
        }
    }
}

static void bln_awning(void)             /* the stall's striped awning */
{
    int32_t i;
    for (i = 0; i < 240; i += 20) {
        uint16_t c = (i / 20) & 1 ? G_PAPER : BLN_COL;
        cv_rect(i, 18, 20, 12, c);
        fill_circle(i + 10, 30, 9, c);
    }
    cv_rect(0, 18, 240, 2, mix(BLN_COL, G_INK, 90));
}

static void bln_stall(uint32_t now)      /* the counter, the pump and its hose, the nozzle */
{
    int32_t i, hy;
    cv_rect(0, 198, 240, 22, G_PLANK);
    cv_rect(0, 196, 240, 3, G_PLANK2);
    for (i = 0; i < 240; i += 40)
        cv_rect(i, 204, 1, 16, G_PLANK2);
    cv_rect(0, 212, 240, 1, G_PLANK2);
    /* the pump: a brass barrel; its handle goes down with each stroke */
    hy = 140;
    if (bln.state == BLN_PLAY && bln.phase == BLN_PUMP) {
        uint32_t sm = (uint32_t)(BLN_STROKE * 1000.0f), u = (now - bln.phase_ms) % sm;
        hy += (int32_t)((u < sm / 2u ? u : sm - u) * 72u / sm);
    }
    cv_rect(31, hy, 3, 156 - hy + 4, RGB(120, 110, 110));   /* the rod */
    round_rect(20, hy - 6, 25, 7, 3, G_INK);                 /* the handle */
    round_rect(24, 156, 17, 42, 3, RGB(214, 160, 60));       /* the barrel */
    cv_rect(27, 158, 3, 38, RGB(245, 205, 120));
    cv_rect(22, 156, 21, 3, RGB(160, 110, 40));
    cv_rect(41, 191, BLN_X - 46, 3, RGB(70, 60, 70));         /* the hose */
    /* the nozzle */
    cv_rect(BLN_X - 6, 191, 13, 5, RGB(214, 160, 60));
    cv_rect(BLN_X - 3, 186, 7, 5, RGB(214, 160, 60));
}

static void bln_confetti(uint32_t now)   /* a perfect one: a little confetti from where it was tied */
{
    static const uint16_t C[5] = {RGB(232, 80, 91), RGB(250, 210, 60), RGB(62, 154, 176), RGB(150, 100, 190),
                                  RGB(110, 190, 110)};
    uint32_t t = now - bln.confetti_ms, s = 0x9e3779b9u;
    float tt = (float)t * 0.001f;
    int i;
    if (!bln.confetti_ms || t > 1400u)
        return;
    for (i = 0; i < 28; i++) {
        float vx, vy;
        int32_t x, y;
        s = s * 1664525u + 1013904223u;
        vx = (float)((int32_t)(s >> 24) - 128) * 0.9f;
        s = s * 1664525u + 1013904223u;
        vy = -60.0f - (float)(s >> 25);
        x = (int32_t)(bln.from_x + vx * tt);
        y = (int32_t)(bln.from_y + vy * tt + 160.0f * tt * tt);
        cv_rect(x, y, (i & 1) ? 3 : 2, (i & 1) ? 2 : 3, C[i % 5]);
    }
}

static void balloon_draw(uint32_t now)
{
    int32_t y0, y;
    char s[40];
    uint32_t pt = now - bln.phase_ms;
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, G_SKY1);
        for (y = 18; y < 198; y += 2)    /* the golden-hour sky behind the stall */
            cv_rect(0, y, 240, 2, mix(G_SKY0, G_SKY2, (y - 18) * 256 / 180));
        cv_rect(210, 70, 22, 126, G_PLANK2);   /* the stall's post, and its price board */
        round_rect(196, 164, 42, 28, 4, G_PAPER);
        text_mid(217, 166, &FONT_XS, "10 FOR", G_INK);
        text_mid(217, 178, &FONT_XS, "A TIE", G_RED);
        bln_awning();
        bln_garland(now);
        bln_stall(now);
        cv_rect(0, 0, 240, 18, BLN_COL);
        cv_text(6, 3, &FONT_XS, "BALLOONS", G_PAPER);
        if (bln.state != BLN_STAND) {
            s[0] = 0;
            int_text(bln.n + 1, s);
            text_cat(s, "/10  SCORE ");
            int_text(bln.score, s + text_len(s));
            cv_text(234 - text_w(&FONT_XS, s), 3, &FONT_XS, s, G_PAPER);
        }
        if (bln.state == BLN_STAND) {   /* the card above, and a balloon waiting on the nozzle below it */
            bln_balloon(BLN_X, BLN_KNOT_Y - (int32_t)bln_ry(13.0f), 13.0f, bln_col(0));
            round_rect(14, 40, 212, 94, 8, G_PAPER);
            cv_text(24, 48, &FONT_XS, "K1 LEVEL", G_PLANK2);
            int_text(clamp(sav.more[SV_BALLOON].level, 1, BLN_LEVELS), s);
            text_cat(s, " ");
            text_cat(s, BLN_LEVEL_NAME[clamp(sav.more[SV_BALLOON].level, 1, BLN_LEVELS) - 1]);
            cv_text(104, 46, &FONT_B, s, G_INK);
            cv_text(24, 70, &FONT_XS, "HOLD ANY KEY TO PUMP IT UP.", G_PLANK2);
            cv_text(24, 84, &FONT_XS, "LET GO AT THE RING TO TIE IT.", G_PLANK2);
            cv_text(24, 98, &FONT_XS, sav.more[SV_BALLOON].level == 2 ? "IT SURGES. TOO FAR AND IT POPS!" :
                                      sav.more[SV_BALLOON].level == 3 ? "FASTER, AND IT POPS SOONER!" :
                                                                        "TOO FAR AND IT POPS!", G_PLANK2);
            s[0] = 0;
            text_cat(s, "10 BALLOONS");
            if (sav.more[SV_BALLOON].best) {
                text_cat(s, ". BEST ");
                int_text(sav.more[SV_BALLOON].best, s + text_len(s));
            }
            cv_text(24, 112, &FONT_B, s, G_INK);
        } else if (bln.state == BLN_OVER) {   /* the string full (or not); the card; confetti for a new best */
            bln_confetti(now);
            round_rect(52, 76, 150, 84, 8, G_PAPER);
            text_mid(127, 82, &FONT_B, "ROUND OVER", G_RED);
            text_cat((s[0] = 0, s), "SCORE ");
            int_text(bln.score, s + 6);
            text_mid(127, 104, &FONT_B, s, G_INK);
            s[0] = 0;
            int_text(bln.tied, s);
            text_cat(s, " TIED, ");
            int_text(bln.popped, s + text_len(s));
            text_cat(s, " POPPED");
            text_mid(127, 126, &FONT_XS, s, G_PLANK2);
            text_mid(127, 140, &FONT_XS, bln.new_best ? "NEW BEST!" : "", G_RED);
        } else {
            uint16_t c = bln_col(bln.n);
            if (bln.phase == BLN_WAIT || bln.phase == BLN_PUMP) {
                float r = bln.r, wob = 0.0f;
                int32_t cx = BLN_X, cy;
                if (bln.phase == BLN_PUMP && bln.pop_at - r < 6.0f) {   /* the warning: a wobble */
                    float near = (6.0f - (bln.pop_at - r)) / 6.0f;
                    wob = fm_sinf((float)now * 0.05f) * near * 2.5f;
                    cx += (int32_t)(fm_sinf((float)now * 0.11f) * near * 2.0f);
                }
                cy = BLN_KNOT_Y - (int32_t)bln_ry(r + wob);
                bln_balloon(cx, cy, r + wob, c);
                bln_ring(BLN_X, BLN_KNOT_Y - (int32_t)bln_ry(bln.target), bln.target,
                         r > bln.target - 1.0f ? G_PAPER : G_INK);
                if (bln.phase == BLN_WAIT && (now / 400u & 1u)) {   /* over the ring, under the string */
                    int32_t ty = BLN_KNOT_Y - 2 * (int32_t)bln_ry(bln.target) - 16;
                    text_mid(BLN_X, ty > 78 ? ty : 78, &FONT_XS, "HOLD ANY KEY", G_INK);
                }
            } else if (bln.phase == BLN_TIED) {   /* tied: a moment on the nozzle by its ring, then up to the string */
                float u = pt < BLN_HOLD_MS ? 0.0f : (float)(pt - BLN_HOLD_MS) / 750.0f;
                int32_t sx = 12 + bln.n * 24, gu = sx % 48, sy = BLN_GARLAND_Y + gu * (48 - gu) / 96 - 18;
                float r;
                u = u > 1.0f ? 1.0f : u;
                u = u * u * (3.0f - 2.0f * u);
                r = bln.from_r + (8.0f - bln.from_r) * u;
                bln_balloon((int32_t)(bln.from_x + ((float)sx - bln.from_x) * u),
                            (int32_t)(bln.from_y + ((float)sy - bln.from_y) * u) + (pt < 250u ? (int32_t)(pt / 60u & 1u) : 0),
                            r, c);
                if (pt < BLN_HOLD_MS)             /* how close it came */
                    bln_ring(BLN_X, BLN_KNOT_Y - (int32_t)bln_ry(bln.target), bln.target,
                             bln.last_pts == 10 ? G_TEAL : bln.r > bln.target - 1.0f ? G_PAPER : G_INK);
                if (u < 1.0f)
                    cv_line((int32_t)(bln.from_x + ((float)sx - bln.from_x) * u),
                            (int32_t)(bln.from_y + ((float)sy - bln.from_y) * u + bln_ry(r)),
                            (int32_t)(bln.from_x + ((float)sx - bln.from_x) * u) - 3,
                            (int32_t)(bln.from_y + ((float)sy - bln.from_y) * u + bln_ry(r)) + 12, G_INK);
                bln_confetti(now);
            } else {                     /* popped: shreds flying */
                int i;
                float tt = (float)pt * 0.001f;
                int32_t cy = BLN_KNOT_Y - (int32_t)bln_ry(bln.r);
                for (i = 0; i < 12; i++) {
                    float a = (float)i * 0.5236f + 0.2f, sp = 90.0f + (float)(i * 37 % 50);
                    int32_t x = BLN_X + (int32_t)(fm_cosf(a) * (bln.r * 0.7f + sp * tt));
                    int32_t yy = cy + (int32_t)(fm_sinf(a) * (bln.r * 0.7f + sp * tt) + 120.0f * tt * tt);
                    cv_rect(x, yy, 4 + (i & 1) * 2, 3, c);
                }
                if (pt < 120u)
                    ring_circle(BLN_X, cy, (int32_t)bln.r + (int32_t)(pt / 8u), G_PAPER);
                cv_rect(BLN_X - 2, BLN_KNOT_Y - 3, 5, 3, mix(c, G_INK, 60));   /* the knot left on the nozzle */
            }
            if (bln.phase == BLN_TIED || bln.phase == BLN_POPPED) {   /* the result, left of the nozzle */
                const char *w = bln_result();
                round_rect(6, 86, 96, 44, 8, G_PAPER);
                text_mid(54, 90, &FONT_B, bln.phase == BLN_POPPED ? "POP!" : w,
                         bln.phase == BLN_POPPED || bln.last_pts == 0 ? G_RED : bln.last_pts == 10 ? G_TEAL : G_INK);
                s[0] = 0;
                if (bln.last_pts > 0) {
                    text_cat(s, "+");
                    int_text(bln.last_pts, s + 1);
                } else {
                    text_cat(s, "NO POINTS");
                }
                text_mid(54, 112, &FONT_XS, s, G_PLANK2);
            }
        }
        cv_rect(0, 220, 240, 20, G_LILAC);
        {
            const char *f = bln.state == BLN_PLAY ? "PLAY: STOP" : bln.state == BLN_OVER ? "PLAY: AGAIN" : "PLAY: START";
            cv_text(232 - text_w(&FONT_XS, f), 224, &FONT_XS, f, G_INK);
            if (bln.state == BLN_STAND || (bln.state == BLN_PLAY && bln.phase == BLN_WAIT))
                cv_text(8, 224, &FONT_XS, "PUMP, DO NOT POP", G_PLANK2);
        }
        band_send(y0);
    }
}

static void bln_fanfare(uint32_t now)   /* the round's end: a run up the chord, on to the top for a new best */
{
    static const uint8_t RUN[6] = {72, 76, 79, 84, 88, 91};
    int n = bln.new_best ? 6 : 4;
    if (bln.fan < n && now - bln.phase_ms >= (uint32_t)bln.fan * 110u) {
        snd_lock();
        bw_note(RUN[bln.fan], bln.fan == n - 1 ? 95 : 80, BW_CHIME);
        snd_unlock();
        bln.fan++;
    }
}

static void balloon_frame(uint32_t now)
{
    if (bln.state == BLN_PLAY)
        bln_step(now);
    else if (bln.state == BLN_OVER)
        bln_fanfare(now);
    balloon_draw(now);
}

static void balloon_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    uint32_t t = ms % 2400u;
    int i = (int)(ms / 2400u % BLN_N);
    int32_t kx = x + w / 2, ky = y + h - 4;
    cv_rect(x, ky, w, 4, G_PLANK);
    if (t < 1900u) {                     /* blown up, then pop */
        float r = 3.0f + (float)t * ((float)(h / 2 - 4) / 1900.0f) * 0.9f;
        bln_balloon(kx, ky - (int32_t)bln_ry(r) - 2, r, BLN_COLS[i]);
    } else {
        int j;
        for (j = 0; j < 6; j++)
            cv_rect(kx + (j - 3) * 6, y + h / 2 - 6 + (j & 1) * 8, 3, 2, BLN_COLS[i]);
    }
}

static void balloon_best(char *t)
{
    t[0] = 0;
    if (sav.more[SV_BALLOON].best) {
        text_cat(t, "BEST ");
        int_text(sav.more[SV_BALLOON].best, t + 5);
    }
}

static uint32_t balloon_keys_lit(uint32_t *mid)   /* the pad glows (any key pumps); the keys held lit */
{
    uint32_t m = 0;
    int k;
    for (k = BLN_PAD_LO; k <= BLN_PAD_HI; k++)
        if (bln_pad(k))
            m |= 1u << k;
    *mid = m;
    return bln.state == BLN_PLAY ? bln.keys & m : 0u;
}

static void balloon_where(char *t)
{
    int lv = clamp(sav.more[SV_BALLOON].level, 1, BLN_LEVELS);
    t[0] = 0;
    if (bln.state == BLN_STAND) {
        text_cat(t, "BALLOONS. KNOB 1, LEVEL ");
        sp_num(t, lv);
        text_cat(t, ", ");
        text_cat(t, BLN_LEVEL_NAME[lv - 1]);
        text_cat(t, ". HOLD ANY KEY TO PUMP. THE RING'S NOTE PLAYS FIRST. LET GO WHEN THE BALLOON SINGS IT, "
                    "TO TIE IT OFF. TOO FAR AND IT POPS. ");
        if (sav.more[SV_BALLOON].best) {
            text_cat(t, "BEST ");
            sp_num(t, sav.more[SV_BALLOON].best);
            text_cat(t, ". ");
        }
        text_cat(t, "PLAY STARTS.");
        return;
    }
    if (bln.state == BLN_OVER) {
        text_cat(t, "BALLOONS. ROUND OVER. SCORE ");
        sp_num(t, bln.score);
        text_cat(t, ". ");
        sp_num(t, bln.tied);
        text_cat(t, " TIED, ");
        sp_num(t, bln.popped);
        text_cat(t, bln.new_best ? " POPPED. NEW BEST. PLAY STARTS AGAIN." : " POPPED. PLAY STARTS AGAIN.");
        return;
    }
    text_cat(t, "BALLOONS. BALLOON ");
    sp_num(t, bln.n + 1);
    text_cat(t, " OF 10. ");
    if (bln.phase == BLN_PUMP) {
        text_cat(t, "SIZE ");
        sp_pct(t, (int32_t)(bln.r * 100.0f / bln.target + 0.5f));
        text_cat(t, " OF THE RING. ");
    } else if (bln.phase == BLN_WAIT) {
        text_cat(t, "HOLD ANY KEY TO PUMP. ");
    } else {
        text_cat(t, bln_result());
        text_cat(t, ". ");
    }
    text_cat(t, "SCORE ");
    sp_num(t, bln.score);
    text_cat(t, ". PLAY STOPS.");
}

static int balloon_playing(void) { return bln.state == BLN_PLAY; }

#ifdef PLAT_HOST
/* the test player: autoballoon on (pumps, easing off near the ring, lets go on it) | over (never lets go: pops) |
 * off */
static int host_autoballoon;
static int balloon_host_cmd(const char *cmd, const char *a)
{
    if (strcmp(cmd, "autoballoon"))
        return 0;
    host_autoballoon = !strcmp(a, "on") ? 1 : !strcmp(a, "over") ? 2 : !strcmp(a, "early") ? 3 : 0;
    if (!host_autoballoon && bln.keys)
        balloon_key(9, 0, plat_ms());
    return 1;
}
static int balloon_expect(const char *what, int *got)
{
    if (!strcmp(what, "balloon_state"))  /* 0 standing, 1 a round, 2 over */
        *got = bln.state;
    else if (!strcmp(what, "balloon_phase"))   /* 0 waiting, 1 pumping, 2 tied, 3 popped */
        *got = bln.phase;
    else if (!strcmp(what, "balloon_n"))
        *got = bln.n;
    else if (!strcmp(what, "balloon_score"))
        *got = bln.score;
    else if (!strcmp(what, "balloon_tied"))
        *got = bln.tied;
    else if (!strcmp(what, "balloon_popped"))
        *got = bln.popped;
    else if (!strcmp(what, "balloon_perfect"))
        *got = bln.perfect;
    else if (!strcmp(what, "balloon_best"))
        *got = sav.more[SV_BALLOON].best;
    else if (!strcmp(what, "balloon_level"))
        *got = sav.more[SV_BALLOON].level;
    else
        return 0;
    return 1;
}
static void balloon_host_ms(uint32_t now)   /* on: let go on the ring; early: at four fifths of it; over: never */
{
    if (!host_autoballoon || !bln.front || bln.state != BLN_PLAY)
        return;
    if (bln.phase == BLN_WAIT && now - bln.phase_ms > 400u && !bln.keys)
        balloon_key(9, 1, now);
    else if (bln.phase == BLN_PUMP && host_autoballoon != 2 &&
             bln_size(now - bln.phase_ms) >= bln.target * (host_autoballoon == 3 ? 0.8f : 1.0f) - 0.1f)
        balloon_key(9, 0, now);
}
static void balloon_speak_dump(void)
{
    int i, ph;
    static const int PTS[6] = {10, 6, 3, 1, 0, 0};
    static const char *const MSG[] = {"TEN BALLOONS, HOLD ANY KEY TO PUMP", "STOPPED", "POP, NO POINTS",
                                      "PERFECT, 10 POINTS", "GOOD, 6 POINTS", "NEAR, 3 POINTS", "FAR, 1 POINT",
                                      "TOO SMALL, NO POINTS", "TOO BIG, NO POINTS", "ROUND OVER, SCORE 64",
                                      "ROUND OVER. NEW BEST, SCORE 64"};
    char t[24];
    sav.more[SV_BALLOON].best = 0;
    for (i = 1; i <= BLN_LEVELS; i++) {  /* each level standing (a best, and none) */
        sav.more[SV_BALLOON].level = (uint8_t)i;
        bln.state = BLN_STAND;
        balloon_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        int_text(i, t);
        text_cat(t, ", ");
        text_cat(t, BLN_LEVEL_NAME[i - 1]);
        sp_join("LEVEL", t);
        sp_out("READOUT", sp_buf);
        sav.more[SV_BALLOON].best = 64;
    }
    bln.level = 1;
    bln.state = BLN_PLAY;
    bln.n = 3;
    bln.score = 22;
    bln.target = 30.0f;
    for (ph = BLN_WAIT; ph <= BLN_POPPED; ph++) {   /* a round: each phase, and each result */
        bln.phase = ph;
        bln.r = 24.0f;
        if (ph == BLN_TIED) {
            for (i = 0; i < 6; i++) {
                bln.last_pts = PTS[i];
                bln.r = i == 0 ? 30.0f : i == 1 ? 32.5f : i == 2 ? 26.0f : i == 3 ? 23.0f : i == 4 ? 15.0f : 45.0f;
                bln.too_big = bln.r > bln.target;
                balloon_where(sp_buf);
                sp_out("SCREEN", sp_buf);
            }
            continue;
        }
        bln.last_pts = -1;
        balloon_where(sp_buf);
        sp_out("SCREEN", sp_buf);
    }
    bln.state = BLN_OVER;
    bln.score = 64;
    bln.tied = 7;
    bln.popped = 3;
    for (i = 0; i < 2; i++) {
        bln.new_best = i;
        balloon_where(sp_buf);
        sp_out("SCREEN", sp_buf);
    }
    for (i = 0; i < (int)(sizeof MSG / sizeof MSG[0]); i++)
        sp_out("MSG", MSG[i]);
    bln.state = BLN_STAND;
}
#endif

static const uint8_t BALLOON_TUNE[] = {67, 0, 72, 72, 76, 0, 72, 0, 79, 0, 76, 72, 74, 0, 0, 0};

static const game_t GAME_BALLOON = {"BALLOONS", BLN_COL, balloon_enter, balloon_leave, balloon_frame, balloon_key,
                                    balloon_button, balloon_enc, 0, balloon_attract, balloon_best, balloon_keys_lit,
                                    BALLOON_TUNE, sizeof BALLOON_TUNE, 190, BW_PLUCK, balloon_where, 0,
                                    balloon_playing, "PUMP, DO NOT POP", 0};
