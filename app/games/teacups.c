/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Teacups: the spinning-teacup ride. The ride's music plays, a fairground waltz in C (the oom on the bass, the pah-pah
 * plucked, the tune on the chime, from the tick); tap any key on the beat and your cup spins faster, off the beat it
 * wobbles and slows, and a beat left out lets it slow too. Each key plays its note snapped to the waltz's chord, so
 * tapping sounds like joining in. At top speed a countermelody comes in, the canopy's bulbs chase and the ride swims a
 * little. A ride is 60 seconds; the score is the tenths of a second held at top speed plus the peak spin.
 *
 * Levels (K1): 1 a tap a bar (the oom), 2 every beat (oom-pah-pah), 3 every beat of a faster waltz, judged closer.
 * During the ride any knob spins the cup a little too (a DJ's scratch), never up to top speed: that takes the beat.
 * PLAY starts and stops. Its level and best are sav.more[SV_TEACUPS]. Part of the unity build. */
#include "game.h"

#define TC_RIDE 60000                    /* ms a ride */
#define TC_TOP 85.0f                     /* the spin that counts as top speed */
#define TC_TOP_OFF 75.0f                 /* below this it is no longer at the top (for the words and the music) */
#define TC_KNOB_MAX 70.0f                /* as far as the knobs alone can spin it */
#define TC_LAG 0                         /* ms from a key to its sound, taken off the taps' times (to set on the FM-1) */
#define TC_LEVELS 3
#define TC_CAB RGB(90, 170, 140)
#define TC_CAB2 RGB(58, 128, 104)

enum { TC_STAND, TC_RIDE_ON, TC_OVER };
enum { TW_NONE, TW_PERFECT, TW_GOOD, TW_WOBBLE, TW_MISSED };

/* each level: its tempo, the window a tap counts in and the closer one that is perfect (ms either side), what a tap
 * gives, what a beat left out and a wobble take (spin, of 100) */
static const struct {
    int32_t bpm, win, perfect;
    float gain, good, miss, wobble;
} TC_LV[TC_LEVELS] = {
    {144, 100, 45, 14.0f, 9.0f, 10.0f, 8.0f},
    {144, 90, 40, 5.0f, 3.0f, 4.0f, 6.0f},
    {176, 70, 35, 4.5f, 2.5f, 4.0f, 6.0f},
};
static const char *const TC_LEVEL_WORD[TC_LEVELS] = {"ONE A BAR", "EVERY BEAT", "FAST WALTZ"};

static struct {
    int state, level, new_best, score;
    int32_t beat;                        /* ms a beat */
    volatile uint32_t t0;                /* plat_ms of the first judged beat (a bar of the waltz before it) */
    volatile float speed, peak;          /* the spin, 0..100 */
    volatile uint32_t top_ms;            /* held at top speed */
    volatile int step;                   /* the tick's next eighth (from -6: the bar before) */
    volatile int judged;                 /* the beats judged so far (a target beat not tapped is missed) */
    volatile int tapped;                 /* the last beat tapped on time */
    volatile int hits, perfect, wobbles, misses;
    volatile int word;                   /* the last tap's word (TW_*), for the screen */
    volatile uint32_t word_ms, wobble_ms, last_tick;
    volatile int knob;                   /* knob detents for the tick to spin with */
    volatile uint8_t at_top;             /* at top speed (with TC_TOP_OFF's slack) */
    uint8_t said_top;
    float cup, plat;                     /* the cup's turn and the platform's (radians; the main loop's) */
    uint32_t drawn_ms, over_ms;
} tc;

/* the waltz: sixteen bars on C, F and G7, and its tune, a note a beat (0 rests, or lets the last ring) */
static const uint8_t TC_CHORDS[16] = {0, 0, 2, 2, 2, 2, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0};
static const uint8_t TC_ROOT[3] = {60, 53, 55};   /* the oom (the bass sounds an octave down) */
static const uint8_t TC_PAH[3][2] = {{64, 67}, {65, 69}, {65, 71}};
static const uint16_t TC_PCS[3] = {1u << 0 | 1u << 4 | 1u << 7, 1u << 5 | 1u << 9 | 1u << 0,
                                   1u << 7 | 1u << 11 | 1u << 2 | 1u << 5};   /* each chord's pitch classes */
static const uint8_t TC_COUNTER[3][3] = {{88, 91, 88}, {89, 93, 89}, {86, 89, 86}};   /* top speed's: on the ands */
static const uint8_t TC_MEL[16][3] = {
    {72, 76, 79}, {79, 0, 76}, {74, 77, 79}, {77, 0, 74}, {71, 74, 77}, {74, 0, 71}, {72, 76, 72}, {72, 0, 0},
    {69, 72, 77}, {77, 0, 76}, {76, 72, 79}, {79, 0, 76}, {74, 77, 74}, {71, 74, 79}, {76, 74, 72}, {72, 0, 0},
};

static int tc_lv(void) { return clamp(sav.more[SV_TEACUPS].level, 1, TC_LEVELS) - 1; }
static int tc_target(int b) { return b >= 0 && (tc.level > 0 || b % 3 == 0); }   /* a beat to tap */
static int tc_chord_at(int32_t t)        /* the chord at ride time t (the bar before: the last bar's) */
{
    int32_t bar = t < 0 ? -1 : t / (tc.beat * 3);
    return TC_CHORDS[(bar + 16) % 16];
}

static int tc_snap(int note, int chord)  /* a note to the nearest of the chord's notes */
{
    int d;
    for (d = 0; d < 7; d++) {
        if (TC_PCS[chord] >> ((note - d) % 12) & 1u)
            return note - d;
        if (TC_PCS[chord] >> ((note + d) % 12) & 1u)
            return note + d;
    }
    return note;
}

static void tc_go(void)
{
    rnd_state ^= plat_ms() * 2654435761u;
    tc.level = tc_lv();
    tc.beat = 60000 / TC_LV[tc.level].bpm;
    tc.speed = tc.peak = 0.0f;
    tc.top_ms = 0;
    tc.step = -6;
    tc.judged = -1;
    tc.tapped = -1;
    tc.hits = tc.perfect = tc.wobbles = tc.misses = 0;
    tc.word = TW_NONE;
    tc.knob = 0;
    tc.at_top = tc.said_top = 0;
    tc.new_best = 0;
    tc.last_tick = plat_ms();
    tc.t0 = plat_ms() + 300u + (uint32_t)(3 * tc.beat);   /* a bar of the waltz first */
    tc.state = TC_RIDE_ON;
    app_say("HOLD ON", tc.level == 0 ? "TAP ON THE FIRST BEAT OF EACH BAR" : "TAP ON EVERY BEAT");
}

/* an eighth of the waltz: on the beats the oom or a pah and the tune; on the ands, at top speed, the countermelody */
static void tc_music(int s)
{
    int beat = s >> 1, bar = beat >= 0 ? beat / 3 : -1, bb = beat >= 0 ? beat % 3 : beat + 3,
        ch = TC_CHORDS[(bar + 16) % 16];
    if (s & 1) {
        if (tc.at_top && bar >= 0)
            bw_note(TC_COUNTER[ch][bb], 50, BW_SINE);
        return;
    }
    if (bb == 0) {
        bw_note(TC_ROOT[ch], 92, BW_BASS);
    } else {
        bw_note(TC_PAH[ch][0], 46, BW_PLUCK);
        bw_note(TC_PAH[ch][1], 42, BW_PLUCK);
    }
    if (bar >= 0 && TC_MEL[bar % 16][bb])
        bw_note(TC_MEL[bar % 16][bb], 72, BW_CHIME);
}

/* the tick: the ride's music, the spin's drag, the beats left out */
static void teacups_tick(uint32_t ms)
{
    int32_t t = (int32_t)(ms - tc.t0), dt;
    float sp;
    if (tc.state != TC_RIDE_ON)
        return;
    dt = clamp((int32_t)(ms - tc.last_tick), 0, 20);
    tc.last_tick = ms;
    if (t >= TC_RIDE)
        return;                          /* (the main loop ends it) */
    while (tc.step * (tc.beat / 2) <= t)
        tc_music(tc.step++);
    if (t < 0)
        return;
    sp = tc.speed;
    if (tc.knob) {                       /* a knob's spin, to TC_KNOB_MAX at most */
        int n = tc.knob < 0 ? -tc.knob : tc.knob;
        tc.knob = 0;
        if (sp < TC_KNOB_MAX)
            sp = fm_minf(sp + 0.8f * (float)n, TC_KNOB_MAX);
    }
    sp -= (1.5f + sp * 0.06f) * (float)dt * 0.001f;   /* the drag: more the faster it goes */
    while ((tc.judged + 1) * tc.beat + TC_LV[tc.level].win < t) {   /* a beat whose window has closed */
        int b = ++tc.judged;
        if (tc_target(b) && tc.tapped != b) {
            tc.misses++;
            sp -= TC_LV[tc.level].miss;
            tc.word = TW_MISSED;
            tc.word_ms = ms;
        }
    }
    sp = fm_clampf(sp, 0.0f, 100.0f);
    tc.speed = sp;
    if (sp > tc.peak)
        tc.peak = sp;
    if (sp >= TC_TOP)
        tc.top_ms += (uint32_t)dt;
    tc.at_top = sp >= TC_TOP ? 1 : sp < TC_TOP_OFF ? 0 : tc.at_top;
}

/* a key (in the tick): its note snapped to the chord; on a beat to tap it spins the cup, off it a wobble */
static void teacups_key(int k, int down, uint32_t ms)
{
    int32_t t = (int32_t)(ms - tc.t0) - TC_LAG, b, d;
    int n;
    if (!down)
        return;
    n = tc_snap(53 + k, tc.state == TC_RIDE_ON ? tc_chord_at(t) : 0);
    if (tc.state != TC_RIDE_ON || t < -tc.beat / 2 || t >= TC_RIDE) {
        bw_note(n, 90, BW_PLUCK);
        return;
    }
    b = (t + tc.beat / 2) / tc.beat;
    d = t - b * tc.beat;
    d = d < 0 ? -d : d;
    if (tc_target(b) && d <= TC_LV[tc.level].win && tc.tapped != b && b > tc.judged) {
        int perfect = d <= TC_LV[tc.level].perfect;
        tc.tapped = b;
        tc.hits++;
        tc.perfect += perfect;
        tc.speed = fm_minf(tc.speed + (perfect ? TC_LV[tc.level].gain : TC_LV[tc.level].good), 100.0f);
        tc.word = perfect ? TW_PERFECT : TW_GOOD;
        bw_note(n, perfect ? 108 : 92, perfect ? BW_CHIME : BW_PLUCK);
    } else {
        tc.wobbles++;
        tc.speed = fm_maxf(tc.speed - TC_LV[tc.level].wobble, 0.0f);
        tc.word = TW_WOBBLE;
        tc.wobble_ms = ms ? ms : 1;
        bw_note(n, 80, BW_DUD);
    }
    tc.word_ms = ms;
}

static void tc_stop(void)
{
    tc.state = TC_STAND;
    tc.speed = 0.0f;
    tc.at_top = 0;
}

static void teacups_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (tc.state == TC_RIDE_ON) {
        tc_stop();
        app_say("STOPPED", "");
    } else {
        tc_go();
    }
}

static void teacups_enc(int role, int steps)
{
    char t[24];
    if (tc.state == TC_RIDE_ON) {        /* any knob spins it a little */
        snd_lock();
        tc.knob += steps;
        snd_unlock();
        return;
    }
    if (role != EN_K1)
        return;
    sav.more[SV_TEACUPS].level = (uint8_t)clamp(sav.more[SV_TEACUPS].level + steps, 1, TC_LEVELS);
    t[0] = 0;
    sp_num(t, sav.more[SV_TEACUPS].level);
    text_cat(t, ", ");
    text_cat(t, TC_LEVEL_WORD[tc_lv()]);
    app_readout("LEVEL", t);
    tc.state = TC_STAND;
    save_dirty();
}

static void teacups_enter(void)
{
    tc_stop();
    tc.drawn_ms = plat_ms();
}
static void teacups_leave(void) { tc_stop(); }

/* ---------------------------------------------------------------- drawing --- */
static void tc_ellipse(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c)
{
    int32_t y;
    for (y = -ry; y <= ry; y++) {
        float f = 1.0f - (float)(y * y) / (float)(ry * ry);
        int32_t w = (int32_t)((float)rx * fm_sqrtf(f > 0.0f ? f : 0.0f) + 0.5f);
        cv_rect(cx - w, cy + y, 2 * w + 1, 1, c);
    }
}

static void tc_lip(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c)   /* an ellipse's front edge, 2 px */
{
    int32_t x;
    for (x = -rx; x <= rx; x++) {
        float f = 1.0f - (float)(x * x) / (float)(rx * rx);
        int32_t y = (int32_t)((float)ry * fm_sqrtf(f > 0.0f ? f : 0.0f) + 0.5f);
        cv_rect(cx + x, cy + y - 1, 1, 2, c);
    }
}

/* the turntable from above at a slant: eight stripes turning with ang; only the rows of the band being drawn */
static void tc_platform(int32_t y0, int32_t cx, int32_t cy, int32_t rx, int32_t ry, float ang)
{
    static const uint16_t STRIPE[2] = {TC_CAB, G_PAPER2};
    float c[4], s[4], k = (float)rx / (float)ry;
    int32_t y, i, ya = cy - ry > y0 ? cy - ry : y0, yb = cy + ry < y0 + BAND_H - 1 ? cy + ry : y0 + BAND_H - 1;
    tc_ellipse(cx, cy + 6, rx, ry, TC_CAB2);   /* its side */
    for (i = 0; i < 4; i++) {
        c[i] = fm_cosf(ang + (float)i * 0.785398f);
        s[i] = fm_sinf(ang + (float)i * 0.785398f);
    }
    for (y = ya; y <= yb; y++) {
        float v = (float)(y - cy) * k, f = 1.0f - (float)((y - cy) * (y - cy)) / (float)(ry * ry);
        int32_t w = (int32_t)((float)rx * fm_sqrtf(f > 0.0f ? f : 0.0f)), x, run = -w;
        int last = -1;
        for (x = -w; x <= w + 1; x++) {
            int p = 0;
            if (x <= w) {
                float u = (float)x;
                for (i = 0; i < 4; i++)
                    p ^= u * s[i] - v * c[i] > 0.0f;
            }
            if (x > w || p != last) {
                if (last >= 0)
                    cv_rect(cx + run, y, x - run, 1, STRIPE[last]);
                last = p;
                run = x;
            }
        }
    }
    tc_lip(cx, cy, rx, ry, G_PAPER);
}

/* a teacup from the side, its rim's middle at (cx, rim): hw half its width, h its depth, turned ang (its handle and
 * its spots go round). big: the saucer under it and two riders in it */
static void tc_cup(int32_t cx, int32_t rim, int32_t hw, int32_t h, float ang, uint16_t body, uint16_t spot, int big)
{
    float sa = fm_sinf(ang), ca = fm_cosf(ang);
    int32_t y, i, ry = hw / 4 + 1;
    int32_t hx = cx + (int32_t)((float)(hw + 2) * sa), hy = rim + h * 2 / 5, hr = hw / 4 + 2;
    uint16_t shade = mix(body, G_INK, 60);
    if (big)
        tc_ellipse(cx, rim + h + 3, hw + 16, 8, mix(G_PAPER2, G_PLANK2, 90));   /* the saucer */
    if (big) {
        tc_ellipse(cx, rim + h + 1, hw + 14, 7, G_PAPER2);
        tc_lip(cx, rim + h + 1, hw + 14, 7, G_PLANK);
    }
    if (ca <= 0.0f)                      /* the handle: behind the cup, or in front of it (after) */
        for (y = 0; y < 3; y++)
            ring_circle(hx, hy, hr - y, shade);
    for (y = 0; y <= h; y++) {           /* the bowl: round at the bottom; shaded at its edges */
        float v = (float)y / (float)h, f = 1.0f - v * v * 0.86f;
        int32_t w = (int32_t)((float)hw * fm_sqrtf(f));
        cv_rect(cx - w, rim + y, 2 * w + 1, 1, body);
        cv_rect(cx + w - w / 4, rim + y, w / 4 + 1, 1, shade);
        cv_rect(cx - w, rim + y, w / 8 + 1, 1, mix(body, G_PAPER, 120));
    }
    for (i = 0; i < 6; i++) {            /* the spots, on the side facing us */
        float a = ang + (float)i * 1.047198f + (i & 1 ? 0.5f : 0.0f), c = fm_cosf(a);
        int32_t row = h * (i & 1 ? 62 : 30) / 100;
        float v = (float)row / (float)h;
        int32_t w = (int32_t)((float)hw * fm_sqrtf(1.0f - v * v * 0.86f));
        if (c > 0.15f)
            fill_circle(cx + (int32_t)((float)(w - 4) * fm_sinf(a)), rim + row, (int32_t)((float)(hw / 9 + 1) * c + 1.0f),
                        spot);
    }
    tc_ellipse(cx, rim, hw, ry, mix(body, G_INK, 150));   /* the inside */
    if (big) {                           /* the riders, going round with the cup */
        for (i = 0; i < 2; i++) {
            float a = ang + 0.9f + (float)i * 3.14159f;
            int32_t px = cx + (int32_t)((float)hw * 0.5f * fm_sinf(a)), py = rim - 7 - (int32_t)(3.0f * fm_cosf(a));
            fill_circle(px, py, 6, G_BLUSH);
            cv_rect(px - 6, py - 6, 13, 4, i ? G_ORANGE : G_WHEEL);   /* a hat */
            cv_rect(px - 2, py - 1, 2, 2, G_INK);
            cv_rect(px + 2, py - 1, 2, 2, G_INK);
        }
    }
    tc_lip(cx, rim, hw, ry, mix(body, G_PAPER, 140));   /* the lip in front */
    if (ca > 0.0f)
        for (y = 0; y < 3; y++)
            ring_circle(hx, hy, hr - y, body);
}

static void tc_swim(int32_t y0, uint32_t now)   /* top speed: the band's rows sway a little, side to side */
{
    int32_t y;
    for (y = y0; y < y0 + BAND_H; y++) {
        int32_t dx = (int32_t)(2.6f * fm_sinf((float)y * 0.11f + (float)(now % 100000u) * 0.007f)), x;
        uint16_t *row = cv_px + (uint32_t)(y - y0) * cv_w;
        if (y < 48 || y > 206 || !dx)
            continue;
        if (dx > 0)
            for (x = 239; x >= dx; x--)
                row[x] = row[x - dx];
        else
            for (x = 0; x < 240 + dx; x++)
                row[x] = row[x - dx];
    }
}

static void tc_scene(int32_t y0, uint32_t now)   /* the sky, the sea, the boards, the canopy, the ride */
{
    int32_t y, i, lit = (int32_t)(tc.speed * 12.0f / 100.0f + 0.5f);
    int top = tc.state == TC_RIDE_ON && tc.at_top;
    for (y = 18; y < 98; y += 2)
        cv_rect(0, y, 240, 2, mix(G_SKY2, G_SKY0, (y - 18) * 256 / 80));
    fill_circle(40, 98, 16, G_SUN);
    cv_rect(0, 98, 240, 14, G_SEA);
    for (i = -20; i < 240; i += 24)
        cv_rect(i + (int32_t)(now / 150u % 24u), 102, 10, 2, G_FOAM);
    cv_rect(0, 112, 240, 108, G_PLANK);
    for (y = 120; y < 220; y += 10)
        cv_rect(0, y, 240, 1, G_PLANK2);
    for (i = 0; i < 12; i++) {           /* the canopy: stripes and scallops; a bulb under each, lit as it spins */
        uint16_t c = i & 1 ? G_PAPER : G_RED;
        int on = i < lit && (!top || ((int32_t)(now / 120u) + i) % 3 != 0);
        cv_rect(i * 20, 18, 20, 16, c);
        fill_circle(i * 20 + 10, 33, 10, c);
        fill_circle(i * 20 + 10, 46, 3, on ? (top ? G_SUN : G_BULB) : G_PLANK2);
    }
    cv_rect(0, 18, 240, 2, G_RED2);
    tc_platform(y0, 120, 160, 112, 38, tc.plat);
}

static void tc_others(int front)         /* the other cups ride the platform: those behind yours, or in front */
{
    int i;
    for (i = 0; i < 2; i++) {
        float a = tc.plat + 1.2f + (float)i * 3.14159f, sn = fm_sinf(a);
        if ((sn > 0.0f) != front)
            continue;
        tc_cup(120 + (int32_t)(84.0f * fm_cosf(a)), 148 + (int32_t)(24.0f * sn), 14, 16, tc.plat * 2.0f + (float)i,
               i ? G_LILAC : G_ORANGE, G_PAPER, 0);
    }
}

static void tc_mine(uint32_t now)        /* your cup, in the middle; a wobble shakes it */
{
    int32_t dx = 0;
    if (tc.wobble_ms && now - tc.wobble_ms < 450u) {
        float f = (float)(450u - (now - tc.wobble_ms)) / 450.0f;
        dx = (int32_t)(6.0f * f * fm_sinf((float)(now - tc.wobble_ms) * 0.06f));
    }
    tc_cup(120 + dx, 120, 40, 34, tc.cup, G_PAPER, G_RED, 1);
}

static void tc_dial(void)                /* the speed: a gauge, its top end red */
{
    char s[8];
    int32_t i, cx = 200, cy = 84, r = 24;
    float a = 3.14159f * (1.0f - tc.speed / 100.0f);
    round_rect(166, 52, 68, 52, 6, G_PAPER);
    for (i = 0; i <= 10; i++) {
        float b = 3.14159f * (1.0f - (float)i / 10.0f);
        int on = tc.speed + 0.5f >= (float)(i * 10);
        fill_circle(cx + (int32_t)((float)r * fm_cosf(b)), cy - (int32_t)((float)r * fm_sinf(b)), 2,
                    i * 10 >= (int32_t)TC_TOP ? (on ? G_RED : mix(G_RED, G_PAPER, 170)) : on ? TC_CAB : G_PAPER2);
    }
    for (i = -1; i <= 1; i++)
        cv_line(cx + i, cy, cx + i + (int32_t)((float)(r - 6) * fm_cosf(a)), cy - (int32_t)((float)(r - 6) * fm_sinf(a)),
                G_INK);
    fill_circle(cx, cy, 3, G_INK);
    int_text((int32_t)(tc.speed + 0.5f), s);
    text_cat(s, "%");
    text_mid(cx, 88, &FONT_XS, s, G_INK);
}

static void tc_tenths(uint32_t ms, char *s)   /* "12.3" */
{
    int_text((int32_t)(ms / 1000u), s);
    text_cat(s, ".");
    int_text((int32_t)(ms / 100u % 10u), s + text_len(s));
}

static void teacups_draw(uint32_t now)
{
    int32_t y0, t = (int32_t)(now - tc.t0);
    char s[32];
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, G_SKY1);
        tc_scene(y0, now);
        tc_others(0);
        tc_mine(now);
        tc_others(1);
        if (tc.state == TC_RIDE_ON && tc.at_top)
            tc_swim(y0, now);
        cv_rect(0, 0, 240, 18, TC_CAB);
        cv_text(6, 3, &FONT_XS, "TEACUPS", G_PAPER);
        s[0] = 0;
        if (tc.state == TC_RIDE_ON) {
            int_text(t < 0 ? TC_RIDE / 1000 : clamp((TC_RIDE - t + 999) / 1000, 0, TC_RIDE / 1000), s);
            text_cat(s, " S LEFT");
        } else if (tc.state == TC_OVER) {
            text_cat(s, "SCORE ");
            int_text(tc.score, s + 6);
        } else if (sav.more[SV_TEACUPS].best) {
            text_cat(s, "BEST ");
            int_text(sav.more[SV_TEACUPS].best, s + 5);
        }
        cv_text(234 - text_w(&FONT_XS, s), 3, &FONT_XS, s, G_PAPER);
        if (tc.state == TC_STAND) {
            round_rect(14, 30, 212, 74, 8, G_PAPER);   /* above your cup, so it shows (its best is up top) */
            cv_text(24, 38, &FONT_XS, "K1 LEVEL", G_PLANK2);
            int_text(tc_lv() + 1, s);
            cv_text(94, 36, &FONT_B, s, G_INK);
            cv_text(110, 38, &FONT_XS, TC_LEVEL_WORD[tc_lv()], G_PLANK2);
            cv_text(24, 60, &FONT_XS, tc_lv() == 0 ? "TAP ANY KEY ON EACH BAR'S FIRST" :
                                                      "TAP ANY KEY ON EVERY BEAT TO SPIN.", G_PLANK2);
            cv_text(24, 74, &FONT_XS, tc_lv() == 0 ? "BEAT TO SPIN. OTHER TAPS WOBBLE." :
                                                      "OFF THE BEAT YOUR CUP WOBBLES.", G_PLANK2);
            cv_text(24, 88, &FONT_XS, "STAY AT TOP SPEED TO SCORE.", G_PLANK2);
        } else {
            round_rect(6, 52, 68, 52, 6, G_PAPER);   /* the time held at the top */
            cv_text(14, 56, &FONT_XS, "AT THE TOP", G_PLANK2);
            tc_tenths(tc.top_ms, s);
            text_cat(s, " S");
            cv_text(14, 74, &FONT_B, s, G_INK);
            tc_dial();
            if (tc.state == TC_RIDE_ON) {   /* the word: the last tap's, or top speed, or the waltz's first bar */
                const char *w = 0;
                uint16_t c = G_INK;
                if (t < 0) {
                    w = "HOLD ON!";
                } else if (tc.word && now - tc.word_ms < 500u) {
                    static const char *const WORD[5] = {"", "PERFECT", "GOOD", "WOBBLE!", "MISSED"};
                    w = WORD[tc.word];
                    c = tc.word == TW_PERFECT ? TC_CAB2 : tc.word == TW_GOOD ? G_INK : G_RED;
                } else if (tc.at_top) {
                    w = "TOP SPEED!";
                    c = now / 200u & 1u ? G_RED : G_ORANGE;
                }
                if (w) {
                    round_rect(120 - text_w(&FONT_B, w) / 2 - 6, 54, text_w(&FONT_B, w) + 12, 22, 6, G_PAPER);
                    text_mid(120, 57, &FONT_B, w, c);
                }
                {                        /* the bar's three beats: the ones to tap ringed, the one now lit (the
                                          * bar before counts in) */
                    int32_t i, b = t < -3 * tc.beat ? -1 : (t + 3 * tc.beat) / tc.beat;
                    int on = b >= 0 && (t + 3 * tc.beat) % tc.beat < 140;
                    for (i = 0; i < 3; i++) {
                        int32_t x = 104 + i * 16;
                        int tap = tc.level > 0 || i == 0;
                        fill_circle(x, 211, tap ? 5 : 3, tap ? G_PAPER : G_PLANK2);
                        if (b >= 0 && b % 3 == i)
                            fill_circle(x, 211, tap ? 4 : 3, on ? (tap ? G_RED : G_INK) : (tap ? G_RED2 : G_INK));
                    }
                }
            } else {                     /* the ride over */
                round_rect(36, 104, 168, 88, 8, G_PAPER);   /* over the riders' hats, not under them */
                text_mid(120, 110, &FONT_B, "RIDE OVER", G_RED);
                text_cat((s[0] = 0, s), "SCORE ");
                int_text(tc.score, s + 6);
                text_mid(120, 130, &FONT_B, s, G_INK);
                text_cat((s[0] = 0, s), "TOP ");
                tc_tenths(tc.top_ms, s + 4);
                text_cat(s, " S, PEAK ");
                int_text((int32_t)(tc.peak + 0.5f), s + text_len(s));
                text_cat(s, "%");
                text_mid(120, 152, &FONT_XS, s, G_PLANK2);
                text_mid(120, 170, &FONT_XS, tc.new_best ? "NEW BEST!" : "", G_RED);
            }
        }
        cv_rect(0, 220, 240, 20, G_LILAC);
        {
            const char *f = tc.state == TC_RIDE_ON ? "PLAY: STOP" : tc.state == TC_OVER ? "PLAY: AGAIN" : "PLAY: START";
            cv_text(232 - text_w(&FONT_XS, f), 224, &FONT_XS, f, G_INK);
        }
        band_send(y0);
    }
}

static void tc_over(uint32_t now)        /* the ride's 60 seconds are up */
{
    char t[24];
    uint16_t *best = &sav.more[SV_TEACUPS].best;
    tc.state = TC_OVER;
    tc.over_ms = now;
    tc.at_top = 0;
    tc.score = (int)(tc.top_ms / 100u) + (int)(tc.peak + 0.5f);
    tc.new_best = tc.score > *best;
    if (tc.new_best) {
        *best = (uint16_t)tc.score;
        save_dirty();
    }
    text_cat((t[0] = 0, t), "SCORE ");
    int_text(tc.score, t + 6);
    app_say(tc.new_best ? "RIDE OVER. NEW BEST" : "RIDE OVER", t);
    snd_lock();                          /* the waltz's last chord */
    bw_note(60, 90, BW_BASS);
    bw_note(72, 80, BW_CHIME);
    bw_note(76, 70, BW_CHIME);
    bw_note(79, 70, BW_CHIME);
    snd_unlock();
}

static void teacups_frame(uint32_t now)
{
    float dt = (float)clamp((int32_t)(now - tc.drawn_ms), 0, 100) * 0.001f;
    tc.drawn_ms = now;
    if (tc.state == TC_RIDE_ON && (int32_t)(now - tc.t0) >= TC_RIDE)
        tc_over(now);
    if (tc.state == TC_RIDE_ON && tc.at_top != tc.said_top) {   /* top speed reached, or lost */
        tc.said_top = tc.at_top;
        app_say(tc.at_top ? "TOP SPEED" : "SLOWING", "");
    }
    if (tc.state == TC_OVER)             /* the ride winds down */
        tc.speed = fm_maxf(tc.speed - 40.0f * dt, 0.0f);
    tc.cup += tc.speed * 0.11f * dt;
    tc.plat += (tc.state == TC_RIDE_ON ? 0.6f + tc.speed * 0.006f : tc.state == TC_OVER ? 0.1f + tc.speed * 0.01f : 0.25f) * dt;
    if (tc.cup > 62.83185f)
        tc.cup -= 62.83185f;
    if (tc.plat > 62.83185f)
        tc.plat -= 62.83185f;
    teacups_draw(now);
}

static void teacups_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    float a = (float)(ms % 6284u) * 0.003f;
    int32_t cx = x + w / 2, rx = w * 2 / 5, ry = h / 6, i;
    tc_ellipse(cx, y + h * 3 / 4 + 2, rx, ry, TC_CAB2);
    tc_ellipse(cx, y + h * 3 / 4, rx, ry, TC_CAB);
    for (i = 0; i < 3; i++) {            /* three cups going round */
        float b = a + (float)i * 2.0944f;
        int32_t px = cx + (int32_t)((float)(rx - 8) * fm_cosf(b)), py = y + h * 3 / 4 + (int32_t)((float)(ry - 2) * fm_sinf(b));
        cv_rect(px - 5, py - 9, 11, 7, i == 0 ? G_PAPER : i == 1 ? G_ORANGE : G_LILAC);
        cv_rect(px - 4, py - 2, 9, 2, i == 0 ? G_PAPER : i == 1 ? G_ORANGE : G_LILAC);
        cv_rect(px - 5, py - 10, 11, 2, G_INK);
    }
}

static void teacups_best(char *t)
{
    t[0] = 0;
    if (sav.more[SV_TEACUPS].best) {
        text_cat(t, "BEST ");
        int_text(sav.more[SV_TEACUPS].best, t + 5);
    }
}

/* the beats to tap flash on every key; under them the lights build with the spin, and chase at top speed */
static uint32_t teacups_keys_lit(uint32_t *mid)
{
    uint32_t now = plat_ms(), keys = 0;
    int32_t t = (int32_t)(now - tc.t0), n, k;
    *mid = 0;
    if (tc.state != TC_RIDE_ON || t < -60)
        return 0;
    {
        int32_t b = (t + 60) / tc.beat, d = t + 60 - b * tc.beat;
        if (d < 150 && tc_target(b))
            keys = (1u << NKEYS) - 1u;
    }
    n = (int32_t)(tc.speed * (float)NKEYS / 100.0f + 0.5f);
    for (k = 0; k < n && k < NKEYS; k++)
        if (!tc.at_top || (k + (int32_t)(now / 90u)) % 3 != 0)
            *mid |= 1u << k;
    return keys;
}

static void teacups_where(char *t)
{
    t[0] = 0;
    if (tc.state == TC_STAND) {
        text_cat(t, "TEACUPS. KNOB 1, LEVEL ");
        sp_num(t, tc_lv() + 1);
        text_cat(t, ", ");
        text_cat(t, TC_LEVEL_WORD[tc_lv()]);
        text_cat(t, tc_lv() == 0 ? ". TAP ANY KEY ON THE FIRST BEAT OF EACH BAR TO SPIN YOUR CUP. OTHER TAPS WOBBLE IT. " :
                                   ". TAP ANY KEY ON EVERY BEAT TO SPIN YOUR CUP. OFF THE BEAT IT WOBBLES. ");
        text_cat(t, "STAY AT TOP SPEED TO SCORE. ");
        if (sav.more[SV_TEACUPS].best) {
            text_cat(t, "BEST ");
            sp_num(t, sav.more[SV_TEACUPS].best);
            text_cat(t, ". ");
        }
        text_cat(t, "PLAY STARTS.");
        return;
    }
    if (tc.state == TC_RIDE_ON) {
        int32_t left = clamp((TC_RIDE - (int32_t)(plat_ms() - tc.t0) + 999) / 1000, 0, TC_RIDE / 1000);
        text_cat(t, tc.at_top ? "TEACUPS. TOP SPEED. SPIN " : "TEACUPS. SPIN ");
        sp_pct(t, (int32_t)(tc.speed + 0.5f));
        text_cat(t, ". ");
        sp_num(t, (int32_t)(tc.top_ms / 1000u));
        text_cat(t, " SECONDS AT THE TOP. ");
        sp_num(t, left);
        text_cat(t, " SECONDS LEFT. PLAY STOPS.");
        return;
    }
    text_cat(t, tc.new_best ? "TEACUPS. RIDE OVER. NEW BEST. SCORE " : "TEACUPS. RIDE OVER. SCORE ");
    sp_num(t, tc.score);
    text_cat(t, ". ");
    sp_num(t, (int32_t)(tc.top_ms / 1000u));
    text_cat(t, " SECONDS AT THE TOP, PEAK ");
    sp_pct(t, (int32_t)(tc.peak + 0.5f));
    text_cat(t, ". PLAY STARTS AGAIN.");
}

static int teacups_playing(void) { return tc.state == TC_RIDE_ON; }

/* the attract tune: the waltz's first bars on the chime */
static const uint8_t TEACUPS_TUNE[] = {72, 76, 79, 79, 0, 76, 74, 77, 79, 77, 0, 74,
                                       71, 74, 77, 74, 0, 71, 72, 76, 72, 72, 0, 0};

static const game_t GAME_TEACUPS = {"TEACUPS", TC_CAB, teacups_enter, teacups_leave, teacups_frame, teacups_key,
                                    teacups_button, teacups_enc, teacups_tick, teacups_attract, teacups_best,
                                    teacups_keys_lit, TEACUPS_TUNE, sizeof TEACUPS_TUNE, 330, BW_CHIME, teacups_where,
                                    1, teacups_playing, "SPIN TO THE BEAT", 0};

#ifdef PLAT_HOST
/* the host simulator: the game's own script commands (1 = handled) and checks (1 = known), a call every
 * simulated millisecond (a test player), and its lines for the speakdump (sp_out).
 *   autoteacups on|late|offbeat|off   taps on every beat to tap, 70 ms late on each, halfway between beats, or none */
static int host_autoteacups;
static int teacups_host_cmd(const char *cmd, const char *a)
{
    if (strcmp(cmd, "autoteacups"))
        return 0;
    host_autoteacups = !strcmp(a, "on") ? 1 : !strcmp(a, "late") ? 2 : !strcmp(a, "offbeat") ? 3 : 0;
    return 1;
}
static int teacups_expect(const char *what, int *got)
{
    if (!strcmp(what, "teacups_state"))  /* 0 standing, 1 riding, 2 over */
        *got = tc.state;
    else if (!strcmp(what, "teacups_speed"))
        *got = (int)(tc.speed + 0.5f);
    else if (!strcmp(what, "teacups_peak"))
        *got = (int)(tc.peak + 0.5f);
    else if (!strcmp(what, "teacups_top"))   /* tenths of a second at top speed */
        *got = (int)(tc.top_ms / 100u);
    else if (!strcmp(what, "teacups_score"))
        *got = tc.score;
    else if (!strcmp(what, "teacups_best"))
        *got = sav.more[SV_TEACUPS].best;
    else if (!strcmp(what, "teacups_hits"))
        *got = tc.hits;
    else if (!strcmp(what, "teacups_perfect"))
        *got = tc.perfect;
    else if (!strcmp(what, "teacups_wobbles"))
        *got = tc.wobbles;
    else if (!strcmp(what, "teacups_misses"))
        *got = tc.misses;
    else if (!strcmp(what, "teacups_level"))
        *got = sav.more[SV_TEACUPS].level;
    else
        return 0;
    return 1;
}
static void teacups_host_ms(uint32_t now)
{
    int32_t t = (int32_t)(now - tc.t0), at;
    if (!host_autoteacups || tc.state != TC_RIDE_ON || t < 0 || t >= TC_RIDE)
        return;
    at = host_autoteacups == 2 ? 70 : host_autoteacups == 3 ? tc.beat / 2 : 0;
    if (t >= at && (t - at) % tc.beat == 0 && (host_autoteacups == 3 || tc_target((t - at) / tc.beat))) {
        teacups_key(14, 1, now);
        teacups_key(14, 0, now);
    }
}
static void teacups_speak_dump(void)
{
    static const char *const MSG[][2] = {
        {"HOLD ON", "TAP ON THE FIRST BEAT OF EACH BAR"}, {"HOLD ON", "TAP ON EVERY BEAT"}, {"TOP SPEED", ""},
        {"SLOWING", ""}, {"STOPPED", ""}, {"RIDE OVER", "SCORE 312"}, {"RIDE OVER. NEW BEST", "SCORE 512"}};
    int i;
    char t[24];
    for (i = 1; i <= TC_LEVELS; i++) {   /* each level, standing, with a best and without */
        sav.more[SV_TEACUPS].level = (uint8_t)i;
        sav.more[SV_TEACUPS].best = (uint16_t)(i == 1 ? 0 : 412);
        tc.state = TC_STAND;
        teacups_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        t[0] = 0;
        sp_num(t, i);
        text_cat(t, ", ");
        text_cat(t, TC_LEVEL_WORD[i - 1]);
        sp_join("LEVEL", t);
        sp_out("READOUT", sp_buf);
    }
    tc.state = TC_RIDE_ON;               /* riding: on the way up, and at the top */
    tc.t0 = plat_ms();
    tc.speed = 42.0f;
    tc.top_ms = 0;
    tc.at_top = 0;
    teacups_where(sp_buf);
    sp_out("SCREEN", sp_buf);
    tc.speed = 96.0f;
    tc.top_ms = 12345;
    tc.at_top = 1;
    teacups_where(sp_buf);
    sp_out("SCREEN", sp_buf);
    tc.state = TC_OVER;                  /* over: a new best, and not */
    tc.at_top = 0;
    tc.score = 512;
    tc.peak = 100.0f;
    tc.top_ms = 41234;
    tc.new_best = 1;
    teacups_where(sp_buf);
    sp_out("SCREEN", sp_buf);
    tc.score = 312;
    tc.new_best = 0;
    teacups_where(sp_buf);
    sp_out("SCREEN", sp_buf);
    for (i = 0; i < (int)(sizeof MSG / sizeof MSG[0]); i++) {
        sp_join(MSG[i][0], MSG[i][1]);
        sp_out("MSG", sp_buf);
    }
    tc_stop();
}
#endif
