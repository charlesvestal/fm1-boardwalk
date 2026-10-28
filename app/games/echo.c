/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Echo: call and response. A groove runs (kick and hat); a bar of woodblock plays a rhythm (the call), and in the
 * next bar you play it back on any key (the answer), then the next call comes, and so on without stopping. Your bar
 * sounds different (the hat thins to quarters) and the keys glow through it. An answer passes with every hit there,
 * none extra, and on time on average (70 %); four passes and the level goes up, from quarter notes to eighths,
 * offbeats and sixteenths; three failed and the game is over.
 *
 * Standing: K1 the tempo, K2 the level to start at; PLAY starts and stops. Part of the unity build. */
#include "game.h"

#define EC_LEVELS 5
#define EC_LIVES 3
#define EC_PASS 70                       /* % to pass */
#define EC_UP 4                          /* passes to go up a level */
#define EC_WIN 150                       /* ms either side of a hit that still counts */
#define EC_HITS 24

/* the calls, a bar of sixteenths: x a hit, . a rest */
#define EC_LAG 0                         /* ms from a key to its sound, taken off the hits' times (to set on the FM-1) */

static const char *const CALLS[EC_LEVELS][6] = {
    {"x...x...x...x...", "x.......x.......", "x...x...x.......", "x.......x...x...", "x...x.......x...",
     "x...x...x...x..."},
    {"x.x.x...x.......", "x...x.x.x.......", "x.x.x.x.x.......", "x...x...x.x.x...", "x.x.....x.x.....",
     "....x...x...x..."},
    {"..x...x...x...x.", "x...x..x..x.....", "x..x..x.x.......", "x..x....x..x....", "x.....x...x.x...",
     "x.x..x..x...x..."},
    {"xxx.x...x.......", "x..xx...xx..x...", "x.xx..x.x..xx...", "x....xx.x....xx.", "xx..x.xx....x...",
     "x.x.xxx.x..x.x.."},
    {"x.xxx..x.x.xx...", "xx.x.xx.x..x.x..", "x..xx.x.xx.x....", ".x.xx..xx.x.x...", "x.x..xxx.x..xx..",
     "xxx..x.x..x.xx.."},
};

enum { ES_STAND, ES_PLAY, ES_OVER };
static struct {
    int state;
    int32_t step;                        /* ms a sixteenth */
    volatile uint32_t t0;                /* plat_ms of bar 0 (the count-in); calls on odd bars, answers on even */
    volatile uint16_t calls[2];          /* the calls, bit i step i: call bar c fills calls[slot(c + 1)], the answer
                                          * bar after it is judged against it */
    int level, lives, score, streak, last_pct, judged_bar, new_best;
    /* the answer: hits in ms from t0 */
    volatile int32_t hit[EC_HITS];
    volatile int nhits, hit_bar;         /* the hits are this answer bar's */
    volatile int tick_step;              /* the tick's next sixteenth to play */
    uint32_t flash_ms;                   /* a call's hit: the keys flash */
    int8_t mark[EC_HITS];                /* your last answer: each hit's step (x 4, for the screen), -1 none */
    uint8_t mark_good[EC_HITS];
    int nmarks;
    uint16_t judged_call;                /* the call that answer was to (drawn under the marks) */
    int32_t lean;                        /* its hits on average: - early, + late (ms) */
    uint32_t hurt_ms, lvup_ms;           /* a life lost; a level up: shown a moment */
} ec;

static uint16_t call_mask(const char *s)
{
    uint16_t m = 0;
    int i;
    for (i = 0; i < 16 && s[i]; i++)
        if (s[i] == 'x')
            m |= (uint16_t)(1u << i);
    return m;
}
static uint16_t pick_call(void) { return call_mask(CALLS[clamp(ec.level, 1, EC_LEVELS) - 1][rnd(6)]); }

static int32_t bar_ms(void) { return ec.step * 16; }
static int slot(int answer_bar) { return answer_bar / 2 & 1; }
static uint16_t call_at(int bar) { return ec.calls[slot(bar & 1 ? bar + 1 : bar)]; }   /* a call or answer bar's */

static void echo_go(void)
{
    rnd_state ^= plat_ms() * 2654435761u;
    ec.step = 60000 / clamp(sav.echo.tempo, 70, 140) / 4;
    ec.level = clamp(sav.echo.level, 1, EC_LEVELS);
    ec.lives = EC_LIVES;
    ec.score = ec.streak = 0;
    ec.last_pct = -1;
    ec.judged_bar = 0;
    ec.nhits = 0;
    ec.hit_bar = 2;
    ec.nmarks = 0;
    ec.calls[0] = ec.calls[1] = 0;
    ec.tick_step = 0;
    ec.t0 = plat_ms() + 300u;
    ec.state = ES_PLAY;
    app_say("LISTEN", "THEN PLAY IT BACK");
}

/* the tick: the groove, the count-in, the calls */
static void echo_tick(uint32_t ms)
{
    int32_t t = (int32_t)(ms - ec.t0);
    if (ec.state != ES_PLAY || t < 0)
        return;
    while (ec.tick_step * ec.step <= t) {
        int s = ec.tick_step, bar = s / 16, st = s % 16;
        if (st == 0 && bar & 1)          /* a call bar starts: its call, at the level now */
            ec.calls[slot(bar + 1)] = pick_call();
        {
            int ans = bar >= 2 && !(bar & 1);   /* your bar: the hat on the quarters only */
            if (st % 2 == 0 && (!ans || st % 4 == 0))
                bw_drum(BW_HAT, st % 4 == 0 ? 90 : 60);
        }
        if (st == 0 || st == 8)
            bw_drum(BW_KICK, 110);
        if (bar == 0 && st % 4 == 0)     /* the count-in: four clicks */
            bw_drum(BW_BLOCK_LO, 90);
        if ((bar & 1) && (ec.calls[slot(bar + 1)] >> st & 1u)) {
            bw_drum(BW_BLOCK_HI, 110);
            ec.flash_ms = ms;
        }
        ec.tick_step++;
    }
}

/* a key: your clap, and a hit for the answer bar it falls in (with EC_WIN's grace either side) */
static void echo_key(int k, int down, uint32_t ms)
{
    int32_t t = (int32_t)(ms - ec.t0) - EC_LAG, b;
    (void)k;
    if (!down)
        return;
    snd_lock();
    bw_drum(BW_CLAP, 110);
    snd_unlock();
    if (ec.state != ES_PLAY)
        return;
    {                                    /* the answer bar it belongs to (even bars from 2), with EC_WIN's grace */
        int32_t bm = bar_ms(), cur = t / bm, in = t % bm;
        b = !(cur & 1) ? cur : in < EC_WIN ? cur - 1 : in >= bm - EC_WIN ? cur + 1 : -1;
    }
    if (b < 2)
        return;
    if (b != ec.hit_bar) {               /* the next answer: its hits start afresh */
        ec.hit_bar = b;
        ec.nhits = 0;
    }
    if (ec.nhits < EC_HITS)
        ec.hit[ec.nhits++] = t;
}

/* an answer bar is over (and its grace): how close it came to the call. Each of the call's hits takes the nearest of
 * yours still free: on time is a whole hit, a little off part of one; your extra hits count against */
static void echo_judge(int bar)
{
    int32_t start = bar * bar_ms(), sum = 0, n = 0, i, j;
    uint8_t used[EC_HITS] = {0};
    int extra = 0, missing = 0, matched = 0, hits = ec.hit_bar == bar ? ec.nhits : 0, pass;
    int32_t lean = 0;
    uint16_t call = call_at(bar);
    char t[32];
    for (i = 0; i < 16; i++) {
        int32_t want = start + i * ec.step, best = -1, bd = EC_WIN + 1;
        if (!(call >> i & 1u))
            continue;
        n++;
        for (j = 0; j < hits; j++) {
            int32_t d = ec.hit[j] - want;
            d = d < 0 ? -d : d;
            if (!used[j] && d < bd) {
                bd = d;
                best = j;
            }
        }
        if (best >= 0) {
            used[best] = 1;
            sum += bd <= 50 ? 100 : bd <= 100 ? 70 : 35;
            lean += ec.hit[best] - want;
            matched++;
        } else {
            missing++;
        }
    }
    ec.nmarks = 0;
    for (j = 0; j < hits; j++) {         /* your hits, for the screen */
        int32_t pos = (ec.hit[j] - start) * 4 / ec.step;   /* quarter steps */
        if (!used[j])
            extra++;
        if (ec.nmarks < EC_HITS) {
            ec.mark[ec.nmarks] = (int8_t)clamp(pos, -2, 64);
            ec.mark_good[ec.nmarks++] = used[j];
        }
    }
    /* a pass: every hit of the call there, none extra, and on time on average; the % shown counts all of it */
    ec.last_pct = n ? clamp((sum - 50 * (extra + missing)) / n, 0, 100) : 0;
    pass = !missing && !extra && n && sum / n >= EC_PASS;
    ec.lean = matched ? lean / matched : 0;
    ec.judged_call = call;
    ec.judged_bar = bar;
    if (pass) {
        ec.score++;
        ec.streak++;
        if (ec.streak % EC_UP == 0 && ec.level < EC_LEVELS) {
            ec.level++;
            ec.lvup_ms = plat_ms();
            snd_lock();
            bw_note(84, 70, BW_CHIME);
            snd_unlock();
            t[0] = 0;
            text_cat(t, "LEVEL ");
            int_text(ec.level, t + 6);
            app_say("LEVEL UP", t);
        } else {
            app_say(sum / n >= 90 ? "GREAT" : "GOOD", "");
        }
    } else {
        ec.lives--;
        ec.streak = 0;
        ec.hurt_ms = plat_ms();
        app_say(ec.lives == 1 ? "MISSED. LAST LIFE" : "MISSED", missing ? "A HIT LEFT OUT" : extra ? "A HIT TOO MANY" : "");
    }
    if (ec.lives <= 0) {
        ec.state = ES_OVER;
        ec.new_best = ec.score > sav.echo.best;
        if (ec.new_best) {
            sav.echo.best = (uint16_t)ec.score;
            save_dirty();
        }
        t[0] = 0;
        text_cat(t, "SCORE ");
        int_text(ec.score, t + 6);
        app_say(ec.new_best ? "GAME OVER. NEW BEST" : "GAME OVER", t);
    }
}

static void echo_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (ec.state == ES_PLAY) {
        ec.state = ES_STAND;
        app_say("STOPPED", "");
    } else {
        echo_go();
    }
}

static void echo_enc(int role, int steps)
{
    char t[16];
    if (ec.state == ES_PLAY)
        return;
    if (role == EN_K1) {
        sav.echo.tempo = (uint8_t)clamp(sav.echo.tempo + 5 * steps, 70, 140);
        int_text(sav.echo.tempo, t);
        text_cat(t, " BPM");
        app_readout("TEMPO", t);
    } else if (role == EN_K2) {
        sav.echo.level = (uint8_t)clamp(sav.echo.level + steps, 1, EC_LEVELS);
        int_text(sav.echo.level, t);
        app_readout("LEVEL", t);
    } else {
        return;
    }
    ec.state = ES_STAND;
    save_dirty();
}

static void echo_enter(void) { ec.state = ES_STAND; }
static void echo_leave(void) { ec.state = ES_STAND; }

static const char *const LEVEL_WORD[EC_LEVELS] = {"QUARTERS", "EIGHTHS", "OFFBEATS", "SIXTEENTHS", "ANYTHING GOES"};

static void echo_draw(uint32_t now)
{
    int32_t y0, i, t = (int32_t)(now - ec.t0), bar = 0, st = 0, gx = 12, gw = 13;
    char s[32];
    if (ec.state == ES_PLAY && t >= 0) {
        bar = t / bar_ms();
        st = t % bar_ms() / ec.step;
    }
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, G_PAPER);
        cv_rect(0, 0, 240, 18, RGB(232, 88, 128));
        cv_text(6, 3, &FONT_XS, "ECHO", G_PAPER);
        if (ec.state == ES_STAND) {
            cv_text(14, 38, &FONT_XS, "K1 TEMPO", G_PLANK2);
            int_text(sav.echo.tempo, s);
            text_cat(s, " BPM");
            cv_text(94, 36, &FONT_B, s, G_INK);
            cv_text(14, 64, &FONT_XS, "K2 LEVEL", G_PLANK2);
            int_text(sav.echo.level, s);
            cv_text(94, 62, &FONT_B, s, G_INK);
            cv_text(94, 80, &FONT_XS, LEVEL_WORD[clamp(sav.echo.level, 1, EC_LEVELS) - 1], G_PLANK2);
            if (sav.echo.best) {
                text_cat((s[0] = 0, s), "BEST ");
                int_text(sav.echo.best, s + 5);
                cv_text(14, 108, &FONT_B, s, G_INK);
            }
            cv_text(14, 140, &FONT_XS, "LISTEN TO THE WOODBLOCK, THEN PLAY", G_PLANK2);
            cv_text(14, 154, &FONT_XS, "IT BACK ON ANY KEY IN THE NEXT BAR.", G_PLANK2);
        } else {
            int calling = bar & 1, answering = bar >= 2 && !(bar & 1);
            /* lives (the one just lost flashing), score, level, and the passes to the next level */
            for (i = 0; i < EC_LIVES; i++) {
                int lost_now = i == ec.lives && now - ec.hurt_ms < 600u && (now / 100u & 1u);
                fill_circle(20 + i * 16, 34, 5, i < ec.lives || lost_now ? RGB(232, 88, 128) : G_PAPER2);
            }
            if (ec.level < EC_LEVELS && ec.state == ES_PLAY)
                for (i = 0; i < EC_UP; i++)
                    fill_circle(20 + i * 10, 50, 3, i < ec.streak % EC_UP ? G_TEAL : G_PAPER2);
            int_text(ec.score, s);
            cv_text(232 - text_w(&FONT_L, s), 24, &FONT_L, s, G_INK);
            text_cat((s[0] = 0, s), "LEVEL ");
            int_text(ec.level, s + 6);
            cv_text(234 - text_w(&FONT_XS, s), 3, &FONT_XS, s, G_PAPER);
            /* what now */
            text_mid(120, 60, &FONT_B, ec.state == ES_OVER ? "GAME OVER" :
                                       ec.lvup_ms && now - ec.lvup_ms < 2000u ? "LEVEL UP" :
                                       bar == 0 ? "GET READY" : calling ? "LISTEN" : "YOUR TURN",
                     calling || ec.state == ES_OVER ? G_INK : RGB(232, 88, 128));
            if (ec.state == ES_PLAY && bar == 0) {   /* the count-in, on screen */
                int_text(st / 4 + 1, s);
                text_mid(120, 92, &FONT_L, s, G_INK);
            }
            /* the call: sixteen steps, the beats marked; the playhead on the step now */
            for (i = 0; i < 16; i++) {
                int32_t x = gx + i * gw;
                int on = bar > 0 && ec.state == ES_PLAY && (call_at(bar) >> i & 1u);
                if (bar == 0 && ec.state == ES_PLAY)
                    break;
                cv_rect(x, 96, gw - 2, 22, on ? G_INK : (i % 4 == 0 ? G_PAPER2 : mix(G_PAPER2, G_PAPER, 128)));
                if (ec.state == ES_PLAY && i == st && (calling || answering))
                    cv_rect(x, 120, gw - 2, 3, calling ? G_INK : RGB(232, 88, 128));
            }
            cv_text(gx, 80, &FONT_XS, "CALL", G_PLANK2);
            /* your answer: the last one judged, each hit where it fell, good ones teal, stray ones red */
            cv_text(gx, 132, &FONT_XS, "YOU", G_PLANK2);
            for (i = 0; i < 16; i++)     /* under your marks: the call they answered, faint */
                cv_rect(gx + i * gw, 148, gw - 2, 22, !answering && (ec.judged_call >> i & 1u) ?
                                                         mix(G_INK, G_PAPER, 170) : G_PAPER2);
            if (ec.state == ES_PLAY && answering) {   /* this answer, as you play it */
                for (i = 0; i < ec.nhits && ec.hit_bar == bar; i++) {
                    int32_t x = gx + (ec.hit[i] - bar * bar_ms()) * gw / ec.step;
                    cv_rect(clamp(x, gx, gx + 16 * gw - 4), 150, 3, 18, RGB(232, 88, 128));
                }
            } else {
                for (i = 0; i < ec.nmarks; i++) {
                    int32_t x = gx + ec.mark[i] * gw / 4;
                    cv_rect(clamp(x, gx, gx + 16 * gw - 4), 150, 3, 18, ec.mark_good[i] ? G_TEAL : G_RED);
                }
            }
            if (ec.state == ES_OVER) {
                text_cat((s[0] = 0, s), "SCORE ");
                int_text(ec.score, s + 6);
                text_mid(120, 176, &FONT_B, s, G_INK);
                text_mid(120, 196, &FONT_XS, ec.new_best ? "NEW BEST!" : "", G_RED);
            } else if (ec.last_pct >= 0) {   /* the last answer: the word, the %, early or late */
                text_cat((s[0] = 0, s), now - ec.hurt_ms < 2400u ? "MISSED " : ec.last_pct >= 90 ? "GREAT " : "GOOD ");
                int_text(ec.last_pct, s + text_len(s));
                text_cat(s, "%");
                text_mid(120, 176, &FONT_B, s, now - ec.hurt_ms < 2400u ? G_RED : G_TEAL);
                if (ec.lean > 30 || ec.lean < -30)
                    text_mid(120, 196, &FONT_XS, ec.lean > 0 ? "A BIT LATE" : "A BIT EARLY", G_PLANK2);
            }
        }
        cv_rect(0, 220, 240, 20, G_LILAC);
        {
            const char *f = ec.state == ES_PLAY ? "PLAY: STOP" : ec.state == ES_OVER ? "PLAY: AGAIN" : "PLAY: START";
            cv_text(232 - text_w(&FONT_XS, f), 224, &FONT_XS, f, G_INK);
        }
        band_send(y0);
    }
}

static void echo_frame(uint32_t now)
{
    int32_t t = (int32_t)(now - ec.t0);
    if (ec.state == ES_PLAY && t > 0) {
        int bar = (t - EC_WIN) / bar_ms() - 1;   /* the last answer bar whose grace is over */
        if (bar >= 2 && !(bar & 1) && bar > ec.judged_bar)
            echo_judge(bar);
    }
    echo_draw(now);
}

static void echo_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    static const uint16_t RH = 0x4949u;  /* x..x..x..x..x.x. backwards: a clave-ish beat */
    int32_t i, st = (int32_t)(ms / 150u % 16u);
    for (i = 0; i < 16; i++) {
        int on = RH >> i & 1u;
        cv_rect(x + 2 + i * (w - 4) / 16, y + h / 2 - (on && i == st ? 8 : 3), (w - 4) / 16 - 1, on && i == st ? 16 : 6,
                on ? RGB(232, 88, 128) : G_PAPER2);
    }
}

static void echo_best(char *t)
{
    t[0] = 0;
    if (sav.echo.best) {
        text_cat(t, "BEST ");
        int_text(sav.echo.best, t + 5);
        text_cat(t, " ROUNDS");
    }
}

static uint32_t echo_keys_lit(uint32_t *mid)   /* the call's hits flash on every key; the beat on the black keys */
{
    static const uint8_t BK[11] = PLAT_BLACK_KEYS;
    uint32_t now = plat_ms(), keys = 0;
    int32_t t = (int32_t)(now - ec.t0);
    int i;
    *mid = 0;
    if (ec.state != ES_PLAY || t < 0)
        return 0;
    if (now - ec.flash_ms < 90u)
        keys = (1u << NKEYS) - 1u;
    if (t / bar_ms() >= 2 && !(t / bar_ms() & 1)) {   /* your bar: every key glows */
        *mid = (1u << NKEYS) - 1u;
        return keys;
    }
    if (t % (ec.step * 4) < 90)
        for (i = 0; i < 11; i++)
            *mid |= 1u << BK[i];
    return keys;
}

static void echo_where(char *t)
{
    char l[8];
    t[0] = 0;
    if (ec.state == ES_STAND) {
        text_cat(t, "ECHO. KNOB 1, TEMPO ");
        sp_num(t, sav.echo.tempo);
        text_cat(t, " BPM. KNOB 2, LEVEL ");
        sp_num(t, sav.echo.level);
        text_cat(t, ", ");
        text_cat(t, LEVEL_WORD[clamp(sav.echo.level, 1, EC_LEVELS) - 1]);
        text_cat(t, ". LISTEN TO THE WOODBLOCK, THEN PLAY IT BACK ON ANY KEY IN THE NEXT BAR. ");
        if (sav.echo.best) {
            text_cat(t, "BEST ");
            sp_num(t, sav.echo.best);
            text_cat(t, ". ");
        }
        text_cat(t, "PLAY STARTS.");
        return;
    }
    if (ec.state == ES_PLAY) {
        int32_t tt = (int32_t)(plat_ms() - ec.t0), bar = tt > 0 ? tt / bar_ms() : 0;
        text_cat(t, bar == 0 ? "ECHO. GET READY. SCORE " : bar & 1 ? "ECHO. LISTEN. SCORE " : "ECHO. YOUR TURN. SCORE ");
    } else {
        text_cat(t, "ECHO. GAME OVER. SCORE ");
    }
    sp_num(t, ec.score);
    if (ec.state == ES_PLAY) {
        text_cat(t, ", LIVES ");
        sp_num(t, ec.lives);
        text_cat(t, ", LEVEL ");
        int_text(ec.level, l);
        text_cat(t, l);
        text_cat(t, ". PLAY STOPS.");
    } else {
        text_cat(t, ec.new_best ? ". NEW BEST. PLAY STARTS AGAIN." : ". PLAY STARTS AGAIN.");
    }
}

/* the attract tune: a clave rhythm on the pluck */
static int echo_playing(void) { return ec.state == ES_PLAY; }

static const uint8_t ECHO_TUNE[] = {76, 0, 0, 76, 0, 0, 76, 0, 0, 0, 79, 0, 79, 0, 0, 0};

static const game_t GAME_ECHO = {"ECHO", RGB(232, 88, 128), echo_enter, echo_leave, echo_frame, echo_key,
                                 echo_button, echo_enc, echo_tick, echo_attract, echo_best, echo_keys_lit,
                                 ECHO_TUNE, sizeof ECHO_TUNE, 150, BW_PLUCK, echo_where, 1, echo_playing, "ANSWER THE RHYTHM"};
