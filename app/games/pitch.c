/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Pitch: two ear games.
 *
 * EAR: a note plays and you find it on the keys. Three lives; five right in a row and the level goes up: the white keys
 * of one octave, every key of it, the whole keyboard, chimes, all with C4 played first to measure from; then without
 * C4 (perfect pitch), then without it in any sound. A wrong answer: the right note follows yours, so the interval
 * between them is heard. PLAY hears it again.
 *
 * TUNE, the tuning duel: two tones, the second drifting out of tune; bring it back with the knobs (K1 coarse, K2
 * fine) until the beating stops. Only your ears say how far off it is. The drift grows faster the longer you hold
 * it; out by a quarter of a semitone and it is over. Scored on the time kept in tune.
 *
 * Standing: K1 the game, K2 the level EAR starts at; PLAY starts. Part of the unity build. */
#include "game.h"

#define EAR_LEVELS 6
#define EAR_LIVES 3
#define EAR_UP 5                         /* right in a row to go up a level */
#define EAR_WAIT 8000u                   /* ms to answer */
#define TUNE_OK 4.0f                     /* cents: in tune */
#define TUNE_OUT 25.0f                   /* cents: lost */

enum { PS_STAND, PS_ASK, PS_ANSWERED, PS_OVER };
static struct {
    int state;
    /* ear */
    int level, lives, score, streak, target, last_target, guess, timbre, ref;
    uint32_t asked_ms, answer_ms, next_ms, ref_ms;
    uint8_t played, ref_done;            /* the note sounded; the reference before it */
    uint32_t reveal_ms, lvup_ms;         /* a wrong answer: the right note a moment later; a level-up shown */
    int reveal_up;                       /* the reveal is a level-up's rising pair, not the right note */
    /* tune */
    float err, rate;                     /* cents, cents a second */
    uint32_t start_ms, last_ms, in_tune_ms;
    int ref_note, sent_err;
    int new_best;
} pt;

static const uint8_t EAR_TIMBRES[4] = {BW_SAW, BW_PLUCK, BW_PIANO, BW_CHIME};   /* level 6 picks among them */
static const char *TIMBRE_NAME(int t)
{
    return t == BW_SAW ? "SAW" : t == BW_PLUCK ? "PLUCK" : t == BW_PIANO ? "PIANO" : "CHIME";
}

static void ear_level_text(int lv, char *t)   /* what a level asks */
{
    static const char *const L[EAR_LEVELS] = {"WHITE KEYS, ONE OCTAVE", "EVERY KEY, ONE OCTAVE", "THE WHOLE KEYBOARD",
                                              "CHIMES", "NO C4 FIRST", "NO C4, ANY SOUND"};
    t[0] = 0;
    text_cat(t, L[clamp(lv, 1, EAR_LEVELS) - 1]);
}

static int ear_pick(void)                /* the next note to name, as a key */
{
    static const uint8_t WHITE_C4[8] = {7, 9, 11, 12, 14, 16, 18, 19};
    int k;
    do {
        if (pt.level <= 1)
            k = WHITE_C4[rnd(8)];
        else if (pt.level == 2 || pt.level == 5)
            k = 7 + (int)rnd(13);        /* C4..C5 */
        else
            k = (int)rnd(NKEYS);
    } while (k == pt.last_target);
    return k;
}

static void ear_again(uint32_t now)      /* PLAY: hear it again (the reference too) */
{
    pt.played = 0;
    pt.ref_done = 0;
    pt.ref_ms = now;
    pt.asked_ms = now + (pt.ref ? 900u : 0u);
}

static void ear_ask(uint32_t now)
{
    pt.target = ear_pick();
    pt.last_target = pt.target;
    pt.timbre = pt.level == 4 ? BW_CHIME : pt.level == 6 ? EAR_TIMBRES[rnd(4)] : BW_SAW;
    pt.ref = pt.level <= 4;              /* C4 first, to measure from: all but the last two levels */
    pt.reveal_ms = 0;
    pt.guess = -1;
    pt.state = PS_ASK;
    ear_again(now);
}

static void ear_sound(uint32_t now)      /* the reference at ref_ms, then the note at asked_ms */
{
    if (pt.state != PS_ASK || pt.played || (int32_t)(now - pt.ref_ms) < 0)
        return;
    if (pt.ref && !pt.ref_done) {
        snd_lock();
        bw_note(60, 90, pt.timbre);
        snd_unlock();
        pt.ref_done = 1;
    } else if ((int32_t)(now - pt.asked_ms) >= 0) {
        snd_lock();
        bw_note(53 + pt.target, 100, pt.timbre);
        snd_unlock();
        pt.played = 1;
        pt.asked_ms = now;               /* the clock to answer starts with the note */
    }
}

static void ear_answer(int k, uint32_t now)
{
    pt.guess = k;
    pt.answer_ms = now;
    pt.state = PS_ANSWERED;
    if (k == pt.target) {
        pt.score++;
        pt.streak++;
        pt.next_ms = now + 700u;
        if (pt.streak % EAR_UP == 0 && pt.level < EAR_LEVELS) {
            char t[32], l[20] = "RIGHT. LEVEL ";
            pt.level++;
            pt.lvup_ms = now;
            pt.next_ms = now + 1800u;
            pt.reveal_ms = now + 300u;   /* a rising pair after the note: up a level */
            pt.reveal_up = 1;
            ear_level_text(pt.level, t);
            int_text(pt.level, l + 13);
            app_say(l, t);
        } else {
            app_say("RIGHT", "");
        }
    } else {
        char t[8];
        pt.lvup_ms = 0;
        pt.lives--;
        pt.streak = 0;
        pt.next_ms = now + 2000u;
        if (k < 0) {                     /* too slow: what it was, now */
            snd_lock();
            bw_note(53 + pt.target, 100, pt.timbre);
            snd_unlock();
        } else {
            pt.reveal_ms = now + 450u;   /* after your note, not on top of it: the interval heard */
            pt.reveal_up = 0;
        }
        note_text(53 + pt.target, t);
        sp_buf[0] = 0;
        text_cat(sp_buf, k < 0 ? "TOO SLOW, IT WAS " : "NO, IT WAS ");
        text_cat(sp_buf, t);
        if (ann_on())
            ann_say(sp_buf, 0, ANN_NOW);
    }
}

static const char *tune_time_text(void)   /* "12.3 SECONDS IN TUNE" */
{
    static char t[32];
    t[0] = 0;
    sp_num(t, (int32_t)(pt.in_tune_ms / 1000u));
    text_cat(t, ".");
    sp_num(t, (int32_t)(pt.in_tune_ms / 100u % 10u));
    text_cat(t, " SECONDS IN TUNE");
    return t;
}

static void tune_send(void)
{
    int e = (int)(pt.err + (pt.err < 0.0f ? -0.5f : 0.5f));
    if (e == pt.sent_err)
        return;
    pt.sent_err = e;
    snd_lock();
    bw_tone(1, pt.ref_note * 100 + e, 45);
    snd_unlock();
}

static void pitch_over(void)
{
    pt.state = PS_OVER;
    pt.new_best = 0;
    if (sav.pitch.mode == 0 && pt.score > sav.pitch.best_ear) {
        sav.pitch.best_ear = (uint16_t)pt.score;
        pt.new_best = 1;
        save_dirty();
    } else if (sav.pitch.mode == 1 && pt.in_tune_ms / 100u > sav.pitch.best_tune) {
        sav.pitch.best_tune = (uint16_t)(pt.in_tune_ms / 100u);
        pt.new_best = 1;
        save_dirty();
    }
    snd_lock();
    bw_tone(0, pt.ref_note * 100, 0);
    bw_tone(1, pt.ref_note * 100 + pt.sent_err, 0);
    snd_unlock();
}

static void pitch_go(void)
{
    uint32_t now = plat_ms();
    rnd_state ^= now * 2654435761u;
    pt.score = pt.streak = 0;
    pt.lives = EAR_LIVES;
    pt.last_target = -1;
    pt.level = clamp(sav.pitch.level, 1, EAR_LEVELS);
    if (sav.pitch.mode == 0) {
        ear_ask(now + 300u);
        app_say("LISTEN", "");
    } else {
        pt.ref_note = 57 + (int)rnd(13);          /* A3..A4 */
        pt.err = (float)(8 + (int)rnd(7)) * (rnd(2) ? 1.0f : -1.0f);
        pt.rate = 0.0f;
        pt.start_ms = pt.last_ms = now;
        pt.in_tune_ms = 0;
        pt.sent_err = 9999;
        pt.state = PS_ASK;
        snd_lock();
        bw_tone(0, pt.ref_note * 100, 45);
        snd_unlock();
        tune_send();
        app_say("MATCH THE TONES", "");
    }
}

static void tune_frame(uint32_t now)
{
    float dt = (float)(now - pt.last_ms) * 0.001f, t = (float)(now - pt.start_ms) * 0.001f, top;
    pt.last_ms = now;
    if (dt > 0.1f)
        dt = 0.1f;
    /* the first 5 s: no drift, and it cannot be lost (time to hear what in tune sounds like). Then the drift: a random
     * walk in its speed, the walk wider and the top speed higher as time goes on */
    if (t < 5.0f) {
        pt.rate = 0.0f;
        pt.err = pt.err > TUNE_OUT ? TUNE_OUT : pt.err < -TUNE_OUT ? -TUNE_OUT : pt.err;
    } else {
        t -= 5.0f;
        top = 1.5f + t * 0.08f;
        pt.rate += ((float)rnd(2001) - 1000.0f) * 0.001f * (2.0f + t * 0.1f) * dt * 4.0f;
        pt.rate = pt.rate > top ? top : pt.rate < -top ? -top : pt.rate;
        pt.err += pt.rate * dt;
    }
    if (pt.err > -TUNE_OK && pt.err < TUNE_OK)
        pt.in_tune_ms += (uint32_t)(dt * 1000.0f);
    tune_send();
    if (pt.err > TUNE_OUT || pt.err < -TUNE_OUT) {
        pitch_over();
        app_say(pt.err > 0.0f ? "IT DRIFTED SHARP" : "IT DRIFTED FLAT", tune_time_text());
        tickets_win(clamp(2 + (int)(pt.in_tune_ms / 1500u), 2, 100));   /* tickets: 2, and 1 for each 1.5 s in tune */
    }
}

static void pitch_key(int k, int down, uint32_t ms)
{
    if (!down || sav.pitch.mode == 1)
        return;
    snd_lock();
    bw_note(53 + k, 100, pt.state == PS_STAND || pt.state == PS_OVER ? BW_SAW : pt.timbre);
    snd_unlock();
    if (pt.state == PS_ASK && pt.played)
        ear_answer(k, ms);
}

static void pitch_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (pt.state == PS_STAND || pt.state == PS_OVER)
        pitch_go();
    else if (sav.pitch.mode == 1) {      /* the duel: stop, the time kept */
        pitch_over();
        app_say("STOPPED", tune_time_text());
    } else if (pt.state == PS_ASK && pt.played) {
        ear_again(plat_ms());
    }
}

static void pitch_enc(int role, int steps)
{
    char t[32];
    if (sav.pitch.mode == 1 && pt.state == PS_ASK) {   /* tuning: K1 big steps, K2 small */
        if (role != EN_K1 && role != EN_K2)
            return;
        pt.err += (float)steps * (role == EN_K2 ? 0.5f : 2.0f);
        tune_send();
        return;
    }
    if (pt.state == PS_ASK || pt.state == PS_ANSWERED)
        return;
    if (role == EN_K1) {
        sav.pitch.mode = (uint8_t)clamp(sav.pitch.mode + steps, 0, 1);
        app_readout("MODE", sav.pitch.mode ? "TUNE" : "EAR");
    } else if (role == EN_K2 && sav.pitch.mode == 0) {
        sav.pitch.level = (uint8_t)clamp(sav.pitch.level + steps, 1, EAR_LEVELS);
        ear_level_text(sav.pitch.level, t);
        app_readout("LEVEL", t);
    } else {
        return;
    }
    pt.state = PS_STAND;
    save_dirty();
}

static void pitch_enter(void)
{
    pt.state = PS_STAND;
    pt.timbre = BW_SAW;
}

static void pitch_leave(void)
{
    if (pt.state == PS_ASK || pt.state == PS_ANSWERED)
        pitch_over();
    pt.state = PS_STAND;
}

static uint16_t pitch_key_col(int k)
{
    if (pt.state == PS_ASK && pt.ref && !pt.played && k == 7)
        return G_TEAL;                   /* C4, while it plays first */
    if (pt.state != PS_ANSWERED && !(pt.state == PS_OVER && sav.pitch.mode == 0))
        return 0;
    if (k == pt.target)
        return G_TEAL;
    if (k == pt.guess)
        return G_RED;
    return 0;
}

static void pitch_draw(uint32_t now)
{
    int32_t y0, i;
    char t[40], u[16];
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, G_PAPER);
        cv_rect(0, 0, 240, 18, G_TEAL);
        cv_text(6, 3, &FONT_XS, sav.pitch.mode ? "PITCH: TUNE" : "PITCH: EAR", G_PAPER);
        if (sav.pitch.mode == 0) {
            /* the level, lives, the score */
            if (pt.state != PS_STAND) {
                t[0] = 0;
                text_cat(t, "LEVEL ");
                int_text(pt.level, t + 6);
                cv_text(234 - text_w(&FONT_XS, t), 3, &FONT_XS, t, G_PAPER);
                for (i = 0; i < EAR_LIVES; i++)
                    fill_circle(20 + i * 16, 34, 5, i < pt.lives ? G_RED : G_PAPER2);
                int_text(pt.score, t);
                cv_text(232 - text_w(&FONT_L, t), 24, &FONT_L, t, G_INK);
                if (pt.level < EAR_LEVELS && pt.state != PS_OVER) {   /* five right in a row: the next level */
                    cv_text(14, 46, &FONT_XS, "NEXT LEVEL", G_PLANK2);
                    for (i = 0; i < EAR_UP; i++)
                        fill_circle(84 + i * 10, 53, 3, i < pt.streak % EAR_UP ? G_TEAL : G_PAPER2);
                }
            }
            /* the middle: what to do, or what it was */
            if (pt.state == PS_STAND) {
                cv_text(14, 40, &FONT_XS, "K1 MODE", G_PLANK2);
                cv_text(94, 38, &FONT_B, "EAR", G_INK);
                cv_text(14, 66, &FONT_XS, "K2 LEVEL", G_PLANK2);
                int_text(sav.pitch.level, t);
                cv_text(94, 64, &FONT_B, t, G_INK);
                ear_level_text(sav.pitch.level, t);
                cv_text(94, 84, &FONT_XS, t, G_PLANK2);
                cv_text(14, 138, &FONT_XS, "A NOTE PLAYS: FIND IT ON THE KEYS.", G_PLANK2);
                if (sav.pitch.level <= 4)
                    cv_text(14, 152, &FONT_XS, "C4 PLAYS FIRST, TO MEASURE FROM.", G_PLANK2);
                if (sav.pitch.best_ear) {
                    text_cat((t[0] = 0, t), "BEST ");
                    int_text(sav.pitch.best_ear, t + 5);
                    cv_text(14, 112, &FONT_B, t, G_INK);
                }
            } else if (pt.state == PS_ASK) {
                int32_t r = 30 + (pt.played && now - pt.asked_ms < 300u ? (int32_t)(300u - (now - pt.asked_ms)) / 30 : 0);
                fill_circle(120, 100, r, G_BLUSH);
                text_mid(120, 84, &FONT_L, "?", G_INK);
                text_mid(120, 140, &FONT_XS, pt.ref && !pt.played ? "FIRST C4, TO MEASURE FROM" :
                         pt.played ? "NOW FIND THIS NOTE ON THE KEYS" : "LISTEN", G_PLANK2);
                text_mid(120, 152, &FONT_XS, TIMBRE_NAME(pt.timbre), G_MUTE);
                if (pt.played) {         /* the time left */
                    int32_t left = (int32_t)EAR_WAIT - (int32_t)(now - pt.asked_ms);
                    cv_rect(60, 166, 120 * clamp(left, 0, (int32_t)EAR_WAIT) / (int32_t)EAR_WAIT, 3, G_TEAL);
                }
            } else {
                int right = pt.guess == pt.target;
                fill_circle(120, 100, 30, right ? G_TEAL : G_RED);
                note_text(53 + pt.target, t);
                text_mid(120, 84, &FONT_L, t, G_PAPER);
                if (pt.state == PS_OVER) {
                    text_mid(120, 140, &FONT_B, pt.new_best ? "NEW BEST!" : "GAME OVER", G_INK);
                } else if (pt.lvup_ms && now - pt.lvup_ms < 1800u) {   /* up a level */
                    text_cat((t[0] = 0, t), "LEVEL ");
                    int_text(pt.level, t + 6);
                    text_mid(120, 136, &FONT_B, t, G_TEAL);
                    ear_level_text(pt.level, t);
                    text_mid(120, 154, &FONT_XS, t, G_PLANK2);
                } else if (!right && pt.guess >= 0) {
                    note_text(53 + pt.guess, u);
                    text_cat((t[0] = 0, t), "NOT ");
                    text_cat(t, u);
                    text_mid(120, 140, &FONT_XS, t, G_RED);
                } else if (!right) {
                    text_mid(120, 140, &FONT_XS, "TOO SLOW", G_RED);
                }
            }
            keyboard(172, 44, pitch_key_col);
        } else {
            /* the duel: two forks, the time in tune */
            if (pt.state == PS_STAND) {
                cv_text(14, 40, &FONT_XS, "K1 MODE", G_PLANK2);
                cv_text(94, 38, &FONT_B, "TUNE", G_INK);
                cv_text(14, 70, &FONT_XS, "MATCH THE RED TONE TO THE TEAL ONE.", G_PLANK2);
                cv_text(14, 84, &FONT_XS, "THE WOBBLE STOPS WHEN THEY MATCH.", G_PLANK2);
                cv_text(14, 98, &FONT_XS, "K1 BIG STEPS, K2 SMALL ONES.", G_PLANK2);
                if (sav.pitch.best_tune) {
                    text_cat((t[0] = 0, t), "BEST ");
                    int_text(sav.pitch.best_tune / 10u, t + 5);
                    text_cat(t, ".");
                    int_text(sav.pitch.best_tune % 10u, t + text_len(t));
                    text_cat(t, " S");
                    cv_text(14, 112, &FONT_B, t, G_INK);
                }
            } else {
                uint32_t ms = pt.in_tune_ms;
                int in = pt.state == PS_ASK && pt.err > -TUNE_OK && pt.err < TUNE_OK;   /* (what the counter shows) */
                for (i = 0; i < 2; i++) {     /* two tuning forks, ringing; the second teal while in tune */
                    uint16_t fc = i && !in ? G_RED : G_TEAL;
                    int32_t x = 80 + i * 80, w = pt.state == PS_ASK ? (int32_t)((now / 40u + (uint32_t)i) & 1u) : 0;
                    cv_rect(x - 9 - w, 40, 4, 50, fc);
                    cv_rect(x + 5 + w, 40, 4, 50, fc);
                    cv_rect(x - 9, 88, 18, 4, fc);
                    cv_rect(x - 2, 92, 4, 24, fc);
                }
                t[0] = 0;
                int_text((int32_t)(ms / 1000u), t);
                text_cat(t, ".");
                int_text((int32_t)(ms / 100u % 10u), t + text_len(t));
                text_cat(t, " S");
                text_mid(120, 128, &FONT_L, t, G_INK);
                text_mid(120, 164, &FONT_XS, "IN TUNE", in ? G_TEAL : G_PLANK2);
                if (pt.state == PS_OVER) {
                    text_mid(120, 184, &FONT_B, pt.new_best ? "NEW BEST!" : pt.err > 0.0f ? "IT DRIFTED SHARP" : "IT DRIFTED FLAT",
                             G_RED);
                }
            }
        }
        cv_rect(0, 220, 240, 20, G_LILAC);
        if (pt.state == PS_STAND || pt.state == PS_OVER)
            cv_text(FOOT_R - text_w(&FONT_XS, "PLAY: START"), FOOT_Y, &FONT_XS, "PLAY: START", G_INK);
        else if (sav.pitch.mode == 0 && pt.state == PS_ASK && pt.played)   /* only when PLAY does it */
            cv_text(FOOT_R - text_w(&FONT_XS, "PLAY: HEAR IT AGAIN"), FOOT_Y, &FONT_XS, "PLAY: HEAR IT AGAIN", G_INK);
        else if (sav.pitch.mode == 1)
            cv_text(FOOT_R - text_w(&FONT_XS, "PLAY: STOP"), FOOT_Y, &FONT_XS, "PLAY: STOP", G_INK);
        band_send(y0);
    }
}

static void pitch_frame(uint32_t now)
{
    if (sav.pitch.mode == 0) {
        ear_sound(now);
        if (pt.reveal_ms && (int32_t)(now - pt.reveal_ms) >= 0) {   /* after your note: the right one, or up a level */
            snd_lock();
            if (pt.reveal_up) {
                bw_note(84, 70, BW_PLUCK);
                bw_note(91, 70, BW_PLUCK);
            } else {
                bw_note(53 + pt.target, 100, pt.timbre);
            }
            snd_unlock();
            pt.reveal_ms = 0;
        }
        if (pt.state == PS_ASK && pt.played && now - pt.asked_ms > EAR_WAIT)
            ear_answer(-1, now);
        if (pt.state == PS_ANSWERED && (int32_t)(now - pt.next_ms) >= 0) {
            if (pt.lives <= 0) {
                pitch_over();
                char t[16] = "SCORE ";
                sp_num(t, pt.score);
                app_say(pt.new_best ? "GAME OVER. NEW BEST" : "GAME OVER", t);
                tickets_win(clamp(2 + pt.score * (3 + pt.level) / 4, 2, 100));   /* tickets: 2, and 1 a note named
                                                                                  * (2.25 on level 6), by the level
                                                                                  * it ended on */
            } else {
                ear_ask(now);
            }
        }
    } else if (pt.state == PS_ASK) {
        tune_frame(now);
    }
    pitch_draw(now);
}

static void pitch_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    int32_t i, k = (int32_t)(ms / 600u % 8u);
    for (i = 0; i < 8; i++)              /* a keyboard, one key lit at a time */
        cv_rect(x + 3 + i * (w - 6) / 8, y + h / 3, (w - 6) / 8 - 1, h / 2, i == k ? RGB(126, 224, 184) : G_PAPER);
}

static void pitch_best(char *t)
{
    t[0] = 0;
    if (sav.pitch.mode == 1 && sav.pitch.best_tune) {
        text_cat(t, "BEST ");
        int_text(sav.pitch.best_tune / 10u, t + 5);
        text_cat(t, ".");
        int_text(sav.pitch.best_tune % 10u, t + text_len(t));
        text_cat(t, " S");
    } else if (sav.pitch.best_ear) {
        text_cat(t, "BEST ");
        int_text(sav.pitch.best_ear, t + 5);
        text_cat(t, " NOTES");
    }
}

static uint32_t pitch_keys_lit(uint32_t *mid)   /* answered: the right key lit, a wrong one at the mid level */
{
    int shown = pt.state == PS_ANSWERED || (pt.state == PS_OVER && sav.pitch.mode == 0);
    if (pt.state == PS_ASK && pt.ref && !pt.played) {   /* C4, while it plays first */
        *mid = 1u << 7;
        return 0;
    }
    *mid = shown && pt.guess >= 0 && pt.guess != pt.target ? 1u << pt.guess : 0u;
    return shown && pt.target >= 0 ? 1u << pt.target : 0u;
}

static void pitch_where(char *t)         /* the screen, for a screen reader */
{
    char l[32];
    t[0] = 0;
    if (sav.pitch.mode == 0) {
        text_cat(t, "PITCH, EAR. ");
        if (pt.state == PS_STAND) {
            ear_level_text(sav.pitch.level, l);
            text_cat(t, "KNOB 1, MODE EAR. KNOB 2, LEVEL ");
            sp_num(t, sav.pitch.level);
            text_cat(t, ", ");
            text_cat(t, l);
            text_cat(t, ". A NOTE PLAYS, FIND IT ON THE KEYS. ");
        } else {
            text_cat(t, pt.state == PS_OVER ? "GAME OVER. SCORE " : "SCORE ");
            sp_num(t, pt.score);
            if (pt.state != PS_OVER) {
                text_cat(t, ", LIVES ");
                sp_num(t, pt.lives);
                text_cat(t, ", LEVEL ");
                sp_num(t, pt.level);
                text_cat(t, pt.played ? ". FIND THE NOTE ON THE KEYS. PLAY HEARS IT AGAIN." : ". LISTEN.");
                return;
            }
            text_cat(t, ". ");
        }
        if (sav.pitch.best_ear) {
            text_cat(t, "BEST ");
            sp_num(t, sav.pitch.best_ear);
            text_cat(t, ". ");
        }
    } else {
        text_cat(t, "PITCH, TUNE. ");
        if (pt.state == PS_ASK) {
            text_cat(t, tune_time_text());
            text_cat(t, ". MATCH THE TONES BY EAR, KNOB 1 BIG STEPS, KNOB 2 SMALL. PLAY STOPS.");
            return;
        }
        if (pt.state == PS_OVER) {
            text_cat(t, tune_time_text());
            text_cat(t, ". ");
        } else {
            text_cat(t, "KNOB 1, MODE TUNE. MATCH THE TONES BY EAR. THE WOBBLE STOPS WHEN THEY MATCH. ");
        }
        if (sav.pitch.best_tune) {
            text_cat(t, "BEST ");
            sp_num(t, sav.pitch.best_tune / 10u);
            text_cat(t, ".");
            sp_num(t, sav.pitch.best_tune % 10u);
            text_cat(t, " SECONDS. ");
        }
    }
    text_cat(t, "PLAY STARTS.");
}

static int pitch_playing(void) { return pt.state == PS_ASK || pt.state == PS_ANSWERED; }

static const uint8_t PITCH_TUNE[] = {60, 64, 67, 72, 0, 67, 64, 0};   /* the attract tune: an arpeggio */

static const game_t GAME_PITCH = {"PITCH", RGB(62, 154, 176), pitch_enter, pitch_leave, pitch_frame, pitch_key,
                                  pitch_button, pitch_enc, 0, pitch_attract, pitch_best, pitch_keys_lit,
                                  PITCH_TUNE, sizeof PITCH_TUNE, 300, BW_SAW, pitch_where, 0, pitch_playing, "NAME THAT NOTE", 0};
