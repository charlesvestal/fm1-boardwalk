/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Tiles: Piano Tiles on the FM-1. Tiles fall down four lanes, one lane for each four white keys (F-B, C-F, G-C,
 * D-G); hit a tile as it reaches the line and it plays the melody's next notes, so four lanes play the whole tune. A
 * miss: a clunk, and the tune drops out until the next tile. The bass plays on regardless, keeping time.
 *
 * The tiles are made from the melody: on HARD a tile for every note, on NORMAL one a beat at most (a tile carries
 * the notes until the next), on EASY one every two beats. A tile's lane follows the tune's contour: up, a lane to the
 * right; down, to the left.
 *
 * Standing: K1 the song, K2 the level, K3 the tempo; PLAY starts and stops. Part of the unity build. */
#include "game.h"

/* --------------------------------------------------------------- the songs --- */
/* public-domain melodies, written out here: (MIDI note, length in sixteenths), 0 a rest. The bass the same way,
 * written an octave up (the bass timbre sounds an octave down) */
typedef struct {
    uint8_t note, len;
} tn_t;
typedef struct {
    const char *name;
    uint16_t unit_ms;                    /* a sixteenth at 100 % */
    uint8_t beat, beats;                 /* sixteenths a beat, beats a bar (the count-in) */
    const tn_t *mel, *bass;
    uint8_t nmel, nbass;
} song_t;

#define C3 48
#define D3 50
#define E3 52
#define F3 53
#define G3 55
#define A3 57
#define B3 59
#define C4 60
#define D4 62
#define E4 64
#define F4 65
#define FS4 66
#define G4 67
#define GS4 68
#define A4 69
#define B4 71
#define C5 72
#define D5 74
#define DS5 75
#define E5 76
#define F5 77

static const tn_t ODE[] = {
    {E4, 4}, {E4, 4}, {F4, 4}, {G4, 4}, {G4, 4}, {F4, 4}, {E4, 4}, {D4, 4}, {C4, 4}, {C4, 4}, {D4, 4}, {E4, 4},
    {E4, 6}, {D4, 2}, {D4, 8},
    {E4, 4}, {E4, 4}, {F4, 4}, {G4, 4}, {G4, 4}, {F4, 4}, {E4, 4}, {D4, 4}, {C4, 4}, {C4, 4}, {D4, 4}, {E4, 4},
    {D4, 6}, {C4, 2}, {C4, 8},
    {D4, 4}, {D4, 4}, {E4, 4}, {C4, 4}, {D4, 4}, {E4, 2}, {F4, 2}, {E4, 4}, {C4, 4},
    {D4, 4}, {E4, 2}, {F4, 2}, {E4, 4}, {D4, 4}, {C4, 4}, {D4, 4}, {G3, 8},
    {E4, 4}, {E4, 4}, {F4, 4}, {G4, 4}, {G4, 4}, {F4, 4}, {E4, 4}, {D4, 4}, {C4, 4}, {C4, 4}, {D4, 4}, {E4, 4},
    {D4, 6}, {C4, 2}, {C4, 8}};
static const tn_t ODE_BASS[] = {
    {C4, 8}, {C4, 8}, {G3, 8}, {G3, 8}, {C4, 8}, {C4, 8}, {G3, 8}, {G3, 8},
    {C4, 8}, {C4, 8}, {G3, 8}, {G3, 8}, {C4, 8}, {C4, 8}, {G3, 8}, {C4, 8},
    {G3, 8}, {C4, 8}, {G3, 8}, {C4, 8}, {G3, 8}, {C4, 8}, {G3, 8}, {G3, 8},
    {C4, 8}, {C4, 8}, {G3, 8}, {G3, 8}, {C4, 8}, {C4, 8}, {G3, 8}, {C4, 8}};

static const tn_t TWINKLE[] = {
    {C4, 4}, {C4, 4}, {G4, 4}, {G4, 4}, {A4, 4}, {A4, 4}, {G4, 8}, {F4, 4}, {F4, 4}, {E4, 4}, {E4, 4},
    {D4, 4}, {D4, 4}, {C4, 8}, {G4, 4}, {G4, 4}, {F4, 4}, {F4, 4}, {E4, 4}, {E4, 4}, {D4, 8},
    {G4, 4}, {G4, 4}, {F4, 4}, {F4, 4}, {E4, 4}, {E4, 4}, {D4, 8}, {C4, 4}, {C4, 4}, {G4, 4}, {G4, 4},
    {A4, 4}, {A4, 4}, {G4, 8}, {F4, 4}, {F4, 4}, {E4, 4}, {E4, 4}, {D4, 4}, {D4, 4}, {C4, 8}};
static const tn_t TWINKLE_BASS[] = {
    {C4, 8}, {C4, 8}, {F3, 8}, {C4, 8}, {F3, 8}, {C4, 8}, {G3, 8}, {C4, 8},
    {C4, 8}, {F3, 8}, {C4, 8}, {G3, 8}, {C4, 8}, {F3, 8}, {C4, 8}, {G3, 8},
    {C4, 8}, {C4, 8}, {F3, 8}, {C4, 8}, {F3, 8}, {C4, 8}, {G3, 8}, {C4, 8}};

static const tn_t JACQUES[] = {
    {C4, 4}, {D4, 4}, {E4, 4}, {C4, 4}, {C4, 4}, {D4, 4}, {E4, 4}, {C4, 4},
    {E4, 4}, {F4, 4}, {G4, 8}, {E4, 4}, {F4, 4}, {G4, 8},
    {G4, 2}, {A4, 2}, {G4, 2}, {F4, 2}, {E4, 4}, {C4, 4}, {G4, 2}, {A4, 2}, {G4, 2}, {F4, 2}, {E4, 4}, {C4, 4},
    {C4, 4}, {G3, 4}, {C4, 8}, {C4, 4}, {G3, 4}, {C4, 8}};
static const tn_t JACQUES_BASS[] = {
    {C4, 8}, {G3, 8}, {C4, 8}, {G3, 8}, {C4, 8}, {G3, 8}, {C4, 8}, {G3, 8},
    {C4, 8}, {G3, 8}, {C4, 8}, {G3, 8}, {C4, 8}, {G3, 8}, {C4, 8}, {C4, 8}};

static const tn_t GREENSLEEVES[] = {
    {A4, 4},
    {C5, 8}, {D5, 4}, {E5, 6}, {F5, 2}, {E5, 4}, {D5, 8}, {B4, 4}, {G4, 6}, {A4, 2}, {B4, 4},
    {C5, 8}, {A4, 4}, {A4, 6}, {GS4, 2}, {A4, 4}, {B4, 8}, {GS4, 4}, {E4, 8}, {A4, 4},
    {C5, 8}, {D5, 4}, {E5, 6}, {F5, 2}, {E5, 4}, {D5, 8}, {B4, 4}, {G4, 6}, {A4, 2}, {B4, 4},
    {C5, 6}, {B4, 2}, {A4, 4}, {GS4, 6}, {FS4, 2}, {GS4, 4}, {A4, 12}, {A4, 12}};
static const tn_t GREENSLEEVES_BASS[] = {
    {0, 4},
    {A3, 12}, {C4, 12}, {G3, 12}, {E3, 12}, {A3, 12}, {E3, 12}, {E3, 12}, {A3, 12},
    {A3, 12}, {C4, 12}, {G3, 12}, {E3, 12}, {A3, 12}, {E3, 12}, {A3, 12}, {A3, 12}};

static const tn_t ELISE[] = {
    {E5, 1}, {DS5, 1},
    {E5, 1}, {DS5, 1}, {E5, 1}, {B4, 1}, {D5, 1}, {C5, 1}, {A4, 2}, {0, 1}, {C4, 1}, {E4, 1}, {A4, 1},
    {B4, 2}, {0, 1}, {E4, 1}, {GS4, 1}, {B4, 1}, {C5, 2}, {0, 1}, {E4, 1}, {E5, 1}, {DS5, 1},
    {E5, 1}, {DS5, 1}, {E5, 1}, {B4, 1}, {D5, 1}, {C5, 1}, {A4, 2}, {0, 1}, {C4, 1}, {E4, 1}, {A4, 1},
    {B4, 2}, {0, 1}, {E4, 1}, {C5, 1}, {B4, 1}, {A4, 6}};
static const tn_t ELISE_BASS[] = {
    {0, 2}, {0, 6}, {A3, 6}, {E3, 6}, {A3, 6}, {0, 6}, {A3, 6}, {E3, 6}, {A3, 6}};

#define N(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))
static const song_t SONGS[] = {
    {"ODE TO JOY", 150, 4, 4, ODE, ODE_BASS, N(ODE), N(ODE_BASS)},
    {"TWINKLE, TWINKLE", 150, 4, 4, TWINKLE, TWINKLE_BASS, N(TWINKLE), N(TWINKLE_BASS)},
    {"FRERE JACQUES", 135, 4, 4, JACQUES, JACQUES_BASS, N(JACQUES), N(JACQUES_BASS)},
    {"GREENSLEEVES", 130, 4, 3, GREENSLEEVES, GREENSLEEVES_BASS, N(GREENSLEEVES), N(GREENSLEEVES_BASS)},
    {"FUR ELISE", 125, 2, 3, ELISE, ELISE_BASS, N(ELISE), N(ELISE_BASS)},
};
#define NSONGS ((int)(sizeof SONGS / sizeof SONGS[0]))
#undef N

/* ------------------------------------------------------------------ the play --- */
#define TL_MAX 96                        /* notes (and tiles) a song */
#define TL_LOOK 1600                     /* ms a tile falls, top to the line */
#define TL_PERFECT 60                    /* ms */
#define TL_GOOD 150
#define TL_LINE 194                      /* the hit line's y */
enum { TS_WAIT, TS_HIT, TS_MISS };
enum { TP_STAND, TP_PLAY, TP_DONE };

static struct {
    int state;
    const song_t *song;
    int32_t unit;                        /* ms a sixteenth, at the tempo chosen */
    /* the melody's notes and the tiles made of them */
    int nnotes, ntiles;
    int32_t note_t[TL_MAX];              /* ms from the start */
    uint8_t note_k[TL_MAX], note_tile[TL_MAX];
    int32_t tile_t[TL_MAX], tile_len[TL_MAX];
    uint8_t tile_lane[TL_MAX], tile_first[TL_MAX];
    volatile uint8_t tile_state[TL_MAX];
    int32_t tile_dt[TL_MAX];             /* how early (-) or late it was hit */
    /* the tick's place */
    volatile uint32_t t0;                /* plat_ms of the song's start */
    volatile int mel_i, bass_i, count_i, notes_played;
    int32_t bass_t[TL_MAX];
    int nbass;
    int32_t end_ms;
    /* the judging */
    int judged, perfect, good, combo, best_combo, pct, new_best;
    int stray;                           /* presses with no tile to hit */
    uint32_t flash_ms[4];                /* a lane's key pressed: it flashes */
    uint8_t flash_bad[4];
} tl;

static void tiles_build(void)
{
    const song_t *s = &SONGS[sav.tiles.song % NSONGS];
    static const uint8_t SPACING[3] = {8, 4, 0};   /* sixteenths between tiles, at least */
    int32_t t = 0, sp = SPACING[sav.tiles.level % 3u];
    int i, lane = 1, dir = 1, prev = -1;
    tl.song = s;
    tl.unit = (int32_t)s->unit_ms * 100 / clamp(sav.tiles.tempo, 60, 140);
    tl.nnotes = tl.ntiles = 0;
    for (i = 0; i < s->nmel && tl.nnotes < TL_MAX; i++) {
        const tn_t *n = &s->mel[i];
        if (n->note) {
            int nn = tl.nnotes++;
            tl.note_t[nn] = t * tl.unit;
            tl.note_k[nn] = n->note;
            if (!tl.ntiles || t * tl.unit - tl.tile_t[tl.ntiles - 1] >= sp * tl.unit) {
                int tt = tl.ntiles++;
                if (prev >= 0) {         /* the lane follows the tune */
                    if (n->note > prev)
                        lane++;
                    else if (n->note < prev)
                        lane--;
                    else
                        lane += dir, dir = -dir;
                    lane = lane > 3 ? 2 : lane < 0 ? 1 : lane;
                }
                prev = n->note;
                tl.tile_t[tt] = t * tl.unit;
                tl.tile_lane[tt] = (uint8_t)lane;
                tl.tile_first[tt] = (uint8_t)nn;
                tl.tile_state[tt] = TS_WAIT;
            }
            tl.note_tile[nn] = (uint8_t)(tl.ntiles - 1);
        }
        t += n->len;
    }
    for (i = 0; i < tl.ntiles; i++)      /* each tile as long as it lasts, to the next */
        tl.tile_len[i] = (i + 1 < tl.ntiles ? tl.tile_t[i + 1] : t * tl.unit) - tl.tile_t[i];
    tl.end_ms = t * tl.unit;
    t = 0;
    tl.nbass = 0;
    for (i = 0; i < s->nbass && i < TL_MAX; i++) {
        tl.bass_t[i] = t * tl.unit;
        t += s->bass[i].len;
        tl.nbass++;
    }
}

static int32_t lead_ms(void)             /* the start, after the count-in and a tile's fall */
{
    int32_t count = (int32_t)(tl.song->beat * tl.song->beats) * tl.unit;
    return (count > TL_LOOK ? count : TL_LOOK) + 300;
}

static void tiles_go(void)
{
    tiles_build();
    tl.judged = tl.perfect = tl.good = tl.combo = tl.best_combo = tl.stray = 0;
    tl.mel_i = tl.bass_i = tl.notes_played = 0;
    tl.count_i = tl.song->beats;
    tl.t0 = plat_ms() + (uint32_t)lead_ms();
    tl.state = TP_PLAY;
    app_say(tl.song->name, "");
}

static void tiles_done(void)
{
    tl.state = TP_DONE;
    tl.pct = tl.nnotes ? tl.notes_played * 100 / tl.nnotes : 0;
    tl.new_best = 0;
    if (tl.pct > sav.tiles.best[sav.tiles.song % 16u]) {
        sav.tiles.best[sav.tiles.song % 16u] = (uint8_t)tl.pct;
        tl.new_best = 1;
        save_dirty();
    }
}

/* the tick: the count-in, the bass, and the melody's notes of the tiles hit */
static void tiles_tick(uint32_t ms)
{
    int32_t t = (int32_t)(ms - tl.t0);
    if (tl.state != TP_PLAY)
        return;
    if (tl.count_i > 0 && t >= -tl.count_i * (int32_t)tl.song->beat * tl.unit) {
        bw_note(tl.count_i == tl.song->beats ? 96 : 91, 60, BW_PLUCK);
        tl.count_i--;
    }
    while (tl.bass_i < tl.nbass && tl.bass_t[tl.bass_i] <= t) {
        uint8_t k = tl.song->bass[tl.bass_i].note;
        if (k)
            bw_note(k, 90, BW_BASS);
        tl.bass_i++;
    }
    while (tl.mel_i < tl.nnotes) {
        int i = tl.mel_i, tile = tl.note_tile[i], st = tl.tile_state[tile];
        int first = tl.tile_first[tile] == i;
        if (st == TS_MISS) {             /* its tile missed: silent */
            tl.mel_i++;
            continue;
        }
        if (st == TS_WAIT || (!first && tl.note_t[i] > t))
            break;                       /* not yet judged, or not yet due (a tile's first note: when hit, early too) */
        bw_note(tl.note_k[i], 100, BW_PIANO);
        tl.notes_played++;
        tl.mel_i++;
    }
}

#define TL_BLACK (1u << 1 | 1u << 3 | 1u << 5 | 1u << 8 | 1u << 10 | 1u << 13 | 1u << 15 | 1u << 17 | 1u << 20 | \
                  1u << 22 | 1u << 25)   /* plat.h PLAT_BLACK_KEYS */
static int lane_of(int k) { return clamp(key_x(k) / 60, 0, 3); }

static void tiles_key(int k, int down, uint32_t ms)
{
    int lane = lane_of(k), i, best = -1;
    int32_t t = (int32_t)(ms - tl.t0), d = 0;
    if (!down)
        return;
    if (tl.state != TP_PLAY) {           /* standing: the keys play */
        snd_lock();
        bw_note(53 + k, 100, BW_PIANO);
        snd_unlock();
        return;
    }
    tl.flash_ms[lane] = ms ? ms : 1;
    for (i = 0; i < tl.ntiles; i++) {    /* the waiting tile in this lane nearest in time, near enough */
        int32_t di = t - tl.tile_t[i];
        if (tl.tile_state[i] != TS_WAIT || tl.tile_lane[i] != lane || di < -TL_GOOD || di > TL_GOOD)
            continue;
        if (best < 0 || (di < 0 ? -di : di) < (d < 0 ? -d : d)) {
            best = i;
            d = di;
        }
    }
    if (best < 0) {                      /* nothing there: a clunk, the combo gone */
        tl.flash_bad[lane] = 1;
        tl.combo = 0;
        tl.stray++;
        snd_lock();
        bw_note(48, 90, BW_CLUNK);
        snd_unlock();
        return;
    }
    tl.flash_bad[lane] = 0;
    tl.tile_dt[best] = d;
    tl.tile_state[best] = TS_HIT;
    tl.judged++;
    if (d >= -TL_PERFECT && d <= TL_PERFECT)
        tl.perfect++;
    else
        tl.good++;
    tl.combo++;
    tl.best_combo = tl.combo > tl.best_combo ? tl.combo : tl.best_combo;
}

static void tiles_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (tl.state == TP_PLAY) {
        tiles_done();
        app_say("STOPPED", "");   /* its result follows on the screen, and on WHERE */
    } else {
        tiles_go();
    }
}

static const char *const LEVEL_NAME[3] = {"EASY", "NORMAL", "HARD"};

static void tiles_enc(int role, int steps)
{
    char t[16];
    if (tl.state == TP_PLAY)
        return;
    if (role == EN_K1) {
        sav.tiles.song = (uint8_t)clamp(sav.tiles.song + steps, 0, NSONGS - 1);
        app_readout("SONG", SONGS[sav.tiles.song].name);
    } else if (role == EN_K2) {
        sav.tiles.level = (uint8_t)clamp(sav.tiles.level + steps, 0, 2);
        app_readout("LEVEL", LEVEL_NAME[sav.tiles.level]);
    } else if (role == EN_K3) {
        sav.tiles.tempo = (uint8_t)clamp(sav.tiles.tempo + 5 * steps, 60, 140);
        int_text(sav.tiles.tempo, t);
        text_cat(t, "%");
        app_readout("TEMPO", t);
    } else {
        return;
    }
    tl.state = TP_STAND;
    save_dirty();
}

static void tiles_enter(void)
{
    tl.state = TP_STAND;
    tiles_build();
}

static void tiles_leave(void)
{
    if (tl.state == TP_PLAY)
        tiles_done();
    tl.state = TP_STAND;
}

static uint16_t tiles_key_col(int k)     /* the lanes on the keys, alternately shaded; a pressed lane lit */
{
    int lane = lane_of(k);
    uint32_t now = plat_ms();
    if (tl.flash_ms[lane] && now - tl.flash_ms[lane] < 120u)
        return tl.flash_bad[lane] ? G_RED : G_TEAL;
    return (lane & 1) && !(TL_BLACK >> k & 1u) ? G_PAPER2 : 0;
}

static void tiles_draw(uint32_t now)
{
    int32_t y0, i, t = (int32_t)(now - tl.t0);
    char s[32];
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, G_PAPER);
        cv_rect(0, 0, 240, 18, G_ORANGE);
        cv_text(6, 3, &FONT_XS, SONGS[sav.tiles.song % NSONGS].name, G_INK);
        if (tl.state == TP_STAND) {
            static const char *const LAB[3] = {"K1 SONG", "K2 LEVEL", "K3 TEMPO"};
            for (i = 0; i < 3; i++) {
                cv_text(14, 38 + i * 26, &FONT_XS, LAB[i], G_PLANK2);
                s[0] = 0;
                if (i == 0)
                    text_cat(s, SONGS[sav.tiles.song % NSONGS].name);
                else if (i == 1)
                    text_cat(s, LEVEL_NAME[sav.tiles.level % 3u]);
                else {
                    int_text(sav.tiles.tempo, s);
                    text_cat(s, "%");
                }
                cv_text(94, 36 + i * 26, &FONT_B, s, G_INK);
            }
            if (sav.tiles.best[sav.tiles.song % 16u]) {
                text_cat((s[0] = 0, s), "BEST ");
                int_text(sav.tiles.best[sav.tiles.song % 16u], s + 5);
                text_cat(s, "%");
                cv_text(14, 128, &FONT_B, s, G_INK);
            }
            cv_text(14, 160, &FONT_XS, "FOUR LANES, FOUR WHITE KEYS EACH", G_PLANK2);
        } else {
            /* the lanes, the tiles falling, the line */
            for (i = 1; i < 4; i++)
                cv_rect(i * 60, 18, 1, TL_LINE - 18, G_PAPER2);
            for (i = 0; i < tl.ntiles; i++) {
                int32_t dt = tl.tile_t[i] - t, yb, h;
                if (dt > TL_LOOK || dt < -600)
                    continue;
                yb = TL_LINE - dt * (TL_LINE - 18) / TL_LOOK;
                h = tl.tile_len[i] * (TL_LINE - 18) / TL_LOOK;
                h = clamp(h - 3, 16, 80);
                if (tl.tile_state[i] == TS_HIT && yb > TL_LINE + 20)
                    continue;
                cv_rect(tl.tile_lane[i] * 60 + 4, clamp(yb - h, 18, 240), 52, yb - clamp(yb - h, 18, 240),
                        tl.tile_state[i] == TS_HIT ? mix(G_TEAL, G_PAPER, 150) : tl.tile_state[i] == TS_MISS ? G_RED : G_INK);
            }
            cv_rect(0, TL_LINE, 240, 2, G_TEAL);
            if (tl.state == TP_PLAY && tl.combo > 1) {
                int_text(tl.combo, s);
                text_mid(120, 40, &FONT_L, s, G_ORANGE);
            }
            if (tl.state == TP_DONE) {
                round_rect(30, 50, 180, 110, 8, G_PAPER);
                int_text(tl.pct, s);
                text_cat(s, "%");
                text_mid(120, 60, &FONT_L, s, G_INK);
                text_mid(120, 100, &FONT_B, tl.new_best ? "NEW BEST!" : tl.pct >= 95 ? "BRAVO" : tl.pct >= 70 ? "NICE" : "KEEP AT IT",
                         G_RED);
                text_cat((s[0] = 0, s), "PERFECT ");
                int_text(tl.perfect, s + 8);
                text_mid(120, 124, &FONT_XS, s, G_PLANK2);
                text_cat((s[0] = 0, s), "BEST COMBO ");
                int_text(tl.best_combo, s + 11);
                text_mid(120, 140, &FONT_XS, s, G_PLANK2);
            }
        }
        if (tl.state != TP_STAND && tl.nnotes) {
            int_text(tl.notes_played * 100 / tl.nnotes, s);
            text_cat(s, "%");
            cv_text(234 - text_w(&FONT_XS, s), 3, &FONT_XS, s, G_INK);
        }
        keyboard(198, 20, tiles_key_col);
        cv_rect(0, 220, 240, 20, G_LILAC);
        cv_text(8, 224, &FONT_XS, LEVEL_NAME[sav.tiles.level % 3u], G_INK);
        cv_text(232 - text_w(&FONT_XS, tl.state == TP_PLAY ? "PLAY: STOP" : "PLAY: START"), 224, &FONT_XS,
                tl.state == TP_PLAY ? "PLAY: STOP" : "PLAY: START", G_INK);
        band_send(y0);
    }
}

static void tiles_frame(uint32_t now)
{
    int32_t t = (int32_t)(now - tl.t0);
    int i;
    if (tl.state == TP_PLAY) {
        for (i = 0; i < tl.ntiles; i++)  /* tiles gone past the line unhit */
            if (tl.tile_state[i] == TS_WAIT && tl.tile_t[i] + TL_GOOD < t) {
                tl.tile_state[i] = TS_MISS;
                tl.judged++;
                tl.combo = 0;
                snd_lock();
                bw_note(48, 90, BW_CLUNK);
                snd_unlock();
            }
        if (t > tl.end_ms + 1500) {
            tiles_done();
            char t[24] = "";
            sp_pct(t, tl.pct);
            text_cat(t, " PLAYED");
            app_say(tl.new_best ? "SONG OVER. NEW BEST" : "SONG OVER", t);
        }
    }
    tiles_draw(now);
}

static void tiles_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    int32_t i;
    for (i = 0; i < 5; i++) {            /* tiles falling down four lanes */
        int32_t lane = (i * 3 + 1) % 4, ty = (int32_t)((ms / 12u + (uint32_t)i * 23u) % (uint32_t)(h + 12)) - 12;
        cv_rect(x + 2 + lane * (w - 4) / 4, y + clamp(ty, 0, h), (w - 4) / 4 - 2, clamp(ty + 12, 0, h) - clamp(ty, 0, h),
                G_ORANGE);
    }
}

static void tiles_best(char *t)
{
    int i, best = 0;
    for (i = 0; i < NSONGS; i++)
        best = sav.tiles.best[i] > best ? sav.tiles.best[i] : best;
    t[0] = 0;
    if (best) {
        text_cat(t, "BEST ");
        int_text(best, t + 5);
        text_cat(t, "%");
    }
}

static void tiles_where(char *t)         /* the screen, for a screen reader */
{
    t[0] = 0;
    if (tl.state == TP_PLAY) {
        text_cat(t, "TILES, ");
        text_cat(t, tl.song->name);
        text_cat(t, ". ");
        sp_pct(t, tl.nnotes ? tl.notes_played * 100 / tl.nnotes : 0);
        text_cat(t, " PLAYED. PLAY STOPS.");
        return;
    }
    if (tl.state == TP_DONE) {
        text_cat(t, "TILES, ");
        text_cat(t, tl.song->name);
        text_cat(t, ". ");
        sp_pct(t, tl.pct);
        text_cat(t, " PLAYED, PERFECT ");
        sp_num(t, tl.perfect);
        text_cat(t, ", BEST COMBO ");
        sp_num(t, tl.best_combo);
        text_cat(t, ". ");
    } else {
        text_cat(t, "TILES. ");
    }
    text_cat(t, "KNOB 1, SONG ");
    text_cat(t, SONGS[sav.tiles.song % NSONGS].name);
    text_cat(t, ". KNOB 2, LEVEL ");
    text_cat(t, LEVEL_NAME[sav.tiles.level % 3u]);
    text_cat(t, ". KNOB 3, TEMPO ");
    sp_pct(t, sav.tiles.tempo);
    if (sav.tiles.best[sav.tiles.song % 16u]) {
        text_cat(t, ". BEST ");
        sp_pct(t, sav.tiles.best[sav.tiles.song % 16u]);
    }
    text_cat(t, ". FOUR LANES, FOUR WHITE KEYS EACH. PLAY STARTS.");
}

/* the attract tune: Ode to Joy's first line */
static const uint8_t TILES_TUNE[] = {64, 64, 65, 67, 67, 65, 64, 62, 60, 60, 62, 64, 64, 0, 62, 62, 0, 0};

static const game_t GAME_TILES = {"TILES", RGB(242, 163, 58), tiles_enter, tiles_leave, tiles_frame, tiles_key,
                                  tiles_button, tiles_enc, tiles_tick, tiles_attract, tiles_best, 0,
                                  TILES_TUNE, sizeof TILES_TUNE, 280, BW_PIANO, tiles_where};
