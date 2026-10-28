/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The shooting gallery: ducks swim across three rows, each one carrying a note, and quack it as they come into view
 * (by ear, again midway). Press a duck's key to shoot it: a clank, and over it goes. A shot at nothing jams the gun
 * for a moment, so mashing the keys gets nowhere. On the first levels the ducks wear their names and their keys
 * light; later only the quack says which key it is. Twenty ducks a round, scored on how many you get.
 *
 * Standing: K1 the level; PLAY starts. Part of the unity build. */
#include "game.h"

#define GL_LEVELS 4
#define GL_DUCKS 20                      /* a round */
#define GL_ON 4                          /* at most this many on at once */
#define GL_ROW_Y(r) (76 + (r) * 50)       /* a row's water line */

enum { GS_STAND, GS_PLAY, GS_OVER };
typedef struct {
    uint8_t on, row, key, hit, quacks;
    int32_t x;                           /* x 16 */
    uint32_t hit_ms, quack_ms;
} duck_t;
static struct {
    int state, spawned, done, score, streak, best_streak, shots, new_best;
    duck_t duck[GL_ON];
    uint32_t next_ms, last_ms, jam_until;
    int tell_key;                        /* the duck that got away, said for a moment */
    uint32_t tell_ms;
} gl;

static const char *const GL_LEVEL_NAME[GL_LEVELS] = {"NAMES, WHITE KEYS", "NAMES, BLACK KEYS TOO", "BY EAR, ONE OCTAVE",
                                                     "BY EAR, EVERY KEY"};

static void gl_clear(void)
{
    int i;
    for (i = 0; i < GL_ON; i++)
        gl.duck[i].on = 0;
}

static int gl_pick(void)                 /* a duck's key */
{
    static const uint8_t WHITE_C4[8] = {7, 9, 11, 12, 14, 16, 18, 19};
    int lv = clamp(sav.gallery.level, 1, GL_LEVELS), k = 0, tries, i, clash;
    for (tries = 0; tries < 8; tries++) {   /* not a note already swimming */
        k = lv == 1 ? WHITE_C4[rnd(8)] : lv <= 3 ? 7 + (int)rnd(13) : (int)rnd(NKEYS);
        for (clash = 0, i = 0; i < GL_ON; i++)
            clash |= gl.duck[i].on && !gl.duck[i].hit && gl.duck[i].key == k;
        if (!clash)
            break;
    }
    return k;
}

static int32_t gl_speed(void)            /* x 16 px a frame of 33 ms */
{
    return 16 + clamp(sav.gallery.level, 1, GL_LEVELS) * 4 + gl.done;
}

static void gallery_go(void)
{
    rnd_state ^= plat_ms() * 2654435761u;
    gl_clear();
    gl.spawned = gl.done = gl.score = gl.streak = gl.best_streak = gl.shots = 0;
    gl.jam_until = 0;
    gl.tell_ms = 0;
    gl.next_ms = gl.last_ms = plat_ms() + 600u;
    gl.state = GS_PLAY;
    app_say("SHOOT THE DUCKS", "");
}

static void gl_spawn(uint32_t now)
{
    int i, row = (int)rnd(3);
    for (i = 0; i < GL_ON; i++)
        if (!gl.duck[i].on) {
            duck_t *d = &gl.duck[i];
            d->on = 1;
            d->hit = 0;
            d->quacks = 0;
            d->row = (uint8_t)row;
            d->key = (uint8_t)gl_pick();
            d->x = row == 1 ? (240 + 16) * 16 : -16 * 16;   /* the middle row swims the other way */
            gl.spawned++;
            break;
        }
    (void)now;
}

static void gl_quack(duck_t *d, uint32_t now)   /* its note: as it comes into view, and (by ear) midway */
{
    snd_lock();
    bw_note(53 + d->key, 100, BW_CHIME);
    snd_unlock();
    d->quacks++;
    d->quack_ms = now;
}

static void gl_gap(uint32_t now)         /* a breath before the next duck, so its quack does not run into your shot */
{
    if ((int32_t)(gl.next_ms - now) < 500)
        gl.next_ms = now + 500u;
}

static void gallery_key(int k, int down, uint32_t ms)
{
    int i, best = -1;
    int32_t far = -1;
    if (!down)
        return;
    snd_lock();
    bw_note(53 + k, 100, BW_PLUCK);
    snd_unlock();
    if (gl.state != GS_PLAY || (int32_t)(ms - gl.jam_until) < 0)   /* (reloading) */
        return;
    gl.shots++;
    for (i = 0; i < GL_ON; i++) {        /* a duck with this note: the one nearest getting away */
        duck_t *d = &gl.duck[i];
        int32_t gone = d->row == 1 ? (240 * 16 - d->x) : d->x;
        if (d->on && !d->hit && d->key == k && gone > far) {
            far = gone;
            best = i;
        }
    }
    if (best < 0) {                      /* nothing there: the gun jams a moment */
        gl.streak = 0;
        gl.jam_until = ms + 600u;
        snd_lock();
        bw_note(41, 80, BW_CLUNK);
        snd_unlock();
        return;
    }
    gl.jam_until = ms + 120u;            /* a chord pressed fires once */
    snd_lock();
    bw_drum(BW_BLOCK_HI, 100);           /* the tin target's clank */
    snd_unlock();
    gl_gap(ms);
    gl.duck[best].hit = 1;
    gl.duck[best].hit_ms = ms;
    gl.score++;
    gl.done++;
    gl.streak++;
    gl.best_streak = gl.streak > gl.best_streak ? gl.streak : gl.best_streak;
}

static void gallery_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (gl.state == GS_PLAY) {
        gl.state = GS_STAND;
        gl_clear();
        app_say("STOPPED", "");
    } else {
        gallery_go();
    }
}

static void gallery_enc(int role, int steps)
{
    char t[32];
    if (role != EN_K1 || gl.state == GS_PLAY)
        return;
    sav.gallery.level = (uint8_t)clamp(sav.gallery.level + steps, 1, GL_LEVELS);
    int_text(sav.gallery.level, t);
    text_cat(t, ", ");
    text_cat(t, GL_LEVEL_NAME[sav.gallery.level - 1]);
    app_readout("LEVEL", t);
    gl.state = GS_STAND;
    save_dirty();
}

static void gallery_enter(void)
{
    gl.state = GS_STAND;
    gl_clear();
}
static void gallery_leave(void)
{
    gl.state = GS_STAND;
    gl_clear();
}

static void duck_draw(const duck_t *d, uint32_t now, int names)
{
    int32_t x = d->x / 16, y = GL_ROW_Y(d->row), dir = d->row == 1 ? -1 : 1, fall = 0;
    char s[8];
    if (d->hit)
        fall = (int32_t)(now - d->hit_ms) / 8;   /* over it goes */
    if (fall > 30)
        return;
    round_rect(x - 17, y - 18 + fall, 34, 18 - fall / 2, 7, RGB(250, 210, 60));   /* the body */
    cv_rect(x - dir * 17 - (dir > 0 ? 0 : 5), y - 16 + fall, 5, 6, RGB(250, 210, 60));   /* the tail */
    fill_circle(x + dir * 12, y - 27 + fall, 8, RGB(250, 210, 60));            /* the head */
    if (d->quacks && now - d->quack_ms < 400u && !d->hit) {   /* quacking: the beak open, a bubble */
        cv_rect(x + dir * 19 - (dir < 0 ? 7 : 0), y - 30, 7, 2, G_ORANGE);
        cv_rect(x + dir * 19 - (dir < 0 ? 7 : 0), y - 26, 7, 2, G_ORANGE);
        fill_circle(x + dir * 22, y - 42, 5, G_PAPER);
        cv_rect(x + dir * 22, y - 46, 1, 7, G_INK);
        fill_circle(x + dir * 22 - 1, y - 39, 2, G_INK);
    } else {
        cv_rect(x + dir * 19 - (dir < 0 ? 7 : 0), y - 28 + fall, 7, 4, G_ORANGE);  /* the beak */
    }
    cv_rect(x + dir * 14, y - 31 + fall, 2, 2, G_INK);                          /* the eye */
    if (names && !d->hit) {
        note_text(53 + d->key, s);
        round_rect(x - 13, y - 16, 24, 13, 4, G_PAPER);
        text_mid(x - 1, y - 17, &FONT_XS, s, G_INK);
    }
}

static void gallery_draw(uint32_t now)
{
    int32_t y0, i, r;
    int names = clamp(sav.gallery.level, 1, GL_LEVELS) <= 2;
    char s[24];
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, RGB(60, 40, 70));       /* the back of the booth */
        for (i = 0; i < 240; i += 20) {  /* the awning */
            cv_rect(i, 0, 10, 22, G_RED);
            cv_rect(i + 10, 0, 10, 22, G_PAPER);
        }
        for (r = 0; r < 3; r++) {        /* three rows of water, their waves moving */
            int32_t y = GL_ROW_Y(r), o = (int32_t)(now / 60u % 16u) * (r == 1 ? -1 : 1);
            cv_rect(0, y - 4, 240, 18, G_SEA);
            for (i = -16; i < 256; i += 16) {    /* the cut-out waves in front */
                fill_circle(i + o + 8, y - 4, 5, G_SEA);
                cv_rect(i + o + 4, y - 9, 8, 2, G_FOAM);
            }
        }
        for (i = 0; i < GL_ON; i++)
            if (gl.duck[i].on)
                duck_draw(&gl.duck[i], now, names);
        cv_rect(0, 196, 240, 24, RGB(60, 40, 70));
        if (gl.state == GS_STAND) {
            round_rect(14, 30, 212, 116, 8, G_PAPER);
            text_cat((s[0] = 0, s), "K1 LEVEL ");
            int_text(sav.gallery.level, s + 9);
            cv_text(24, 38, &FONT_B, s, G_INK);
            cv_text(24, 58, &FONT_XS, GL_LEVEL_NAME[clamp(sav.gallery.level, 1, GL_LEVELS) - 1], G_PLANK2);
            cv_text(24, 84, &FONT_XS, "PLAY THE NOTE A DUCK QUACKS.", G_PLANK2);
            cv_text(24, 98, &FONT_XS, "A MISSED SHOT JAMS THE GUN.", G_PLANK2);
            text_cat((s[0] = 0, s), "20 DUCKS");
            if (sav.gallery.best) {
                text_cat(s, ". BEST ");
                int_text(sav.gallery.best, s + text_len(s));
            }
            cv_text(24, 120, &FONT_XS, s, G_INK);
        } else {
            text_cat((s[0] = 0, s), "HITS ");
            int_text(gl.score, s + 5);
            cv_text(8, 200, &FONT_B, s, G_PAPER);
            s[0] = 0;
            if (gl.tell_ms && now - gl.tell_ms < 1500u) {   /* the one that got away */
                if (clamp(sav.gallery.level, 1, GL_LEVELS) >= 3) {
                    char n[6];
                    note_text(53 + gl.tell_key, n);
                    text_cat(s, "THAT WAS ");
                    text_cat(s, n);
                } else {
                    text_cat(s, "MISSED");
                }
            } else {
                text_cat(s, "DUCK ");
                int_text(gl.spawned, s + 5);
                text_cat(s, " OF 20");
            }
            cv_text(232 - text_w(&FONT_XS, s), 202, &FONT_XS, s, G_PAPER);
            if (gl.state == GS_OVER) {
                round_rect(40, 72, 160, 76, 8, G_PAPER);
                int_text(gl.score, s);
                text_cat(s, " OF 20");
                text_mid(120, 78, &FONT_L, s, G_INK);
                text_mid(120, 112, &FONT_B, gl.new_best ? "NEW BEST!" : gl.score == 20 ?
                         "CLEAN SWEEP!" : gl.score >= 15 ? "SHARP SHOOTING!" : "ROUND OVER", G_RED);
                text_cat((s[0] = 0, s), "BEST RUN ");
                int_text(gl.best_streak, s + 9);
                text_mid(120, 130, &FONT_XS, s, G_PLANK2);
            }
        }
        cv_rect(0, 220, 240, 20, G_LILAC);
        {
            const char *f = gl.state == GS_PLAY ? "PLAY: STOP" : gl.state == GS_OVER ? "PLAY: AGAIN" : "PLAY: START";
            cv_text(232 - text_w(&FONT_XS, f), 224, &FONT_XS, f, G_INK);
        }
        band_send(y0);
    }
}

static void gallery_frame(uint32_t now)
{
    int i, live = 0;
    if (gl.state == GS_PLAY) {
        int32_t frames = (int32_t)(now - gl.last_ms) / 33;   /* the ducks move by time, whatever the frame rate */
        if (frames < 0)
            frames = 0;
        gl.last_ms += (uint32_t)frames * 33u;
        for (i = 0; i < GL_ON; i++) {
            duck_t *d = &gl.duck[i];
            if (!d->on)
                continue;
            if (d->hit) {
                if (now - d->hit_ms > 300u)
                    d->on = 0;
                continue;
            }
            live++;
            d->x += (d->row == 1 ? -1 : 1) * gl_speed() * frames;
            {
                int32_t in = d->row == 1 ? 240 * 16 - d->x : d->x;
                if (!d->quacks && in >= 16 * 16)
                    gl_quack(d, now);
                else if (d->quacks == 1 && clamp(sav.gallery.level, 1, GL_LEVELS) >= 3 && in >= 120 * 16)
                    gl_quack(d, now);    /* by ear: again, midway */
            }
            if (d->x < -20 * 16 || d->x > 260 * 16) {   /* got away */
                d->on = 0;
                gl.done++;
                gl.streak = 0;
                gl.tell_key = d->key;
                gl.tell_ms = now;
                gl_gap(now);
                snd_lock();
                bw_drum(BW_BLOCK_LO, 50);
                snd_unlock();
                if (clamp(sav.gallery.level, 1, GL_LEVELS) >= 3) {
                    char n[6];
                    note_text(53 + d->key, n);
                    app_say("GOT AWAY", n);
                }
            }
        }
        if (gl.spawned < GL_DUCKS && live < 1 + clamp(sav.gallery.level, 1, GL_LEVELS) / 2 &&
            (int32_t)(now - gl.next_ms) >= 0) {
            gl_spawn(now);
            gl.next_ms = now + 900u + rnd(900);
        }
        if (gl.done >= GL_DUCKS) {
            char t[12];
            gl.state = GS_OVER;
            gl.new_best = gl.score > sav.gallery.best;
            if (gl.new_best) {
                sav.gallery.best = (uint16_t)gl.score;
                save_dirty();
            }
            int_text(gl.score, t);
            text_cat(t, " OF 20");
            app_say(gl.new_best ? "ROUND OVER. NEW BEST" : "ROUND OVER", t);
        }
    }
    gallery_draw(now);
}

static void gallery_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    int32_t dx = (int32_t)(ms / 30u % (uint32_t)(w + 20)) - 10;
    cv_rect(x, y + h * 2 / 3, w, 3, G_SEA);
    if (dx > 0 && dx < w - 8) {
        cv_rect(x + dx, y + h * 2 / 3 - 5, 8, 5, RGB(250, 210, 60));
        cv_rect(x + dx + 6, y + h * 2 / 3 - 8, 3, 3, RGB(250, 210, 60));
    }
}

static void gallery_best(char *t)
{
    t[0] = 0;
    if (sav.gallery.best) {
        text_cat(t, "BEST ");
        int_text(sav.gallery.best, t + 5);
    }
}

static uint32_t gallery_keys_lit(uint32_t *mid)   /* the ducks' keys, on the levels with names */
{
    uint32_t keys = 0;
    int i;
    *mid = 0;
    if (gl.state != GS_PLAY || clamp(sav.gallery.level, 1, GL_LEVELS) > 2)
        return 0;
    for (i = 0; i < GL_ON; i++)
        if (gl.duck[i].on && !gl.duck[i].hit)
            *mid |= 1u << gl.duck[i].key;
    return keys;
}

static void gallery_where(char *t)
{
    t[0] = 0;
    if (gl.state == GS_STAND) {
        text_cat(t, "SHOOTING GALLERY. KNOB 1, LEVEL ");
        sp_num(t, sav.gallery.level);
        text_cat(t, ", ");
        text_cat(t, GL_LEVEL_NAME[clamp(sav.gallery.level, 1, GL_LEVELS) - 1]);
        text_cat(t, ". PLAY THE NOTE A DUCK QUACKS TO SHOOT IT. TWENTY DUCKS A ROUND. ");
    } else {
        text_cat(t, gl.state == GS_OVER ? "SHOOTING GALLERY. ROUND OVER. " : "SHOOTING GALLERY. ");
        sp_num(t, gl.score);
        text_cat(t, " OF ");
        sp_num(t, gl.done);
        text_cat(t, " DUCKS HIT. ");
        if (gl.state == GS_PLAY) {
            text_cat(t, "DUCK ");
            sp_num(t, gl.spawned);
            text_cat(t, " OF 20. ");
        }
    }
    if (sav.gallery.best) {
        text_cat(t, "BEST ");
        sp_num(t, sav.gallery.best);
        text_cat(t, ". ");
    }
    text_cat(t, gl.state == GS_PLAY ? "PLAY STOPS." : gl.state == GS_OVER ? "PLAY STARTS AGAIN." : "PLAY STARTS.");
}

static int gallery_playing(void) { return gl.state == GS_PLAY; }

static const uint8_t GALLERY_TUNE[] = {67, 0, 72, 0, 76, 0, 79, 0, 0, 0, 76, 79, 0, 0, 0, 0};

static const game_t GAME_GALLERY = {"GALLERY", RGB(60, 40, 70), gallery_enter, gallery_leave, gallery_frame,
                                    gallery_key, gallery_button, gallery_enc, 0, gallery_attract, gallery_best,
                                    gallery_keys_lit, GALLERY_TUNE, sizeof GALLERY_TUNE, 180, BW_CHIME, gallery_where, 0, gallery_playing};
