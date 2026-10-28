/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The boardwalk at golden hour (mockups/cabinets.html, "B"): sky, low sun, a Ferris wheel turning, gulls, the sea's
 * waves, a string of bulbs, the planks and the rail, and the row of cabinets in front, the selected one in the
 * middle at full size, its neighbours shrunk and dimmed. Each cabinet's little screen plays its game's attract
 * animation, its best score under it. Part of the unity build. */

#define CAB_W 96
#define CAB_H 162
#define CAB_STEP 110                     /* one cabinet to the next */
#define CAB_BASE 228                     /* the row's feet */
#define SMALL 184                        /* a neighbour's size, /256 */

static void sky(uint32_t ms)
{
    int32_t y, i;
    for (y = 0; y < 118; y++)            /* peach, to sand at 60 %, to lilac */
        cv_rect(0, y, 240, 1, y < 86 ? mix(G_SKY0, G_SKY1, y * 256 / 86) : mix(G_SKY1, G_SKY2, (y - 86) * 256 / 58));
    fill_circle(201, 29, 15, mix(G_SUN, G_SKY0, 140));   /* the sun's glow, then the sun */
    fill_circle(201, 29, 13, G_SUN);
    for (i = 0; i < 2; i++) {            /* gulls, crossing every 14 s */
        int32_t x = 250 - (int32_t)((ms / 50u + (uint32_t)i * 140u) % 280u), gy = 30 + i * 9, f = (int32_t)(ms / 300u + (uint32_t)i) & 1;
        cv_line(x - 4, gy - f, x, gy + 1, G_GULL);
        cv_line(x, gy + 1, x + 4, gy - f, G_GULL);
    }
}

static void wheel(uint32_t ms)
{
    const int32_t cx = 49, cy = 71, r = 34;
    float a0 = (float)(ms % 18000u) * (6.2831853f / 18000.0f);
    int i;
    cv_line(cx, cy, cx - 18, 120, G_WHEEL);   /* the legs */
    cv_line(cx, cy, cx + 18, 120, G_WHEEL);
    ring_circle(cx, cy, r, G_WHEEL);
    ring_circle(cx, cy, r - 1, G_WHEEL);
    for (i = 0; i < 8; i++) {
        float a = a0 + (float)i * 0.785398f;
        int32_t x = cx + (int32_t)(fm_cosf(a) * (float)r), y = cy + (int32_t)(fm_sinf(a) * (float)r);
        if (!(i & 1))
            cv_line(cx, cy, x, y, mix(G_WHEEL, G_SKY1, 100));
        if (!(i & 1))
            cv_rect(x - 3, y, 7, 6, G_CAR);   /* the cars hang below their pins */
    }
}

static void sea(uint32_t ms)
{
    int32_t x, o1 = (int32_t)(ms / 150u % 20u), o2 = 22 - (int32_t)(ms / 230u % 22u);
    cv_rect(0, 118, 240, 26, G_SEA);
    for (x = -20; x < 240; x += 20)
        cv_rect(x + o1, 123, 10, 3, G_FOAM);
    for (x = -22; x < 240; x += 22)
        cv_rect(x + o2, 133, 6, 2, mix(G_FOAM, G_SEA, 80));
    cv_rect(150, 118, 70, 1, mix(G_SUN, G_SEA, 60));   /* the sun on the water */
    cv_rect(170, 121, 40, 1, mix(G_SUN, G_SEA, 110));
}

static void bulbs(uint32_t ms)
{
    int i;
    for (i = 0; i < 14; i++) {
        int32_t x = 8 + i * 16 + 2, lit = ((ms / 800u + (uint32_t)i) & 1u) != 0;
        cv_line(x - 8, 11 + (i & 1), x + 8, 11 + !(i & 1), mix(G_WHEEL, G_SKY0, 120));
        fill_circle(x, 13, 2, lit ? G_BULB : mix(G_BULB, G_SKY0, 150));
    }
}

static void deck(void)
{
    int32_t x;
    cv_rect(0, 144, 240, 96, G_PLANK);
    for (x = 22; x < 240; x += 24)
        cv_rect(x, 144, 2, 96, G_PLANK2);
    cv_rect(0, 140, 240, 4, G_RAIL);
}

/* one cabinet: its feet at (cx, CAB_BASE), size s/256 */
static void cabinet(const game_t *g, int32_t cx, int32_t s, int dim, uint32_t ms)
{
#define S(v) ((v) * s / 256)
    int32_t x0 = cx - S(CAB_W) / 2, y0 = CAB_BASE - S(CAB_H), y;
    uint16_t body = dim ? mix(g->cab, G_PLANK2, 90) : g->cab;
    char t[24];
    /* the body: straight, a ledge for the panel, then narrower to the floor */
    for (y = S(16); y < S(CAB_H); y++) {
        int32_t in = y < S(94) ? S(6) : y < S(104) ? S(6) + (y - S(94)) * S(8) / (S(10) + 1) : S(14);
        cv_rect(x0 + in, y0 + y, S(CAB_W) - 2 * in, 1, body);
    }
    round_rect(x0 + S(2), y0, S(CAB_W) - S(4), S(22), S(5), dim ? mix(G_PAPER, G_PLANK2, 70) : G_PAPER);
    text_mid(cx, y0 + S(22) / 2 - (s > 220 ? 8 : 6), s > 220 ? &FONT_B : &FONT_XS, g->name, G_INK);
    cv_rect(x0 + S(16), y0 + S(30), S(CAB_W) - S(32), S(56), G_SCREEN);
    if (g->attract)
        g->attract(x0 + S(16), y0 + S(30), S(CAB_W) - S(32), S(56), ms);
    cv_rect(x0 + S(10), y0 + S(96), S(CAB_W) - S(20), S(10), G_INK);
    fill_circle(x0 + S(29), y0 + S(101), S(3), G_PAPER);
    fill_circle(x0 + S(CAB_W) - S(29), y0 + S(101), S(3), G_PAPER);
    t[0] = 0;
    if (g->best)
        g->best(t);
    if (t[0] && s > 220)
        text_mid(cx, y0 + S(CAB_H) - 18, &FONT_XS, t, G_PAPER);
#undef S
}

/* the whole scene; pos: the row's position in cabinets x 256 (slides between them) */
static void scene_draw(const game_t *const *games, int n, int32_t pos, uint32_t ms)
{
    int32_t y0, i;
    char t[16];
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, G_PLANK);
        sky(ms);
        bulbs(ms);
        wheel(ms);
        sea(ms);
        deck();
        for (i = 0; i < n; i++) {        /* the far ones first, the one in front last */
            int32_t d = i * 256 - pos, ad = d < 0 ? -d : d;
            if (ad >= 256 * 2)
                continue;
            if (ad < 128)
                continue;
            cabinet(games[i], 120 + d * CAB_STEP / 256, 256 - (ad > 256 ? 256 : ad) * (256 - SMALL) / 256, 1, ms);
        }
        for (i = 0; i < n; i++) {
            int32_t d = i * 256 - pos;
            if (d > -128 && d < 128)
                cabinet(games[i], 120 + d * CAB_STEP / 256, 256 - (d < 0 ? -d : d) * (256 - SMALL) / 256, 0, ms);
        }
        cv_text(6, 229, &FONT_XS, "< OCT-", G_INK);
        t[0] = (char)('1' + (pos + 128) / 256);
        t[1] = 0;
        cv_text(112, 229, &FONT_XS, t, G_INK);
        cv_text(124, 229, &FONT_XS, "/", G_INK);
        t[0] = (char)('0' + n);
        cv_text(132, 229, &FONT_XS, t, G_INK);
        cv_text(234 - text_w(&FONT_XS, "OCT+ >"), 229, &FONT_XS, "OCT+ >", G_INK);
        band_send(y0);
    }
}
