/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Simon: the four tones of the old memory game (an A major chord: E, A, C sharp, E), on four lanes of white keys as
 * in Tiles (F-B, C-F, G-C, D-G). It plays a sequence, lighting each lane; you play it back; it adds one and plays it
 * again, a little faster each time. One wrong and it is over. Scored on the longest sequence played back.
 *
 * Standing: K1 the speed; PLAY starts. Part of the unity build. */
#include "game.h"

#define SM_MAX 64
#define SM_WAIT 4000u                    /* ms to the next press before it gives up on you */
static const uint8_t SM_NOTE[4] = {52, 57, 61, 64};
static const uint16_t SM_COL[4] = {RGB(60, 170, 90), RGB(220, 60, 60), RGB(240, 190, 40), RGB(50, 110, 220)};

enum { SS_STAND, SS_SHOW, SS_YOURS, SS_OVER };
static struct {
    int state, len, pos, lit, new_best, wrong;
    uint8_t seq[SM_MAX];
    uint32_t next_ms, lit_until, yours_ms;
} sm;

static int32_t sm_step(void)             /* ms a tone, at the speed chosen, faster as the sequence grows */
{
    static const int16_t BASE[3] = {620, 470, 340};
    return clamp(BASE[clamp(sav.simon.level, 1, 3) - 1] - sm.len * 10, 180, 700);
}

static void sm_light(int lane, uint32_t now, int32_t ms)
{
    sm.lit = lane;
    sm.lit_until = now + (uint32_t)ms;
    snd_lock();
    bw_note(SM_NOTE[lane], 110, BW_SAW);
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
    app_say("WATCH", "");
}

static void simon_over(uint32_t now)
{
    char t[8];
    (void)now;
    sm.state = SS_OVER;
    sm.new_best = sm.len - 1 > sav.simon.best;
    if (sm.new_best) {
        sav.simon.best = (uint16_t)(sm.len - 1);
        save_dirty();
    }
    snd_lock();
    bw_note(41, 120, BW_CLUNK);
    snd_unlock();
    int_text(sm.len - 1, t);
    app_say(sm.new_best ? "GAME OVER. NEW BEST, SCORE" : "GAME OVER, SCORE", t);
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
        simon_over(ms);
        return;
    }
    sm.yours_ms = ms;
    if (++sm.pos == sm.len) {            /* all of it: one more */
        char t[8];
        if (sm.len < SM_MAX)
            sm.seq[sm.len++] = (uint8_t)rnd(4);
        sm_show(ms);
        int_text(sm.len - 1, t);
        app_say("RIGHT", t);
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
    sm.state = SS_STAND;
    save_dirty();
}

static void simon_enter(void) { sm.state = SS_STAND; }
static void simon_leave(void) { sm.state = SS_STAND; }

static void simon_draw(uint32_t now)
{
    int32_t y0, i;
    char s[16];
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, G_PAPER);
        cv_rect(0, 0, 240, 18, G_INK);
        cv_text(6, 3, &FONT_XS, "SIMON", G_PAPER);
        for (i = 0; i < 4; i++) {        /* four pads, over the lanes of keys they belong to */
            int lit = sm.lit == i && now < sm.lit_until;
            round_rect(i * 60 + 4, 70, 52, 110, 10, lit ? mix(SM_COL[i], C_WHITE, 120) : mix(SM_COL[i], G_INK, 80));
            if (sm.state == SS_OVER && i == sm.seq[sm.pos])
                round_rect(i * 60 + 20, 116, 20, 20, 10, G_PAPER);   /* the one it was */
        }
        if (sm.state == SS_STAND) {
            text_mid(120, 26, &FONT_B, "WATCH, THEN PLAY IT BACK", G_INK);
            text_cat((s[0] = 0, s), "K1 SPEED: ");
            text_cat(s, SPEED_NAME[clamp(sav.simon.level, 1, 3) - 1]);
            text_mid(120, 46, &FONT_XS, s, G_PLANK2);
        } else {
            text_mid(120, 26, &FONT_B, sm.state == SS_SHOW ? "WATCH" : sm.state == SS_YOURS ? "YOUR TURN" :
                                       sm.new_best ? "NEW BEST!" : "GAME OVER", G_INK);
            int_text(sm.len - 1, s);
            text_mid(120, 44, &FONT_B, s, G_PLANK2);
        }
        if (sav.simon.best && sm.state != SS_SHOW && sm.state != SS_YOURS) {
            text_cat((s[0] = 0, s), "BEST ");
            int_text(sav.simon.best, s + 5);
            text_mid(120, 186, &FONT_XS, s, G_PLANK2);
        }
        keyboard(198, 20, 0);
        for (i = 0; i < 4; i++)          /* each lane's colour under its keys */
            cv_rect(i * 60 + 2, 196, 56, 2, SM_COL[i]);
        cv_rect(0, 220, 240, 20, G_LILAC);
        cv_text(232 - text_w(&FONT_XS, sm.state == SS_SHOW || sm.state == SS_YOURS ? "PLAY: STOP" : "PLAY: START"), 224,
                &FONT_XS, sm.state == SS_SHOW || sm.state == SS_YOURS ? "PLAY: STOP" : "PLAY: START", G_INK);
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
    if (sm.state == SS_YOURS && now - sm.yours_ms > SM_WAIT)
        simon_over(now);
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
        text_cat(t, "SIMON. KNOB 1, SPEED ");
        text_cat(t, SPEED_NAME[clamp(sav.simon.level, 1, 3) - 1]);
        text_cat(t, ". FOUR TONES ON FOUR LANES OF WHITE KEYS. LISTEN TO THE SEQUENCE, THEN PLAY IT BACK. ");
    } else {
        text_cat(t, sm.state == SS_SHOW ? "SIMON. LISTEN. SCORE " : sm.state == SS_YOURS ? "SIMON. YOUR TURN. SCORE " :
                                                                             "SIMON. GAME OVER. SCORE ");
        sp_num(t, sm.len - 1);
        text_cat(t, ". ");
    }
    if (sav.simon.best) {
        text_cat(t, "BEST ");
        sp_num(t, sav.simon.best);
        text_cat(t, ". ");
    }
    text_cat(t, sm.state == SS_SHOW || sm.state == SS_YOURS ? "PLAY STOPS." : "PLAY STARTS.");
}

static int simon_playing(void) { return sm.state == SS_SHOW || sm.state == SS_YOURS; }

static const uint8_t SIMON_TUNE[] = {52, 57, 61, 64, 0, 64, 61, 57, 0, 0};

static const game_t GAME_SIMON = {"SIMON", RGB(60, 170, 90), simon_enter, simon_leave, simon_frame, simon_key,
                                  simon_button, simon_enc, 0, simon_attract, simon_best, simon_keys_lit,
                                  SIMON_TUNE, sizeof SIMON_TUNE, 260, BW_PLUCK, simon_where, 0, simon_playing};
