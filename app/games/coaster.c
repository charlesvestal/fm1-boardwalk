/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Coaster: the roller coaster, side on, at golden hour. The track is a melody: the rails rise and fall with the notes
 * of runs (scales and arpeggios), each note a marker on the rail with its name over it. Play each note as the train
 * reaches it, roughly in time: a right note sounds on the track's voice and gives the train what it needs for the
 * climb to the next note and a burst besides; a wrong note squeals the brakes (a dull note, speed lost). A note let go
 * by gives nothing, and on a climb the train slows; if it stops on a climb it rolls back and the ride is over.
 * Between the runs the train drops into a valley (arms up, a whoosh), where a booster sends it into the next run.
 * After the last run, the station. Scored on the distance ridden; runs played clean are counted too.
 *
 * The beat is the train's own: the tick moves the train (1 kHz), and each note marker it crosses clacks, with the
 * run's bass on the strong ones, so the time is set by the ride and the notes fall on it. Keys are judged in the tick.
 *
 * K1 the level: 1 C major scales, stepwise and slow; 2 other keys and arpeggios; 3 faster, longer runs, sevenths.
 * PLAY starts and stops. Its saved level and best (metres) are sav.more[SV_COASTER]. Part of the unity build. */
#include "game.h"

#define CO_CAB RGB(180, 60, 90)
#define CO_GY 206                        /* the ground's line: a height h is at screen y CO_GY - h */
#define CO_CAR_X 76                      /* the train's front on the screen */
#define CO_S 40                          /* px from note to note */
#define CO_PTS 200
#define CO_RUNS 10
#define CO_HST 50                        /* the first station's height */
#define CO_HEND 30                       /* the last station's */
#define CO_PX_M 4                        /* px a metre */
#define CO_WIN 0.19f                     /* s: a note counts this far either side of its marker */
#define CO_CAR_L 20                      /* a car's length, px */
#define CO_LEVELS 3

enum { CO_STAND, CO_RIDE, CO_ROLL, CO_END };
enum { CO_STOPPED, CO_STALLED, CO_STATION };
enum { CO_PFLAT, CO_PVALLEY, CO_PNOTE, CO_PSTATION };   /* the kinds of point the track is drawn through */
enum { CO_NWAIT, CO_NHIT, CO_NMISS };
enum { CO_KUP, CO_KDOWN, CO_KUPDOWN, CO_KARP_UP, CO_KARP_UPDOWN };

typedef struct {
    uint8_t root, minor;                 /* the key: its pitch class, minor or major */
    uint8_t kind, chord, seventh;        /* an arpeggio: the chord's degree in the key, with its seventh */
    uint8_t n, first;                    /* notes, the first note's point */
} co_run_t;

static struct {
    volatile int state;
    int end_kind, level, nruns, new_best, dist, said_end;
    float x, v, v0, g, xmax, x_start, x_station;
    int seg;                             /* the segment under the train's front */
    volatile int nx;                     /* the next note point to judge */
    int cross;                           /* the next point to cross (its sounds) */
    int npts;
    int32_t px[CO_PTS];
    int16_t ph[CO_PTS];
    uint8_t pnote[CO_PTS], pkind[CO_PTS], prun[CO_PTS];
    volatile uint8_t pstate[CO_PTS];
    co_run_t run[CO_RUNS];
    volatile uint8_t run_clean[CO_RUNS];
    volatile int hits, wrongs, misses, runs_done, runs_clean;
    volatile int ev_run, said_run;       /* a run begun (its number), and the one last said */
    volatile uint32_t burst_ms, wrong_ms, clean_ms, roll_ms, slow_ms;
    volatile int ding_i, ding_n;         /* a clean run's ding-dong: chimes still to ring, its top note */
    int tone_lv, tone_c;
    uint32_t tone_ms;
} co;

static const char *const CO_LEVEL_NAME[CO_LEVELS] = {"C MAJOR", "MORE KEYS", "FAST"};
static const char *const CO_LEVEL_SAY[CO_LEVELS] = {"C MAJOR SCALES", "MORE KEYS AND ARPEGGIOS",
                                                    "FASTER AND LONGER RUNS"};
static const uint8_t CO_BPM[CO_LEVELS] = {80, 96, 116};
static const uint8_t CO_TOL[CO_LEVELS] = {26, 24, 22};   /* px of climb a train at cruise can coast up */
static const uint8_t CO_NRUNS[CO_LEVELS] = {6, 8, 10};
static const uint8_t SC_MAJ[7] = {0, 2, 4, 5, 7, 9, 11}, SC_MIN[7] = {0, 2, 3, 5, 7, 8, 10};

static int co_level(void) { return clamp(sav.more[SV_COASTER].level, 1, CO_LEVELS); }
static float note_h(int n) { return 22.0f + (float)(n - 53) * 4.4f; }   /* F3 low, G5 high */

/* ---------------------------------------------------------------- the track --- */
static float cr(float p0, float p1, float p2, float p3, float t)   /* Catmull-Rom: smooth through the points */
{
    return 0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t +
                   (3.0f * p1 - p0 - 3.0f * p2 + p3) * t * t * t);
}

static float co_h(float x, int *seg)     /* the track's height at world x; *seg a hint, kept */
{
    int i = clamp(*seg, 0, co.npts - 2);
    float t, h;
    while (i > 0 && x < (float)co.px[i])
        i--;
    while (i < co.npts - 2 && x >= (float)co.px[i + 1])
        i++;
    *seg = i;
    t = (x - (float)co.px[i]) / (float)(co.px[i + 1] - co.px[i]);
    t = fm_clampf(t, 0.0f, 1.0f);
    h = cr(co.ph[i > 0 ? i - 1 : 0], co.ph[i], co.ph[i + 1], co.ph[i + 2 < co.npts ? i + 2 : co.npts - 1], t);
    return h < 4.0f ? 4.0f : h;
}

static int co_point(int32_t x, float h, int kind, int note, int run)
{
    int i = co.npts;
    if (i >= CO_PTS)
        return i - 1;
    co.px[i] = x;
    co.ph[i] = (int16_t)(h + 0.5f);
    co.pkind[i] = (uint8_t)kind;
    co.pnote[i] = (uint8_t)note;
    co.prun[i] = (uint8_t)run;
    co.pstate[i] = CO_NWAIT;
    co.npts++;
    return i;
}

static int deg_note(int base, int minor, int deg)   /* a degree of the scale on base (negative: below it) */
{
    int o = deg >= 0 ? deg / 7 : -((-deg + 6) / 7);
    return base + 12 * o + (minor ? SC_MIN : SC_MAJ)[deg - o * 7];
}

static int run_deg(const co_run_t *r, int d0, int i)   /* the run's i-th note, as a degree */
{
    static const uint8_t T[4] = {0, 2, 4, 6};
    int m = (r->n + 1) / 2, nt = r->seventh ? 4 : 3, j;
    switch (r->kind) {
    case CO_KUP: return d0 + i;
    case CO_KDOWN: return d0 - i;
    case CO_KUPDOWN: return i < m ? d0 + i : d0 + 2 * (m - 1) - i;
    default:
        j = r->kind == CO_KARP_UP ? i : i < m ? i : 2 * (m - 1) - i;
        return r->chord + (j / nt) * 7 + T[j % nt];
    }
}

/* a run for this level that starts no more than two semitones above where the train is (h_prev), all its notes on
 * the keys: its notes into notes[]; 0 when this try found none */
static int run_try(co_run_t *r, int lv, int first, float h_prev, uint8_t *notes)
{
    static const uint8_t KEYS[10][2] = {{0, 0}, {7, 0}, {2, 0}, {9, 0}, {9, 1}, {4, 1}, {4, 0}, {11, 0}, {11, 1},
                                        {6, 1}};
    static const uint8_t KINDS2[4] = {CO_KUP, CO_KDOWN, CO_KARP_UP, CO_KARP_UPDOWN}, CHORDS[3] = {0, 3, 4};   /* I IV V */
    int d0, o, i, cand[8], nc = 0;
    if (lv == 1) {
        r->root = 0;
        r->minor = 0;
        r->kind = (uint8_t)(first ? CO_KUP : rnd(2) ? CO_KUP : CO_KDOWN);
        r->n = (uint8_t)(first ? 8 : 6 + rnd(3));
        d0 = r->kind == CO_KUP ? (rnd(2) ? 0 : 4) : (rnd(2) ? 7 : 4);
    } else {
        int k = (int)rnd(lv == 2 ? 6 : 10);
        r->root = KEYS[k][0];
        r->minor = KEYS[k][1];
        r->kind = lv == 2 ? KINDS2[rnd(4)] : (uint8_t)rnd(5);
        if (first)
            r->kind = rnd(2) ? CO_KUP : CO_KARP_UP;
        r->chord = r->kind >= CO_KARP_UP ? CHORDS[rnd(3)] : 0;
        r->seventh = (uint8_t)(lv == 3 && r->kind >= CO_KARP_UP && rnd(2));
        if (lv == 2)
            r->n = (uint8_t)(r->kind == CO_KARP_UPDOWN ? 7 : r->kind == CO_KARP_UP ? 5 + rnd(3) : 6 + rnd(3));
        else
            r->n = (uint8_t)(r->kind == CO_KUPDOWN || r->kind == CO_KARP_UPDOWN ? 9 + 2 * rnd(3) :
                             r->kind == CO_KARP_UP ? 7 + rnd(3) : 9 + rnd(4));
        d0 = (int)rnd(7);
    }
    for (o = 3; o <= 6; o++) {           /* every octave it fits in */
        int base = 12 * o + r->root, ok = 1;
        for (i = 0; i < r->n && ok; i++) {
            int n = deg_note(base, r->minor, run_deg(r, d0, i));
            ok = n >= 53 && n < 53 + NKEYS && (i || note_h(n) <= h_prev + 9.0f);
        }
        if (ok && nc < 8)
            cand[nc++] = base;
    }
    if (!nc)
        return 0;
    o = cand[rnd((uint32_t)nc)];
    for (i = 0; i < r->n; i++)
        notes[i] = (uint8_t)deg_note(o, r->minor, run_deg(r, d0, i));
    return 1;
}

static void co_build(void)               /* the whole ride: the station, the runs, the drops, the station */
{
    int lv = co_level(), r, i, tries;
    int32_t x;
    float h;
    uint8_t notes[16];
    co.npts = 0;
    co.level = lv;
    co.nruns = CO_NRUNS[lv - 1];
    for (x = -240; x <= 0; x += 80)      /* the first station, flat */
        co_point(x, CO_HST, CO_PSTATION, 0, 0);
    co_point(60, CO_HST, CO_PFLAT, 0, 0);
    x = 60;
    h = CO_HST;
    for (r = 0; r < co.nruns; r++) {
        co_run_t *rn = &co.run[r];
        float h0, hv;
        for (tries = 0; tries < 60 && !run_try(rn, lv, r == 0, h, notes); tries++)
            ;
        if (tries == 60) {               /* (never, it seems: a plain run up from F3) */
            memset(rn, 0, sizeof *rn);
            rn->n = 6;
            for (i = 0; i < 6; i++)
                notes[i] = (uint8_t)deg_note(48, 0, 3 + i);
        }
        h0 = note_h(notes[0]);
        hv = (h < h0 ? h : h0) - 14.0f;  /* the valley under both, its booster */
        hv = hv < 8.0f ? 8.0f : hv;
        x += 34 + (int32_t)((h - hv) * 1.0f);
        co_point(x, hv, CO_PVALLEY, 0, r);
        x += 34 + (int32_t)((h0 - hv) * 1.0f);
        rn->first = (uint8_t)co.npts;
        for (i = 0; i < rn->n; i++) {
            co_point(x, note_h(notes[i]), CO_PNOTE, notes[i], r);
            x += CO_S;
        }
        x -= CO_S;
        h = note_h(notes[rn->n - 1]);
        co.run_clean[r] = 1;
    }
    {                                    /* the last drop, and the station */
        float hv = (h < CO_HEND ? h : CO_HEND) - 12.0f;
        hv = hv < 8.0f ? 8.0f : hv;
        x += 34 + (int32_t)((h - hv) * 1.0f);
        co_point(x, hv, CO_PVALLEY, 0, co.nruns);
        x += 50;
        co.x_station = (float)x;
        co_point(x, CO_HEND, CO_PSTATION, 0, co.nruns);
        for (i = 1; i <= 4; i++)
            co_point(x + i * 90, CO_HEND, CO_PSTATION, 0, co.nruns);
    }
}

/* -------------------------------------------------------------- the ride --- */
static void co_tone_off(void) { bw_tone(0, 2800, 0); }

static void co_go(void)
{
    int lv;
    snd_lock();                          /* (the tick may still be braking the last ride into its station) */
    co.state = CO_STAND;
    co_tone_off();
    snd_unlock();
    rnd_state ^= plat_ms() * 2654435761u;
    co_build();
    lv = co.level;
    co.v0 = (float)(CO_S * CO_BPM[lv - 1]) / 60.0f;
    co.g = 1.3225f * co.v0 * co.v0 * 0.5f / (float)CO_TOL[lv - 1];   /* (1.15 v0)^2 / 2 over the climb it coasts */
    co.x = co.xmax = co.x_start = 40.0f;
    co.v = co.v0;
    co.seg = 0;
    co.nx = 0;
    while (co.nx < co.npts && co.pkind[co.nx] != CO_PNOTE)
        co.nx++;
    co.cross = 0;
    while (co.cross < co.npts && (float)co.px[co.cross] <= co.x)
        co.cross++;
    co.hits = co.wrongs = co.misses = co.runs_done = co.runs_clean = 0;
    co.ev_run = co.said_run = 0;
    co.burst_ms = co.wrong_ms = co.clean_ms = co.slow_ms = 0;
    co.ding_i = 0;
    co.said_end = 0;
    co.tone_lv = -1;
    co.state = CO_RIDE;
    app_say("ALL ABOARD", "");
}

static void co_stop(int kind)            /* the tick or the main loop (sounds the caller's to lock) */
{
    co.end_kind = kind;
    co.state = kind == CO_STOPPED ? CO_STAND : CO_END;
    co_tone_off();
}

static int next_note(int p)              /* the note point after p, -1 none */
{
    for (p++; p < co.npts; p++)
        if (co.pkind[p] == CO_PNOTE)
            return p;
    return -1;
}

static void run_judged(int p)            /* a note judged: its run done if it was the last */
{
    int r = co.prun[p], q = next_note(p);
    co.nx = q < 0 ? co.npts : q;
    if (q >= 0 && co.prun[q] == r)
        return;
    co.runs_done = r + 1;
    if (co.run_clean[r]) {
        co.runs_clean++;
        co.clean_ms = plat_ms() | 1u;
        co.ding_n = 84 + co.run[r].root % 12 - (co.run[r].root > 7 ? 12 : 0);   /* a ding-dong, rung by the tick
                                                                               * just after the note (not under it) */
        co.ding_i = 2;
    }
}

/* a key (in the tick): the next note judged by where the train is */
static void coaster_key(int k, int down, uint32_t ms)
{
    int p = co.nx;
    if (!down)
        return;
    if (co.state != CO_RIDE) {           /* standing: the keys play the track's voice */
        if (co.state != CO_ROLL)
            bw_note(53 + k, 90, BW_SAW);
        return;
    }
    if (p < co.npts && co.pkind[p] == CO_PNOTE) {
        float d = (float)co.px[p] - co.x, v = co.v > co.v0 * 0.3f ? co.v : co.v0 * 0.3f;
        int near = d >= 0.0f ? d <= v * CO_WIN || d < 5.0f : -d <= v * CO_WIN || -d < 5.0f;
        if (near && 53 + k == co.pnote[p]) {   /* right, in time: it sounds, and lifts the train */
            int q = next_note(p);
            float rise = q >= 0 && co.prun[q] == co.prun[p] ? (float)(co.ph[q] - co.ph[p]) : 0.0f;
            co.v = fm_sqrtf(co.v0 * co.v0 + 2.0f * co.g * (rise > 0.0f ? rise : 0.0f)) + 0.08f * co.v0;   /* the
                                                                       * beat's pace, the climb's lift, a burst */
            co.pstate[p] = CO_NHIT;
            co.hits++;
            co.burst_ms = ms | 1u;
            bw_note(53 + k, 105, BW_SAW);
            run_judged(p);
            return;
        }
        co.run_clean[co.prun[p]] = 0;
    }
    co.wrongs++;                         /* wrong, or nothing there: the brakes */
    co.wrong_ms = ms | 1u;
    co.v -= 0.3f * co.v0;
    bw_note(53 + k, 100, BW_DUD);
}

static void co_sounds(int i)             /* a point crossed: the clack, the bass, the booster */
{
    int kind = co.pkind[i];
    if (kind == CO_PNOTE) {
        const co_run_t *r = &co.run[co.prun[i]];
        int j = i - r->first, bass = 48 + r->root % 12;
        bw_drum(BW_HAT, 40);
        if (j % 4 == 0) {
            bw_note(bass, 90, BW_BASS);
            if (co.level > 1)
                bw_drum(BW_KICK, 70);
        } else if (j % 4 == 2) {
            bw_note(bass + 7 - (bass + 7 > 59 ? 12 : 0), 80, BW_BASS);
        }
    } else if (kind == CO_PVALLEY) {      /* the booster: whoosh, and into the next run */
        bw_drum(BW_KICK, 90);
        bw_drum(BW_HAT, 110);
        if (co.v < 1.6f * co.v0)
            co.v = 1.6f * co.v0;
        if (co.prun[i] < co.nruns)
            co.ev_run = co.prun[i] + 1;
    }
    if (i > 0 && co.pkind[i - 1] == CO_PNOTE && kind == CO_PVALLEY && co.ph[i - 1] - co.ph[i] > 30)
        bw_drum(BW_CLAP, 50);            /* over the top of a big drop: the riders' cheer */
}

static void coaster_tick(uint32_t ms)    /* the ride, a millisecond at a time */
{
    float dt = 0.001f, s, a;
    int seg;
    if (co.state == CO_STAND)
        return;
    if (co.ding_i == 2 && ms - co.clean_ms >= 90u) {
        bw_note(co.ding_n - 5, 100, BW_CHIME);
        co.ding_i = 1;
    } else if (co.ding_i == 1 && ms - co.clean_ms >= 220u) {
        bw_note(co.ding_n, 110, BW_CHIME);
        co.ding_i = 0;
    }
    if (co.state == CO_END) {            /* into the station: the brakes, to a stop */
        if (co.end_kind == CO_STATION && co.v > 0.0f) {
            co.v -= (co.v * 2.5f + 15.0f) * dt;
            co.x += (co.v > 0.0f ? co.v : 0.0f) * dt;
        }
        return;
    }
    seg = co.seg;
    s = co_h(co.x + 0.5f, &seg) - co_h(co.x - 0.5f, &seg);
    co.seg = seg;
    a = -co.g * s;
    if (co.state == CO_ROLL) {           /* back down the hill, then it is over */
        co.v += a * dt;
        co.x += co.v * dt;
        if (ms - co.roll_ms > 1600u)
            co_stop(CO_STALLED);
        return;
    }
    {
        int in_run = co.pkind[seg] == CO_PNOTE && seg + 1 < co.npts && co.pkind[seg + 1] == CO_PNOTE;
        if (in_run && s < 0.05f && co.v > co.v0)   /* through a run the pace stays near the beat */
            a -= 2.5f * (co.v - co.v0);
        co.v += a * dt;
        if (co.v > (in_run ? 1.6f : 2.3f) * co.v0)
            co.v = (in_run ? 1.6f : 2.3f) * co.v0;
    }
    if (co.v <= 0.0f || (co.slow_ms && ms - co.slow_ms > 2500u)) {   /* stalled: it rolls back */
        co.state = CO_ROLL;
        co.roll_ms = ms;
        co.v = co.v < 0.0f ? co.v : 0.0f;
        bw_note(41, 110, BW_DUD);
        bw_tone(0, 2600, 0);
        return;
    }
    co.slow_ms = co.v < 0.12f * co.v0 ? (co.slow_ms ? co.slow_ms : ms) : 0u;
    co.x += co.v * dt;
    if (co.x > co.xmax)
        co.xmax = co.x;
    while (co.cross < co.npts && (float)co.px[co.cross] <= co.x)
        co_sounds(co.cross++);
    while (co.nx < co.npts && co.pkind[co.nx] == CO_PNOTE &&   /* a note gone by unplayed */
           co.x - (float)co.px[co.nx] > (co.v * CO_WIN > 5.0f ? co.v * CO_WIN : 5.0f)) {
        int p = co.nx;
        co.pstate[p] = CO_NMISS;
        co.misses++;
        co.run_clean[co.prun[p]] = 0;
        bw_note(48, 40, BW_CLUNK);
        run_judged(p);
    }
    if (co.x >= co.x_station) {          /* home */
        co.runs_done = co.nruns;
        co_stop(CO_STATION);
        return;
    }
    if (ms - co.tone_ms >= 30u) {        /* the rumble of the wheels: louder and higher as it goes faster */
        float rr = co.v / co.v0;
        int lv = clamp((int)(3.0f + 9.0f * rr) + (s < -0.3f ? 6 : 0), 0, 24), c = 2800 + (int)(rr * 700.0f);
        co.tone_ms = ms;
        if (lv - co.tone_lv > 1 || co.tone_lv - lv > 1 || c - co.tone_c > 40 || co.tone_c - c > 40) {
            co.tone_lv = lv;
            co.tone_c = c;
            bw_tone(0, c, lv);
        }
    }
}

/* ------------------------------------------------------------- the main loop --- */
static void run_text(int r, char *t, int full)   /* "G MAJOR SCALE UP FROM G3"; not full: "G MAJOR SCALE" */
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const co_run_t *rn = &co.run[r];
    char n[8];
    if (rn->kind >= CO_KARP_UP) {         /* the chord: its root, its third, its seventh */
        const uint8_t *sc = rn->minor ? SC_MIN : SC_MAJ;
        int c = rn->chord, root = (rn->root + sc[c % 7]) % 12;
        int third = (sc[(c + 2) % 7] - sc[c % 7] + 12) % 12, sev = (sc[(c + 6) % 7] - sc[c % 7] + 12) % 12;
        text_cat(t, N[root]);
        if (rn->seventh)
            text_cat(t, third == 4 ? (sev == 10 ? " SEVENTH" : " MAJOR SEVENTH") : " MINOR SEVENTH");
        else
            text_cat(t, third == 4 ? " MAJOR" : " MINOR");
        text_cat(t, " ARPEGGIO");
    } else {
        text_cat(t, N[rn->root % 12]);
        text_cat(t, rn->minor ? " MINOR SCALE" : " MAJOR SCALE");
    }
    if (!full)
        return;
    text_cat(t, rn->kind == CO_KUP || rn->kind == CO_KARP_UP ? " UP FROM " : rn->kind == CO_KDOWN ? " DOWN FROM " :
                                                                         " UP AND DOWN FROM ");
    note_text(co.pnote[rn->first], n);
    text_cat(t, n);
}

static int co_metres(void)
{
    float d = (co.end_kind == CO_STATION && co.state == CO_END ? co.x_station : co.xmax) - co.x_start;
    return d > 0.0f ? (int)(d / (float)CO_PX_M) : 0;
}

static int co_run_on(void)               /* the run the train is on (or stalled on), from 1 */
{
    return clamp(co.nx < co.npts ? co.prun[co.nx] + 1 : co.nruns, 1, co.nruns);
}

static void co_end_said(void)           /* the end, said once, the best kept */
{
    char t[48] = "";
    co.dist = co_metres();
    co.new_best = co.dist > sav.more[SV_COASTER].best;
    if (co.new_best) {
        sav.more[SV_COASTER].best = (uint16_t)co.dist;
        save_dirty();
    }
    sp_num(t, co.dist);
    text_cat(t, " METRES");
    if (co.end_kind == CO_STATION) {
        text_cat(t, ", ");
        sp_num(t, co.runs_clean);
        text_cat(t, co.runs_clean == 1 ? " RUN CLEAN" : " RUNS CLEAN");
        app_say(co.new_best ? "STATION. NEW BEST" : "STATION", t);
    } else {
        app_say(co.new_best ? "STALLED. NEW BEST" : "STALLED", t);
    }
}

static void coaster_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (co.state == CO_RIDE || co.state == CO_ROLL) {
        snd_lock();
        co_stop(CO_STOPPED);
        snd_unlock();
        co.x = co.xmax = co.x_start;
        co_build();                      /* the station again */
        app_say("STOPPED", "");
    } else {
        co_go();
    }
}

static void coaster_enc(int role, int steps)
{
    char t[40];
    if (role != EN_K1 || co.state == CO_RIDE || co.state == CO_ROLL)
        return;
    sav.more[SV_COASTER].level = (uint8_t)clamp(co_level() + steps, 1, CO_LEVELS);
    t[0] = 0;
    sp_num(t, co_level());
    text_cat(t, ", ");
    text_cat(t, CO_LEVEL_SAY[co_level() - 1]);
    app_readout("LEVEL", t);
    co.state = CO_STAND;
    co.x = co.xmax = co.x_start = 40.0f;     /* back at the station, the new level's track ahead */
    co_build();
    save_dirty();
}

static void coaster_enter(void)
{
    co.state = CO_STAND;
    co.x = co.xmax = co.x_start = 40.0f;
    co_build();
}

static void coaster_leave(void)
{
    snd_lock();
    co_stop(CO_STOPPED);
    snd_unlock();
}

/* ------------------------------------------------------------- the drawing --- */
#define CO_LAT RGB(250, 230, 208)        /* the lattice: white wood in the evening light */
#define CO_SKIN RGB(240, 190, 150)
#define CO_ARM RGB(214, 150, 112)

static int16_t co_ty[240];
static void coaster_best_text(char *t);               /* the track's screen y, column by column, this frame */

static void co_rider(int32_t x, int32_t y, int arms, uint16_t hair, uint16_t shirt)   /* a rider sitting in a car
                                                                                         * whose top is at y */
{
    if (arms) {                          /* hands up */
        cv_line(x - 3, y - 3, x - 6, y - 14, CO_ARM);
        cv_line(x - 2, y - 3, x - 5, y - 14, CO_ARM);
        cv_line(x + 3, y - 3, x + 6, y - 14, CO_ARM);
        cv_line(x + 4, y - 3, x + 7, y - 14, CO_ARM);
    }
    cv_rect(x - 3, y - 4, 7, 5, shirt);
    fill_circle(x, y - 8, 3, CO_SKIN);
    cv_rect(x - 3, y - 11, 7, 2, hair);
    cv_rect(x - 3, y - 9, 1, 2, hair);
}

static void co_train(int32_t cam, float x, int arms, uint32_t now)   /* two cars, their riders, following the rails */
{
    static const uint16_t HAIR[4] = {RGB(74, 46, 40), RGB(242, 163, 58), RGB(90, 58, 78), RGB(184, 58, 69)};
    static const uint16_t SHIRT[4] = {RGB(62, 154, 176), RGB(255, 244, 223), RGB(242, 163, 58), RGB(110, 143, 196)};
    int c, i;
    for (c = 0; c < 2; c++) {
        int32_t front = (int32_t)x - cam - c * (CO_CAR_L + 3), back = front - CO_CAR_L;
        for (i = 0; i < 2; i++) {         /* the riders, two a car, sitting in it */
            int32_t rx = back + 6 + i * 9;
            int up = arms && (c + i + (int)(now / 500u)) % 5 != 0;   /* (most of them) */
            if (rx >= 0 && rx < 240)
                co_rider(rx, co_ty[rx] - 11, up, HAIR[(c * 2 + i) & 3], SHIRT[(c * 2 + i + 1) & 3]);
        }
        for (i = back; i <= front; i++) {   /* the body, column by column on the rails */
            int32_t y, top;
            if (i < 0 || i >= 240)
                continue;
            y = co_ty[i];
            top = y - 11;
            if (c == 0 && i > front - 4)  /* the front car's nose, rounded */
                top += (i - (front - 4)) * (i - (front - 4));
            else if (i == back)
                top += 1;
            cv_rect(i, top, 1, y - 2 - top, CO_CAB);
            cv_rect(i, y - 4, 1, 2, G_RED2);
            if (top <= y - 9)
                cv_rect(i, top + 1, 1, 2, G_CAR);
        }
        for (i = 0; i < 2; i++) {         /* its wheels */
            int32_t wx = i ? front - 4 : back + 4;
            if (wx >= 0 && wx < 240)
                fill_circle(wx, co_ty[wx] - 1, 2, G_INK);
        }
    }
}

static void co_station(int32_t cam, int32_t wx0, int32_t wx1, int32_t h)   /* a platform with its awning */
{
    int32_t x0 = wx0 - cam, x1 = wx1 - cam, y = CO_GY - h, i;
    if (x1 < 0 || x0 > 240)
        return;
    cv_rect(x0, y + 4, x1 - x0, CO_GY - y - 4, G_PLANK2);
    cv_rect(x0, y + 4, x1 - x0, 3, G_PLANK);
    for (i = x0 + 8; i < x1; i += 30)   /* its legs and the awning's posts */
        cv_rect(i, y - 44, 2, 44, G_WHEEL);
    for (i = x0 - 4; i < x1 + 4; i += 8)   /* the striped awning */
        cv_rect(i, y - 50, 8, 7, ((i - x0) / 8) & 1 ? G_PAPER : CO_CAB);
}

static void coaster_draw(uint32_t now)
{
    int32_t cam = (int32_t)co.x - CO_CAR_X, y0, i, x;
    int seg = 0, state = co.state, arms;
    char s[48];
    float sl;
    for (x = 0; x < 240; x++)            /* the rails' line, once a frame */
        co_ty[x] = (int16_t)(CO_GY - (int32_t)co_h((float)(cam + x), &seg));
    seg = co.seg;
    sl = co_h(co.x - 10.0f, &seg) - co_h(co.x - 30.0f, &seg);
    arms = state != CO_STAND && (sl < -6.0f || (co.v > 1.7f * co.v0 && state == CO_RIDE));
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, G_SKY1);
        for (i = 18; i < 150; i++)       /* the sky, lilac to peach to gold */
            cv_rect(0, i, 240, 1, i < 90 ? mix(G_SKY2, G_SKY0, (i - 18) * 256 / 72) : mix(G_SKY0, G_SKY1, (i - 90) * 256 / 60));
        fill_circle(176, 150, 24, G_SUN);
        cv_rect(0, 150, 240, CO_GY - 150, G_SEA);   /* the sea, slow behind */
        for (i = -40; i < 260; i += 26) {
            int32_t fx = i - (cam / 6) % 26;
            cv_rect(fx, 156 + (i / 26 & 1) * 14, 12, 2, G_FOAM);
            cv_rect(fx + 9, 184 + (i / 26 & 1) * 8, 9, 1, G_FOAM);
        }
        cv_rect(150, 151, 52, 2, G_SUN);  /* the sun on the water */
        cv_rect(160, 158, 32, 2, G_SUN);
        cv_rect(168, 166, 16, 1, G_SUN);
        cv_rect(0, CO_GY, 240, 14, G_PLANK);   /* the boardwalk, under it all */
        for (i = -((cam % 14 + 14) % 14); i < 240; i += 14)
            cv_rect(i, CO_GY, 1, 14, G_PLANK2);
        co_station(cam, -160, 64, CO_HST);
        co_station(cam, (int32_t)co.x_station - 10, (int32_t)co.x_station + 250, CO_HEND);
        {                                /* the lattice under the track: posts, braces, ties */
            int32_t wx, py = 0, pxx = 0, first = 1;
            for (wx = cam - ((cam % 16) + 16) % 16 - 16; wx < cam + 256; wx += 16) {
                int32_t sx = wx - cam, yy = sx >= 0 && sx < 240 ? co_ty[sx] : CO_GY - (int32_t)co_h((float)wx, &seg);
                if (sx >= -16 && sx < 256) {
                    cv_rect(sx, yy + 4, 1, CO_GY - yy - 4, CO_LAT);
                    if (!first) {
                        int32_t yb = (yy > py ? yy : py) + 8, t;
                        if (yb < CO_GY - 6) {    /* one brace a bay, this way and that */
                            if ((wx >> 4) & 1)
                                cv_line(pxx + 1, yb, sx - 1, CO_GY - 1, mix(CO_LAT, G_SEA, 110));
                            else
                                cv_line(pxx + 1, CO_GY - 1, sx - 1, yb, mix(CO_LAT, G_SEA, 110));
                        }
                        for (t = CO_GY - 32; t > yb - 4; t -= 32)
                            cv_rect(pxx, t, sx - pxx, 1, mix(CO_LAT, G_SEA, 60));
                    }
                }
                py = yy;
                pxx = sx;
                first = 0;
            }
        }
        for (x = 0; x < 240; x++) {      /* the rails, and a sleeper every 8 px */
            int32_t y = co_ty[x];
            cv_rect(x, y, 1, 2, CO_CAB);
            cv_rect(x, y + 2, 1, 2, G_WHEEL);
            if (((cam + x) & 7) == 0)
                cv_rect(x, y - 1, 1, 6, G_INK);
        }
        for (i = 0; i < co.npts; i++) {  /* the notes on the rails, the boosters */
            int32_t sx = co.px[i] - cam, y;
            if (sx < -20 || sx > 260)
                continue;
            if (co.pkind[i] == CO_PVALLEY && co.prun[i] < co.nruns && sx > -100) {   /* the booster: a run's name */
                y = CO_GY - co.ph[i];
                cv_rect(sx - 8, y - 2, 16, 3, G_ORANGE);
            }
            if (co.pkind[i] != CO_PNOTE)
                continue;
            y = CO_GY - co.ph[i];
            fill_circle(sx, y, 3, co.pstate[i] == CO_NHIT ? G_TEAL : co.pstate[i] == CO_NMISS ? G_MUTE : G_PAPER);
            ring_circle(sx, y, 3, G_INK);
        }
        for (i = 0; i < co.nruns; i++) { /* each run's name, on a banner over its start */
            int32_t sx = co.px[co.run[i].first] - cam, w;
            if (sx < -200 || sx > 250 || state == CO_STAND || state == CO_END)
                continue;
            s[0] = 0;
            run_text(i, s, 0);
            w = text_w(&FONT_XS, s);
            cv_rect(sx - 4, 22, 1, 6, G_PLANK2);
            round_rect(sx - 6, 26, w + 12, 16, 4, G_PAPER);
            cv_text(sx, 26, &FONT_XS, s, CO_CAB);
        }
        co_train(cam, co.x, arms, now);
        for (i = co.nx > 2 ? co.nx - 2 : 0; i < co.npts; i++) {   /* the notes' names, over the train: the next
                                                                    * one dark, one played teal, one missed grey */
            int32_t sx = co.px[i] - cam, y = CO_GY - co.ph[i], w;
            int st = co.pstate[i], next = i == co.nx && state == CO_RIDE;
            if (sx > 260 || state == CO_END)   /* (over: the card's, not the notes') */
                break;
            if (co.pkind[i] != CO_PNOTE || sx < -20 ||
                (st != CO_NWAIT && (float)co.px[i] <= co.x - 2.0f && state != CO_STAND))   /* (gone by: the
                                                                                           * marker only) */
                continue;
            note_text(co.pnote[i], s);
            w = text_w(&FONT_XS, s);
            cv_rect(sx, y - 14, 1, 10, G_PAPER2);
            round_rect(sx - w / 2 - 3, y - 30, w + 6, 16, 5,
                       st == CO_NHIT ? G_TEAL : st == CO_NMISS ? G_MUTE : next ? G_INK : G_PAPER);
            text_mid(sx, y - 30, &FONT_XS, s, st == CO_NWAIT && !next ? G_INK : G_PAPER);
        }
        if (state == CO_RIDE && co.burst_ms && now - co.burst_ms < 260u)   /* a burst: speed lines behind */
            for (i = 0; i < 3; i++)
                cv_rect(CO_CAR_X - 2 * CO_CAR_L - 16 - i * 7, co_ty[CO_CAR_X - 2 * CO_CAR_L - 6] - 14 + i * 4, 12, 1,
                        G_PAPER);
        if (state == CO_RIDE && co.wrong_ms && now - co.wrong_ms < 300u)   /* the brakes: sparks at the wheels */
            for (i = 0; i < 6; i++)
                cv_rect(CO_CAR_X - 4 - (int32_t)((now / 30u + (uint32_t)i * 7u) % 14u), co_ty[CO_CAR_X - 4] - 2 + i % 3,
                        2, 1, i & 1 ? G_ORANGE : G_SUN);
        if (state == CO_RIDE && co.clean_ms && now - co.clean_ms < 1100u)
        {
            round_rect(70, 46, 100, 20, 8, G_PAPER);
            text_mid(120, 48, &FONT_B, "CLEAN RUN!", G_TEAL);
        }
        /* the header, the footer */
        cv_rect(0, 0, 240, 18, CO_CAB);
        cv_text(6, 3, &FONT_XS, "COASTER", G_PAPER);
        if (state != CO_STAND) {
            s[0] = 0;
            text_cat(s, "RUN ");
            sp_num(s, co_run_on());
            text_cat(s, " OF ");
            sp_num(s, co.nruns);
            text_cat(s, "   ");
            sp_num(s, co_metres());
            text_cat(s, " M");
            cv_text(234 - text_w(&FONT_XS, s), 3, &FONT_XS, s, G_PAPER);
        }
        if (state == CO_STAND) {
            round_rect(8, 24, 224, 96, 8, G_PAPER);
            cv_text(14, 32, &FONT_XS, "K1 LEVEL", G_PLANK2);
            s[0] = 0;
            sp_num(s, co_level());
            text_cat(s, "  ");
            text_cat(s, CO_LEVEL_NAME[co_level() - 1]);
            cv_text(94, 30, &FONT_B, s, G_INK);
            cv_text(14, 56, &FONT_XS, "PLAY EACH NOTE AS THE TRAIN REACHES IT.", G_PLANK2);
            cv_text(14, 70, &FONT_XS, "RIGHT NOTES PULL IT UP THE HILLS,", G_PLANK2);
            cv_text(14, 84, &FONT_XS, "WRONG ONES BRAKE. DON'T STALL!", G_PLANK2);
            if (sav.more[SV_COASTER].best) {
                coaster_best_text(s);
                cv_text(14, 100, &FONT_B, s, G_INK);
            }
        }
        if (state == CO_ROLL) {          /* stalled: why it is going backwards */
            round_rect(70, 46, 100, 20, 8, G_PAPER);
            text_mid(120, 48, &FONT_B, "TOO SLOW!", G_RED);
        }
        if (state == CO_END) {
            round_rect(40, 24, 160, co.new_best ? 96 : 80, 8, G_PAPER);
            text_mid(120, 29, &FONT_B, co.end_kind == CO_STATION ? "STATION!" : "STALLED",
                     co.end_kind == CO_STATION ? G_TEAL : G_RED);
            s[0] = 0;
            sp_num(s, co.dist);
            text_cat(s, " M");
            text_mid(120, 45, &FONT_L, s, G_INK);
            s[0] = 0;
            if (co.end_kind == CO_STATION) {
                sp_num(s, co.runs_clean);
                text_cat(s, " OF ");
                sp_num(s, co.nruns);
                text_cat(s, " RUNS CLEAN");
            } else {                     /* stalled: how far it got */
                text_cat(s, "ON RUN ");
                sp_num(s, co_run_on());
                text_cat(s, " OF ");
                sp_num(s, co.nruns);
            }
            text_mid(120, 80, &FONT_XS, s, G_PLANK2);
            if (co.new_best)
                text_mid(120, 98, &FONT_B, "NEW BEST!", G_RED);
        }
        cv_rect(0, 220, 240, 20, G_LILAC);
        cv_text(8, 224, &FONT_XS, CO_LEVEL_NAME[co_level() - 1], G_INK);
        {
            const char *f = state == CO_RIDE || state == CO_ROLL ? "PLAY: STOP" : state == CO_END ? "PLAY: AGAIN" : "PLAY: START";
            cv_text(232 - text_w(&FONT_XS, f), 224, &FONT_XS, f, G_INK);
        }
        band_send(y0);
    }
}

static void coaster_frame(uint32_t now)
{
    if (co.state == CO_RIDE && co.ev_run != co.said_run) {   /* a run begins: what it is */
        char t[48] = "RUN ";
        co.said_run = co.ev_run;
        sp_num(t, co.said_run);
        if (ann_on()) {
            char d[48] = "";
            run_text(co.said_run - 1, d, 1);
            app_say(t, d);
        }
    }
    if (co.state == CO_END && !co.said_end) {
        co.said_end = 1;
        co_end_said();
    }
    coaster_draw(now);
}

static void coaster_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    int32_t i, cx = w / 3;
    float ph = (float)ms * 0.004f;
    for (i = 0; i < w; i++) {            /* a wave of track, rolling by */
        int32_t ty = y + h / 2 + (int32_t)(fm_sinf((float)i * 0.09f + ph) * (float)(h / 4));
        cv_rect(x + i, ty, 1, 2, CO_CAB);
        if ((i & 7) == 0)
            cv_rect(x + i, ty + 2, 1, y + h - ty - 2, CO_LAT);
    }
    {
        int32_t ty = y + h / 2 + (int32_t)(fm_sinf((float)cx * 0.09f + ph) * (float)(h / 4));
        cv_rect(x + cx - 6, ty - 5, 12, 5, G_CAR);
    }
}

static void coaster_best_text(char *t)
{
    t[0] = 0;
    if (sav.more[SV_COASTER].best) {
        text_cat(t, "BEST ");
        int_text(sav.more[SV_COASTER].best, t + 5);
        text_cat(t, " M");
    }
}
static void coaster_best(char *t) { coaster_best_text(t); }

static uint32_t coaster_keys_lit(uint32_t *mid)   /* none: the notes are on the screen, the keys are yours to find */
{
    *mid = 0;
    return 0;
}

static void coaster_where(char *t)
{
    char n[8];
    t[0] = 0;
    if (co.state == CO_STAND) {
        text_cat(t, "COASTER. KNOB 1, LEVEL ");
        sp_num(t, co_level());
        text_cat(t, ", ");
        text_cat(t, CO_LEVEL_SAY[co_level() - 1]);
        text_cat(t, ". PLAY EACH NOTE AS THE TRAIN REACHES IT. RIGHT NOTES PULL IT UP THE HILLS, WRONG ONES BRAKE. ");
        if (sav.more[SV_COASTER].best) {
            text_cat(t, "BEST ");
            sp_num(t, sav.more[SV_COASTER].best);
            text_cat(t, " METRES. ");
        }
        text_cat(t, "PLAY STARTS.");
        return;
    }
    if (co.state == CO_END) {
        text_cat(t, co.end_kind == CO_STATION ? "COASTER. STATION, " : "COASTER. STALLED, ");
        sp_num(t, co.dist);
        if (co.end_kind == CO_STATION) {
            text_cat(t, " METRES, ");
            sp_num(t, co.runs_clean);
            text_cat(t, " OF ");
            sp_num(t, co.nruns);
            text_cat(t, " RUNS CLEAN.");
        } else {
            text_cat(t, " METRES, ON RUN ");
            sp_num(t, co_run_on());
            text_cat(t, " OF ");
            sp_num(t, co.nruns);
            text_cat(t, ".");
        }
        text_cat(t, co.new_best ? " NEW BEST. PLAY STARTS AGAIN." : " PLAY STARTS AGAIN.");
        return;
    }
    text_cat(t, co.state == CO_ROLL ? "COASTER. STALLED, ROLLING BACK. " : "COASTER. ");
    if (co.state == CO_RIDE && co.nx < co.npts) {
        int r = co.prun[co.nx];
        text_cat(t, "RUN ");
        sp_num(t, r + 1);
        text_cat(t, " OF ");
        sp_num(t, co.nruns);
        text_cat(t, ", ");
        run_text(r, t, 1);
        text_cat(t, ". NEXT NOTE ");
        note_text(co.pnote[co.nx], n);
        text_cat(t, n);
        text_cat(t, ". ");
    }
    sp_num(t, co_metres());
    text_cat(t, " METRES. PLAY STOPS.");
}

static int coaster_playing(void) { return co.state == CO_RIDE || co.state == CO_ROLL; }

#ifdef PLAT_HOST
/* the host simulator: autocoaster on|some|off (the test player: each note played as the train reaches it; some:
 * about one in four let go by); the checks */
static int host_autocoaster;
static int coaster_host_cmd(const char *cmd, const char *a)
{
    if (!strcmp(cmd, "autocoaster")) {
        host_autocoaster = !strcmp(a, "on") ? 1 : !strcmp(a, "some") ? 2 : 0;
        return 1;
    }
    return 0;
}
static int coaster_expect(const char *what, int *got)
{
    if (!strcmp(what, "coaster_state"))  /* 0 standing, 1 riding, 2 rolling back, 3 over */
        *got = co.state;
    else if (!strcmp(what, "coaster_end"))   /* 0 stopped, 1 stalled, 2 the station */
        *got = co.end_kind;
    else if (!strcmp(what, "coaster_dist"))  /* metres */
        *got = co.state == CO_END ? co.dist : co_metres();
    else if (!strcmp(what, "coaster_clean"))
        *got = co.runs_clean;
    else if (!strcmp(what, "coaster_runs"))
        *got = co.runs_done;
    else if (!strcmp(what, "coaster_hits"))
        *got = co.hits;
    else if (!strcmp(what, "coaster_wrongs"))
        *got = co.wrongs;
    else if (!strcmp(what, "coaster_misses"))
        *got = co.misses;
    else if (!strcmp(what, "coaster_best"))
        *got = sav.more[SV_COASTER].best;
    else if (!strcmp(what, "coaster_level"))
        *got = co_level();
    else
        return 0;
    return 1;
}
static void coaster_host_ms(uint32_t now)
{
    int p = co.nx;
    if (!host_autocoaster || co.state != CO_RIDE || p >= co.npts ||
        co.pkind[p] != CO_PNOTE)
        return;
    if (host_autocoaster == 2 && p % 4 == 3)   /* some: every fourth note or so let go by */
        return;
    if (co.x >= (float)co.px[p] - co.v * 0.004f) {   /* on its marker, to a few ms */
        coaster_key(co.pnote[p] - 53, 1, now);
        coaster_key(co.pnote[p] - 53, 0, now);
    }
}
static void coaster_speak_dump(void)
{
    static const char *const MSG[] = {"ALL ABOARD", "STOPPED", "STALLED, 134 METRES", "STALLED. NEW BEST, 134 METRES",
                                      "STATION, 412 METRES, 6 RUNS CLEAN", "STATION. NEW BEST, 412 METRES, 1 RUN CLEAN"};
    int i, lv, k;
    char t[64];
    uint8_t keep_level = sav.more[SV_COASTER].level;
    uint16_t keep_best = sav.more[SV_COASTER].best;
    for (lv = 1; lv <= CO_LEVELS; lv++) {   /* each level: its readout, standing, its runs said as they begin */
        sav.more[SV_COASTER].level = (uint8_t)lv;
        sav.more[SV_COASTER].best = (uint16_t)(lv == 2 ? 412 : 0);
        t[0] = 0;
        sp_num(t, lv);
        text_cat(t, ", ");
        text_cat(t, CO_LEVEL_SAY[lv - 1]);
        sp_join("LEVEL", t);
        sp_out("READOUT", sp_buf);
        co.state = CO_STAND;
        co_build();
        coaster_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        for (i = 0; i < co.nruns; i++) {
            t[0] = 0;
            run_text(i, t, 1);
            sp_out("MSG", t);
        }
    }
    for (k = 0; k < 5; k++)              /* every kind of run, in a minor key and with sevenths */
        for (i = 0; i < 3; i++) {
            co.run[0].root = (uint8_t)(i == 2 ? 6 : 9);
            co.run[0].minor = (uint8_t)(i != 1);
            co.run[0].kind = (uint8_t)k;
            co.run[0].chord = (uint8_t)(i == 1 ? 4 : 0);
            co.run[0].seventh = (uint8_t)(i > 0);
            t[0] = 0;
            run_text(0, t, 1);
            sp_out("MSG", t);
        }
    co_build();
    co.state = CO_RIDE;                  /* riding, rolling back, the ends */
    co.nx = co.run[0].first;
    co.xmax = co.x = 200.0f;
    coaster_where(sp_buf);
    sp_out("SCREEN", sp_buf);
    co.state = CO_ROLL;
    coaster_where(sp_buf);
    sp_out("SCREEN", sp_buf);
    co.state = CO_END;
    co.dist = 134;
    co.runs_clean = 2;
    for (i = 0; i < 4; i++) {
        co.end_kind = i & 1 ? CO_STATION : CO_STALLED;
        co.new_best = i >> 1;
        coaster_where(sp_buf);
        sp_out("SCREEN", sp_buf);
    }
    for (i = 0; i < (int)(sizeof MSG / sizeof MSG[0]); i++)
        sp_out("MSG", MSG[i]);
    for (i = 1; i <= 2; i++) {
        t[0] = 0;
        text_cat(t, "RUN ");
        sp_num(t, i);
        sp_join(t, "G MAJOR SCALE UP FROM G3");
        sp_out("MSG", sp_buf);
    }
    co.state = CO_STAND;
    sav.more[SV_COASTER].level = keep_level;
    sav.more[SV_COASTER].best = keep_best;
}
#endif

static const uint8_t COASTER_TUNE[] = {60, 62, 64, 65, 67, 69, 71, 72, 0, 76, 72, 67, 64, 60, 0, 0};

static const game_t GAME_COASTER = {"COASTER", CO_CAB, coaster_enter, coaster_leave, coaster_frame, coaster_key,
                                    coaster_button, coaster_enc, coaster_tick, coaster_attract, coaster_best,
                                    coaster_keys_lit, COASTER_TUNE, sizeof COASTER_TUNE, 170, BW_SAW, coaster_where, 1,
                                    coaster_playing, "PLAY THE TRACK", 0};
