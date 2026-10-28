/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Gull: fly a gull between the pier's posts, two ways (K1):
 *
 * FLAP: any key flaps; gravity does the rest. Each flap sings a note of a pentatonic scale, higher the higher the gull
 * is, so a flight draws a tune.
 *
 * SING: the key you hold is the gull's height, low keys low and high keys high; let go and it sinks. Each gap between
 * the posts is a note: it chimes as the posts come on, and you fly through by playing it. Level 1 shows the gap's note
 * on its post; level 2 is by ear.
 *
 * Into a post or the sea and it is over; scored on the posts passed, faster as it goes. K2 the level. PLAY starts.
 * Part of the unity build. */
#include "game.h"

#define GU_X 64                          /* the gull's x */
#define GU_TOP 20                        /* the sky's top, the sea's line */
#define GU_SEA 198
#define GU_POSTS 3
#define GU_POST_W 22
#define GU_SPACE 128                     /* px from post to post */
#define GU_R 6                           /* the gull, for hitting things */

enum { GS2_STAND, GS2_FLY, GS2_OVER };
static struct {
    int state, score, new_best, mode;
    float y, vy, speed;                  /* px, px/s, px/s */
    float post_x[GU_POSTS];
    int post_gap[GU_POSTS];              /* the gap's middle (y) */
    int post_key[GU_POSTS];              /* SING: the gap's note, as a key */
    uint8_t post_passed[GU_POSTS];
    uint32_t last_ms, flap_ms, over_ms;
    int held;                            /* SING: the key held (-1 none) */
    uint32_t keys;                       /* held now */
} gu;

static int gu_half(void)                 /* half a gap */
{
    return gu.mode ? 20 : clamp(36 - gu.score / 4, 26, 36);
}

static int key_y(int k)                  /* SING: a key's height (F3 low, G5 high) */
{
    return GU_SEA - 14 - k * (GU_SEA - GU_TOP - 28) / (NKEYS - 1);
}

static void gu_post(int i, float x)      /* a new post: its gap (and note) */
{
    static const uint8_t C_MAJOR[16] = PLAT_WHITE_KEYS;
    gu.post_x[i] = x;
    gu.post_passed[i] = 0;
    if (gu.mode) {
        gu.post_key[i] = C_MAJOR[1 + rnd(14)];   /* a white key, not the very ends */
        gu.post_gap[i] = key_y(gu.post_key[i]);
    } else {
        gu.post_key[i] = -1;
        gu.post_gap[i] = GU_TOP + 40 + (int)rnd((uint32_t)(GU_SEA - GU_TOP - 80));
    }
}

static void gull_go(void)
{
    int i;
    rnd_state ^= plat_ms() * 2654435761u;
    gu.mode = sav.gull.mode & 1;
    gu.score = 0;
    gu.y = (GU_TOP + GU_SEA) / 2;
    gu.vy = 0.0f;
    gu.speed = 62.0f;
    gu.held = -1;
    gu.last_ms = gu.flap_ms = plat_ms();
    for (i = 0; i < GU_POSTS; i++)
        gu_post(i, 260.0f + (float)(i * GU_SPACE));
    gu.state = GS2_FLY;
    app_say(gu.mode ? "SING THROUGH THE GAPS" : "FLAP", "");
}

static void gull_over(void)
{
    char t[16] = "SCORE ";
    gu.state = GS2_OVER;
    gu.over_ms = plat_ms();
    gu.new_best = gu.score > sav.gull.best[gu.mode];
    if (gu.new_best) {
        sav.gull.best[gu.mode] = (uint16_t)gu.score;
        save_dirty();
    }
    snd_lock();                          /* the splash */
    bw_drum(BW_HAT, 120);
    bw_note(40, 90, BW_CLUNK);
    snd_unlock();
    int_text(gu.score, t + 6);
    app_say(gu.new_best ? "SPLASH. NEW BEST" : "SPLASH", t);
}

static int flap_note(void)               /* FLAP: the note of a flap, from the gull's height: C major pentatonic */
{
    static const uint8_t PENTA[11] = {60, 62, 64, 67, 69, 72, 74, 76, 79, 81, 84};
    int i = (int)((GU_SEA - gu.y) * 11.0f / (float)(GU_SEA - GU_TOP));
    return PENTA[clamp(i, 0, 10)];
}

static void gull_key(int k, int down, uint32_t ms)
{
    if (down)
        gu.keys |= 1u << k;
    else
        gu.keys &= ~(1u << k);
    if (!down)
        return;
    if (gu.state != GS2_FLY) {           /* standing: the keys play */
        snd_lock();
        bw_note(53 + k, 90, gu.mode ? BW_SAW : BW_PLUCK);
        snd_unlock();
        return;
    }
    if (gu.mode == 0) {                  /* a flap, and its note */
        gu.vy = -185.0f;
        gu.flap_ms = ms;
        snd_lock();
        bw_note(flap_note(), 95, BW_PLUCK);
        snd_unlock();
    } else {                             /* sing: the gull goes to this key's height */
        gu.held = k;
        snd_lock();
        bw_note(53 + k, 90, BW_SAW);
        snd_unlock();
    }
}

static void gull_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (gu.state == GS2_FLY) {
        gu.state = GS2_STAND;
        app_say("STOPPED", "");
    } else {
        gull_go();
    }
}

static const char *const GU_MODE[2] = {"FLAP", "SING"};

static void gull_enc(int role, int steps)
{
    if (gu.state == GS2_FLY)
        return;
    if (role == EN_K1) {
        sav.gull.mode = (uint8_t)clamp(sav.gull.mode + steps, 0, 1);
        app_readout("MODE", GU_MODE[sav.gull.mode]);
    } else if (role == EN_K2 && sav.gull.mode == 1) {
        sav.gull.level = (uint8_t)clamp(sav.gull.level + steps, 1, 2);
        app_readout("LEVEL", sav.gull.level == 1 ? "1, NOTES SHOWN" : "2, BY EAR");
    } else {
        return;
    }
    gu.mode = sav.gull.mode & 1;
    gu.state = GS2_STAND;
    save_dirty();
}

static void gull_enter(void)
{
    gu.state = GS2_STAND;
    gu.mode = sav.gull.mode & 1;
    gu.y = (GU_TOP + GU_SEA) / 2;
    gu.keys = 0;
}
static void gull_leave(void) { gu.state = GS2_STAND; }

static void gull_step(uint32_t now)      /* the flight, by time */
{
    float dt = (float)(now - gu.last_ms) * 0.001f;
    int i;
    gu.last_ms = now;
    if (dt > 0.05f)
        dt = 0.05f;
    if (gu.mode == 0) {
        gu.vy += 520.0f * dt;
        gu.y += gu.vy * dt;
        if (gu.y < GU_TOP + GU_R) {
            gu.y = GU_TOP + GU_R;
            gu.vy = 0.0f;
        }
    } else {                             /* sing: to the held key's height; nothing held, it sinks */
        if (gu.held >= 0 && !(gu.keys >> gu.held & 1u)) {   /* let go: another key still held takes over */
            int k;
            gu.held = -1;
            for (k = NKEYS - 1; k >= 0; k--)
                if (gu.keys >> k & 1u) {
                    gu.held = k;
                    break;
                }
        }
        if (gu.held >= 0)
            gu.y += ((float)key_y(gu.held) - gu.y) * (dt * 9.0f > 1.0f ? 1.0f : dt * 9.0f);
        else
            gu.y += 45.0f * dt;
    }
    for (i = 0; i < GU_POSTS; i++) {
        float was = gu.post_x[i];
        gu.post_x[i] -= gu.speed * dt;
        if (was > 240.0f && gu.post_x[i] <= 240.0f && gu.post_key[i] >= 0) {   /* SING: the gap's note, as it comes */
            snd_lock();
            bw_note(53 + gu.post_key[i], 90, BW_CHIME);
            snd_unlock();
        }
        if (!gu.post_passed[i] && gu.post_x[i] + GU_POST_W < GU_X - GU_R) {   /* passed */
            gu.post_passed[i] = 1;
            gu.score++;
            gu.speed += 1.5f;
            snd_lock();
            bw_drum(BW_BLOCK_HI, 50);
            snd_unlock();
        }
        if (gu.post_x[i] < -GU_POST_W) {  /* off the left: the next one on the right */
            float far = gu.post_x[0];
            int j;
            for (j = 1; j < GU_POSTS; j++)
                far = gu.post_x[j] > far ? gu.post_x[j] : far;
            gu_post(i, far + GU_SPACE);
        }
        /* into it: the gull's box against the post above the gap and the one below */
        if (gu.post_x[i] < GU_X + GU_R && gu.post_x[i] + GU_POST_W > GU_X - GU_R &&
            (gu.y - GU_R < gu.post_gap[i] - gu_half() || gu.y + GU_R > gu.post_gap[i] + gu_half())) {
            gull_over();
            return;
        }
    }
    if (gu.y + GU_R > GU_SEA)
        gull_over();
}

static void gull_bird(int32_t x, int32_t y, int up)   /* a gull: white, grey wings up or down, an orange beak */
{
    fill_circle(x, y, 6, C_WHITE);
    fill_circle(x + 6, y - 3, 4, C_WHITE);
    cv_rect(x + 9, y - 3, 5, 2, G_ORANGE);
    cv_rect(x + 7, y - 5, 2, 2, G_INK);
    if (up) {
        cv_line(x - 4, y - 2, x - 10, y - 10, RGB(150, 150, 160));
        cv_line(x - 3, y - 2, x - 8, y - 11, RGB(150, 150, 160));
    } else {
        cv_line(x - 4, y + 1, x - 11, y + 6, RGB(150, 150, 160));
        cv_line(x - 3, y + 1, x - 9, y + 7, RGB(150, 150, 160));
    }
}

static void gull_draw(uint32_t now)
{
    int32_t y0, i, y;
    char s[32];
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, G_SKY1);
        for (y = GU_TOP; y < GU_SEA; y++)   /* the golden-hour sky */
            cv_rect(0, y, 240, 1, mix(G_SKY0, G_SKY2, (y - GU_TOP) * 256 / (GU_SEA - GU_TOP)));
        cv_rect(0, GU_SEA, 240, 22, G_SEA);
        for (i = -20; i < 240; i += 20)
            cv_rect(i + (int32_t)(now / 120u % 20u), GU_SEA + 4, 10, 2, G_FOAM);
        cv_rect(0, 0, 240, 18, G_SEA);
        cv_text(6, 3, &FONT_XS, gu.mode ? "GULL: SING" : "GULL: FLAP", G_PAPER);
        if (gu.state != GS2_STAND) {
            int_text(gu.score, s);
            cv_text(234 - text_w(&FONT_XS, s), 3, &FONT_XS, s, G_PAPER);
        }
        for (i = 0; i < GU_POSTS; i++) { /* the pier's posts: from the sea up, and hanging from the top */
            int32_t x = (int32_t)gu.post_x[i], g = gu.post_gap[i], h = gu_half();
            if (x > 240 || x < -GU_POST_W || gu.state == GS2_STAND)
                continue;
            cv_rect(x, g + h, GU_POST_W, GU_SEA - g - h, G_PLANK2);
            cv_rect(x - 2, g + h, GU_POST_W + 4, 5, G_PLANK);
            cv_rect(x, GU_TOP, GU_POST_W, g - h - GU_TOP, G_PLANK2);
            cv_rect(x - 2, g - h - 5, GU_POST_W + 4, 5, G_PLANK);
            if (gu.mode && sav.gull.level == 1 && gu.post_key[i] >= 0) {   /* level 1: the gap's note on its post */
                note_text(53 + gu.post_key[i], s);
                text_mid(x + GU_POST_W / 2, g + h + 8, &FONT_XS, s, G_PAPER);
            }
        }
        if (gu.state == GS2_STAND) {
            round_rect(14, 36, 212, 136, 8, G_PAPER);
            cv_text(24, 44, &FONT_XS, "K1 MODE", G_PLANK2);
            cv_text(104, 42, &FONT_B, GU_MODE[sav.gull.mode & 1], G_INK);
            if (sav.gull.mode) {
                cv_text(24, 66, &FONT_XS, "K2 LEVEL", G_PLANK2);
                cv_text(104, 64, &FONT_B, sav.gull.level == 1 ? "NOTES SHOWN" : "BY EAR", G_INK);
                cv_text(24, 92, &FONT_XS, "HOLD A KEY: THAT IS YOUR HEIGHT.", G_PLANK2);
                cv_text(24, 106, &FONT_XS, "PLAY EACH GAP'S NOTE TO FLY IT.", G_PLANK2);
                cv_text(24, 120, &FONT_XS, "LET GO AND THE GULL SINKS.", G_PLANK2);
            } else {
                cv_text(24, 92, &FONT_XS, "ANY KEY FLAPS. MISS THE POSTS", G_PLANK2);
                cv_text(24, 106, &FONT_XS, "AND KEEP OUT OF THE SEA. EACH", G_PLANK2);
                cv_text(24, 120, &FONT_XS, "FLAP SINGS, HIGHER AS YOU CLIMB.", G_PLANK2);
            }
            if (sav.gull.best[sav.gull.mode & 1]) {
                text_cat((s[0] = 0, s), "BEST ");
                int_text(sav.gull.best[sav.gull.mode & 1], s + 5);
                cv_text(24, 146, &FONT_B, s, G_INK);
            }
        }
        gull_bird(GU_X, (int32_t)gu.y + (gu.state == GS2_OVER ? (int32_t)((now - gu.over_ms) / 20u) : 0),
                  gu.mode == 0 ? now - gu.flap_ms < 150u : (now / 160u & 1u));
        if (gu.state == GS2_OVER) {
            round_rect(40, 70, 160, 70, 8, G_PAPER);
            text_mid(120, 76, &FONT_B, "SPLASH!", G_RED);
            text_cat((s[0] = 0, s), "SCORE ");
            int_text(gu.score, s + 6);
            text_mid(120, 96, &FONT_B, s, G_INK);
            text_mid(120, 118, &FONT_XS, gu.new_best ? "NEW BEST!" : "", G_RED);
        }
        cv_rect(0, 220, 240, 20, G_LILAC);
        {
            const char *f = gu.state == GS2_FLY ? "PLAY: STOP" : gu.state == GS2_OVER ? "PLAY: AGAIN" : "PLAY: START";
            cv_text(232 - text_w(&FONT_XS, f), 224, &FONT_XS, f, G_INK);
        }
        band_send(y0);
    }
}

static void gull_frame(uint32_t now)
{
    if (gu.state == GS2_FLY)
        gull_step(now);
    gull_draw(now);
}

static void gull_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    int32_t px = w - (int32_t)(ms / 25u % (uint32_t)(w + 12)), gy = y + h / 2 + (int32_t)(ms / 300u % 3u) - 1;
    cv_rect(x, y + h - 6, w, 6, G_SEA);
    if (px > 0 && px < w - 6) {
        cv_rect(x + px, y, 6, h / 2 - 8, G_PLANK2);
        cv_rect(x + px, y + h / 2 + 8, 6, h / 2 - 14, G_PLANK2);
    }
    fill_circle(x + w / 3, gy, 3, C_WHITE);
    cv_rect(x + w / 3 + 3, gy - 1, 2, 1, G_ORANGE);
}

static void gull_best(char *t)
{
    t[0] = 0;
    if (sav.gull.best[sav.gull.mode & 1]) {
        text_cat(t, "BEST ");
        int_text(sav.gull.best[sav.gull.mode & 1], t + 5);
        text_cat(t, " POSTS");
    }
}

static uint32_t gull_keys_lit(uint32_t *mid)   /* none: the game is finding them */
{
    *mid = 0;
    return 0;
}

static void gull_where(char *t)
{
    t[0] = 0;
    if (gu.state == GS2_STAND) {
        text_cat(t, sav.gull.mode ? "GULL. KNOB 1, MODE SING. KNOB 2, LEVEL " : "GULL. KNOB 1, MODE FLAP. ");
        if (sav.gull.mode) {
            text_cat(t, sav.gull.level == 1 ? "1, NOTES SHOWN. " : "2, BY EAR. ");
            text_cat(t, "HOLD A KEY FOR YOUR HEIGHT, PLAY EACH GAP'S NOTE TO FLY THROUGH IT. ");
        } else {
            text_cat(t, "ANY KEY FLAPS. MISS THE POSTS AND KEEP OUT OF THE SEA. ");
        }
        if (sav.gull.best[sav.gull.mode & 1]) {
            text_cat(t, "BEST ");
            sp_num(t, sav.gull.best[sav.gull.mode & 1]);
            text_cat(t, ". ");
        }
        text_cat(t, "PLAY STARTS.");
        return;
    }
    text_cat(t, gu.state == GS2_OVER ? "GULL. SPLASH. SCORE " : "GULL. SCORE ");
    sp_num(t, gu.score);
    text_cat(t, gu.state == GS2_OVER ? (gu.new_best ? ". NEW BEST. PLAY STARTS AGAIN." : ". PLAY STARTS AGAIN.") :
                ". PLAY STOPS.");
}

static int gull_playing(void) { return gu.state == GS2_FLY; }

static const uint8_t GULL_TUNE[] = {72, 0, 76, 79, 0, 0, 81, 79, 76, 0, 72, 0, 0, 0};

static const game_t GAME_GULL = {"GULL", RGB(110, 143, 196), gull_enter, gull_leave, gull_frame, gull_key,
                                 gull_button, gull_enc, 0, gull_attract, gull_best, gull_keys_lit,
                                 GULL_TUNE, sizeof GULL_TUNE, 200, BW_PLUCK, gull_where, 0, gull_playing,
                                 "FLY THE GULL"};
