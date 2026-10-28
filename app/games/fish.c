/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Fishing: the fishing pond under the pier, a column a key. Fish swim to and fro below the water, each with its own
 * note: it hums it softly as it passes under your float. Press a key to cast there; a fish nearby comes to the bait,
 * nibbles, and takes it: the float dips with the fish's note (the plip). Press that note's key while the float is
 * under to hook it (before the dip, or too late, or the wrong note, and it gets away). Then reel it in: the line tugs
 * on a steady beat (heard, and seen on the line and the keys); a tap in time with the tug reels, frantic taps tighten
 * the line until it snaps, and a tug with no tap lets the fish run. Turning any knob also reels, a little at a time.
 *
 * A round is ten casts or 90 seconds; the score is the weight in the bucket. Levels (K1): 1 big slow fish with their
 * notes shown, 2 by ear, 3 faster and fussier fish (they nibble more, take the bait for less time, pull harder).
 * PLAY starts and stops. The keys never light the answer: the one your float is on glows. Part of the unity build. */
#include "game.h"

#define FI_CASTS 10
#define FI_ROUND_MS 90000u
#define FI_LEVELS 3
#define FI_FISH 6
#define FI_SURF 64                       /* the water's line */
#define FI_BED 194                       /* the pond's bed */
#define FI_TIP 36                        /* the rod's tip: the line hangs from it */
#define FI_LOST 150                      /* the line run out this far: the fish is gone */

enum { FS_STAND, FS_AIM, FS_WAIT, FS_REEL, FS_OVER };
typedef struct {
    uint8_t on, key, kind, size, flee, under;   /* key: its note (53 + key), and the middle of its swim */
    int8_t dir;
    int16_t y, span;                     /* its depth; how far it swims either side of its key */
    uint16_t wt;                         /* tenths of a kilo */
    float x, speed;
} fish_t;

static struct {
    volatile int state;
    int level, new_best, ended_by_time, in_front;
    fish_t fish[FI_FISH];
    int casts, catches, weight;          /* casts made; weight in tenths of a kilo */
    uint32_t t0, last_ms, stop_ms;       /* stop_ms: the round's clock stopped there (over) */
    int col;                             /* the key the float is on */
    uint32_t cast_ms;
    int plopped;
    /* a bite */
    int taker, arrived;                  /* the fish coming to the bait (-1 none); at the float */
    uint32_t look_ms;                    /* when a fish next looks at the bait */
    volatile uint32_t dip_ms;            /* the dip (0: none yet); the tick plips at it */
    volatile uint32_t nib_ms[3];
    volatile int nnib, nib_done, dipped;
    int window;                          /* ms to hook it after the dip */
    /* the fight */
    int hf;                              /* the hooked fish */
    volatile uint32_t hook_ms, period;   /* the tug: every period ms from hook_ms */
    volatile int pulse_n;                /* the tick's next tug */
    volatile int tug_note;
    int judged_n, good_n;
    float dist, tension, step;
    uint32_t last_tap_ms, knob_ms, landed_ms;
    int warned;
    int snaps, lost;                     /* for the tests */
    /* what was said, on the screen a moment */
    char msg[24];
    uint32_t msg_ms;
    uint16_t msg_col;
    uint8_t caught_size[FI_CASTS], caught_kind[FI_CASTS];
} fi;

static const char *const FI_LEVEL_NAME[FI_LEVELS] = {"NOTES SHOWN", "BY EAR", "FUSSY FISH"};
static const uint16_t FI_BODY[4] = {RGB(242, 163, 58), RGB(200, 210, 222), RGB(120, 160, 90), RGB(232, 120, 140)};

static int fi_note(const fish_t *f) { return 53 + f->key; }

static void wt_text(int w, char *t)      /* tenths of a kilo: "1.4" */
{
    int_text(w / 10, t + text_len(t));
    text_cat(t, ".");
    int_text(w % 10, t + text_len(t));
}

static void fi_say(const char *a, const char *b, const char *screen, uint16_t col)   /* said, and shown a moment */
{
    fi.msg[0] = 0;
    text_cat(fi.msg, screen);
    fi.msg_ms = plat_ms();
    fi.msg_col = col;
    app_say(a, b);
}

/* ------------------------------------------------------------------ the fish --- */
static void fish_spawn(int i)            /* a new fish: its note clear of the others', swimming in from a side */
{
    fish_t *f = &fi.fish[i];
    int tries, k = 0, j;
    for (tries = 0; tries < 40; tries++) {
        int ok = 1;
        k = 1 + (int)rnd(NKEYS - 2);
        for (j = 0; j < FI_FISH; j++)
            if (j != i && fi.fish[j].on && !fi.fish[j].flee) {
                int d = fi.fish[j].key - k;
                if ((d < 0 ? -d : d) < 3)
                    ok = 0;
            }
        if (ok)
            break;
    }
    f->on = 1;
    f->key = (uint8_t)k;
    f->kind = (uint8_t)rnd(4);
    f->size = (uint8_t)(fi.level == 1 ? 1 + rnd(2) : rnd(10) < 4 ? 0 : rnd(10) < 7 ? 1 : 2);   /* level 1: big */
    f->wt = (uint16_t)(f->size == 0 ? 2 + rnd(5) : f->size == 1 ? 7 + rnd(9) : 18 + rnd(28));
    f->flee = 0;
    f->under = 0;
    f->span = (int16_t)(14 + rnd(24));
    f->y = (int16_t)(96 + rnd(84));
    f->speed = fi.level == 1 ? 8.0f + (float)rnd(7) : fi.level == 2 ? 12.0f + (float)rnd(10) :
                                                                     22.0f + (float)rnd(16);
    f->dir = key_x(k) < 120 ? 1 : -1;    /* in from the nearer side */
    f->x = f->dir > 0 ? -16.0f : 256.0f;
}

static int fish_count(void) { return fi.level == 1 ? 4 : fi.level == 2 ? 5 : 6; }

static void fish_flee(int i)             /* off, fast, away from the float; a new one comes */
{
    fi.fish[i].flee = 1;
    fi.fish[i].dir = fi.fish[i].x < (float)key_x(fi.col) ? -1 : 1;
}

static void fish_swim(uint32_t now, float dt)
{
    int i;
    float fx = (float)key_x(fi.col);
    for (i = 0; i < fish_count(); i++) {
        fish_t *f = &fi.fish[i];
        float home = (float)key_x(f->key);
        int under;
        if (!f->on) {
            fish_spawn(i);
            continue;
        }
        if (fi.state == FS_REEL && i == fi.hf)
            continue;
        if (f->flee) {
            f->x += (float)f->dir * 90.0f * dt;
            if (f->x < -30.0f || f->x > 270.0f)
                fish_spawn(i);
            continue;
        }
        if (fi.state == FS_WAIT && i == fi.taker) {   /* to the bait */
            float sp = f->speed < 14.0f ? 14.0f : f->speed, d = fx - f->x;
            if (d > 1.0f || d < -1.0f) {
                f->dir = d > 0 ? 1 : -1;
                f->x += (float)f->dir * sp * dt;
                if ((d > 0) != (fx - f->x > 0))
                    f->x = fx;
            }
        } else {                         /* to and fro about its key */
            f->x += (float)f->dir * f->speed * dt;
            if (f->x > home + (float)f->span && f->dir > 0)
                f->dir = -1;
            else if (f->x < home - (float)f->span && f->dir < 0)
                f->dir = 1;
        }
        under = f->x - fx < 3.0f && fx - f->x < 3.0f;
        if (under && !f->under && (fi.state == FS_AIM || fi.state == FS_WAIT)) {   /* it hums under the float */
            snd_lock();
            bw_note(fi_note(f), 30, BW_SINE);
            snd_unlock();
        }
        f->under = (uint8_t)under;
    }
    (void)now;
}

/* ------------------------------------------------------------------- a round --- */
static void fish_end(uint32_t now)
{
    char t[40] = "";
    fi.state = FS_OVER;
    fi.stop_ms = now;
    fi.new_best = fi.weight > sav.more[SV_FISH].best;
    if (fi.new_best) {
        sav.more[SV_FISH].best = (uint16_t)fi.weight;
        save_dirty();
    }
    wt_text(fi.weight, t);
    text_cat(t, " KILOS, ");
    int_text(fi.catches, t + text_len(t));
    text_cat(t, " FISH");
    app_say(fi.ended_by_time ? (fi.new_best ? "TIME'S UP. NEW BEST" : "TIME'S UP") :
                               (fi.new_best ? "LAST CAST. NEW BEST" : "LAST CAST"), t);
    snd_lock();
    bw_note(72, 70, BW_CHIME);
    bw_note(76, 60, BW_CHIME);
    bw_note(79, 60, BW_CHIME);
    snd_unlock();
}

static void cast_over(uint32_t now)      /* the line comes in: another cast, or the end */
{
    fi.state = FS_AIM;
    fi.taker = -1;
    fi.dip_ms = 0;
    if (fi.casts >= FI_CASTS)
        fish_end(now);
}

static void fish_cast(int k, uint32_t now)
{
    fi.col = k;
    fi.casts++;
    fi.cast_ms = now;
    fi.plopped = 0;
    fi.taker = -1;
    fi.arrived = 0;
    fi.dip_ms = 0;
    fi.nnib = fi.nib_done = fi.dipped = 0;
    fi.look_ms = now + 600u + rnd(900);
    fi.state = FS_WAIT;
    snd_lock();
    bw_drum(BW_HAT, 45);                 /* the whizz of the line */
    snd_unlock();
}

static void fish_go(void)
{
    int i;
    uint32_t now = plat_ms();
    rnd_state ^= now * 2654435761u;
    sav.more[SV_FISH].level = (uint8_t)clamp(sav.more[SV_FISH].level, 1, FI_LEVELS);
    fi.level = sav.more[SV_FISH].level;
    for (i = 0; i < FI_FISH; i++)
        fi.fish[i].on = 0;
    for (i = 0; i < fish_count(); i++) { /* the pond's fish, already swimming */
        fish_spawn(i);
        fi.fish[i].x = (float)key_x(fi.fish[i].key);
    }
    fi.casts = fi.catches = fi.weight = 0;
    fi.snaps = fi.lost = 0;
    fi.t0 = fi.last_ms = now;
    fi.taker = -1;
    fi.dip_ms = 0;
    fi.ended_by_time = 0;
    fi.msg_ms = 0;
    fi.landed_ms = 0;
    fi.state = FS_AIM;
    app_say("TEN CASTS", "PRESS A KEY TO CAST THERE");
}

static void fish_bite_plan(uint32_t now) /* at the float: its nibbles, then the dip */
{
    fish_t *f = &fi.fish[fi.taker];
    int i, n = fi.level == 1 ? 1 : fi.level == 2 ? 1 + (int)rnd(2) : 2 + (int)rnd(2);
    uint32_t t = now + 350u + rnd(500);
    fi.arrived = 1;
    for (i = 0; i < n; i++) {
        fi.nib_ms[i] = t;
        t += 450u + rnd(500);
    }
    fi.nnib = n;
    fi.nib_done = 0;
    fi.dipped = 0;
    fi.window = fi.level == 1 ? 900 : fi.level == 2 ? 700 : 450;
    fi.tug_note = fi_note(f);
    /* level 3: a fussy fish may think better of it */
    fi.dip_ms = fi.level == 3 && rnd(10) < 3 ? 0u : t + rnd(400);
    if (!fi.dip_ms)
        fi.look_ms = t;                  /* (when it swims off) */
}

static void fish_hook(uint32_t ms)
{
    fish_t *f = &fi.fish[fi.taker];
    int need = (f->size == 0 ? 5 : f->size == 1 ? 7 : 9) + (fi.level == 3 ? 2 : 0);
    fi.hf = fi.taker;
    fi.dist = 100.0f;
    fi.tension = 10.0f;
    fi.step = 100.0f / (float)need;
    fi.judged_n = fi.good_n = 0;
    fi.last_tap_ms = ms;
    fi.knob_ms = 0;
    fi.warned = 0;
    fi.tug_note = fi_note(f) - 12;
    fi.period = (uint32_t)(fi.level == 1 ? 650 : fi.level == 2 ? 560 : 480) + (f->size == 2 ? 40u : 0u);
    fi.hook_ms = ms;
    fi.pulse_n = 1;
    fi.state = FS_REEL;                  /* (last: the tick tugs from here) */
    snd_lock();
    bw_drum(BW_BLOCK_HI, 90);
    bw_note(fi_note(f) + 12, 60, BW_CHIME);
    snd_unlock();
    fi_say("HOOKED", "REEL IN TIME WITH THE TUG", "HOOKED!", G_TEAL);
}

static void fish_away(const char *a, const char *b, const char *screen, uint32_t now)   /* the bite missed */
{
    if (fi.taker >= 0)
        fish_flee(fi.taker);
    fi_say(a, b, screen, G_RED);
    cast_over(now);
}

static void fish_landed(uint32_t now)
{
    fish_t *f = &fi.fish[fi.hf];
    char t[24] = "", s[24] = "+";
    if (fi.catches < FI_CASTS) {
        fi.caught_size[fi.catches] = f->size;
        fi.caught_kind[fi.catches] = f->kind;
    }
    fi.catches++;
    fi.weight += f->wt;
    fi.landed_ms = now;
    wt_text(f->wt, t);
    text_cat(t, " KILOS");
    wt_text(f->wt, s);
    text_cat(s, " KG");
    snd_lock();                          /* a splash, then bells */
    bw_drum(BW_CLAP, 90);
    bw_bell(0, 84, 120);
    bw_bell(1, f->size == 2 ? 91 : 88, 115);
    if (f->size == 2)
        bw_bell(2, 96, 110);
    snd_unlock();
    fi_say(f->size == 2 ? "LANDED A BIG ONE" : "LANDED", t, s, G_ORANGE);
    fish_spawn(fi.hf);
    fi.hf = -1;
    fi.taker = -1;
    cast_over(now);
}

static void fish_off(int snapped, uint32_t now)   /* the fight lost */
{
    snd_lock();
    if (snapped) {
        bw_drum(BW_CLAP, 110);
        bw_note(41, 90, BW_DUD);
    } else {
        bw_note(64, 60, BW_PLUCK);
        bw_note(60, 55, BW_PLUCK);
        bw_note(55, 50, BW_PLUCK);
    }
    snd_unlock();
    if (snapped)
        fi.snaps++;
    else
        fi.lost++;
    fish_flee(fi.hf);
    fi.hf = -1;
    fi.taker = -1;
    fi_say(snapped ? "SNAP" : "IT GOT AWAY", snapped ? "THE LINE BROKE, TAP STEADIER" : "TAP WITH EVERY TUG",
           snapped ? "SNAP!" : "IT GOT AWAY", G_RED);
    cast_over(now);
}

static void reel_tap(uint32_t ms)        /* a tap while reeling: judged against the tug */
{
    int32_t p = (int32_t)fi.period, rel = (int32_t)(ms - fi.hook_ms), n = (rel + p / 2) / p, e = rel - n * p;
    if (e < 0)
        e = -e;
    snd_lock();
    if ((int32_t)(ms - fi.last_tap_ms) < p * 55 / 100) {   /* frantic */
        fi.tension += fi.level == 3 ? 30.0f : 24.0f;
        fi.dist -= fi.step * 0.15f;
        bw_note(48, 60, BW_DUD);
    } else if (n >= 1 && e <= p * 28 / 100 && fi.good_n < n) {   /* in time: it reels */
        fi.good_n = n;
        fi.dist -= fi.step;
        fi.tension -= 6.0f;
        bw_drum(BW_BLOCK_HI, 75);
    } else {                             /* off the beat: a little */
        fi.dist -= fi.step * 0.35f;
        fi.tension += 8.0f;
        bw_drum(BW_BLOCK_HI, 35);
    }
    snd_unlock();
    fi.last_tap_ms = ms;
}

static void fish_key(int k, int down, uint32_t ms)
{
    if (!down)
        return;
    switch (fi.state) {
    case FS_AIM:
        fish_cast(k, ms);
        break;
    case FS_WAIT:
        if (fi.dip_ms && (int32_t)(ms - fi.dip_ms) >= -30) {   /* the float is under: the strike */
            if (k == fi.fish[fi.taker].key) {
                fish_hook(ms);
            } else {
                char t[16] = "IT WAS ";
                note_text(fi_note(&fi.fish[fi.taker]), t + 7);
                snd_lock();
                bw_note(53 + k, 70, BW_DUD);
                snd_unlock();
                fish_away("WRONG NOTE", t, "WRONG NOTE", ms);
            }
        } else if (fi.arrived) {         /* nibbling: too soon */
            snd_lock();
            bw_drum(BW_BLOCK_LO, 60);
            snd_unlock();
            fish_away("TOO EARLY", "IT SWAM OFF", "TOO EARLY", ms);
        } else {                         /* nothing at the bait: in, and out again here */
            cast_over(ms);
            if (fi.state == FS_AIM)
                fish_cast(k, ms);
        }
        break;
    case FS_REEL:
        reel_tap(ms);
        break;
    default:                             /* standing: the keys play */
        snd_lock();
        bw_note(53 + k, 90, BW_PLUCK);
        snd_unlock();
    }
}

static void fish_step(uint32_t now)
{
    float dt = (float)(now - fi.last_ms) * 0.001f;
    fi.last_ms = now;
    if (dt > 0.05f)
        dt = 0.05f;
    fish_swim(now, dt);
    if (fi.state != FS_REEL && now - fi.t0 >= FI_ROUND_MS) {
        fi.ended_by_time = 1;
        fish_end(now);
        return;
    }
    if (fi.state == FS_WAIT) {
        if (!fi.plopped && now - fi.cast_ms >= 250u) {   /* the float lands */
            fi.plopped = 1;
            snd_lock();
            bw_drum(BW_BLOCK_LO, 45);
            snd_unlock();
        }
        if (fi.taker < 0 && (int32_t)(now - fi.look_ms) >= 0) {   /* a fish near the bait comes to it */
            int i, best = -1;
            float fx = (float)key_x(fi.col), bd = fi.level == 1 ? 80.0f : fi.level == 2 ? 70.0f : 60.0f;
            for (i = 0; i < fish_count(); i++) {
                float d = fi.fish[i].x - fx;
                d = d < 0 ? -d : d;
                if (fi.fish[i].on && !fi.fish[i].flee && d < bd) {
                    bd = d;
                    best = i;
                }
            }
            fi.taker = best;
            fi.arrived = 0;
            fi.look_ms = now + 1500u;
        }
        if (fi.taker >= 0 && !fi.arrived && fi.fish[fi.taker].x - (float)key_x(fi.col) <= 1.0f &&
            (float)key_x(fi.col) - fi.fish[fi.taker].x <= 1.0f)
            fish_bite_plan(now);
        if (fi.arrived && !fi.dip_ms && (int32_t)(now - fi.look_ms) >= 0) {   /* a fussy one swims off */
            fi.taker = -1;
            fi.arrived = 0;
            fi.nnib = 0;
            fi.look_ms = now + 1200u;
        }
        if (fi.dip_ms && (int32_t)(now - fi.dip_ms) > fi.window)
            fish_away("TOO LATE", "IT TOOK THE BAIT", "TOO LATE", now);
        else if (fi.taker < 0 && now - fi.cast_ms > 12000u) {
            fi_say("NOTHING BITING", "CAST OVER A FISH", "NOTHING BITING", G_PLANK2);
            cast_over(now);
        }
        return;
    }
    if (fi.state == FS_REEL) {
        int32_t p = (int32_t)fi.period;
        float decay = fi.level == 1 ? 40.0f : fi.level == 2 ? 32.0f : 26.0f;
        while ((int32_t)(now - fi.hook_ms) >= (fi.judged_n + 1) * p + p * 30 / 100) {   /* each tug, judged */
            uint32_t at = fi.hook_ms + (uint32_t)((fi.judged_n + 1) * p);
            int tended = fi.good_n > fi.judged_n ||
                         (fi.knob_ms && (int32_t)(fi.knob_ms - at) > -p * 70 / 100 && (int32_t)(fi.knob_ms - at) <= p * 30 / 100);
            fi.judged_n++;
            if (!tended)                 /* no tap with it: the fish runs */
                fi.dist += fi.step * (fi.level == 1 ? 0.85f : fi.level == 2 ? 1.0f : 1.2f);
            if (fi.fish[fi.hf].size == 2)   /* a big one pulls */
                fi.tension += fi.level == 3 ? 8.0f : 5.0f;
        }
        fi.tension -= decay * dt;
        if (fi.tension < 0.0f)
            fi.tension = 0.0f;
        if (fi.tension > 75.0f && !fi.warned) {
            fi.warned = 1;
            app_say("EASY", "THE LINE IS TIGHT");
        }
        if (fi.tension >= 100.0f)
            fish_off(1, now);
        else if (fi.dist <= 0.0f)
            fish_landed(now);
        else if (fi.dist >= (float)FI_LOST || now - fi.hook_ms > 25000u)
            fish_off(0, now);
    }
}

/* the tick: the bite's sounds and the tug, on time */
static void fish_tick(uint32_t ms)
{
    if (fi.state == FS_WAIT && fi.arrived) {
        if (fi.nib_done < fi.nnib && (int32_t)(ms - fi.nib_ms[fi.nib_done]) >= 0) {
            fi.nib_done++;
            bw_drum(BW_HAT, 22);         /* a nibble: the float knocks */
        }
        if (fi.dip_ms && !fi.dipped && (int32_t)(ms - fi.dip_ms) >= 0) {
            fi.dipped = 1;
            bw_note(fi.tug_note, 118, BW_PLUCK);   /* the plip: the fish's note */
        }
    } else if (fi.state == FS_REEL && (int32_t)(ms - (fi.hook_ms + (uint32_t)fi.pulse_n * fi.period)) >= 0) {
        fi.pulse_n++;
        bw_note(fi.tug_note, 80, BW_PLUCK);   /* the tug: the line twangs */
        bw_drum(BW_KICK, 45);
    }
}

static void fish_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (fi.state == FS_AIM || fi.state == FS_WAIT || fi.state == FS_REEL) {
        fi.state = FS_STAND;
        app_say("STOPPED", "");
    } else {
        fish_go();
    }
}

static void fish_enc(int role, int steps)
{
    char t[24];
    if (fi.state == FS_REEL) {           /* any knob reels: slowly, and it tightens the line if rushed */
        int a = steps < 0 ? -steps : steps;
        fi.dist -= fi.step * 0.15f * (float)a;
        fi.tension += 3.0f * (float)a;
        fi.knob_ms = plat_ms();
        snd_lock();
        bw_drum(BW_BLOCK_HI, 25);
        snd_unlock();
        return;
    }
    if (role != EN_K1 || fi.state == FS_AIM || fi.state == FS_WAIT)
        return;
    sav.more[SV_FISH].level = (uint8_t)clamp(sav.more[SV_FISH].level + steps, 1, FI_LEVELS);
    int_text(sav.more[SV_FISH].level, t);
    text_cat(t, ", ");
    text_cat(t, FI_LEVEL_NAME[sav.more[SV_FISH].level - 1]);
    app_readout("LEVEL", t);
    fi.state = FS_STAND;
    fi.level = sav.more[SV_FISH].level;
    save_dirty();
}

static void fish_enter(void)
{
    int i;
    sav.more[SV_FISH].level = (uint8_t)clamp(sav.more[SV_FISH].level, 1, FI_LEVELS);
    fi.level = sav.more[SV_FISH].level;
    fi.state = FS_STAND;
    fi.in_front = 1;
    fi.col = 13;
    fi.taker = fi.hf = -1;
    fi.last_ms = plat_ms();
    for (i = 0; i < FI_FISH; i++)        /* the pond, for the standing screen */
        fi.fish[i].on = 0;
    for (i = 0; i < fish_count(); i++) {
        fish_spawn(i);
        fi.fish[i].x = (float)key_x(fi.fish[i].key);
    }
}
static void fish_leave(void)
{
    fi.state = FS_STAND;
    fi.in_front = 0;
    snd_lock();
    bw_bells_damp();
    snd_unlock();
}

/* ------------------------------------------------------------------ drawing --- */
static void fish_shape(int32_t x, int32_t y, int size, int kind, int dir, uint16_t shade)   /* its middle at (x, y) */
{
    int32_t L = size == 0 ? 7 : size == 1 ? 10 : 14, H = size == 0 ? 3 : size == 1 ? 5 : 7, i;
    uint16_t c = mix(FI_BODY[kind & 3], shade, 70);
    for (i = -H; i <= H; i++) {          /* the body: an oval */
        int32_t w = (int32_t)((float)L * fm_sqrtf(1.0f - (float)(i * i) / (float)(H * H + 1)));
        cv_rect(x - w, y + i, 2 * w + 1, 1, c);
    }
    for (i = 0; i <= H + 1; i++)         /* the tail, behind it */
        cv_rect(x - dir * (L + i), y - i * 3 / 4 - 1, 1, i * 3 / 2 + 2, c);
    if (kind == 2)                       /* a perch's stripes */
        for (i = -1; i <= 1; i++)
            cv_rect(x + i * L / 2, y - H + 1, 2, 2 * H - 1, mix(c, G_INK, 90));
    cv_rect(x - dir * L / 3 - 1, y - H / 2 - 1, 2 * L / 3 + 2, 1, mix(c, C_WHITE, 90));   /* a sheen */
    cv_rect(x + dir * (L - 3) - 1, y - 1, 2, 2, G_INK);   /* the eye */
}

static void fish_float(int32_t x, int32_t y)   /* the float: red over white, a stick on top */
{
    int32_t i;
    cv_rect(x, y - 8, 1, 4, G_RED2);
    for (i = -4; i <= 4; i++) {
        int32_t w = (int32_t)fm_sqrtf((float)(16 - i * i) + 0.5f);
        cv_rect(x - w, y + i, 2 * w + 1, 1, i < 0 ? G_RED : C_WHITE);
    }
}

static uint16_t fish_key_col(int k)      /* the key the float is on */
{
    return (fi.state == FS_WAIT || fi.state == FS_REEL) && k == fi.col ? RGB(120, 200, 230) : 0;
}

static void fish_draw(uint32_t now)
{
    int32_t y0, i, y, fx = key_x(fi.col);
    int names = fi.level == 1 && fi.state != FS_STAND;
    uint32_t tug_ago = fi.state == FS_REEL ? (now - fi.hook_ms) % fi.period : 9999u;
    int tug = fi.state == FS_REEL && now - fi.hook_ms >= fi.period && tug_ago < 90u;
    char s[40];
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, G_SKY1);
        /* the sky, the far sea, the pier's planks */
        for (y = 18; y < 46; y++)
            cv_rect(0, y, 240, 1, mix(G_SKY0, G_SKY1, (y - 18) * 256 / 28));
        fill_circle(196, 46, 11, G_SUN);
        cv_rect(0, 46, 240, 6, mix(G_SEA, G_SKY0, 90));
        for (i = 0; i < 240; i += 30)
            cv_rect(i + (int32_t)(now / 200u % 30u), 48, 12, 1, G_FOAM);
        cv_rect(0, 52, 240, 9, G_PLANK);
        for (i = 0; i < 240; i += 24)
            cv_rect(i, 52, 1, 9, G_PLANK2);
        cv_rect(0, 60, 240, 2, G_PLANK2);
        /* the pond */
        for (y = 62; y < FI_BED; y++)
            cv_rect(0, y, 240, 1, mix(RGB(96, 160, 196), RGB(28, 78, 118), (y - 62) * 256 / (FI_BED - 62)));
        for (i = 0; i < 240; i += 26)    /* glints on the water */
            cv_rect(i + (int32_t)((now / 90u + (uint32_t)i) % 26u), FI_SURF + 1 + (i / 26 & 1) * 3, 7, 1,
                    mix(G_FOAM, RGB(96, 160, 196), 120));
        for (i = 8; i < 240; i += 37) {  /* weed on the bed */
            int32_t sway = (int32_t)(fm_sinf((float)(now % 6283u) * 0.002f + (float)i) * 2.0f);
            cv_line(i, FI_BED, i + sway, FI_BED - 14 - (i % 5) * 2, RGB(50, 110, 90));
            cv_line(i + 3, FI_BED, i + 3 - sway, FI_BED - 9, RGB(50, 110, 90));
        }
        cv_rect(0, FI_BED, 240, 4, RGB(110, 90, 70));
        /* the fish */
        for (i = 0; i < fish_count(); i++) {
            fish_t *f = &fi.fish[i];
            int32_t fy = f->y + (int32_t)(fm_sinf((float)(now % 6283u) * 0.003f + (float)i * 1.7f) * 2.0f);
            if (!f->on || (fi.state == FS_REEL && i == fi.hf))
                continue;
            fish_shape((int32_t)f->x, fy, f->size, f->kind, f->dir, RGB(40, 90, 130));
            if (names && !f->flee) {
                note_text(fi_note(f), s);
                text_mid((int32_t)f->x, fy - 18, &FONT_XS, s, G_PAPER);
            }
        }
        /* the rod, the line, the float (or the fish on it) */
        if (fi.state != FS_STAND) {
            int32_t tip = FI_TIP + (tug ? 4 : 0), fly = FI_SURF;
            uint16_t lc = tug ? G_PAPER : RGB(70, 60, 70);
            cv_line(fx + 34, 60, fx + 2, tip, G_WHEEL);
            cv_line(fx + 35, 60, fx + 3, tip, G_WHEEL);
            if (fi.state == FS_REEL) {
                fish_t *f = &fi.fish[fi.hf];
                float d = fi.dist < 0.0f ? 0.0f : fi.dist;
                int32_t hy = clamp(FI_SURF + 12 + (int32_t)(d * 0.8f), FI_SURF + 10, FI_BED - 10);
                int32_t hx = fx + (int32_t)(fm_sinf((float)(now % 6283u) * 0.005f) * 16.0f * (d + 20.0f) / 120.0f);
                cv_line(fx + 2, tip, hx, hy, lc);
                fish_shape(hx, hy, f->size, f->kind, hx > fx ? 1 : -1, RGB(40, 90, 130));
            } else {
                int32_t wob = 0;
                if (fi.state == FS_WAIT) {
                    uint32_t ago = now - fi.cast_ms;
                    if (ago < 250u)      /* the float on its way down */
                        fly = tip + (FI_SURF - tip) * (int32_t)ago / 250;
                    for (i = 0; i < fi.nnib && fi.arrived; i++)
                        if ((int32_t)(now - fi.nib_ms[i]) >= 0 && now - fi.nib_ms[i] < 160u)
                            wob = 2;
                    if (fi.dip_ms && (int32_t)(now - fi.dip_ms) >= 0)
                        fly = FI_SURF + 7;   /* under */
                } else {
                    fly = tip + 14;      /* hanging, ready */
                }
                cv_line(fx + 2, tip, fx, fly - 8 + wob, lc);
                fish_float(fx, fly + wob);
                if (fi.state == FS_WAIT && fly >= FI_SURF) {   /* the rings it makes */
                    uint32_t ago = fi.dip_ms && (int32_t)(now - fi.dip_ms) >= 0 ? now - fi.dip_ms : now - fi.cast_ms - 250u;
                    int32_t r = 6 + (int32_t)(ago / 40u);
                    if (ago < 700u) {
                        cv_rect(fx - r - 6, FI_SURF + 1, 6, 1, G_FOAM);
                        cv_rect(fx + r + 1, FI_SURF + 1, 6, 1, G_FOAM);
                    }
                }
            }
        }
        /* the header: the name, the casts and the clock */
        cv_rect(0, 0, 240, 18, RGB(50, 130, 170));
        cv_text(6, 3, &FONT_XS, "FISHING", G_PAPER);
        if (fi.state != FS_STAND) {
            uint32_t el = (fi.state == FS_OVER ? fi.stop_ms : now) - fi.t0, left;
            el = el > FI_ROUND_MS ? FI_ROUND_MS : el;
            left = (FI_ROUND_MS - el + 999u) / 1000u;
            text_cat((s[0] = 0, s), "CAST ");
            int_text(fi.casts, s + text_len(s));
            text_cat(s, "/10   ");
            int_text((int32_t)(left / 60u), s + text_len(s));
            text_cat(s, left % 60u < 10u ? ":0" : ":");
            int_text((int32_t)(left % 60u), s + text_len(s));
            cv_text(234 - text_w(&FONT_XS, s), 3, &FONT_XS, s, G_PAPER);
            /* the bucket on the pier: it fills, and the catch beside it */
            {
                int32_t fill = clamp(fi.weight * 26 / 150, 0, 26), bx = 8, by = 24;
                for (i = 0; i < 28; i++)  /* tapering down */
                    cv_rect(bx + i / 7, by + i, 24 - 2 * (i / 7), 1, i >= 28 - fill ? G_TEAL : RGB(150, 160, 170));
                cv_rect(bx - 1, by, 26, 2, RGB(110, 120, 130));
                if (now - fi.landed_ms < 600u && fi.landed_ms)
                    fish_shape(bx + 12, by - 2 - (int32_t)((now - fi.landed_ms) / 60u), 0, 0, 1, G_SKY1);
                s[0] = 0;
                wt_text(fi.weight, s);
                text_cat(s, " KG");
                cv_text(40, 22, &FONT_B, s, G_INK);
                for (i = 0; i < fi.catches && i < FI_CASTS; i++)
                    fish_shape(48 + i * 15, 46, 0, fi.caught_kind[i], 1, G_SKY1);
            }
        }
        if (fi.state == FS_REEL) {       /* the line's tension, and the tug's light */
            int32_t h = clamp((int32_t)fi.tension, 0, 100) * 110 / 100;
            uint16_t c = fi.tension < 50.0f ? G_TEAL : fi.tension < 75.0f ? G_ORANGE : G_RED;
            round_rect(222, 76, 12, 116, 4, RGB(20, 50, 80));
            cv_rect(225, 189 - h, 6, h, c);
            fill_circle(228, 68, 5, tug ? G_PAPER : RGB(20, 50, 80));
            text_mid(120, 72, &FONT_XS, "TAP WITH THE TUG", G_PAPER);
        }
        if (fi.state == FS_AIM && fi.casts == 0)
            text_mid(120, 72, &FONT_XS, "PRESS A KEY TO CAST THERE", G_PAPER);
        if (fi.msg_ms && now - fi.msg_ms < 1400u && fi.state != FS_STAND && fi.state != FS_OVER) {
            int32_t w = text_w(&FONT_B, fi.msg) + 20;
            round_rect(120 - w / 2, 96, w, 26, 8, G_PAPER);
            text_mid(120, 99, &FONT_B, fi.msg, fi.msg_col);
        }
        if (fi.state == FS_STAND) {
            round_rect(6, 24, 228, 120, 8, G_PAPER);
            cv_text(14, 32, &FONT_XS, "K1 LEVEL", G_PLANK2);
            int_text(fi.level, s);
            text_cat(s, " ");
            text_cat(s, FI_LEVEL_NAME[fi.level - 1]);
            cv_text(94, 30, &FONT_B, s, G_INK);
            cv_text(14, 58, &FONT_XS, "PRESS A KEY TO CAST YOUR LINE THERE.", G_PLANK2);
            cv_text(14, 72, &FONT_XS, "WHEN THE FLOAT DIPS, PRESS ITS NOTE.", G_PLANK2);
            cv_text(14, 86, &FONT_XS, "TAP IN TIME WITH THE TUG TO REEL IN.", G_PLANK2);
            text_cat((s[0] = 0, s), "10 CASTS, 90 SECONDS");
            if (sav.more[SV_FISH].best) {
                text_cat(s, ". BEST ");
                wt_text(sav.more[SV_FISH].best, s);
                text_cat(s, " KG");
            }
            cv_text(14, 116, &FONT_XS, s, G_INK);
        }
        if (fi.state == FS_OVER) {
            round_rect(40, 80, 160, 76, 8, G_PAPER);
            text_mid(120, 86, &FONT_B, fi.ended_by_time ? "TIME'S UP" : "LAST CAST", G_RED);
            s[0] = 0;
            wt_text(fi.weight, s);
            text_cat(s, " KG, ");
            int_text(fi.catches, s + text_len(s));
            text_cat(s, " FISH");
            text_mid(120, 108, &FONT_B, s, G_INK);
            text_mid(120, 134, &FONT_XS, fi.new_best ? "NEW BEST!" : "", G_RED);
        }
        keyboard(198, 20, fish_key_col);
        cv_rect(0, 220, 240, 20, G_LILAC);
        {
            const char *f = fi.state == FS_STAND ? "PLAY: START" : fi.state == FS_OVER ? "PLAY: AGAIN" : "PLAY: STOP";
            cv_text(232 - text_w(&FONT_XS, f), 224, &FONT_XS, f, G_INK);
        }
        band_send(y0);
    }
}

static void fish_frame(uint32_t now)
{
    if (fi.state == FS_AIM || fi.state == FS_WAIT || fi.state == FS_REEL) {
        fish_step(now);
    } else {                             /* standing, or over: the pond swims on */
        float dt = (float)(now - fi.last_ms) * 0.001f;
        fi.last_ms = now;
        fish_swim(now, dt > 0.05f ? 0.05f : dt);
    }
    fish_draw(now);
}

static void fish_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    int32_t px = x + 6 + (int32_t)(ms / 40u % (uint32_t)(w - 12)), fy = y + h / 3 + (int32_t)(ms / 500u % 2u);
    cv_rect(x, y + h / 3, w, h - h / 3, RGB(50, 110, 150));
    cv_rect(x + w / 2, fy - 4, 1, 3, G_RED);
    cv_rect(x + w / 2 - 2, fy - 1, 5, 2, G_RED);
    cv_rect(x + w / 2 - 2, fy + 1, 5, 2, C_WHITE);
    cv_rect(px - 5, y + 2 * h / 3, 10, 4, G_ORANGE);
    cv_rect(px - 8, y + 2 * h / 3, 3, 4, G_ORANGE);
}

static void fish_best(char *t)
{
    t[0] = 0;
    if (sav.more[SV_FISH].best) {
        text_cat(t, "BEST ");
        wt_text(sav.more[SV_FISH].best, t);
        text_cat(t, " KG");
    }
}

static uint32_t fish_keys_lit(uint32_t *mid)   /* the float's key glows (where it is, not where to go); each tug
                                                 * flashes them all; a catch lights them */
{
    uint32_t now = plat_ms();
    *mid = fi.state == FS_WAIT ? 1u << fi.col : 0u;
    if (fi.state == FS_REEL && now - fi.hook_ms >= fi.period && (now - fi.hook_ms) % fi.period < 90u)
        *mid = (1u << NKEYS) - 1u;
    return fi.landed_ms && now - fi.landed_ms < 500u && (now / 90u & 1u) ? (1u << NKEYS) - 1u : 0u;
}

static void fish_where(char *t)
{
    t[0] = 0;
    if (fi.state == FS_STAND) {
        text_cat(t, "FISHING. KNOB 1, LEVEL ");
        sp_num(t, sav.more[SV_FISH].level);
        text_cat(t, ", ");
        text_cat(t, FI_LEVEL_NAME[clamp(sav.more[SV_FISH].level, 1, FI_LEVELS) - 1]);
        text_cat(t, ". PRESS A KEY TO CAST THERE. WHEN THE FLOAT DIPS, PRESS THE NOTE YOU HEAR. TAP IN TIME WITH THE "
                    "TUG TO REEL IN. ");
        if (sav.more[SV_FISH].best) {
            text_cat(t, "BEST ");
            wt_text(sav.more[SV_FISH].best, t);
            text_cat(t, " KILOS. ");
        }
        text_cat(t, "PLAY STARTS.");
        return;
    }
    text_cat(t, "FISHING. ");
    if (fi.state == FS_OVER) {
        text_cat(t, fi.ended_by_time ? "TIME'S UP. " : "LAST CAST. ");
        wt_text(fi.weight, t);
        text_cat(t, " KILOS, ");
        sp_num(t, fi.catches);
        text_cat(t, fi.new_best ? " FISH. NEW BEST. PLAY STARTS AGAIN." : " FISH. PLAY STARTS AGAIN.");
        return;
    }
    if (fi.state == FS_REEL) {
        text_cat(t, "HOOKED, TAP IN TIME WITH THE TUG. LINE TENSION ");
        sp_pct(t, clamp((int32_t)fi.tension, 0, 100));
        text_cat(t, ". ");
    } else if (fi.state == FS_WAIT) {
        text_cat(t, fi.dip_ms && (int32_t)(plat_ms() - fi.dip_ms) >= 0 ? "A BITE, PRESS ITS NOTE. " :
                                                                         "WAITING FOR A BITE AT ");
        if (!(fi.dip_ms && (int32_t)(plat_ms() - fi.dip_ms) >= 0)) {
            note_text(53 + fi.col, t + text_len(t));
            text_cat(t, ". ");
        }
    } else {
        text_cat(t, "PRESS A KEY TO CAST. ");
    }
    sp_num(t, fi.casts);
    text_cat(t, " OF 10 CASTS, ");
    {
        uint32_t el = plat_ms() - fi.t0;
        sp_num(t, (int32_t)(el > FI_ROUND_MS ? 0u : (FI_ROUND_MS - el + 999u) / 1000u));
    }
    text_cat(t, " SECONDS LEFT. ");
    wt_text(fi.weight, t);
    text_cat(t, " KILOS. PLAY STOPS.");
}

static int fish_playing(void) { return fi.state == FS_AIM || fi.state == FS_WAIT || fi.state == FS_REEL; }

static const uint8_t FISH_TUNE[] = {67, 0, 72, 74, 76, 0, 74, 72, 69, 0, 67, 0, 0, 0};

#ifdef PLAT_HOST
/* the host simulator: the game's own script commands (1 = handled) and checks (1 = known), a call every
 * simulated millisecond (a test player), and its lines for the speakdump (sp_out) */
static int host_autofish;                /* 1 a good angler, 2 frantic on the reel, 3 slow on the reel, 4 strikes early */
static int fish_host_cmd(const char *cmd, const char *a)
{
    if (strcmp(cmd, "autofish"))
        return 0;
    host_autofish = !strcmp(a, "on") ? 1 : !strcmp(a, "frantic") ? 2 : !strcmp(a, "slow") ? 3 :
                    !strcmp(a, "early") ? 4 : 0;
    return 1;
}
static int fish_expect(const char *what, int *got)
{
    if (!strcmp(what, "fish_state"))     /* 0 standing, 1 aiming, 2 waiting, 3 reeling, 4 over */
        *got = fi.state;
    else if (!strcmp(what, "fish_catches"))
        *got = fi.catches;
    else if (!strcmp(what, "fish_weight"))   /* tenths of a kilo */
        *got = fi.weight;
    else if (!strcmp(what, "fish_casts"))
        *got = fi.casts;
    else if (!strcmp(what, "fish_snaps"))
        *got = fi.snaps;
    else if (!strcmp(what, "fish_lost"))
        *got = fi.lost;
    else if (!strcmp(what, "fish_best"))
        *got = sav.more[SV_FISH].best;
    else if (!strcmp(what, "fish_level"))
        *got = sav.more[SV_FISH].level;
    else
        return 0;
    return 1;
}
static void fish_host_ms(uint32_t now)
{
    if (!host_autofish || !fi.in_front)
        return;
    if (fi.state == FS_AIM && now % 200u == 0u) {   /* cast on the fish nearest the middle */
        int i, best = -1, bd = 999;
        for (i = 0; i < fish_count(); i++) {
            int d = (int)fi.fish[i].x - 120;
            d = d < 0 ? -d : d;
            if (fi.fish[i].on && !fi.fish[i].flee && fi.fish[i].x > 10.0f && fi.fish[i].x < 230.0f && d < bd) {
                bd = d;
                best = i;
            }
        }
        if (best >= 0)
            fish_key(fi.fish[best].key, 1, now);
    } else if (fi.state == FS_WAIT) {
        if (host_autofish == 4 && fi.arrived && fi.nib_done > 0 && !fi.dipped)
            fish_key(fi.fish[fi.taker].key, 1, now);   /* at the first nibble */
        else if (fi.dip_ms && now == fi.dip_ms + 180u)   /* a quick ear */
            fish_key(fi.fish[fi.taker].key, 1, now);
    } else if (fi.state == FS_REEL && now - fi.hook_ms >= fi.period) {
        uint32_t ph = (now - fi.hook_ms) % fi.period, n = (now - fi.hook_ms) / fi.period;
        if (host_autofish == 2 ? now % 140u == 0u : host_autofish == 3 ? ph == 10u && n % 3u == 0u : ph == 10u)
            fish_key(fi.col, 1, now);
    }
}
static void fish_speak_dump(void)
{
    static const char *const MSG[] = {
        "TEN CASTS, PRESS A KEY TO CAST THERE", "HOOKED, REEL IN TIME WITH THE TUG", "WRONG NOTE, IT WAS C#4",
        "TOO EARLY, IT SWAM OFF", "TOO LATE, IT TOOK THE BAIT", "NOTHING BITING, CAST OVER A FISH",
        "LANDED, 1.4 KILOS", "LANDED A BIG ONE, 3.2 KILOS", "SNAP, THE LINE BROKE, TAP STEADIER",
        "IT GOT AWAY, TAP WITH EVERY TUG", "EASY, THE LINE IS TIGHT", "TIME'S UP, 7.4 KILOS, 4 FISH",
        "TIME'S UP. NEW BEST, 12.1 KILOS, 6 FISH", "LAST CAST, 0.0 KILOS, 0 FISH",
        "LAST CAST. NEW BEST, 9.8 KILOS, 5 FISH", "STOPPED"};
    fish_t keep = fi.fish[0];
    int i, keep_state = fi.state;
    char lv[24];
    for (i = 1; i <= FI_LEVELS; i++) {   /* each level standing */
        sav.more[SV_FISH].level = (uint8_t)i;
        fi.state = FS_STAND;
        fish_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        lv[0] = 0;
        int_text(i, lv);
        text_cat(lv, ", ");
        text_cat(lv, FI_LEVEL_NAME[i - 1]);
        sp_join("LEVEL", lv);
        sp_out("READOUT", sp_buf);
    }
    sav.more[SV_FISH].best = 74;
    fish_where(sp_buf);
    sp_out("SCREEN", sp_buf);
    fi.casts = 3;
    fi.weight = 31;
    fi.catches = 2;
    fi.t0 = plat_ms();
    fi.col = 12;
    fi.fish[0].key = 9;
    fi.taker = 0;
    fi.tension = 40.0f;
    for (i = FS_AIM; i <= FS_OVER; i++) {
        fi.state = i;
        fi.dip_ms = 0;
        fish_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        if (i == FS_WAIT) {              /* the float under */
            fi.dip_ms = plat_ms() ? plat_ms() : 1u;
            fish_where(sp_buf);
            sp_out("SCREEN", sp_buf);
            fi.dip_ms = 0;
        }
    }
    fi.new_best = 1;
    fi.ended_by_time = 1;
    fish_where(sp_buf);
    sp_out("SCREEN", sp_buf);
    fi.new_best = fi.ended_by_time = 0;
    for (i = 0; i < (int)(sizeof MSG / sizeof MSG[0]); i++)
        sp_out("MSG", MSG[i]);
    fi.fish[0] = keep;
    fi.state = keep_state;
    fi.taker = -1;
}
#endif

static const game_t GAME_FISH = {"FISHING", RGB(50, 130, 170), fish_enter, fish_leave, fish_frame, fish_key,
                                 fish_button, fish_enc, fish_tick, fish_attract, fish_best, fish_keys_lit, FISH_TUNE,
                                 sizeof FISH_TUNE, 190, BW_PLUCK, fish_where, 0, fish_playing, "HOOK A BIG ONE", 0};
