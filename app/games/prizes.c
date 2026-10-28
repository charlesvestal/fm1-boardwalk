/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The prize counter, at the start of the row: the tickets the games pay out (tickets_win) traded for prizes. Sixteen
 * on four shelves, cheapest first, one on each white key, low to high (the shelves read left to right, top to
 * bottom); K1 or SELECT walks along them too. PLAY trades (the dearer ones ask again first). A prize is yours for
 * good: it sits on the boardwalk's rail. Two change the boardwalk itself, NIGHT SKY and FIREWORKS: PLAY on them
 * turns them on and off. Its state is sav.prize. Part of the unity build. */
#include "game.h"

#define PZ_N 16
#define PZ_NIGHT 13
#define PZ_FIREWORKS 14
#define PZ_ASK 200                       /* tickets from which PLAY asks again */
#define PZ_CAB RGB(214, 160, 52)
#define PZ_WALL RGB(150, 98, 70)
#define PZ_SHELF RGB(110, 70, 52)
#define PZ_ROW 42                        /* a shelf's height */
#define PZ_TOP 20

static const struct {
    const char *name;
    uint16_t price;
} PZ[PZ_N] = {
    {"STICKER", 10},      {"SPIDER RING", 15}, {"RUBBER DUCK", 25}, {"WHISTLE", 35},
    {"BOUNCY BALL", 50},  {"PINWHEEL", 75},    {"KAZOO", 100},      {"YO-YO", 150},
    {"SNOW GLOBE", 250},  {"PLUSH GULL", 400}, {"PLUSH MOLE", 500}, {"TOY TRAIN", 750},
    {"LAVA LAMP", 1200},  {"NIGHT SKY", 1500}, {"FIREWORKS", 2500}, {"GIANT TEDDY", 5000},
};

static struct {
    int at;                              /* the prize picked */
    int ask;                             /* PLAY asked again for this one, -1 none */
    uint32_t ask_ms;
    int got;                             /* the one just traded for, -1 none */
    uint32_t got_ms;
    int fan;                             /* its fanfare: notes still to come */
    uint32_t fan_ms;
} pz = {0, -1, 0, -1, 0, 0, 0};

static const uint8_t PZ_WK[16] = PLAT_WHITE_KEYS;

static int pz_owned(int i) { return (int)(sav.prize.owned >> i & 1u); }
static int pz_on(int i) { return i == PZ_NIGHT ? sav.prize.night : i == PZ_FIREWORKS ? sav.prize.fireworks : 1; }

/* ------------------------------------------------------------ the prizes --- */
/* each drawn round (x, y), s its size in quarters (4: the shelf's, about 20 px across) */
static void prize_icon(int i, int32_t x, int32_t y, int32_t s)
{
#define S(v) ((v) * s / 4)
    int32_t k;
    switch (i) {
    case 0:                              /* a sticker: a smiling sun */
        fill_circle(x, y, S(9), G_SUN);
        ring_circle(x, y, S(9), G_ORANGE);
        cv_rect(x - S(3), y - S(3), S(2) + 1, S(2) + 1, G_INK);
        cv_rect(x + S(2), y - S(3), S(2) + 1, S(2) + 1, G_INK);
        cv_rect(x - S(4), y + S(3), S(8) + 1, S(1) + 1, G_INK);
        break;
    case 1:                              /* a spider ring */
        ring_circle(x, y + S(4), S(6), G_SUN);
        ring_circle(x, y + S(4), S(6) - 1, G_ORANGE);
        for (k = -1; k <= 1; k += 2) {
            cv_line(x, y - S(4), x + k * S(7), y - S(8), G_INK);
            cv_line(x, y - S(4), x + k * S(8), y - S(3), G_INK);
            cv_line(x, y - S(4), x + k * S(7), y + S(1), G_INK);
        }
        fill_circle(x, y - S(4), S(4), G_INK);
        break;
    case 2:                              /* a rubber duck */
        fill_circle(x - S(1), y + S(3), S(8), RGB(250, 210, 60));
        fill_circle(x + S(4), y - S(5), S(5), RGB(250, 210, 60));
        cv_rect(x + S(8), y - S(5), S(4) + 1, S(2) + 1, G_ORANGE);
        cv_rect(x + S(5), y - S(7), S(2) + 1, S(2) + 1, G_INK);
        break;
    case 3:                              /* a whistle on its cord */
        cv_line(x - S(9), y - S(9), x - S(4), y - S(2), G_RED);
        round_rect(x - S(7), y - S(3), S(13), S(9), S(4), RGB(205, 210, 220));
        cv_rect(x + S(4), y - S(3), S(6), S(4), RGB(205, 210, 220));
        fill_circle(x - S(1), y - S(1), S(2), G_SCREEN);
        break;
    case 4:                              /* a bouncy ball */
        fill_circle(x, y, S(8), G_RED);
        cv_rect(x - S(8), y - S(1), S(16) + 1, S(2) + 1, G_SUN);
        fill_circle(x - S(3), y - S(4), S(2), C_WHITE);
        break;
    case 5:                              /* a pinwheel */
        cv_line(x, y, x, y + S(11), G_PLANK2);
        fill_circle(x - S(4), y - S(4), S(4), G_RED);
        fill_circle(x + S(4), y - S(4), S(4), G_TEAL);
        fill_circle(x + S(4), y + S(4), S(4), G_SUN);
        fill_circle(x - S(4), y + S(4), S(4), RGB(150, 110, 200));
        fill_circle(x, y, S(1) + 1, C_WHITE);
        break;
    case 6:                              /* a kazoo */
        round_rect(x - S(10), y - S(3), S(20), S(7), S(3), G_RED);
        fill_circle(x, y - S(3), S(3), G_RED2);
        cv_rect(x + S(7), y - S(2), S(2), S(5), G_RED2);
        break;
    case 7:                              /* a yo-yo on its string */
        cv_line(x, y - S(10), x, y - S(2), G_INK);
        fill_circle(x, y + S(3), S(7), G_TEAL);
        fill_circle(x, y + S(3), S(3), RGB(150, 210, 225));
        break;
    case 8:                              /* a snow globe */
        fill_circle(x, y - S(2), S(8), RGB(170, 210, 240));
        fill_circle(x, y + S(2), S(3), C_WHITE);
        cv_rect(x - S(4), y - S(6), 1, 1, C_WHITE);
        cv_rect(x + S(3), y - S(5), 1, 1, C_WHITE);
        cv_rect(x - S(1), y - S(8), 1, 1, C_WHITE);
        round_rect(x - S(8), y + S(5), S(16), S(5), S(2), PZ_SHELF);
        break;
    case 9:                              /* a plush gull */
        fill_circle(x, y + S(2), S(8), C_WHITE);
        fill_circle(x - S(3), y + S(3), S(4), RGB(170, 170, 180));
        fill_circle(x + S(4), y - S(5), S(4), C_WHITE);
        cv_rect(x + S(7), y - S(5), S(4) + 1, S(2) + 1, G_ORANGE);
        cv_rect(x + S(4), y - S(7), S(1) + 1, S(1) + 1, G_INK);
        break;
    case 10:                             /* a plush mole */
        fill_circle(x, y + S(1), S(9), RGB(120, 84, 60));
        fill_circle(x + S(6), y + S(1), S(3), RGB(240, 150, 160));
        cv_rect(x, y - S(4), S(1) + 1, S(1) + 1, G_INK);
        cv_rect(x + S(4), y - S(4), S(1) + 1, S(1) + 1, G_INK);
        fill_circle(x - S(4), y + S(8), S(2), RGB(240, 150, 160));
        break;
    case 11:                             /* a toy train */
        cv_rect(x - S(10), y - S(2), S(14), S(8), G_RED);
        cv_rect(x + S(3), y - S(7), S(7), S(13), G_RED2);
        cv_rect(x - S(7), y - S(8), S(3), S(6), G_INK);
        fill_circle(x - S(6), y + S(7), S(3), G_INK);
        fill_circle(x + S(6), y + S(7), S(3), G_INK);
        break;
    case 12:                             /* a lava lamp */
        cv_rect(x - S(3), y - S(11), S(6), S(3), RGB(205, 210, 220));
        for (k = 0; k < S(14); k++) {
            int32_t w = S(4) + k * S(4) / (S(14) + 1);
            cv_rect(x - w, y - S(8) + k, 2 * w, 1, RGB(130, 70, 160));
        }
        fill_circle(x - S(1), y - S(3), S(2), G_ORANGE);
        fill_circle(x + S(2), y + S(2), S(3), G_ORANGE);
        cv_rect(x - S(8), y + S(6), S(16), S(4), RGB(205, 210, 220));
        break;
    case PZ_NIGHT:                       /* the night sky: a moon and stars */
        fill_circle(x, y, S(10), RGB(48, 46, 96));
        fill_circle(x - S(2), y, S(6), G_BULB);
        fill_circle(x + S(1), y - S(2), S(5), RGB(48, 46, 96));
        cv_rect(x + S(5), y + S(4), 1, 1, C_WHITE);
        cv_rect(x + S(4), y - S(6), 1, 1, C_WHITE);
        cv_rect(x - S(5), y + S(6), 1, 1, C_WHITE);
        break;
    case PZ_FIREWORKS:                   /* a burst */
        for (k = 0; k < 8; k++) {
            static const int8_t DX[8] = {10, 7, 0, -7, -10, -7, 0, 7}, DY[8] = {0, 7, 10, 7, 0, -7, -10, -7};
            uint16_t c = k & 1 ? G_SUN : k & 2 ? G_RED : G_TEAL;
            cv_line(x + S(DX[k]) / 3, y + S(DY[k]) / 3, x + S(DX[k]), y + S(DY[k]), c);
            fill_circle(x + S(DX[k]), y + S(DY[k]), S(1), c);
        }
        break;
    default:                             /* the giant teddy, with a bow */
        fill_circle(x - S(7), y - S(7), S(4), RGB(170, 120, 80));
        fill_circle(x + S(7), y - S(7), S(4), RGB(170, 120, 80));
        fill_circle(x, y, S(10), RGB(170, 120, 80));
        fill_circle(x, y + S(3), S(4), RGB(225, 190, 150));
        cv_rect(x - S(4), y - S(3), S(2) + 1, S(2) + 1, G_INK);
        cv_rect(x + S(3), y - S(3), S(2) + 1, S(2) + 1, G_INK);
        cv_rect(x - S(1), y + S(1), S(2) + 1, S(1) + 1, G_INK);
        fill_circle(x - S(3), y + S(10), S(2), G_RED);
        fill_circle(x + S(3), y + S(10), S(2), G_RED);
        break;
    }
#undef S
}

static void pz_say(int i)                /* a prize: its name, its price or that it is yours, its place */
{
    char t[16];
    if (!ann_on())
        return;
    sp_buf[0] = 0;
    text_cat(sp_buf, PZ[i].name);
    if (pz_owned(i)) {
        text_cat(sp_buf, (i == PZ_NIGHT || i == PZ_FIREWORKS) ? (pz_on(i) ? ", YOURS, ON" : ", YOURS, OFF") : ", YOURS");
    } else {
        text_cat(sp_buf, ", ");
        sp_num(sp_buf, PZ[i].price);
        text_cat(sp_buf, " TICKETS");
    }
    text_cat(sp_buf, ", ");
    t[0] = 0;
    sp_num(t, i + 1);
    text_cat(sp_buf, t);
    text_cat(sp_buf, " OF 16");
    ann_say(sp_buf, 0, ANN_NOW);
}

static void pz_pick(int i)
{
    i = clamp(i, 0, PZ_N - 1);
    if (i == pz.at)
        return;
    pz.at = i;
    pz.ask = -1;
    snd_lock();
    bw_note(53 + PZ_WK[i], 60, BW_CHIME);
    snd_unlock();
    pz_say(i);
}

static void pz_trade(uint32_t now)       /* PLAY */
{
    int i = pz.at;
    char t[48];
    if (pz_owned(i)) {
        if (i == PZ_NIGHT)
            sav.prize.night ^= 1u;
        else if (i == PZ_FIREWORKS)
            sav.prize.fireworks ^= 1u;
        else
            return;
        save_dirty();
        snd_lock();
        bw_note(pz_on(i) ? 79 : 67, 80, BW_CHIME);
        snd_unlock();
        app_say(PZ[i].name, pz_on(i) ? "ON" : "OFF");
        return;
    }
    if (sav.prize.tickets < PZ[i].price) {
        t[0] = 0;
        sp_num(t, (int32_t)(PZ[i].price - sav.prize.tickets));
        text_cat(t, " MORE TICKETS");
        snd_lock();
        bw_note(48, 70, BW_DUD);
        snd_unlock();
        app_say("NOT ENOUGH", t);
        return;
    }
    if (PZ[i].price >= PZ_ASK && (pz.ask != i || now - pz.ask_ms > 4000u)) {   /* a dear one: asked again */
        pz.ask = i;
        pz.ask_ms = now;
        t[0] = 0;
        text_cat(t, "PLAY AGAIN TO TRADE ");
        sp_num(t, PZ[i].price);
        app_say(t, "");
        return;
    }
    pz.ask = -1;
    sav.prize.tickets -= PZ[i].price;
    sav.prize.owned |= 1u << i;
    if (i == PZ_NIGHT)
        sav.prize.night = 1;
    if (i == PZ_FIREWORKS)
        sav.prize.fireworks = 1;
    save_dirty();
    pz.got = i;
    pz.got_ms = now;
    pz.fan = 5;
    pz.fan_ms = now;
    t[0] = 0;
    sp_num(t, (int32_t)sav.prize.tickets);
    text_cat(t, " TICKETS LEFT");
    app_say(PZ[i].name, i == PZ_NIGHT || i == PZ_FIREWORKS ? "YOURS, ON THE BOARDWALK NOW" : "YOURS");
    if (ann_on())
        ann_say(t, 0, 0);
}

static void prizes_key(int k, int down, uint32_t ms)
{
    int i;
    (void)ms;
    if (!down)
        return;
    for (i = 0; i < PZ_N; i++)
        if (PZ_WK[i] == k) {
            if (i == pz.at) {            /* again: heard again */
                snd_lock();
                bw_note(53 + k, 60, BW_CHIME);
                snd_unlock();
                pz_say(i);
            }
            pz_pick(i);
            return;
        }
}

static void prizes_button(int b, int down)
{
    if (down && b == B_PLAY)
        pz_trade(plat_ms());
}

static void prizes_enc(int role, int steps)
{
    if (role == EN_K1 || role == EN_SELECT)
        pz_pick(pz.at + steps);
}

static void prizes_enter(void)
{
    pz.ask = pz.got = -1;
    pz.fan = 0;
}
static void prizes_leave(void) { pz.fan = 0; }

/* ------------------------------------------------------------- drawing --- */
static void prizes_draw(uint32_t now)
{
    int32_t y0, i;
    char s[32];
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, PZ_WALL);
        cv_rect(0, 0, 240, 18, PZ_CAB);
        cv_text(6, 3, &FONT_XS, "PRIZES", G_INK);
        s[0] = 0;
        sp_num(s, (int32_t)sav.prize.tickets);
        ticket_shape(232 - text_w(&FONT_XS, s) - 20, 2, text_w(&FONT_XS, s) + 24, 15, G_ORANGE);
        cv_text(232 - text_w(&FONT_XS, s) - 4, 2, &FONT_XS, s, G_PAPER);
        for (i = 0; i < PZ_N; i++) {     /* the shelves */
            int32_t cx = 30 + (i & 3) * 60, ry = PZ_TOP + (i >> 2) * PZ_ROW, iy = ry + 15;
            int own = pz_owned(i), can = sav.prize.tickets >= PZ[i].price;
            if ((i & 3) == 0)
                cv_rect(0, ry + PZ_ROW - 4, 240, 3, PZ_SHELF);
            if (i == pz.at)
                round_rect(cx - 28, ry + 1, 56, PZ_ROW - 5, 6, G_PAPER2);
            if (pz.got == i && now - pz.got_ms < 900u)   /* just traded for: a hop */
                iy -= (int32_t)(8 - (int32_t)((now - pz.got_ms) / 56u) % 8);
            prize_icon(i, cx, iy, 4);
            if (own) {
                s[0] = 0;
                text_cat(s, (i == PZ_NIGHT || i == PZ_FIREWORKS) ? (pz_on(i) ? "ON" : "OFF") : "YOURS");
                text_mid(cx, ry + 26, &FONT_XS, s, i == pz.at ? G_TEAL : RGB(150, 225, 235));
            } else {
                s[0] = 0;
                sp_num(s, PZ[i].price);
                text_mid(cx, ry + 26, &FONT_XS, s, i == pz.at ? (can ? G_INK : G_RED2) : (can ? G_PAPER : G_MUTE));
            }
        }
        if (pz.got >= 0 && now - pz.got_ms < 1400u)   /* confetti over the shelf */
            for (i = 0; i < 14; i++) {
                int32_t t = (int32_t)(now - pz.got_ms), cx = 30 + (pz.got & 3) * 60, cy = PZ_TOP + (pz.got >> 2) * PZ_ROW;
                int32_t dx = (int32_t)((i * 37) % 60) - 30, dy = -20 + t * (20 + (i * 13) % 25) / 600;
                static const uint16_t C[4] = {G_RED, G_SUN, G_TEAL, C_WHITE};
                cv_rect(cx + dx * t / 700, cy + dy, 3, 3, C[i & 3]);
            }
        {                                /* the one picked: its name, its price */
            int own = pz_owned(pz.at);
            round_rect(6, 190, 228, 28, 6, G_PAPER);
            cv_text(14, 195, &FONT_B, PZ[pz.at].name, G_INK);
            s[0] = 0;
            if (own) {
                text_cat(s, (pz.at == PZ_NIGHT || pz.at == PZ_FIREWORKS) ? (pz_on(pz.at) ? "YOURS, ON" : "YOURS, OFF")
                                                                          : "YOURS");
            } else if (sav.prize.tickets >= PZ[pz.at].price) {
                sp_num(s, PZ[pz.at].price);
                text_cat(s, " TICKETS");
            } else {
                text_cat(s, "NEED ");
                sp_num(s, (int32_t)(PZ[pz.at].price - sav.prize.tickets));
                text_cat(s, " MORE");
            }
            cv_text(226 - text_w(&FONT_XS, s), 197, &FONT_XS, s, own ? G_TEAL : sav.prize.tickets >= PZ[pz.at].price
                                                                                   ? G_INK : G_RED2);
        }
        cv_rect(0, 220, 240, 20, G_LILAC);
        cv_text(FOOT_L, FOOT_Y, &FONT_XS, "WHITE KEYS PICK", G_PLANK2);
        {
            const char *f = "";
            if (pz_owned(pz.at))
                f = pz.at == PZ_NIGHT || pz.at == PZ_FIREWORKS ? (pz_on(pz.at) ? "PLAY: OFF" : "PLAY: ON") : "";
            else if (sav.prize.tickets >= PZ[pz.at].price)
                f = pz.ask == pz.at && now - pz.ask_ms < 4000u ? "PLAY AGAIN: SURE" : "PLAY: TRADE";
            cv_text(FOOT_R - text_w(&FONT_XS, f), FOOT_Y, &FONT_XS, f, pz.ask == pz.at ? G_RED2 : G_INK);
        }
        band_send(y0);
    }
}

static void prizes_frame(uint32_t now)
{
    if (pz.fan && now - pz.fan_ms >= 90u) {   /* the fanfare: up an arpeggio, the last one long */
        static const uint8_t F[5] = {72, 76, 79, 84, 88};
        snd_lock();
        bw_note(F[5 - pz.fan], pz.fan == 1 ? 110 : 90, BW_CHIME);
        if (pz.fan == 1)
            bw_drum(BW_CLAP, 60);
        snd_unlock();
        pz.fan--;
        pz.fan_ms = now;
    }
    prizes_draw(now);
}

static void prizes_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    int i, first = (int)(ms / 2000u % 4u) * 4;   /* a shelf at a time */
    for (i = 0; i < 4; i++)
        prize_icon(first + i, x + w * (2 * i + 1) / 8, y + h / 2 - 4, 2);
    cv_rect(x + 2, y + h / 2 + 6, w - 4, 2, PZ_SHELF);
}

static void prizes_best(char *t)
{
    t[0] = 0;
    if (!sav.prize.won)
        return;
    sp_num(t, (int32_t)sav.prize.tickets);
    text_cat(t, " TICKETS");
}

static uint32_t prizes_keys_lit(uint32_t *mid)   /* the one picked bright; the ones the tickets would buy, mid */
{
    int i;
    *mid = 0;
    for (i = 0; i < PZ_N; i++)
        if (!pz_owned(i) && sav.prize.tickets >= PZ[i].price)
            *mid |= 1u << PZ_WK[i];
    return 1u << PZ_WK[pz.at];
}

static void prizes_where(char *t)
{
    t[0] = 0;
    text_cat(t, "PRIZES. ");
    sp_num(t, (int32_t)sav.prize.tickets);
    text_cat(t, " TICKETS. ");
    text_cat(t, PZ[pz.at].name);
    if (pz_owned(pz.at)) {
        text_cat(t, ", YOURS");
        if (pz.at == PZ_NIGHT || pz.at == PZ_FIREWORKS)
            text_cat(t, pz_on(pz.at) ? ", ON. PLAY TURNS IT OFF." : ", OFF. PLAY TURNS IT ON.");
        else
            text_cat(t, ".");
    } else {
        text_cat(t, ", ");
        sp_num(t, PZ[pz.at].price);
        text_cat(t, " TICKETS. ");
        text_cat(t, sav.prize.tickets >= PZ[pz.at].price ? "PLAY TRADES." : "NOT ENOUGH YET.");
    }
    text_cat(t, " THE WHITE KEYS PICK A PRIZE, LOW TO HIGH. WIN TICKETS AT THE GAMES.");
}

#ifdef PLAT_HOST
static int prizes_host_cmd(const char *cmd, const char *a)
{
    (void)cmd;
    (void)a;
    return 0;
}
static int prizes_expect(const char *what, int *got)
{
    if (!strcmp(what, "prize_at"))
        *got = pz.at;
    else if (!strcmp(what, "owned"))     /* the prizes owned, a bit each */
        *got = (int)sav.prize.owned;
    else if (!strcmp(what, "night"))
        *got = sav.prize.night;
    else if (!strcmp(what, "fireworks"))
        *got = sav.prize.fireworks;
    else
        return 0;
    return 1;
}
static void prizes_speak_dump(void)
{
    int i, keep = pz.at;
    uint32_t t = sav.prize.tickets, o = sav.prize.owned;
    for (i = 0; i < PZ_N; i++) {         /* each prize: dear, then cheap enough, then yours */
        pz.at = i;
        sav.prize.tickets = 5;
        sav.prize.owned = 0;
        prizes_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        sav.prize.tickets = 6000;
        prizes_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        sav.prize.owned = 1u << i;
        prizes_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        sp_buf[0] = 0;
        text_cat(sp_buf, PZ[i].name);
        text_cat(sp_buf, ", YOURS, 3 OF 16");
        sp_out("MSG", sp_buf);
        sp_join(PZ[i].name, "120 TICKETS, 3 OF 16");
        sp_out("MSG", sp_buf);
        sp_join(PZ[i].name, "ON");
        sp_out("MSG", sp_buf);
        sp_join(PZ[i].name, "YOURS, ON THE BOARDWALK NOW");
        sp_out("MSG", sp_buf);
    }
    sp_join("NOT ENOUGH", "37 MORE TICKETS");
    sp_out("MSG", sp_buf);
    sp_join("PLAY AGAIN TO TRADE 750", "");
    sp_out("MSG", sp_buf);
    sp_join("85 TICKETS LEFT", "");
    sp_out("MSG", sp_buf);
    sp_join("12 TICKETS. 340 IN ALL.", "");
    sp_out("MSG", sp_buf);
    sp_join("1 TICKET. 341 IN ALL.", "");
    sp_out("MSG", sp_buf);
    pz.at = keep;
    sav.prize.tickets = t;
    sav.prize.owned = o;
}
#endif

static const uint8_t PRIZES_TUNE[] = {72, 76, 79, 76, 72, 0, 79, 0, 81, 79, 77, 76, 74, 0, 72, 0};

static const game_t GAME_PRIZES = {"PRIZES", PZ_CAB, prizes_enter, prizes_leave, prizes_frame, prizes_key,
                                   prizes_button, prizes_enc, 0, prizes_attract, prizes_best, prizes_keys_lit,
                                   PRIZES_TUNE, sizeof PRIZES_TUNE, 200, BW_CHIME, prizes_where, 0, 0,
                                   "TRADE YOUR TICKETS", 0};
