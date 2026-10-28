/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Parrot: a memory game, inspired by Simon. Four tones (an A major chord: E, A, C sharp, E) on four pads, each a lane of
 * four white keys as in Tiles (F-B, C-F, G-C, D-G). It plays a tune, lighting each pad; you play it back; it adds one
 * and plays it again, a little faster each time. Every five rounds the four tones move to a new key: the pads stay,
 * but by ear it is the tune's shape you follow now. One wrong (or too slow) and it is over. Scored on the rounds
 * played back.
 *
 * Standing: K1 the speed; PLAY starts. Part of the unity build. */
#include "game.h"

#define SM_MAX 64
#define SM_WAIT 4000u                    /* ms to the next press before it gives up on you */
static const uint8_t SM_NOTE[4] = {52, 57, 61, 64};
static const uint16_t SM_COL[4] = {RGB(235, 110, 90), RGB(40, 160, 150), RGB(235, 180, 60), RGB(150, 120, 200)};
static const char *const SM_LANE[4] = {"F-B", "C-F", "G-C", "D-G"};

enum { SS_STAND, SS_SHOW, SS_YOURS, SS_OVER };
static struct {
    int state, len, pos, lit, new_best, wrong, why;   /* why it ended: 0 a wrong pad, 1 too slow */
    uint8_t seq[SM_MAX];
    uint32_t next_ms, lit_until, yours_ms;
} sm;

static int32_t sm_step(void)             /* ms a tone, at the speed chosen, faster as the sequence grows */
{
    static const int16_t BASE[3] = {620, 470, 340};
    return clamp(BASE[clamp(sav.simon.level, 1, 3) - 1] - sm.len * 10, 180, 700);
}

static int sm_key(void)                  /* the tones' key: up a fourth, down a tone... every five rounds */
{
    static const int8_t K[6] = {0, 5, -2, 3, -4, 1};
    return K[(sm.len - 1) / 5 % 6];
}

static void sm_light(int lane, uint32_t now, int32_t ms)
{
    sm.lit = lane;
    sm.lit_until = now + (uint32_t)ms;
    snd_lock();
    bw_note(SM_NOTE[lane] + sm_key(), 110, BW_SAW);
    snd_unlock();
}

static void sm_show(uint32_t now)        /* the sequence again, from the start */
{
    sm.state = SS_SHOW;
    sm.pos = 0;
    sm.next_ms = now + 700u;
}

static void simon_go(void)
{
    uint32_t now = plat_ms();
    rnd_state ^= now * 2654435761u;
    sm.len = 1;
    sm.seq[0] = (uint8_t)rnd(4);
    sm.wrong = -1;
    sm_show(now);
    app_say("LISTEN", "");
}

static void simon_over(int why)
{
    char t[16];
    sm.why = why;
    sm.state = SS_OVER;
    sm.new_best = sm.len - 1 > sav.simon.best;
    if (sm.new_best) {
        sav.simon.best = (uint16_t)(sm.len - 1);
        save_dirty();
    }
    snd_lock();
    bw_note(41, 120, BW_CLUNK);
    snd_unlock();
    text_cat((t[0] = 0, t), "SCORE ");
    int_text(sm.len - 1, t + 6);
    app_say(why ? (sm.new_best ? "TOO SLOW. NEW BEST" : "TOO SLOW") : (sm.new_best ? "WRONG PAD. NEW BEST" : "WRONG PAD"),
            t);
    sm.lit = sm.seq[sm.pos];             /* the right pad, lit a moment */
    sm.lit_until = plat_ms() + 900u;
}

static void simon_key(int k, int down, uint32_t ms)
{
    int lane = key_lane(k);
    if (!down)
        return;
    if (sm.state != SS_YOURS) {          /* standing: the tones to try */
        if (sm.state != SS_SHOW)
            sm_light(lane, ms, 250);
        return;
    }
    sm_light(lane, ms, 250);
    if (lane != sm.seq[sm.pos]) {
        sm.wrong = lane;
        simon_over(0);
        return;
    }
    sm.yours_ms = ms;
    if (++sm.pos == sm.len) {            /* all of it: one more */
        char t[8];
        if (sm.len < SM_MAX)
            sm.seq[sm.len++] = (uint8_t)rnd(4);
        sm_show(ms);
        sm.next_ms = ms + 1000u;         /* (the round said first, not over the tones) */
        int_text(sm.len, t);
        if (sm.len % 5 == 1 && sm.len > 1) {   /* a new key: its four tones, quickly, first */
            int i;
            snd_lock();
            for (i = 0; i < 4; i++)
                bw_note(SM_NOTE[i] + sm_key(), 70, BW_PLUCK);
            snd_unlock();
            sm.next_ms += 400u;
            app_say("NEW KEY. ROUND", t);
        } else {
            app_say("ROUND", t);
        }
    }
}

static void simon_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (sm.state == SS_STAND || sm.state == SS_OVER)
        simon_go();
    else {
        sm.state = SS_STAND;
        app_say("STOPPED", "");
    }
}

static const char *const SPEED_NAME[3] = {"SLOW", "MEDIUM", "FAST"};

static void simon_enc(int role, int steps)
{
    if (role != EN_K1 || sm.state == SS_SHOW || sm.state == SS_YOURS)
        return;
    sav.simon.level = (uint8_t)clamp(sav.simon.level + steps, 1, 3);
    app_readout("SPEED", SPEED_NAME[sav.simon.level - 1]);
    save_dirty();
}

static void simon_enter(void) { sm.state = SS_STAND; }
static void simon_leave(void) { sm.state = SS_STAND; }

static uint16_t sm_keycol(int k)         /* each pad's keys in its colour */
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS;
    int i;
    for (i = 0; i < 16; i++)
        if (WK[i] == k)
            return mix(SM_COL[key_lane(k)], G_PAPER, 150);
    return 0;
}

static void simon_draw(uint32_t now)
{
    int32_t y0, i;
    char s[32];
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, G_PAPER);
        cv_rect(0, 0, 240, 18, RGB(40, 160, 150));
        cv_text(6, 3, &FONT_XS, "PARROT", G_PAPER);
        for (i = 0; i < 4; i++) {        /* four pads, over the lanes of keys they belong to */
            int lit = sm.lit == i && now < sm.lit_until;
            round_rect(i * 60 + 4, 68, 52, 106, 10, lit ? mix(SM_COL[i], C_WHITE, 120) : mix(SM_COL[i], G_INK, 80));
            text_mid(i * 60 + 30, 146, &FONT_B, SM_LANE[i], G_PAPER);
            if (sm.state == SS_OVER && i == sm.seq[sm.pos])
                fill_circle(i * 60 + 30, 112, 10, G_PAPER);    /* the one it was */
            if (sm.state == SS_OVER && i == sm.wrong) {         /* the one you pressed */
                cv_line(i * 60 + 20, 102, i * 60 + 40, 122, G_PAPER);
                cv_line(i * 60 + 40, 102, i * 60 + 20, 122, G_PAPER);
            }
        }
        if (sm.state == SS_STAND) {
            text_mid(120, 26, &FONT_B, "LISTEN, THEN PLAY IT BACK", G_INK);
            text_cat((s[0] = 0, s), "K1 SPEED: ");
            text_cat(s, SPEED_NAME[clamp(sav.simon.level, 1, 3) - 1]);
            text_mid(120, 46, &FONT_XS, s, G_PLANK2);
        } else if (sm.state == SS_OVER) {
            text_mid(120, 26, &FONT_B, sm.new_best ? "NEW BEST!" : "GAME OVER", G_INK);
            text_cat((s[0] = 0, s), sm.why ? "TOO SLOW, SCORE " : "WRONG PAD, SCORE ");
            int_text(sm.len - 1, s + text_len(s));
            text_mid(120, 46, &FONT_XS, s, G_PLANK2);
        } else {
            text_mid(120, 26, &FONT_B, sm.state == SS_SHOW ? "LISTEN" : "YOUR TURN", G_INK);
            text_cat((s[0] = 0, s), "ROUND ");
            int_text(sm.len, s + 6);
            text_mid(120, 46, &FONT_XS, s, G_PLANK2);
        }
        if (sav.simon.best && sm.state != SS_SHOW && sm.state != SS_YOURS) {
            text_cat((s[0] = 0, s), "BEST ");
            int_text(sav.simon.best, s + 5);
            text_mid(120, 180, &FONT_XS, s, G_PLANK2);
        }
        keyboard(198, 20, sm_keycol);
        cv_rect(0, 220, 240, 20, G_LILAC);
        {
            const char *f = sm.state == SS_SHOW || sm.state == SS_YOURS ? "PLAY: STOP" :
                            sm.state == SS_OVER ? "PLAY: AGAIN" : "PLAY: START";
            cv_text(232 - text_w(&FONT_XS, f), 224, &FONT_XS, f, G_INK);
        }
        band_send(y0);
    }
}

static void simon_frame(uint32_t now)
{
    if (sm.state == SS_SHOW && (int32_t)(now - sm.next_ms) >= 0) {
        if (sm.pos < sm.len) {
            int32_t st = sm_step();
            sm_light(sm.seq[sm.pos++], now, st * 7 / 10);
            sm.next_ms = now + (uint32_t)st;
        } else {
            sm.state = SS_YOURS;
            sm.pos = 0;
            sm.yours_ms = now;
        }
    }
    if (sm.state == SS_YOURS && now - sm.yours_ms > SM_WAIT) {
        sm.wrong = -1;
        simon_over(1);
    }
    simon_draw(now);
}

static void simon_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    int i, lit = (int)(ms / 400u % 4u);
    for (i = 0; i < 4; i++)
        cv_rect(x + 3 + i * (w - 6) / 4, y + h / 4, (w - 6) / 4 - 2, h / 2, i == lit ? mix(SM_COL[i], C_WHITE, 100) :
                                                                                   mix(SM_COL[i], G_INK, 100));
}

static void simon_best(char *t)
{
    t[0] = 0;
    if (sav.simon.best) {
        text_cat(t, "BEST ");
        int_text(sav.simon.best, t + 5);
        text_cat(t, " ROUNDS");
    }
}

static uint32_t simon_keys_lit(uint32_t *mid)   /* the lit pad's four white keys */
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS;
    uint32_t keys = 0;
    int w;
    *mid = 0;
    if (sm.lit >= 0 && plat_ms() < sm.lit_until)
        for (w = 0; w < 4; w++)
            keys |= 1u << WK[sm.lit * 4 + w];
    return keys;
}

static void simon_where(char *t)
{
    t[0] = 0;
    if (sm.state == SS_STAND) {
        text_cat(t, "PARROT. LISTEN TO THE TONES, THEN PLAY THEM BACK. EACH PAD IS FOUR WHITE KEYS, LOW TO HIGH. "
                    "KNOB 1, SPEED ");
        text_cat(t, SPEED_NAME[clamp(sav.simon.level, 1, 3) - 1]);
        text_cat(t, ". ");
    } else if (sm.state == SS_OVER) {
        text_cat(t, sm.why ? "PARROT. TOO SLOW. SCORE " : "PARROT. WRONG PAD. SCORE ");
        sp_num(t, sm.len - 1);
        text_cat(t, ". ");
    } else {
        text_cat(t, sm.state == SS_SHOW ? "PARROT. LISTEN. ROUND " : "PARROT. YOUR TURN. ROUND ");
        sp_num(t, sm.len);
        text_cat(t, ". ");
    }
    if (sav.simon.best) {
        text_cat(t, "BEST ");
        sp_num(t, sav.simon.best);
        text_cat(t, ". ");
    }
    text_cat(t, sm.state == SS_SHOW || sm.state == SS_YOURS ? "PLAY STOPS." :
                sm.state == SS_OVER ? "PLAY STARTS AGAIN." : "PLAY STARTS.");
}

static int simon_playing(void) { return sm.state == SS_SHOW || sm.state == SS_YOURS; }

static const uint8_t SIMON_TUNE[] = {52, 57, 61, 64, 0, 64, 61, 57, 0, 0};

static const game_t GAME_SIMON = {"PARROT", RGB(40, 160, 150), simon_enter, simon_leave, simon_frame, simon_key,
                                  simon_button, simon_enc, 0, simon_attract, simon_best, simon_keys_lit,
                                  SIMON_TUNE, sizeof SIMON_TUNE, 260, BW_PLUCK, simon_where, 0, simon_playing, "COPY THE TUNE", 0};
