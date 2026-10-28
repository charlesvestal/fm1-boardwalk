/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Striker: the fairground High Striker, made a game of timing. A tall board with marks 1 to 10 and a bell on top; a
 * meter (the puck's ghost, with a pointer) swings up and down the board by itself. Press a key to swing the mallet:
 * the puck flies to where the meter was at that moment. The machine asks for a mark ("RING IT TO 7", the mark lit),
 * so strike as the meter passes it. Ten strikes a round, each scored by how close the puck came: on the mark 10
 * points, one off 6, two off 3. Struck at the very top, the bell rings; asked for the bell and rung, 5 more.
 *
 * Levels (K1): 1 a slow meter, the target a band of two marks; 2 a single mark, a faster meter; 3 the meter changes
 * speed every swing and eases unevenly (slow away from the foot, fast back), and a new mark every strike. On levels
 * 1 and 2 each target is asked twice running (the second go to put the first right). Standing, the meter runs and
 * a key is a practice strike. Any key strikes. PLAY starts and stops. Best: points in a round
 * (sav.more[SV_STRIKER]). Part of the unity build. */
#include "game.h"

#define SK_LEVELS 3
#define SK_STRIKES 10
#define SK_BASE 194                      /* the board: mark 0's y */
#define SK_STEP 14                       /* px a mark */
#define SK_X 40                          /* the board's middle */
#define SK_WIRE 49                       /* the puck's wire */
#define SK_G 1000.0f                     /* the puck's gravity, px/s/s */
#define SK_TOP 10.4f                     /* the meter's top, in marks: past 9.5 rings the bell */
#define SK_COL RGB(200, 70, 50)

enum { SK_STAND, SK_READY, SK_FLY, SK_WAIT, SK_OVER };

static struct {
    int state, level, n, score, new_best, exact, rung;   /* n: strikes taken; exact, rung: this round's */
    uint8_t tg[SK_STRIKES];              /* each strike's target (level 1: the band's lower mark) */
    int practice;                        /* the flight is a standing practice, not scored */
    /* the meter: a phase (a swing up and down is 1) moving at rate cycles a ms, kept from ph_ms */
    float ph, rate;
    uint32_t ph_ms;
    float at;                            /* where the meter is, in marks (drawn) */
    /* the flight */
    float v0, apex, h;                   /* px/s up, px at the top, px now */
    int mark, scored, landed;            /* the mark it reached */
    int pts, off;                        /* the last result */
    uint32_t fly_ms, scored_ms, next_ms, bell_ms, over_ms;
} sk;

static const char *const SK_LEVEL_NAME[SK_LEVELS] = {"TWO MARKS", "ONE MARK", "UNEVEN"};

static int sk_lo(void) { return sk.tg[sk.n < SK_STRIKES ? sk.n : SK_STRIKES - 1]; }
static int sk_hi(void) { return sk_lo() + (sk.level == 1); }
static int sk_level(void) { return clamp(sav.more[SV_STRIKER].level, 1, SK_LEVELS); }
static int sk_standing(void) { return sk.state == SK_STAND || (sk.state == SK_FLY && sk.practice); }

static void sk_target_text(char *t, int lo)   /* "7", "6 TO 7", "THE BELL" */
{
    t[0] = 0;
    if (lo >= 10) {
        text_cat(t, "THE BELL");
        return;
    }
    sp_num(t, lo);
    if (sk.level == 1) {
        text_cat(t, " TO ");
        sp_num(t, lo + 1);
    }
}

static void sk_ask_text(char *t, int lo) /* "RING IT TO 7", "RING THE BELL" */
{
    t[0] = 0;
    text_cat(t, lo >= 10 ? "RING " : "RING IT TO ");
    sk_target_text(t + text_len(t), lo);
}

/* ------------------------------------------------------------------ meter --- */
static float sk_swing_rate(int level)    /* cycles a ms: 1 a swing of 2.8 s, 1.8 s; level 3 1.1 to 2.3 s, a new one
                                          * each half swing */
{
    float s = level == 1 ? 2.8f : level == 2 ? 1.8f : 1.1f + (float)rnd(13) * 0.1f;
    return 1.0f / (s * 1000.0f);
}

static float sk_meter_at(float ph, int level)   /* marks, 0 .. SK_TOP */
{
    float f = ph - (float)(int)ph, tri = f < 0.5f ? 2.0f * f : 2.0f - 2.0f * f;
    if (level == 3 && f < 0.5f)          /* uneven: eased away from the foot and into the top, straight back down */
        tri = tri * tri * (3.0f - 2.0f * tri);
    return tri * SK_TOP;
}

static float sk_meter_ms(uint32_t ms)    /* the meter at a moment (a key's, exact to the ms) */
{
    return sk_meter_at(sk.ph + sk.rate * (float)(int32_t)(ms - sk.ph_ms), sk.level);
}

static void sk_meter_step(uint32_t now)
{
    float was = sk.ph;
    sk.ph += sk.rate * (float)(int32_t)(now - sk.ph_ms);
    sk.ph_ms = now;
    if (sk.ph > 1000.0f)
        sk.ph -= 1000.0f;
    if (sk.level == 3 && (int)(was * 2.0f) != (int)(sk.ph * 2.0f))   /* a new speed each half swing */
        sk.rate = sk_swing_rate(3);
    sk.at = sk_meter_at(sk.ph, sk.level);
}

static void sk_meter_reset(uint32_t now)
{
    sk.ph = 0.0f;
    sk.ph_ms = now;
    sk.rate = sk_swing_rate(sk.level);
    sk.at = 0.0f;
}

/* ------------------------------------------------------------------- game --- */
static void sk_plan(void)                /* the round's targets: the bell asked once (twice on level 3) */
{
    int i, prev = 0, top = sk.level == 1 ? 9 : 10;
    if (sk.level < 3) {                  /* five targets, each asked twice */
        int bell = (int)rnd(5);
        for (i = 0; i < 5; i++) {
            int t;
            if (i == bell)
                t = top;
            else
                do
                    t = 1 + (int)rnd((uint32_t)(top - 1));
                while (t == prev);
            sk.tg[2 * i] = sk.tg[2 * i + 1] = (uint8_t)t;
            prev = t;
        }
    } else {                             /* ten, a new one each strike */
        int b1 = (int)rnd(5), b2 = 5 + (int)rnd(5);
        for (i = 0; i < SK_STRIKES; i++) {
            int t;
            if (i == b1 || i == b2)
                t = 10;
            else
                do
                    t = 1 + (int)rnd(9);
                while (t == prev);
            sk.tg[i] = (uint8_t)t;
            prev = t;
        }
    }
}

static void striker_go(void)
{
    char t[24];
    uint32_t now = plat_ms();
    rnd_state ^= now * 2654435761u;
    sk.level = sk_level();
    sk_plan();
    sk.n = sk.score = sk.exact = sk.rung = 0;
    sk.new_best = 0;
    sk.scored_ms = 0;
    sk.h = 0.0f;
    sk.landed = 1;
    sk.state = SK_READY;
    sk_meter_reset(now);
    sk_ask_text(t, sk_lo());
    app_say("TEN STRIKES", t);
}

static void striker_end(uint32_t now)
{
    sk.state = SK_OVER;
    sk.over_ms = now;
    sk.new_best = sk.score > sav.more[SV_STRIKER].best;
    if (sk.new_best) {
        sav.more[SV_STRIKER].best = (uint16_t)sk.score;
        save_dirty();
    }
    tickets_win(clamp(sk.score * (3 + sk.level) / 12, 2, 100));   /* tickets: a third of the points, x1.25 on level
                                                                   * 2, x1.5 on 3; 2 at least */
    snd_lock();                          /* the fanfare: a little rising call */
    bw_note(72, 90, BW_CHIME);
    bw_note(79, 85, BW_CHIME);
    if (sk.new_best)
        bw_note(84, 95, BW_CHIME);
    snd_unlock();
}

static void sk_launch(float at, uint32_t now)   /* the mallet comes down: the puck flies to the meter's height */
{
    float hm = at > 10.0f ? 10.0f : at;
    sk.mark = clamp((int)(at + 0.5f), 0, 10);
    sk.apex = hm * (float)SK_STEP;
    sk.v0 = fm_sqrtf(2.0f * SK_G * (sk.apex > 2.0f ? sk.apex : 2.0f));
    sk.fly_ms = now;
    sk.scored = sk.landed = 0;
    sk.h = 0.0f;
    sk.state = SK_FLY;
    snd_lock();                          /* the thwack, harder the higher it goes */
    bw_drum(BW_KICK, 80 + sk.mark * 4);
    bw_drum(BW_CLAP, 60 + sk.mark * 5);
    snd_unlock();
}

static void striker_key(int k, int down, uint32_t ms)
{
    (void)k;
    if (!down)
        return;
    if (sk.state == SK_READY) {
        sk.practice = 0;
        sk_launch(sk_meter_ms(ms), ms);
    } else if (sk.state == SK_STAND && sk.landed) {   /* standing: a practice strike */
        sk.practice = 1;
        sk_launch(sk_meter_ms(ms), ms);
    }
}

static void sk_score(uint32_t now)       /* the puck at the top: how close, said and heard */
{
    char a[40], b[48];
    int lo = sk_lo(), hi = sk_hi(), bonus = 0;
    sk.scored = 1;
    sk.scored_ms = now;
    snd_lock();
    if (sk.mark >= 10) {                 /* the bell */
        sk.bell_ms = now;
        bw_bell(0, 84, 127);
        bw_bell(1, 91, 112);
    }
    snd_unlock();
    a[0] = 0;
    text_cat(a, sk.mark >= 10 ? "THE BELL" : "UP TO ");
    if (sk.mark < 10)
        sp_num(a, sk.mark);
    if (sk.practice) {
        app_say(a, "");
        return;
    }
    sk.off = sk.mark < lo ? lo - sk.mark : sk.mark > hi ? sk.mark - hi : 0;
    sk.pts = sk.off == 0 ? 10 : sk.off == 1 ? 6 : sk.off == 2 ? 3 : 0;
    if (sk.off == 0 && hi == 10 && sk.mark == 10) {   /* asked for the bell, and rung */
        bonus = 5;
        sk.rung++;
    }
    sk.pts += bonus;
    sk.exact += sk.off == 0;
    sk.score += sk.pts;
    sk.n++;
    snd_lock();
    if (sk.off == 0) {                   /* on the mark: a bright chord; the bell asked: one more bell */
        bw_note(84, 95, BW_CHIME);
        bw_note(88, 85, BW_CHIME);
        bw_note(91, 80, BW_CHIME);
        if (bonus)
            bw_bell(2, 96, 120);
    } else if (sk.off == 1) {
        bw_note(79, 85, BW_CHIME);
    } else if (sk.off == 2) {
        bw_note(67, 80, BW_PLUCK);
    } else {
        bw_note(45, 100, BW_DUD);
    }
    snd_unlock();
    text_cat(a, sk.off == 0 ? (bonus ? ", RUNG" : ", ON THE MARK") : sk.off == 1 ? ", ONE OFF" :
                sk.off == 2 ? ", TWO OFF" : ", MISSED");
    b[0] = 0;
    sp_num(b, sk.pts);
    text_cat(b, " POINTS");
    if (sk.n < SK_STRIKES) {             /* and the next target */
        text_cat(b, ". NOW ");
        text_cat(b, sk.tg[sk.n] == sk.tg[sk.n - 1] && sk.level < 3 ? "AGAIN " : "");
        sk_target_text(b + text_len(b), sk.tg[sk.n]);
    } else {
        text_cat(b, ". SCORE ");
        sp_num(b, sk.score);
        if (sk.score > sav.more[SV_STRIKER].best)
            text_cat(b, ", NEW BEST");
    }
    app_say(a, b);
}

static void sk_step(uint32_t now)        /* the meter and the puck, by time */
{
    float t;
    if (sk.state != SK_OVER)
        sk_meter_step(now);
    if (sk.state == SK_WAIT) {
        if ((int32_t)(now - sk.next_ms) >= 0) {
            if (sk.n >= SK_STRIKES)
                striker_end(now);
            else
                sk.state = SK_READY;
        }
        return;
    }
    if (sk.state != SK_FLY)
        return;
    t = (float)(now - sk.fly_ms) * 0.001f;
    sk.h = sk.v0 * t - 0.5f * SK_G * t * t;
    if (!sk.scored && t >= sk.v0 / SK_G)
        sk_score(now);
    snd_lock();                          /* the whoosh: a tone that follows the puck up, quiet from the top (the result
                                          * rings out clear) */
    bw_tone(0, 4300 + (int)(sk.h * 24.0f), sk.h > 0.0f && !sk.scored ? 34 : 0);
    snd_unlock();
    if (sk.h <= 0.0f && sk.scored) {     /* down again: the clack at the foot */
        sk.h = 0.0f;
        sk.landed = 1;
        snd_lock();
        bw_tone(0, 4300, 0);
        bw_drum(BW_BLOCK_LO, 60);
        snd_unlock();
        if (sk.practice) {
            sk.state = SK_STAND;
        } else {
            sk.state = SK_WAIT;
            sk.next_ms = now + 450u;
        }
    }
}

static void striker_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (sk_standing() || sk.state == SK_OVER) {
        striker_go();
    } else {
        sk.state = SK_STAND;
        sk.h = 0.0f;
        sk.landed = 1;
        snd_lock();
        bw_tone(0, 4300, 0);
        snd_unlock();
        app_say("STOPPED", "");
    }
}

static void striker_enc(int role, int steps)
{
    char t[24];
    if (role != EN_K1 || (!sk_standing() && sk.state != SK_OVER))
        return;
    sav.more[SV_STRIKER].level = (uint8_t)clamp(sav.more[SV_STRIKER].level + steps, 1, SK_LEVELS);
    t[0] = 0;
    sp_num(t, sav.more[SV_STRIKER].level);
    text_cat(t, ", ");
    text_cat(t, SK_LEVEL_NAME[sav.more[SV_STRIKER].level - 1]);
    app_readout("LEVEL", t);
    sk.state = SK_STAND;
    sk.level = sk_level();
    sk_meter_reset(plat_ms());
    save_dirty();
}

static void striker_enter(void)
{
    sk.state = SK_STAND;
    sk.h = 0.0f;
    sk.landed = 1;
    sk.scored_ms = sk.bell_ms = sk.fly_ms = 0;
    sk.level = sk_level();
    sk_meter_reset(plat_ms());
}
static void striker_leave(void)
{
    sk.state = SK_STAND;
    snd_lock();
    bw_tone(0, 4300, 0);
    bw_bells_damp();
    snd_unlock();
}

/* ---------------------------------------------------------------- drawing --- */
static int sk_mark_y(int m) { return SK_BASE - m * SK_STEP; }   /* the line at the top of mark m's band */

static int sk_shown(void)                /* the target on the board and card: the one struck at, until the next is ready
                                          * (so a result is never read against the next target) */
{
    if ((sk.state == SK_FLY || sk.state == SK_WAIT) && sk.scored && sk.n > 0)
        return sk.tg[sk.n - 1];
    return sk_lo();
}

static void sk_board(uint32_t now)       /* the tower: posts with bulbs, the marks, the wire, the meter, the bell */
{
    int m, lo = 0, hi = -1, i;
    int flash = sk.scored_ms && now - sk.scored_ms < 1200u;
    int win = flash && sk.off == 0 && !sk.practice;
    char s[4];
    if (sk.state == SK_READY || sk.state == SK_WAIT || (sk.state == SK_FLY && !sk.practice)) {
        lo = sk_shown();
        hi = lo + (sk.level == 1);
    }
    cv_rect(SK_X - 22, 48, 4, SK_BASE - 48 + 6, G_WHEEL);   /* the posts */
    cv_rect(SK_X + 18, 48, 4, SK_BASE - 48 + 6, G_WHEEL);
    cv_rect(SK_X - 22, 46, 44, 4, G_WHEEL);
    for (m = 1; m <= 10; m++) {          /* the marks: warm to hot, the asked band teal, the reached one flashing */
        int32_t y = sk_mark_y(m);
        uint16_t c = mix(G_SUN, G_RED, (m - 1) * 26), tc = G_INK;
        if (m >= lo && m <= hi) {
            c = G_TEAL;
            tc = G_PAPER;
        }
        if (flash && m == sk.mark && (now / 90u & 1u)) {
            c = C_WHITE;
            tc = G_RED;
        }
        cv_rect(SK_X - 18, y, 36, SK_STEP - 1, c);
        cv_rect(SK_X - 18, y + SK_STEP - 1, 36, 1, G_WHEEL);
        s[0] = 0;
        sp_num(s, m);
        text_mid(SK_X - 7, y, &FONT_XS, s, tc);
    }
    cv_rect(SK_X - 18, sk_mark_y(0), 36, 6, G_PLANK2);
    cv_rect(SK_WIRE, sk_mark_y(10) - 2, 1, SK_BASE - sk_mark_y(10) + 2, G_INK);   /* the wire */
    for (i = 0; i < 13; i++) {           /* the bulbs up the posts: twinkling; a win flashes them all */
        int32_t y = 54 + i * 11;
        int on = win ? (int)(now / 80u & 1u) : (int)((i + now / 220u) % 3u != 0u);
        uint16_t c = on ? G_BULB : RGB(150, 110, 90);
        fill_circle(SK_X - 20, y, 2, c);
        fill_circle(SK_X + 20, y, 2, c);
    }
    {                                    /* the bell on top: it shakes when rung */
        float ang = 0.0f;
        uint32_t bt = now - sk.bell_ms;
        if (sk.bell_ms && bt < 1200u)
            ang = 0.35f * fm_sinf((float)bt * 0.025f) * (1.0f - (float)bt / 1200.0f);
        cv_rect(SK_X - 1, 18, 3, 6, G_WHEEL);
        draw_bell(SK_X, 22, ang, 1.2f, sk.bell_ms && bt < 600u && (now / 70u & 1u) ? G_BULB : G_SUN, G_ORANGE,
                  sk.bell_ms && bt < 200u);
    }
    if (sk.state == SK_READY || (sk.state == SK_STAND && sk.landed)) {   /* the meter: the puck's ghost and a pointer */
        float at = sk.at > 10.0f ? 10.0f : sk.at;
        int32_t gy = SK_BASE - (int32_t)(at * (float)SK_STEP) - 8, k;
        cv_rect(SK_WIRE - 7, gy, 15, 8, G_INK);
        cv_rect(SK_WIRE - 6, gy + 1, 13, 6, G_PAPER);
        cv_rect(SK_X - 18, gy + 4, SK_WIRE - 7 - (SK_X - 18), 1, G_INK);   /* the needle across the board */
        for (k = 0; k < 8; k++)          /* the pointer, by the right post */
            cv_rect(SK_X + 24 + k, gy + 4 - (8 - k), 1, 2 * (8 - k), G_INK);
    }
    {                                    /* the puck */
        int32_t py = SK_BASE - (int32_t)(sk.h > 0.0f ? sk.h : 0.0f) - 8;
        cv_rect(SK_WIRE - 7, py, 15, 8, G_RED2);
        cv_rect(SK_WIRE - 6, py + 1, 13, 2, G_RED);
    }
}

static void sk_ground(uint32_t now)      /* the plinth, the pad and its lever, the mallet */
{
    int down = sk.fly_ms && now - sk.fly_ms < 180u;
    int32_t i;
    cv_rect(0, 200, 240, 20, G_PLANK);   /* the boardwalk's planks */
    for (i = 0; i < 240; i += 30)
        cv_rect(i, 200, 1, 20, G_PLANK2);
    cv_rect(0, 200, 240, 1, G_PLANK2);
    cv_rect(8, SK_BASE + 2, 120, 6, G_PLANK2);   /* the plinth */
    cv_line(SK_X + 18, SK_BASE, 86, SK_BASE - (down ? 0 : 4), G_INK);   /* the lever */
    cv_rect(78, SK_BASE - (down ? 3 : 7), 28, 5, G_RED);   /* the pad */
    cv_rect(78, SK_BASE - (down ? 3 : 7), 28, 1, G_RED2);
    if (down) {                          /* the mallet on the pad */
        cv_rect(81, SK_BASE - 19, 22, 13, G_PLANK2);
        cv_rect(81, SK_BASE - 19, 22, 2, G_INK);
        cv_line(102, SK_BASE - 13, 140, SK_BASE - 44, G_INK);
        cv_line(102, SK_BASE - 12, 140, SK_BASE - 43, G_INK);
        cv_text(112, SK_BASE - 30, &FONT_B, "BAM!", G_RED);
    } else {                             /* lying beside it */
        cv_rect(112, SK_BASE - 8, 18, 10, G_PLANK2);
        cv_rect(112, SK_BASE - 8, 18, 2, G_INK);
        cv_rect(130, SK_BASE - 4, 34, 2, G_INK);
    }
}

static void sk_sky(int32_t y0)
{
    int32_t y;
    for (y = y0 > 18 ? y0 : 18; y < y0 + BAND_H && y < 172; y++)   /* the golden-hour sky */
        cv_rect(0, y, 240, 1, mix(G_SKY0, G_SKY2, (y - 18) * 256 / 154));
    fill_circle(196, 168, 26, G_SUN);    /* the sun, setting into the sea */
    cv_rect(0, 168, 240, 32, G_SEA);
    cv_rect(160, 172, 72, 2, G_FOAM);
    cv_rect(176, 178, 40, 2, G_FOAM);
    cv_rect(188, 184, 18, 2, G_FOAM);
}

static void sk_play_card(uint32_t now)
{
    char s[32];
    int bell;
    const int32_t x = 84, w = 148;
    round_rect(x, 26, w, 104, 8, G_PAPER);
    if (sk.state == SK_OVER) {           /* the end card */
        text_mid(x + w / 2, 32, &FONT_B, "TEN STRIKES", G_RED);
        text_cat((s[0] = 0, s), "SCORE ");
        sp_num(s, sk.score);
        text_mid(x + w / 2, 52, &FONT_M, s, G_INK);
        s[0] = 0;
        sp_num(s, sk.exact);
        text_cat(s, " ON THE MARK");
        if (sk.rung) {
            text_cat(s, ", ");
            sp_num(s, sk.rung);
            text_cat(s, sk.rung == 1 ? " BELL" : " BELLS");
        }
        text_mid(x + w / 2, 82, &FONT_XS, s, G_PLANK2);
        if (sk.new_best) {
            text_mid(x + w / 2, 102, &FONT_B, "NEW BEST!", G_RED);
        } else {
            text_cat((s[0] = 0, s), "BEST ");
            sp_num(s, sav.more[SV_STRIKER].best);
            text_mid(x + w / 2, 104, &FONT_XS, s, G_INK);
        }
        return;
    }
    {                                    /* the target */
        int lo = sk_shown();
        cv_text(x + 10, 32, &FONT_XS, lo >= 10 ? "RING THE" : "RING IT TO", G_PLANK2);
        s[0] = 0;
        if (lo >= 10) {
            text_cat(s, "BELL");
        } else {
            sp_num(s, lo);
            if (sk.level == 1) {
                text_cat(s, "-");
                sp_num(s, lo + 1);
            }
        }
        cv_text(x + 10, 46, &FONT_L, s, G_TEAL);
        bell = lo + (sk.level == 1) >= 10;
    }
    cv_text(x + w - 10 - text_w(&FONT_XS, "STRIKE"), 32, &FONT_XS, "STRIKE", G_PLANK2);
    s[0] = 0;
    sp_num(s, sk.n + (sk.state == SK_READY || (sk.state == SK_FLY && !sk.scored) ? 1 : 0));
    text_cat(s, "/10");
    cv_text(x + w - 10 - text_w(&FONT_M, s), 48, &FONT_M, s, G_INK);
    if (sk.scored_ms && now - sk.scored_ms < 1800u && !sk.practice) {   /* the result */
        const char *w2 = sk.off == 0 ? (sk.pts > 10 ? "RUNG!" : "ON THE MARK!") : sk.off == 1 ? "ONE OFF" :
                         sk.off == 2 ? "TWO OFF" : "MISSED";
        s[0] = 0;
        text_cat(s, "+");
        sp_num(s, sk.pts);
        cv_text(x + 10, 94, &FONT_M, s, sk.off == 0 ? G_RED : sk.pts ? G_ORANGE : G_MUTE);
        cv_text(x + 16 + text_w(&FONT_M, s), 100, &FONT_B, w2, sk.off == 0 ? G_RED : G_INK);
    } else if (bell) {                   /* the bell asked: its bonus */
        cv_text(x + 10, 98, &FONT_XS, "THE BELL IS WORTH", G_PLANK2);
        cv_text(x + 10, 110, &FONT_XS, "5 MORE!", G_RED);
    } else if (sk.state == SK_READY && sk.n == 0) {
        cv_text(x + 10, 98, &FONT_XS, "PRESS ANY KEY AS THE", G_PLANK2);
        cv_text(x + 10, 110, &FONT_XS, "POINTER PASSES THE MARK", G_PLANK2);
    }
}

static void sk_stand_card(void)
{
    char s[24];
    const int32_t x = 84, w = 148;
    int lv = sk_level();
    round_rect(x, 26, w, 120, 8, G_PAPER);   /* clear of the mallet's swing below */
    cv_text(x + 8, 34, &FONT_XS, "K1 LEVEL", G_PLANK2);
    s[0] = 0;
    sp_num(s, lv);
    text_cat(s, " ");
    text_cat(s, SK_LEVEL_NAME[lv - 1]);
    cv_text(x + 58, 32, &FONT_B, s, G_INK);
    cv_text(x + 8, 50, &FONT_XS, lv == 1 ? "SLOW, A BAND OF TWO" : lv == 2 ? "FASTER, ONE MARK" :
                                 "IT SPEEDS AND SLOWS", G_TEAL);
    cv_text(x + 8, 70, &FONT_XS, "PRESS ANY KEY AS THE", G_PLANK2);
    cv_text(x + 8, 84, &FONT_XS, "POINTER PASSES THE MARK.", G_PLANK2);
    cv_text(x + 8, 98, &FONT_XS, "TRY ONE NOW!", G_PLANK2);
    s[0] = 0;
    text_cat(s, "10 STRIKES");
    if (sav.more[SV_STRIKER].best) {
        text_cat(s, ". BEST ");
        sp_num(s, sav.more[SV_STRIKER].best);
    }
    cv_text(x + 8, 120, &FONT_B, s, G_INK);
}

static void striker_draw(uint32_t now)
{
    int32_t y0;
    char s[24];
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, G_SKY1);
        sk_sky(y0);
        sk_ground(now);
        sk_board(now);
        if (sk_standing())
            sk_stand_card();
        else
            sk_play_card(now);
        cv_rect(0, 0, 240, 18, SK_COL);  /* the header */
        cv_text(6, 3, &FONT_XS, "STRIKER", G_PAPER);
        if (!sk_standing()) {
            text_cat((s[0] = 0, s), "SCORE ");
            sp_num(s, sk.score);
            cv_text(234 - text_w(&FONT_XS, s), 3, &FONT_XS, s, G_PAPER);
        }
        cv_rect(0, 220, 240, 20, G_LILAC);
        {
            const char *f = sk_standing() ? "PLAY: START" : sk.state == SK_OVER ? "PLAY: AGAIN" : "PLAY: STOP";
            cv_text(FOOT_R - text_w(&FONT_XS, f), FOOT_Y, &FONT_XS, f, G_INK);
        }
        band_send(y0);
    }
}

static void striker_frame(uint32_t now)
{
    sk_step(now);
    striker_draw(now);
}

static void striker_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    uint32_t t = ms % 2400u;             /* a strike every 2.4 s: up to the bell, and down */
    float f = t < 1400u ? (float)t / 700.0f : 0.0f;
    int32_t px = x + w / 2, top = y + 10, bot = y + h - 4;
    int32_t ph = (int32_t)((float)(bot - top - 6) * (f < 1.0f ? f * (2.0f - f) : (2.0f - f) * f));
    cv_rect(px - 6, top, 12, bot - top, G_PAPER);
    cv_rect(px - 6, top + (bot - top) / 3, 12, 2, G_RED);
    cv_rect(px - 6, top + 2 * (bot - top) / 3, 12, 2, G_ORANGE);
    fill_circle(px, top - 3, 5, t > 650u && t < 1000u && (ms / 80u & 1u) ? G_BULB : G_SUN);
    cv_rect(px + 1, bot - 6 - ph, 5, 4, G_RED2);
}

static void striker_best(char *t)
{
    t[0] = 0;
    if (sav.more[SV_STRIKER].best) {
        text_cat(t, "BEST ");
        int_text(sav.more[SV_STRIKER].best, t + 5);
    }
}

static uint32_t striker_keys_lit(uint32_t *mid)   /* every key glows while it waits for a strike (any key strikes);
                                                    * on the mark, they flash bright */
{
    uint32_t all = (1u << NKEYS) - 1u, now = plat_ms();
    *mid = sk.state == SK_READY || (sk.state == SK_STAND && sk.landed) ? all : 0u;
    if (sk.scored_ms && now - sk.scored_ms < 700u && sk.off == 0 && !sk.practice && (now / 90u & 1u))
        return all;
    return 0;
}

static void striker_where(char *t)
{
    t[0] = 0;
    if (sk_standing()) {
        int lv = sk_level();
        text_cat(t, "STRIKER. KNOB 1, LEVEL ");
        sp_num(t, lv);
        text_cat(t, ", ");
        text_cat(t, SK_LEVEL_NAME[lv - 1]);
        text_cat(t, ". PRESS ANY KEY AS THE POINTER PASSES THE MARK ASKED. THE BELL IS WORTH 5 MORE. ");
        if (sav.more[SV_STRIKER].best) {
            text_cat(t, "BEST ");
            sp_num(t, sav.more[SV_STRIKER].best);
            text_cat(t, ". ");
        }
        text_cat(t, "PLAY STARTS.");
        return;
    }
    if (sk.state == SK_OVER) {
        text_cat(t, "STRIKER. TEN STRIKES, SCORE ");
        sp_num(t, sk.score);
        text_cat(t, ". ");
        sp_num(t, sk.exact);
        text_cat(t, " ON THE MARK");
        if (sk.new_best) {
            text_cat(t, ". NEW BEST");
        } else if (sav.more[SV_STRIKER].best) {
            text_cat(t, ". BEST ");
            sp_num(t, sav.more[SV_STRIKER].best);
        }
        text_cat(t, ". PLAY STARTS AGAIN.");
        return;
    }
    text_cat(t, "STRIKER. ");
    sk_ask_text(t + text_len(t), sk.state == SK_WAIT && sk.n < SK_STRIKES ? sk.tg[sk.n] : sk_lo());
    text_cat(t, ". STRIKE ");
    sp_num(t, sk.n < SK_STRIKES ? sk.n + 1 : SK_STRIKES);
    text_cat(t, " OF 10, SCORE ");
    sp_num(t, sk.score);
    text_cat(t, ". PLAY STOPS.");
}

static int striker_playing(void) { return !sk_standing() && sk.state != SK_OVER; }

#ifdef PLAT_HOST
/* the host simulator: the test player strikes as the meter passes the target ("autostriker on"), or the mark above
 * it ("autostriker near") */
static int sk_auto;                      /* 0 off, 1 on the mark, 2 a mark high */
static int striker_host_cmd(const char *cmd, const char *a)
{
    if (strcmp(cmd, "autostriker"))
        return 0;
    sk_auto = !strcmp(a, "on") ? 1 : !strcmp(a, "near") ? 2 : 0;
    return 1;
}
static int striker_expect(const char *what, int *got)
{
    if (!strcmp(what, "striker_state"))  /* 0 standing, 1 ready, 2 flying, 3 between, 4 over */
        *got = sk.state;
    else if (!strcmp(what, "striker_score"))
        *got = sk.score;
    else if (!strcmp(what, "striker_n"))
        *got = sk.n;
    else if (!strcmp(what, "striker_mark"))
        *got = sk.mark;
    else if (!strcmp(what, "striker_exact"))
        *got = sk.exact;
    else if (!strcmp(what, "striker_rung"))
        *got = sk.rung;
    else if (!strcmp(what, "striker_pts"))
        *got = sk.pts;
    else if (!strcmp(what, "striker_best"))
        *got = sav.more[SV_STRIKER].best;
    else if (!strcmp(what, "striker_level"))
        *got = sav.more[SV_STRIKER].level;
    else
        return 0;
    return 1;
}
static void striker_host_ms(uint32_t now)
{
    float at, aim;
    int m;
    if (!sk_auto || sk.state != SK_READY || now - sk.ph_ms > 100u)   /* (only while the game is in front) */
        return;
    m = sk_hi() == 10 ? 10 : sk_lo();    /* (level 1's top band: the bell) */
    if (sk_auto == 2)
        m = m < 10 ? m + 1 : 8;
    aim = m >= 10 ? 10.15f : (float)m;
    at = sk_meter_ms(now);
    if (at - aim < 0.12f && aim - at < 0.12f)
        striker_key(9, 1, now);
}
static void striker_speak_dump(void)
{
    char t[40];
    int i, keep_level = sav.more[SV_STRIKER].level;
    uint16_t keep_best = sav.more[SV_STRIKER].best;
    static const char *const MSG[][2] = {
        {"TEN STRIKES", "RING IT TO 7"}, {"TEN STRIKES", "RING IT TO 6 TO 7"}, {"TEN STRIKES", "RING THE BELL"},
        {"UP TO 7, ON THE MARK", "10 POINTS. NOW AGAIN 7"}, {"UP TO 6, ONE OFF", "6 POINTS. NOW THE BELL"},
        {"UP TO 5, TWO OFF", "3 POINTS. NOW 9 TO 10"}, {"UP TO 2, MISSED", "0 POINTS. NOW 4"},
        {"THE BELL, RUNG", "15 POINTS. SCORE 72, NEW BEST"}, {"THE BELL, ONE OFF", "6 POINTS. SCORE 40"},
        {"UP TO 4", ""}, {"THE BELL", ""}, {"STOPPED", ""}};
    for (i = 0; i < (int)(sizeof MSG / sizeof MSG[0]); i++) {
        sp_join(MSG[i][0], MSG[i][1]);
        sp_out("MSG", sp_buf);
    }
    for (i = 1; i <= SK_LEVELS; i++) {   /* the knob, and the standing screen at each level */
        t[0] = 0;
        sp_num(t, i);
        text_cat(t, ", ");
        text_cat(t, SK_LEVEL_NAME[i - 1]);
        sp_join("LEVEL", t);
        sp_out("READOUT", sp_buf);
        sav.more[SV_STRIKER].level = (uint8_t)i;
        sav.more[SV_STRIKER].best = (uint16_t)(i == 2 ? 64 : 0);
        sk.state = SK_STAND;
        striker_where(sp_buf);
        sp_out("SCREEN", sp_buf);
    }
    sk.level = 1;                        /* playing, between, over */
    sk.tg[0] = sk.tg[1] = 6;
    sk.tg[2] = 3;
    sk.n = 1;
    sk.score = 24;
    sk.exact = 2;
    for (i = SK_READY; i <= SK_OVER; i++) {
        sk.state = i;
        sk.new_best = i == SK_OVER;
        striker_where(sp_buf);
        sp_out("SCREEN", sp_buf);
    }
    sk.new_best = 0;
    striker_where(sp_buf);
    sp_out("SCREEN", sp_buf);
    sk.level = 2;
    sk.state = SK_READY;
    striker_where(sp_buf);
    sp_out("SCREEN", sp_buf);
    sk.state = SK_STAND;
    sk.n = sk.score = sk.exact = 0;
    sav.more[SV_STRIKER].level = (uint8_t)keep_level;
    sav.more[SV_STRIKER].best = keep_best;
}
#endif

static const uint8_t STRIKER_TUNE[] = {67, 0, 72, 76, 79, 0, 84, 0, 0, 79, 84, 0, 0, 0, 0, 0};

static const game_t GAME_STRIKER = {"STRIKER", SK_COL, striker_enter, striker_leave, striker_frame, striker_key,
                                    striker_button, striker_enc, 0, striker_attract, striker_best, striker_keys_lit,
                                    STRIKER_TUNE, sizeof STRIKER_TUNE, 170, BW_CHIME, striker_where, 0,
                                    striker_playing, "RING THE BELL", 0};
