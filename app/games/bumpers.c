/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Bumpers: the dodgems, seen from above. Your car drives on by itself; the white keys steer it, the left half left
 * and the right half right, sharper the further from the middle; a black key honks. The other cars each sing a note
 * of a chord, written on them: bump one and it sounds. Bump them in rising order and each bump of the run scores
 * its place in it (1, 2, 3...); the chord's root, third and fifth within 2 s is a CHORD, 5 more. A bump from behind
 * costs a point and spins you round. Sixty seconds a session; the best score is kept.
 *
 * Levels (K1): 1 four slow cars; 2 five faster ones, and now and then one comes after you; 3 as 2, and the cars
 * dodge when you drive at them. SELECT turns the wheel too. PLAY starts and stops. Its saved level and best are
 * sav.more[SV_BUMPERS]. Part of the unity build. */
#include "game.h"

#define BP_CAB RGB(240, 150, 40)
#define BP_CARS 6                        /* yours (0) and up to five others */
#define BP_R 12                          /* a car, for bumping */
#define BP_L 5                           /* the rink's rail, inside */
#define BP_RT 235
#define BP_T 31
#define BP_B 217
#define BP_ROUND 60000                   /* ms a session */
#define BP_STEP 5                        /* ms a physics step */
#define BP_LEVELS 3
#define BP_RUN_GAP 4000u                 /* a rising run: the next bump within this */
#define BP_CHORD_GAP 2000u               /* a chord: root, third and fifth within this */
#define BP_FANS 8

enum { BS_STAND, BS_PLAY, BS_OVER };

typedef struct {
    float x, y, vx, vy, hx, hy;          /* where, how it moves, which way it faces (a unit vector) */
    float turn;                          /* the others: how they wander, rad/s */
    uint32_t turn_ms;                    /* the next change of mind */
    uint32_t cool;                       /* a bump with you counts again after this */
    uint8_t note, role;                  /* its note; root 0, third 1, fifth 2 */
} car_t;

static struct {
    int state, level, ncars, score, bumps, combos, behind, new_best;
    car_t car[BP_CARS];
    uint32_t t0, last_ms, sim;           /* the session's start; the physics' clock (sim, ms) */
    float spd, ai_spd;
    int steer;                           /* the white key steering, -1 none */
    uint32_t held;                       /* keys held */
    uint32_t spin_until;                 /* spun round: the wheel does nothing until then */
    int chord;                           /* BP_CHORD's */
    int last_note, run;                  /* the rising run */
    uint32_t last_bump, role_ms[3];
    int bully;                           /* a car after you (levels 2, 3), -1 none */
    uint32_t bully_until, bully_next, thud_ms;
    struct {
        uint8_t note;
        uint32_t at;
    } fan[BP_FANS];                      /* a fanfare's notes to come */
    struct {
        int16_t x, y;
        uint32_t ms;
    } burst[6];                          /* sparks where cars met */
    int nburst;
    char pop[12], big[12];               /* "+3" over your car; "CHORD!" across the rink */
    uint32_t pop_ms, big_ms, flash_ms;
    int16_t pop_x, pop_y;
    int ten_said;
} bp;

static const char *const BP_LEVEL_NAME[BP_LEVELS] = {"SLOW", "BUSY", "TRICKY"};

/* the chords the cars play: root (as a MIDI note in the fourth octave) and minor or not */
static const struct {
    uint8_t root, minor;
    const char *name;
} BP_CHORD[6] = {{60, 0, "C MAJOR"}, {65, 0, "F MAJOR"}, {67, 0, "G MAJOR"},
                 {57, 1, "A MINOR"}, {62, 1, "D MINOR"}, {64, 1, "E MINOR"}};

/* the cars' colours, low note to high: a rising run goes round the rainbow */
static const uint16_t BP_COL[5] = {RGB(232, 80, 91), RGB(242, 163, 58), RGB(240, 210, 70), RGB(110, 190, 110),
                                   RGB(130, 170, 240)};

static void bp_say_level(void)
{
    char t[24];
    int_text(sav.more[SV_BUMPERS].level, t);
    text_cat(t, ", ");
    text_cat(t, BP_LEVEL_NAME[clamp(sav.more[SV_BUMPERS].level, 1, BP_LEVELS) - 1]);
    app_readout("LEVEL", t);
}

static float bp_norm(float *x, float *y)  /* a vector made one long; its length before */
{
    float d = fm_sqrtf(*x * *x + *y * *y);
    if (d > 0.0001f) {
        *x /= d;
        *y /= d;
    }
    return d;
}

static void bp_turn(car_t *c, float a)   /* turned a radians (small), clockwise on the screen */
{
    float hx = c->hx - c->hy * a, hy = c->hy + c->hx * a;
    c->hx = hx;
    c->hy = hy;
    bp_norm(&c->hx, &c->hy);
}

static int bp_free(float x, float y, int n)   /* room for a car here, clear of the first n */
{
    int i;
    for (i = 0; i < n; i++) {
        float dx = bp.car[i].x - x, dy = bp.car[i].y - y;
        if (dx * dx + dy * dy < (float)(4 * BP_R * BP_R) * 2.2f)
            return 0;
    }
    return 1;
}

static void bp_place(void)               /* the cars on the floor: yours low in the middle, facing up */
{
    int i, j;
    memset(bp.car, 0, sizeof bp.car);
    bp.level = clamp(sav.more[SV_BUMPERS].level, 1, BP_LEVELS);
    bp.ncars = bp.level == 1 ? 5 : 6;
    bp.car[0].x = 120.0f;
    bp.car[0].y = (float)(BP_B - 30);
    bp.car[0].hy = -1.0f;
    for (i = 1; i < bp.ncars; i++) {
        car_t *c = &bp.car[i];
        float a = (float)rnd(628) * 0.01f;
        for (j = 0; j < 40; j++) {
            c->x = (float)(BP_L + BP_R + 4 + (int)rnd(BP_RT - BP_L - 2 * BP_R - 8));
            c->y = (float)(BP_T + BP_R + 4 + (int)rnd(BP_B - BP_T - 2 * BP_R - 70));
            if (bp_free(c->x, c->y, i))
                break;
        }
        c->hx = fm_cosf(a);
        c->hy = fm_sinf(a);
    }
}

static void bp_line_up(void)             /* standing: the others in a row under the card, low note to high (the
                                          * colours run round the rainbow), yours below them */
{
    int i;
    for (i = 1; i < bp.ncars; i++) {
        bp.car[i].x = (float)(120 + (2 * i - bp.ncars) * 20);
        bp.car[i].y = 160.0f;
        bp.car[i].hx = 0.0f;
        bp.car[i].hy = 1.0f;
    }
    bp.car[0].x = 120.0f;
    bp.car[0].y = 198.0f;
    bp.car[0].hx = 0.0f;
    bp.car[0].hy = -1.0f;
}

static void bp_notes(void)               /* the chord's tones on the cars: root, third, fifth, root, third up */
{
    static const uint8_t ROLE[5] = {0, 1, 2, 0, 1};
    int i, r = BP_CHORD[bp.chord].root, m = BP_CHORD[bp.chord].minor;
    for (i = 1; i < bp.ncars; i++) {
        int t = ROLE[i - 1], up = i >= 4 ? 12 : 0;
        bp.car[i].role = (uint8_t)t;
        bp.car[i].note = (uint8_t)(r + up + (t == 0 ? 0 : t == 1 ? (m ? 3 : 4) : 7));
    }
}

static void bumpers_go(void)
{
    char t[24];
    uint32_t now = plat_ms();
    rnd_state ^= now * 2654435761u;
    bp_place();
    bp.chord = bp.level == 1 ? (int)rnd(3) : (int)rnd(6);   /* level 1: a major chord */
    bp_notes();
    bp.spd = bp.level == 1 ? 72.0f : 80.0f;
    bp.ai_spd = bp.level == 1 ? 38.0f : bp.level == 2 ? 58.0f : 62.0f;
    bp.score = bp.bumps = bp.combos = bp.behind = 0;
    bp.last_note = -1;
    bp.run = 0;
    bp.last_bump = 0;
    memset(bp.role_ms, 0, sizeof bp.role_ms);
    memset(bp.fan, 0, sizeof bp.fan);
    bp.nburst = 0;
    bp.pop_ms = bp.big_ms = bp.flash_ms = 0;
    bp.sim = 0;
    bp.spin_until = 0;
    bp.bully = -1;
    bp.bully_next = 7000u;
    bp.ten_said = 0;
    bp.t0 = bp.last_ms = now;
    bp.state = BS_PLAY;
    text_cat((t[0] = 0, t), "THE CARS PLAY ");
    text_cat(t, BP_CHORD[bp.chord].name);
    app_say("SIXTY SECONDS", t);
    snd_lock();                          /* the power comes on: a buzz from the grid */
    bw_note(36, 70, BW_SAW);
    bw_drum(BW_HAT, 90);
    snd_unlock();
}

static void bp_fanfare(const uint8_t *n, int count, uint32_t gap)   /* notes in turn, from the physics' clock */
{
    int i, j = 0;
    for (i = 0; i < BP_FANS && j < count; i++)
        if (!bp.fan[i].note) {
            bp.fan[i].note = n[j];
            bp.fan[i].at = bp.sim + (uint32_t)j * gap;
            j++;
        }
}

static void bp_burst(float x, float y, uint32_t now)
{
    int i = bp.nburst++ % 6;
    bp.burst[i].x = (int16_t)x;
    bp.burst[i].y = (int16_t)y;
    bp.burst[i].ms = now;
}

static void bp_pop(const char *s, uint32_t now)
{
    bp.pop[0] = 0;
    text_cat(bp.pop, s);
    bp.pop_ms = now;
    bp.pop_x = (int16_t)bp.car[0].x;
    bp.pop_y = (int16_t)bp.car[0].y;
}

static void bp_bump(int j, uint32_t now)   /* you bumped car j: its note, the run, the chord */
{
    car_t *c = &bp.car[j];
    char t[12], s[16];
    int pts, chord = 0, i;
    bp.bumps++;
    if (bp.last_note >= 0 && c->note > bp.last_note && bp.sim - bp.last_bump < BP_RUN_GAP)
        bp.run++;
    else
        bp.run = 1;
    bp.last_note = c->note;
    bp.last_bump = bp.sim;
    pts = bp.run;
    bp.role_ms[c->role] = bp.sim | 1u;   /* (never 0: 0 is "not yet") */
    for (i = 0; i < 3; i++)
        if (!bp.role_ms[i] || bp.sim - bp.role_ms[i] > BP_CHORD_GAP)
            break;
    if (i == 3) {
        chord = 1;
        memset(bp.role_ms, 0, sizeof bp.role_ms);
    }
    bp.score += pts + (chord ? 5 : 0);
    snd_lock();                          /* the thud, and the car's note */
    bw_drum(BW_BLOCK_LO, 100);
    bw_drum(BW_KICK, 60);
    bw_note(c->note, 110, BW_PIANO);
    snd_unlock();
    s[0] = '+';
    int_text(pts + (chord ? 5 : 0), s + 1);
    bp_pop(s, now);
    note_text(c->note, t);
    if (chord) {                         /* the chord rings out, low to high, then the top note again */
        uint8_t f[4];
        int r = BP_CHORD[bp.chord].root, m = BP_CHORD[bp.chord].minor;
        f[0] = (uint8_t)(r + 12);
        f[1] = (uint8_t)(r + 12 + (m ? 3 : 4));
        f[2] = (uint8_t)(r + 19);
        f[3] = (uint8_t)(r + 24);
        bp_fanfare(f, 4, 90);
        bp.combos++;
        bp.flash_ms = now;
        text_cat((bp.big[0] = 0, bp.big), "CHORD!");
        bp.big_ms = now;
        app_say("CHORD", "PLUS 5");
    } else if (bp.run >= 3) {            /* a run: a little fanfare up from its note */
        uint8_t f[3];
        f[0] = (uint8_t)(c->note + 12);
        f[1] = (uint8_t)(c->note + 16);
        f[2] = (uint8_t)(c->note + 19);
        bp_fanfare(f, 3, 80);
        if (bp.run == 3)
            bp.combos++;
        bp.flash_ms = now;
        text_cat((bp.big[0] = 0, bp.big), "RUN ");
        int_text(bp.run, bp.big + 4);
        text_cat(bp.big, "!");
        bp.big_ms = now;
        text_cat((s[0] = 0, s), "RUN ");
        int_text(bp.run, s + 4);
        app_say(t, s);
    } else {
        if (bp.run == 2) {
            text_cat((s[0] = 0, s), "RUN 2");
            app_say(t, s);
        } else {
            app_say(t, "");
        }
    }
}

static void bp_behind(uint32_t now)      /* bumped from behind: a point, and round you go */
{
    bp.behind++;
    if (bp.score > 0)
        bp.score--;
    bp.run = 0;
    bp.last_note = -1;
    bp.spin_until = bp.sim + 900u;
    bp_pop("-1", now);
    snd_lock();                          /* the thump, and a woozy slide down */
    bw_drum(BW_KICK, 110);
    bw_drum(BW_BLOCK_LO, 80);
    bw_note(55, 80, BW_SAW);
    snd_unlock();
    {
        static const uint8_t F[3] = {52, 49, 46};
        bp_fanfare(F, 3, 110);
    }
    app_say("BUMPED FROM BEHIND", "MINUS 1");
}

static void bp_steer_ai(int i)           /* the others: wander; a bully comes after you; at level 3, dodge */
{
    car_t *c = &bp.car[i], *u = &bp.car[0];
    float dx, dy, w = c->turn, spd = bp.ai_spd;
    int want = 0;
    if (bp.sim >= c->turn_ms) {          /* a change of mind */
        c->turn = (float)((int)rnd(300) - 150) * 0.01f;
        c->turn_ms = bp.sim + 500u + rnd(1200);
    }
    dx = 120.0f - c->x;                  /* the rail ahead: back towards the middle */
    dy = (float)(BP_T + BP_B) * 0.5f - c->y;
    if ((c->x < BP_L + 34 && c->hx < 0.0f) || (c->x > BP_RT - 34 && c->hx > 0.0f) ||
        (c->y < BP_T + 34 && c->hy < 0.0f) || (c->y > BP_B - 34 && c->hy > 0.0f))
        want = 1;
    if (i == bp.bully && bp.sim < bp.bully_until) {   /* after you */
        dx = u->x - c->x;
        dy = u->y - c->y;
        want = 1;
        spd *= 1.4f;
    } else if (bp.level == 3) {          /* you are coming at it: away, to its side of your line */
        float ox = c->x - u->x, oy = c->y - u->y, d = bp_norm(&ox, &oy);
        if (d < 100.0f && ox * u->hx + oy * u->hy > 0.6f) {
            float rx = -u->hy, ry = u->hx;
            float side = ox * rx + oy * ry;
            dx = side >= 0.0f ? rx : -rx;
            dy = side >= 0.0f ? ry : -ry;
            want = 1;
            spd *= 1.5f;
        }
    }
    if (want) {
        float cr, dt;
        bp_norm(&dx, &dy);
        cr = c->hx * dy - c->hy * dx;
        dt = c->hx * dx + c->hy * dy;
        w = fm_clampf(cr * 5.0f + (dt < 0.0f ? (cr >= 0.0f ? 2.5f : -2.5f) : 0.0f), -3.5f, 3.5f);
    }
    bp_turn(c, w * (float)BP_STEP * 0.001f);
    c->vx += (c->hx * spd - c->vx) * 0.0125f;
    c->vy += (c->hy * spd - c->vy) * 0.0125f;
}

static float bp_rate(int k)              /* a white key's turn, rad/s: - left, + right; 0 a black key */
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS;
    int i;
    for (i = 0; i < 16; i++)
        if (WK[i] == k)
            return i < 8 ? -(0.9f + (float)(8 - i) * 0.45f) : 0.9f + (float)(i - 7) * 0.45f;
    return 0.0f;
}

static void bp_physics(uint32_t now)     /* one fixed step: drive, bounce off the rail, bump */
{
    float dt = (float)BP_STEP * 0.001f;
    car_t *u = &bp.car[0];
    int i, j;
    bp.sim += BP_STEP;
    /* your car: the wheel (or a spin), and the motor */
    if (bp.sim < bp.spin_until) {
        bp_turn(u, 14.0f * dt);
        u->vx += (u->hx * 20.0f - u->vx) * 0.01f;
        u->vy += (u->hy * 20.0f - u->vy) * 0.01f;
    } else {
        if (bp.steer >= 0)
            bp_turn(u, bp_rate(bp.steer) * dt);
        u->vx += (u->hx * bp.spd - u->vx) * 0.0125f;
        u->vy += (u->hy * bp.spd - u->vy) * 0.0125f;
    }
    /* a bully now and then (levels 2, 3) */
    if (bp.level >= 2 && bp.sim >= bp.bully_next) {
        bp.bully = 1 + (int)rnd((uint32_t)(bp.ncars - 1));
        bp.bully_until = bp.sim + 3500u;
        bp.bully_next = bp.sim + 5000u + rnd(3000);
    }
    for (i = 1; i < bp.ncars; i++)
        bp_steer_ai(i);
    for (i = 0; i < bp.ncars; i++) {     /* move, and the rail: bounced back, turned off it */
        car_t *c = &bp.car[i];
        c->x += c->vx * dt;
        c->y += c->vy * dt;
        if (c->x < BP_L + BP_R) {
            c->x = BP_L + BP_R;
            if (c->vx < 0.0f)
                c->vx = -c->vx * 0.7f;
            if (c->hx < 0.0f)
                c->hx = -c->hx;
        } else if (c->x > BP_RT - BP_R) {
            c->x = BP_RT - BP_R;
            if (c->vx > 0.0f)
                c->vx = -c->vx * 0.7f;
            if (c->hx > 0.0f)
                c->hx = -c->hx;
        }
        if (c->y < BP_T + BP_R) {
            c->y = BP_T + BP_R;
            if (c->vy < 0.0f)
                c->vy = -c->vy * 0.7f;
            if (c->hy < 0.0f)
                c->hy = -c->hy;
        } else if (c->y > BP_B - BP_R) {
            c->y = BP_B - BP_R;
            if (c->vy > 0.0f)
                c->vy = -c->vy * 0.7f;
            if (c->hy > 0.0f)
                c->hy = -c->hy;
        }
    }
    for (i = 0; i < bp.ncars; i++)       /* the cars against each other: circles, equal weights */
        for (j = i + 1; j < bp.ncars; j++) {
            car_t *a = &bp.car[i], *b = &bp.car[j];
            float nx = b->x - a->x, ny = b->y - a->y, d = bp_norm(&nx, &ny), vn, over, ua, ub;
            if (d >= 2.0f * BP_R || d < 0.0001f)
                continue;
            over = (2.0f * BP_R - d) * 0.5f;   /* apart */
            a->x -= nx * over;
            a->y -= ny * over;
            b->x += nx * over;
            b->y += ny * over;
            vn = (b->vx - a->vx) * nx + (b->vy - a->vy) * ny;
            if (vn >= 0.0f)              /* already parting */
                continue;
            ua = a->vx * nx + a->vy * ny;   /* who drove into whom */
            ub = -(b->vx * nx + b->vy * ny);
            a->vx += nx * vn * 0.95f;    /* (1 + e) / 2, e 0.9 */
            a->vy += ny * vn * 0.95f;
            b->vx -= nx * vn * 0.95f;
            b->vy -= ny * vn * 0.95f;
            for (int k = 0; k < 2; k++) {   /* the others glance off where they were knocked */
                car_t *c = k ? b : a;
                float hx = c->vx, hy = c->vy;
                if (c != u && bp_norm(&hx, &hy) > 10.0f) {
                    c->hx = hx;
                    c->hy = hy;
                }
            }
            if (i == 0 && -vn > 18.0f && bp.sim >= b->cool) {   /* you and car j: a car sings once, then not
                                                                    * again for 1.5 s (no pushing one along) */
                float front = u->hx * nx + u->hy * ny;
                b->cool = bp.sim + 1500u;
                bp_burst(a->x + nx * BP_R, a->y + ny * BP_R, now);
                if (front < -0.4f && bp.sim >= bp.spin_until) {
                    bp_behind(now);
                } else if (front >= -0.4f && ua >= ub * 0.8f) {   /* (a car into your side: just a knock) */
                    b->vx += nx * 45.0f; /* knocked away, and you back a little */
                    b->vy += ny * 45.0f;
                    a->vx -= nx * 20.0f;
                    a->vy -= ny * 20.0f;
                    bp_bump(j, now);
                } else {
                    snd_lock();
                    bw_drum(BW_BLOCK_LO, 70);
                    snd_unlock();
                }
            } else if (-vn > 25.0f && bp.sim - bp.thud_ms > 150u) {   /* the others, or a car just bumped: a dull thud */
                bp.thud_ms = bp.sim;
                bp_burst(a->x + nx * BP_R, a->y + ny * BP_R, now);
                snd_lock();
                bw_drum(BW_BLOCK_LO, 45);
                snd_unlock();
            }
        }
    for (i = 0; i < BP_FANS; i++)        /* the fanfares' notes, on time */
        if (bp.fan[i].note && bp.sim >= bp.fan[i].at) {
            snd_lock();
            bw_note(bp.fan[i].note, 85, BW_CHIME);
            snd_unlock();
            bp.fan[i].note = 0;
        }
}

static void bp_end(void)
{
    char t[16];
    bp.state = BS_OVER;
    bp.new_best = bp.score > sav.more[SV_BUMPERS].best;
    if (bp.new_best) {
        sav.more[SV_BUMPERS].best = (uint16_t)bp.score;
        save_dirty();
    }
    memset(bp.fan, 0, sizeof bp.fan);
    snd_lock();                          /* the power goes off: a buzz down, a bell (a new best: a chord) */
    bw_note(43, 70, BW_SAW);
    bw_note(72, 90, BW_CHIME);
    bw_note(79, 80, BW_CHIME);
    if (bp.new_best) {
        bw_note(76, 85, BW_CHIME);
        bw_note(84, 90, BW_CHIME);
    }
    snd_unlock();
    text_cat((t[0] = 0, t), "SCORE ");
    int_text(bp.score, t + 6);
    app_say(bp.new_best ? "TIME'S UP. NEW BEST" : "TIME'S UP", t);
    {                                    /* tickets: two thirds of the score, x1 .. x1.67 by level; 2 to 100 */
        int n = bp.score * (1 + bp.level) / 3;
        tickets_win(n < 2 ? 2 : n > 100 ? 100 : n);
    }
}

static void bumpers_key(int k, int down, uint32_t ms)
{
    (void)ms;
    if (down)
        bp.held |= 1u << k;
    else
        bp.held &= ~(1u << k);
    if (bp.state != BS_PLAY) {           /* standing: the keys play */
        if (down) {
            snd_lock();
            bw_note(53 + k, 90, BW_PIANO);
            snd_unlock();
        }
        return;
    }
    if (bp_rate(k) == 0.0f) {            /* a black key: the horn */
        if (down) {
            snd_lock();
            bw_note(53 + k, 95, BW_SAW);
            bw_note(53 + k - 5, 70, BW_SAW);
            snd_unlock();
        }
        return;
    }
    if (down) {
        bp.steer = k;
    } else if (k == bp.steer) {          /* let go: the sharpest of the white keys still held, or straight on */
        int i;
        float best = 0.0f;
        bp.steer = -1;
        for (i = 0; i < NKEYS; i++)
            if ((bp.held >> i & 1u) && fm_fabsf(bp_rate(i)) > best) {
                best = fm_fabsf(bp_rate(i));
                bp.steer = i;
            }
    }
}

static void bumpers_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (bp.state == BS_PLAY) {
        bp.state = BS_STAND;
        memset(bp.fan, 0, sizeof bp.fan);
        bp_line_up();
        app_say("STOPPED", "");
    } else {
        bumpers_go();
    }
}

static void bumpers_enc(int role, int steps)
{
    if (bp.state == BS_PLAY) {           /* SELECT turns the wheel too: a nudge a detent */
        int i;
        if (role == EN_SELECT && bp.sim >= bp.spin_until)
            for (i = 0; i < 3; i++)
                bp_turn(&bp.car[0], (float)clamp(steps, -4, 4) * 0.1f);
        return;
    }
    if (role != EN_K1)
        return;
    sav.more[SV_BUMPERS].level = (uint8_t)clamp(sav.more[SV_BUMPERS].level + steps, 1, BP_LEVELS);
    bp_say_level();
    bp.state = BS_STAND;
    bp_place();
    bp_line_up();
    bp.chord = 0;
    bp_notes();
    save_dirty();
}

static void bumpers_enter(void)
{
    bp.state = BS_STAND;
    bp.steer = -1;
    bp.held = 0;
    bp.chord = 0;
    bp_place();
    bp_line_up();
    bp_notes();
}
static void bumpers_leave(void)
{
    bp.state = BS_STAND;
    memset(bp.fan, 0, sizeof bp.fan);
}

/* ------------------------------------------------------------- drawing --- */
static void bp_car(const car_t *c, uint16_t body, int you, const char *label, uint32_t now)
{
    int32_t x = (int32_t)c->x, y = (int32_t)c->y;
    int32_t fx = x + (int32_t)(c->hx * 7.0f), fy = y + (int32_t)(c->hy * 7.0f);
    int32_t bx = x - (int32_t)(c->hx * 6.0f), by = y - (int32_t)(c->hy * 6.0f);
    fill_circle(x, y, BP_R, G_INK);      /* the rubber bumper all round */
    fill_circle(x, y, BP_R - 3, body);
    fill_circle(fx, fy, 3, you ? G_RED : mix(body, C_WHITE, 120));   /* the bonnet: which way it faces */
    fill_circle(bx, by, 2, G_MUTE);      /* the pole, up to the grid */
    if (label)
        text_mid(x, y - 7, &FONT_XS, label, G_INK);
    (void)now;
}

static void bp_floor(void)               /* the steel floor, its plates, the rail */
{
    int32_t i;
    cv_rect(0, 18, 240, 202, RGB(58, 52, 72));
    for (i = BP_L + 23; i < BP_RT; i += 24)
        cv_rect(i, BP_T, 1, BP_B - BP_T, RGB(70, 64, 86));
    for (i = BP_T + 23; i < BP_B; i += 24)
        cv_rect(BP_L, i, BP_RT - BP_L, 1, RGB(70, 64, 86));
    cv_rect(0, 27, 240, BP_T - 27, BP_CAB);   /* the rail: a rubber band in the cabinet's orange */
    cv_rect(0, BP_B, 240, 220 - BP_B, BP_CAB);
    cv_rect(0, 27, BP_L, 193, BP_CAB);
    cv_rect(BP_RT, 27, 240 - BP_RT, 193, BP_CAB);
}

static void bp_ceiling(uint32_t now)     /* the grid over it, and its sparks where the cars' poles touch */
{
    int32_t i;
    cv_rect(0, 18, 240, 9, G_SCREEN);
    for (i = 0; i < 240; i += 8) {       /* the mesh */
        cv_line(i, 18, i + 8, 26, RGB(110, 100, 128));
        cv_line(i + 8, 18, i, 26, RGB(110, 100, 128));
    }
    for (i = 0; i < bp.ncars; i++) {
        uint32_t h = (now / 50u + (uint32_t)i * 7u) * 2654435761u;
        int32_t x = (int32_t)bp.car[i].x;
        if (bp.state == BS_PLAY ? (h >> 28) < 11u : (h >> 28) < 2u) {
            uint16_t c = (h >> 24 & 1u) ? G_BULB : C_WHITE;
            cv_rect(x - 1, 21, 3, 3, c);
            cv_pset(x - 3, 20, c);
            cv_pset(x + 3, 24, c);
            cv_pset(x + 3, 19, c);
            cv_pset(x - 2, 25, c);
        }
    }
}

static void bumpers_draw(uint32_t now)
{
    int32_t y0, i;
    char s[24];
    uint32_t t = bp.state == BS_PLAY ? now - bp.t0 : 0u;
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, RGB(58, 52, 72));
        bp_floor();
        bp_ceiling(now);
        cv_rect(0, 0, 240, 18, BP_CAB);
        cv_text(6, 3, &FONT_XS, "BUMPERS", G_PAPER);
        if (bp.state != BS_STAND) {
            int32_t left = bp.state == BS_PLAY ? clamp((int32_t)(BP_ROUND - (int32_t)t + 999) / 1000, 0, 60) : 0;
            text_cat((s[0] = 0, s), "SCORE ");
            int_text(bp.score, s + 6);
            text_cat(s, "   0:");
            if (left < 10)
                text_cat(s, "0");
            int_text(left, s + text_len(s));
            cv_text(234 - text_w(&FONT_XS, s), 3, &FONT_XS, s, bp.state == BS_PLAY && left <= 10 ? G_RED2 : G_PAPER);
        }
        for (i = 0; i < 6; i++) {        /* sparks where cars met */
            uint32_t a = now - bp.burst[i].ms;
            if (bp.burst[i].ms && a < 260u) {
                int32_t r = 3 + (int32_t)a / 30, x = bp.burst[i].x, y = bp.burst[i].y;
                cv_line(x - r, y, x - r / 2, y, G_BULB);
                cv_line(x + r / 2, y, x + r, y, G_BULB);
                cv_line(x, y - r, x, y - r / 2, G_BULB);
                cv_line(x, y + r / 2, x, y + r, G_BULB);
                cv_pset(x - r * 2 / 3, y - r * 2 / 3, C_WHITE);
                cv_pset(x + r * 2 / 3, y - r * 2 / 3, C_WHITE);
                cv_pset(x - r * 2 / 3, y + r * 2 / 3, C_WHITE);
                cv_pset(x + r * 2 / 3, y + r * 2 / 3, C_WHITE);
            }
        }
        for (i = 1; i < bp.ncars; i++) { /* the others, each with its note; a bully glowers red */
            car_t *c = &bp.car[i];
            int ord = 0, j;
            for (j = 1; j < bp.ncars; j++)   /* its colour by its note's place, low to high */
                ord += bp.car[j].note < c->note;
            note_text(c->note, s);
            if (bp.state == BS_PLAY && i == bp.bully && bp.sim < bp.bully_until)
                ring_circle((int32_t)c->x, (int32_t)c->y, BP_R + 2, G_RED);
            bp_car(c, BP_COL[clamp(ord, 0, 4)], 0, s, now);
        }
        bp_car(&bp.car[0], G_PAPER, 1, 0, now);   /* yours: cream, a red bonnet */
        if (bp.state == BS_PLAY) {
            if (bp.pop_ms && now - bp.pop_ms < 900u) {   /* "+3" rising off your car (under it, by the top rail) */
                int32_t rise = (int32_t)(now - bp.pop_ms) / 40, py = bp.pop_y - 30 - rise;
                if (bp.pop_y - 30 < BP_T + 2)
                    py = bp.pop_y + 14 + rise;
                text_mid(bp.pop_x, py, &FONT_B, bp.pop, bp.pop[0] == '-' ? G_RED : G_BULB);
            }
            if (bp.big_ms && now - bp.big_ms < 1300u)
                text_mid(120, 44, &FONT_L, bp.big, (now / 100u & 1u) ? G_ORANGE : G_BULB);
        }
        if (bp.state == BS_STAND) {
            round_rect(14, 36, 212, 100, 8, G_PAPER);   /* (the cars wait in their row below it) */
            cv_text(24, 44, &FONT_XS, "K1 LEVEL", G_PLANK2);
            int_text(sav.more[SV_BUMPERS].level, s);
            text_cat(s, " ");
            text_cat(s, BP_LEVEL_NAME[clamp(sav.more[SV_BUMPERS].level, 1, BP_LEVELS) - 1]);
            cv_text(104, 42, &FONT_B, s, G_INK);
            cv_text(24, 66, &FONT_XS, "HOLD A WHITE KEY TO STEER: LEFT", G_PLANK2);
            cv_text(24, 80, &FONT_XS, "HALF LEFT, RIGHT HALF RIGHT.", G_PLANK2);
            cv_text(24, 94, &FONT_XS, "BUMP THE CARS LOW NOTE TO HIGH.", G_PLANK2);
            text_cat((s[0] = 0, s), "60 SECONDS");
            if (sav.more[SV_BUMPERS].best) {
                text_cat(s, ". BEST ");
                int_text(sav.more[SV_BUMPERS].best, s + text_len(s));
            }
            cv_text(24, 112, &FONT_B, s, G_INK);
        }
        if (bp.state == BS_OVER) {
            round_rect(40, 64, 160, 92, 8, G_PAPER);
            text_mid(120, 70, &FONT_B, "TIME'S UP", G_RED);
            text_cat((s[0] = 0, s), "SCORE ");
            int_text(bp.score, s + 6);
            text_mid(120, 92, &FONT_B, s, G_INK);
            text_cat((s[0] = 0, s), "BUMPS ");
            int_text(bp.bumps, s + 6);
            text_cat(s, "  COMBOS ");
            int_text(bp.combos, s + text_len(s));
            text_mid(120, 118, &FONT_XS, s, G_PLANK2);
            text_mid(120, 134, &FONT_XS, bp.new_best ? "NEW BEST!" : "", G_RED);
        }
        cv_rect(0, 220, 240, 20, G_LILAC);
        if (bp.state != BS_STAND)
            cv_text(FOOT_L, FOOT_Y, &FONT_XS, BP_CHORD[bp.chord].name, G_INK);
        {
            const char *f = bp.state == BS_PLAY ? "PLAY: STOP" : bp.state == BS_OVER ? "PLAY: AGAIN" : "PLAY: START";
            cv_text(FOOT_R - text_w(&FONT_XS, f), FOOT_Y, &FONT_XS, f, G_INK);
        }
        band_send(y0);
    }
}

static void bumpers_frame(uint32_t now)
{
    if (bp.state == BS_PLAY) {
        uint32_t n = (now - bp.last_ms) / BP_STEP;
        if (n > 20u)                     /* (a long stall: not caught up all at once) */
            n = 20u;
        bp.last_ms += (now - bp.last_ms) / BP_STEP * BP_STEP;
        while (n--)
            bp_physics(now);
        if (!bp.ten_said && (int32_t)(now - bp.t0) >= BP_ROUND - 10000) {
            bp.ten_said = 1;
            app_say("TEN SECONDS", "");
            snd_lock();
            bw_drum(BW_BLOCK_HI, 90);
            snd_unlock();
        }
        if ((int32_t)(now - bp.t0) >= BP_ROUND)
            bp_end();
    }
    bumpers_draw(now);
}

static void bumpers_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    float a = (float)(ms % 3000u) * 0.0020944f;   /* two cars round the rink, meeting now and then */
    int32_t cx = x + w / 2, cy = y + h / 2 + 2, rx = w / 2 - 8, ry = h / 2 - 9;
    int32_t ax = cx + (int32_t)(fm_cosf(a) * (float)rx), ay = cy + (int32_t)(fm_sinf(a) * (float)ry);
    int32_t bx = cx + (int32_t)(fm_cosf(-a * 1.5f + 1.0f) * (float)rx * 0.7f),
            by = cy + (int32_t)(fm_sinf(-a * 1.5f + 1.0f) * (float)ry * 0.7f);
    cv_rect(x, y, w, 3, G_SCREEN);
    if (ms / 70u % 3u)
        cv_rect(ax - 1, y, 3, 2, G_BULB);
    fill_circle(ax, ay, 5, G_INK);
    fill_circle(ax, ay, 3, G_PAPER);
    fill_circle(bx, by, 5, G_INK);
    fill_circle(bx, by, 3, RGB(232, 80, 91));
}

static void bumpers_best(char *t)
{
    t[0] = 0;
    if (sav.more[SV_BUMPERS].best) {
        text_cat(t, "BEST ");
        int_text(sav.more[SV_BUMPERS].best, t + 5);
    }
}

static uint32_t bumpers_keys_lit(uint32_t *mid)   /* the key steering, bright; a combo flashes them all */
{
    uint32_t now = plat_ms();
    *mid = 0;
    if (bp.state != BS_PLAY)
        return 0;
    if (bp.flash_ms && now - bp.flash_ms < 500u && (now / 80u & 1u))
        return (1u << NKEYS) - 1u;
    return bp.steer >= 0 ? 1u << bp.steer : 0u;
}

static void bumpers_where(char *t)
{
    t[0] = 0;
    if (bp.state == BS_STAND) {
        text_cat(t, "BUMPERS. KNOB 1, LEVEL ");
        sp_num(t, clamp(sav.more[SV_BUMPERS].level, 1, BP_LEVELS));
        text_cat(t, ", ");
        text_cat(t, BP_LEVEL_NAME[clamp(sav.more[SV_BUMPERS].level, 1, BP_LEVELS) - 1]);
        text_cat(t, ". HOLD A WHITE KEY TO STEER, THE LEFT HALF LEFT AND THE RIGHT HALF RIGHT. BUMP THE CARS "
                    "LOW NOTE TO HIGH. ");
        if (sav.more[SV_BUMPERS].best) {
            text_cat(t, "BEST ");
            sp_num(t, sav.more[SV_BUMPERS].best);
            text_cat(t, ". ");
        }
        text_cat(t, "PLAY STARTS.");
        return;
    }
    if (bp.state == BS_PLAY) {
        int32_t left = clamp((int32_t)(BP_ROUND - (int32_t)(plat_ms() - bp.t0) + 999) / 1000, 0, 60);
        text_cat(t, "BUMPERS. SCORE ");
        sp_num(t, bp.score);
        text_cat(t, ", ");
        sp_num(t, left);
        text_cat(t, " SECONDS LEFT. THE CARS PLAY ");
        text_cat(t, BP_CHORD[bp.chord].name);
        text_cat(t, ". PLAY STOPS.");
        return;
    }
    text_cat(t, "BUMPERS. TIME'S UP. SCORE ");
    sp_num(t, bp.score);
    text_cat(t, ". BUMPS ");
    sp_num(t, bp.bumps);
    text_cat(t, ". COMBOS ");
    sp_num(t, bp.combos);
    text_cat(t, bp.new_best ? ". NEW BEST. PLAY STARTS AGAIN." : ". PLAY STARTS AGAIN.");
}

static int bumpers_playing(void) { return bp.state == BS_PLAY; }

#ifdef PLAT_HOST
/* the host simulator: the game's own script commands (1 = handled) and checks (1 = known), a call every
 * simulated millisecond (a test player), and its lines for the speakdump (sp_out) */
static int host_autobumpers;             /* 1: at the nearest car; 2: at the next note up (a tune) */
static int host_bp_key = -1;

static int bumpers_host_cmd(const char *cmd, const char *a)
{
    if (!strcmp(cmd, "autobumpers")) {   /* autobumpers on|tune|off: steered at the nearest car, or the next up */
        host_autobumpers = !strcmp(a, "on") ? 1 : !strcmp(a, "tune") ? 2 : 0;
        if (!host_autobumpers && host_bp_key >= 0) {
            bumpers_key(host_bp_key, 0, plat_ms());
            host_bp_key = -1;
        }
        return 1;
    }
    return 0;
}
static int bumpers_expect(const char *what, int *got)
{
    if (!strcmp(what, "bumpers_state"))  /* 0 standing, 1 playing, 2 over */
        *got = bp.state;
    else if (!strcmp(what, "bumpers_score"))
        *got = bp.score;
    else if (!strcmp(what, "bumpers_bumps"))
        *got = bp.bumps;
    else if (!strcmp(what, "bumpers_combos"))
        *got = bp.combos;
    else if (!strcmp(what, "bumpers_behind"))
        *got = bp.behind;
    else if (!strcmp(what, "bumpers_best"))
        *got = sav.more[SV_BUMPERS].best;
    else if (!strcmp(what, "bumpers_level"))
        *got = sav.more[SV_BUMPERS].level;
    else if (!strcmp(what, "bumpers_steer"))   /* the key steering, -1 none */
        *got = bp.steer;
    else
        return 0;
    return 1;
}
static void bumpers_host_ms(uint32_t now)
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS;
    car_t *u = &bp.car[0];
    int i, best = -1, want = -1;
    float bd = 1e9f;
    if (!host_autobumpers || bp.state != BS_PLAY || now % 20u)
        return;
    for (i = 1; i < bp.ncars; i++) {     /* the target */
        car_t *c = &bp.car[i];
        float dx = c->x - u->x, dy = c->y - u->y, d = dx * dx + dy * dy;
        if (host_autobumpers == 2) {     /* the lowest note above the last one (or the lowest), nearest of those */
            int lo = 999, j;
            for (j = 1; j < bp.ncars; j++)
                if ((bp.last_note < 0 || bp.car[j].note > bp.last_note || bp.sim - bp.last_bump > 3000u) &&
                    bp.car[j].note < lo)
                    lo = bp.car[j].note;
            if (lo == 999)
                for (j = 1; j < bp.ncars; j++)
                    lo = bp.car[j].note < lo ? bp.car[j].note : lo;
            if (c->note != lo)
                continue;
        }
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    if (best > 0) {
        float dx = bp.car[best].x - u->x, dy = bp.car[best].y - u->y, cr, dt;
        bp_norm(&dx, &dy);
        cr = u->hx * dy - u->hy * dx;
        dt = u->hx * dx + u->hy * dy;
        if (dt < 0.0f)                   /* behind: the sharpest turn its way */
            want = cr >= 0.0f ? WK[15] : WK[0];
        else if (cr > 0.25f)
            want = WK[13];
        else if (cr > 0.06f)
            want = WK[9];
        else if (cr < -0.25f)
            want = WK[2];
        else if (cr < -0.06f)
            want = WK[6];
    }
    if (want != host_bp_key) {
        if (host_bp_key >= 0)
            bumpers_key(host_bp_key, 0, now);
        if (want >= 0)
            bumpers_key(want, 1, now);
        host_bp_key = want;
    }
}
static void bumpers_speak_dump(void)
{
    int i, keep = bp.state;
    char t[24];
    uint16_t best = sav.more[SV_BUMPERS].best;
    uint8_t lv = sav.more[SV_BUMPERS].level;
    for (i = 1; i <= BP_LEVELS; i++) {   /* standing, each level, with a best and without */
        sav.more[SV_BUMPERS].level = (uint8_t)i;
        sav.more[SV_BUMPERS].best = (uint16_t)(i == 1 ? 0 : 27);
        bp.state = BS_STAND;
        bumpers_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        int_text(i, t);
        text_cat(t, ", ");
        text_cat(t, BP_LEVEL_NAME[i - 1]);
        sp_join("LEVEL", t);
        sp_out("READOUT", sp_buf);
    }
    bp.score = 14;
    bp.bumps = 11;
    bp.combos = 2;
    for (i = 0; i < 6; i++) {            /* playing, each chord */
        bp.chord = i;
        bp.state = BS_PLAY;
        bp.t0 = plat_ms();
        bumpers_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        text_cat((t[0] = 0, t), "THE CARS PLAY ");
        text_cat(t, BP_CHORD[i].name);
        sp_join("SIXTY SECONDS", t);
        sp_out("SAY", sp_buf);
    }
    for (i = 0; i < 2; i++) {            /* over, a new best or not */
        bp.state = BS_OVER;
        bp.new_best = i;
        bumpers_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        sp_join(i ? "TIME'S UP. NEW BEST" : "TIME'S UP", "SCORE 14");
        sp_out("SAY", sp_buf);
    }
    sp_join("E4", "");
    sp_out("SAY", sp_buf);
    sp_join("G4", "RUN 2");
    sp_out("SAY", sp_buf);
    sp_join("C5", "RUN 3");
    sp_out("SAY", sp_buf);
    sp_join("CHORD", "PLUS 5");
    sp_out("SAY", sp_buf);
    sp_join("BUMPED FROM BEHIND", "MINUS 1");
    sp_out("SAY", sp_buf);
    sp_join("TEN SECONDS", "");
    sp_out("SAY", sp_buf);
    sp_join("STOPPED", "");
    sp_out("SAY", sp_buf);
    bp.state = keep;
    bp.chord = 0;
    sav.more[SV_BUMPERS].best = best;
    sav.more[SV_BUMPERS].level = lv;
}
#endif

static const uint8_t BUMPERS_TUNE[] = {60, 64, 67, 72, 0, 67, 0, 72, 76, 79, 0, 0, 77, 74, 71, 67, 0, 0};

static const game_t GAME_BUMPERS = {"BUMPERS", BP_CAB, bumpers_enter, bumpers_leave, bumpers_frame, bumpers_key,
                                    bumpers_button, bumpers_enc, 0, bumpers_attract, bumpers_best, bumpers_keys_lit,
                                    BUMPERS_TUNE, sizeof BUMPERS_TUNE, 170, BW_PLUCK, bumpers_where, 0,
                                    bumpers_playing, "BUMP IN TUNE", 0};
