/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Drawing on fm1-base's canvas (gfx.c), in screen coordinates: the screen is drawn as two bands of 240 x 120, each
 * drawn whole and sent at once, so nothing tears and nothing needs a full framebuffer. Golden hour's colours (the
 * mockups' "B"). Part of the unity build. */

/* golden hour */
#define G_SKY0 RGB(247, 169, 139)
#define G_SKY1 RGB(251, 217, 176)
#define G_SKY2 RGB(230, 201, 232)
#define G_SUN RGB(255, 207, 112)
#define G_SEA RGB(110, 143, 196)
#define G_FOAM RGB(255, 227, 200)
#define G_WHEEL RGB(122, 74, 110)
#define G_CAR RGB(255, 207, 112)
#define G_GULL RGB(90, 58, 78)
#define G_BULB RGB(255, 233, 160)
#define G_PLANK RGB(201, 156, 120)
#define G_PLANK2 RGB(167, 124, 92)
#define G_RAIL RGB(255, 241, 220)
#define G_INK RGB(74, 46, 40)
#define G_PAPER RGB(255, 244, 223)
#define G_PAPER2 RGB(243, 226, 198)
#define G_SCREEN RGB(20, 19, 32)
#define G_RED RGB(232, 80, 91)
#define G_RED2 RGB(184, 58, 69)
#define G_TEAL RGB(62, 154, 176)
#define G_ORANGE RGB(242, 163, 58)
#define G_LILAC RGB(230, 201, 232)
#define G_BLUSH RGB(251, 217, 176)
#define G_MUTE RGB(200, 174, 146)

#define BAND_H 120

static void band(int32_t y0, uint16_t bg)   /* the next band: screen rows y0 .. y0 + 119 */
{
    cv_begin(240, BAND_H, bg);
    cv_oy = -y0;
    cv_y0 = 0;
    cv_y1 = BAND_H;
}
static void band_send(int32_t y0)
{
    cv_oy = 0;
    cv_blit(0, (uint32_t)y0);
}

static uint16_t mix(uint16_t a, uint16_t b, int32_t t)   /* a to b, t 0..256 */
{
    int32_t ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31, br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    t = clamp(t, 0, 256);
    return (uint16_t)(((ar + ((br - ar) * t >> 8)) << 11) | ((ag + ((bg - ag) * t >> 8)) << 5) |
                      (ab + ((bb - ab) * t >> 8)));
}

static void fill_circle(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t y;
    for (y = -r; y <= r; y++) {
        int32_t w = (int32_t)fm_sqrtf((float)(r * r - y * y) + 0.5f);
        cv_rect(cx - w, cy + y, 2 * w + 1, 1, c);
    }
}

static void ring_circle(int32_t cx, int32_t cy, int32_t r, uint16_t c)   /* an outline, 1 px */
{
    int32_t x = r, y = 0, e = 1 - r;
    while (x >= y) {
        cv_pset(cx + x, cy + y, c);
        cv_pset(cx - x, cy + y, c);
        cv_pset(cx + x, cy - y, c);
        cv_pset(cx - x, cy - y, c);
        cv_pset(cx + y, cy + x, c);
        cv_pset(cx - y, cy + x, c);
        cv_pset(cx + y, cy - x, c);
        cv_pset(cx - y, cy - x, c);
        y++;
        if (e < 0)
            e += 2 * y + 1;
        else {
            x--;
            e += 2 * (y - x) + 1;
        }
    }
}

static void round_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    int32_t i;
    if (r * 2 > h)
        r = h / 2;
    for (i = 0; i < r; i++) {            /* the corners: a quarter circle a row */
        int32_t d = r - (int32_t)fm_sqrtf((float)(r * r - (r - i) * (r - i)));
        cv_rect(x + d, y + i, w - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - i, w - 2 * d, 1, c);
    }
    cv_rect(x, y + r, w, h - 2 * r, c);
}

static void text_mid(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

/* a tower bell hung from its headstock at (cx, cy), turned ang radians (0: mouth down, pi: mouth up, at rest),
 * size s (1: 26 px across the lip). Each pixel near it is turned back into the bell's own frame and tested */
static int bell_inside(float x, float y)   /* the bell's frame: headstock at 0, mouth at y = 20 */
{
    float half, f;
    if (y < 0.0f || y > 20.0f)
        return 0;
    f = y - 12.0f;
    half = y < 2.5f ? 3.5f + y * 1.4f : y < 12.0f ? 7.0f + (y - 2.5f) * 0.12f : 8.1f + f * 0.3f + f * f * 0.06f;
    return fm_fabsf(x) <= half;          /* a rounded crown, the waist, the flare to the lip */
}
static void draw_bell(int32_t cx, int32_t cy, float ang, float s, uint16_t body, uint16_t rim, int struck)
{
    float c = fm_cosf(ang), sn = fm_sinf(ang), inv = 1.0f / s;
    int32_t x, y, R = (int32_t)(24.0f * s) + 2;
    for (y = -R; y <= R; y++)
        for (x = -R; x <= R; x++) {
            float bx = ((float)x * c + (float)y * sn) * inv, by = (-(float)x * sn + (float)y * c) * inv;
            if (!bell_inside(bx, by))
                continue;
            /* the lip and the outline darker */
            cv_pset(cx + x, cy + y, (by > 18.0f || !bell_inside(bx + 1.2f, by) || !bell_inside(bx - 1.2f, by)) ? rim : body);
        }
    {                                    /* the clapper: against the lip when struck */
        float py = struck ? 16.5f : 18.5f, px = 0.0f;
        fill_circle(cx + (int32_t)((px * c - py * sn) * s), cy + (int32_t)((px * sn + py * c) * s),
                    (int32_t)(2.5f * s + 0.5f), G_INK);
    }
    cv_rect(cx - 3, cy - 3, 6, 5, G_WHEEL);   /* the headstock */
}

/* the FM-1's keys, F3..G5, across the screen at y (h tall): white keys 15 px wide, the black ones over them. Each
 * key's colour from col(k) (0: as it is) */
static void keyboard(int32_t y, int32_t h, uint16_t (*col)(int k))
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
    static const uint8_t AT[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};   /* the white key each black one follows */
    int i;
    cv_rect(0, y - 1, 240, h + 1, G_MUTE);   /* the bed the keys sit in: their gaps */
    for (i = 0; i < 16; i++) {
        uint16_t c = col ? col(WK[i]) : 0;
        cv_rect(i * 15, y, 14, h, c ? c : G_PAPER);
    }
    for (i = 0; i < 11; i++) {
        uint16_t c = col ? col(BK[i]) : 0;
        cv_rect(AT[i] * 15 + 10, y, 9, h * 3 / 5, c ? c : G_INK);
    }
}

static int key_x(int k)                  /* a key's middle on that keyboard */
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
    static const uint8_t AT[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};
    int i;
    for (i = 0; i < 16; i++)
        if (WK[i] == k)
            return i * 15 + 7;
    for (i = 0; i < 11; i++)
        if (BK[i] == k)
            return AT[i] * 15 + 14;
    return 0;
}

static void note_text(int note, char *t)  /* "C#4" */
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    t[0] = 0;
    text_cat(t, N[note % 12]);
    int_text(note / 12 - 1, t + (N[note % 12][1] ? 2 : 1));
}

static uint32_t rnd_state = 0x1234567u;  /* the games' dice */
static uint32_t rnd(uint32_t n)
{
    rnd_state = rnd_state * 1664525u + 1013904223u;
    return n ? (rnd_state >> 8) % n : 0;
}
