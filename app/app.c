/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Boardwalk on fm1-base: the shell and the app's unity build (compiled into fm1-base's, after the platform: gfx.c and
 * announce.c are in scope). The shell owns the boardwalk (scene.c), moving between it and a game (games/game.h),
 * HOME, MASTER, saving and the lights; the sound engine is dsp/bw.c, its own -O2 unit.
 *
 * Controls: on the boardwalk, OCT- / OCT+ (or SELECT) walk along the cabinets and PLAY steps up to one. In a game,
 * HOME goes back to the boardwalk and everything else is the game's. MASTER is always the volume. */
#include "fastmath.h"

save_t sav;

/* ------------------------------------------------------------------- text --- */
static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static uint32_t text_len(const char *s)  /* (the firmware's libc has no strlen) */
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

static void int_text(int32_t v, char *b)     /* signed, no padding */
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

/* the sound engine's commands come from the main loop and from the tick (a game's bells on time): one at a time */
static uint32_t snd_f;
static void snd_lock(void) { snd_f = plat_lock(); }
static void snd_unlock(void) { plat_unlock(snd_f); }

/* the screen reader (announce.c, fm1-reader): what the player did is answered at once; a knob's value when the knob
 * settles; the whole screen when the reader asks (speak.c). The text is built in sp_buf, not on the stack */
static char sp_buf[ANN_MAX + 1];

static void sp_join(const char *a, const char *b)
{
    sp_buf[0] = 0;
    text_cat(sp_buf, a);
    if (b && b[0]) {
        text_cat(sp_buf, ", ");
        text_cat(sp_buf, b);
    }
}
void app_say(const char *a, const char *b)
{
    if (!ann_on())
        return;
    sp_join(a, b);
    ann_say(sp_buf, 0, ANN_NOW);
}
static void app_warn(const char *a, const char *b)   /* heard whole */
{
    if (!ann_on())
        return;
    sp_join(a, b);
    ann_say(sp_buf, 0, ANN_NOW | ANN_KEEP);
}
static void app_readout(const char *a, const char *b)   /* a knob's value, once it settles */
{
    if (!ann_on())
        return;
    sp_join(a, b);
    ann_later(sp_buf, ANN_NOW, 120u, plat_ms());
}
static void sp_num(char *t, int32_t v) { int_text(v, t + text_len(t)); }
static void sp_pct(char *t, int32_t v)
{
    sp_num(t, v);
    text_cat(t, "%");
}

#include "ringing.c"
#include "art.c"
#include "games/bells.c"
#include "games/pitch.c"
#include "games/tiles.c"
#include "scene.c"

/* --------------------------------------------------------------- cabinets --- */
static const game_t *const GAMES[] = {&GAME_BELLS, &GAME_PITCH, &GAME_TILES};
#define NGAMES ((int)(sizeof GAMES / sizeof GAMES[0]))

/* ------------------------------------------------------------------- save --- */
static uint8_t dirty;
static uint32_t dirty_ms;
void save_dirty(void)
{
    dirty = 1;
    dirty_ms = plat_ms();
}

static void save_defaults(void)
{
    memset(&sav, 0, sizeof sav);
    sav.magic = SAVE_MAGIC;
    sav.format = SAVE_FORMAT;
    sav.surf = 60;
    sav.tunes = 40;
    sav.bells.method = 4;                /* Plain Bob Minor */
    sav.bells.bell = 0;                  /* listening, the first time */
    sav.bells.pace = 22;
    sav.pitch.level = 1;
    sav.tiles.level = 1;
    sav.tiles.tempo = 100;
}

/* ---------------------------------------------------------------- contract --- */
void app_init(void)
{
    bw_init();
    if (plat_store_load(OBJ_SAVE, &sav, sizeof sav) != (int)sizeof sav || sav.magic != SAVE_MAGIC
        || sav.format != SAVE_FORMAT)
        save_defaults();
    sav.cab = (uint8_t)(sav.cab % NGAMES);
    sav.bells.method = (uint8_t)(sav.bells.method % RING_NMETHODS);
    sav.bells.pace = (uint8_t)clamp(sav.bells.pace, PACE_MIN, PACE_MAX);
    sav.pitch.mode &= 1u;
    sav.pitch.level = (uint8_t)clamp(sav.pitch.level, 1, EAR_LEVELS);
    sav.tiles.song = (uint8_t)(sav.tiles.song % NSONGS);
    sav.tiles.level = (uint8_t)(sav.tiles.level % 3u);
    sav.tiles.tempo = (uint8_t)clamp(sav.tiles.tempo, 60, 140);
    sav.surf = (uint8_t)clamp(sav.surf, 0, 100);
    sav.tunes = (uint8_t)clamp(sav.tunes, 0, 100);
}
void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12) { bw_render(lr, n, gain_q12); }
void app_panic(void) { bw_panic(); }

/* --------------------------------------------------------------------- UI --- */
static struct {
    const game_t *game;                  /* in front of you; 0 on the boardwalk */
    int32_t pos;                         /* the row's position, cabinets x 256 (slides to sav.cab) */
    uint32_t btn;
    char hud[24];                        /* a setting just turned, in place of the page count */
    uint32_t hud_until, drawn_ms;
    /* the attract tune of the cabinet in front: faded out when the row moves, the next one faded in */
    volatile uint32_t moved_ms;
    volatile int tune_cab, tune_i;
    uint32_t tune_next;
} ui;

static void surf_for(const game_t *g)    /* the sea: full on the boardwalk, under a game a quarter */
{
    snd_lock();
    bw_surf(g ? sav.surf / 4 : sav.surf);
    snd_unlock();
}

static void tune_fade(void)              /* the tune out; the tick brings the next one in */
{
    snd_lock();
    bw_attract(0);
    ui.tune_cab = -1;
    ui.moved_ms = plat_ms();
    snd_unlock();
}

static void boardwalk_where(char *t)    /* the boardwalk, for a screen reader */
{
    char b[24];
    t[0] = 0;
    text_cat(t, "BOARDWALK. ");
    text_cat(t, GAMES[sav.cab]->name);
    text_cat(t, ", ");
    sp_num(t, sav.cab + 1);
    text_cat(t, " OF ");
    sp_num(t, NGAMES);
    GAMES[sav.cab]->best(b);
    if (b[0]) {
        text_cat(t, ". ");
        text_cat(t, b);
    }
    text_cat(t, ". OCTAVE BUTTONS WALK, PLAY STEPS UP. KNOB 1, SEA ");
    sp_pct(t, sav.surf);
    text_cat(t, ". KNOB 2, TUNES ");
    sp_pct(t, sav.tunes);
    text_cat(t, ".");
}

static void walk(int d)                  /* along the boardwalk */
{
    int c = clamp(sav.cab + d, 0, NGAMES - 1);
    if (c == sav.cab)
        return;
    sav.cab = (uint8_t)c;
    save_dirty();
    tune_fade();
    if (ann_on()) {                      /* the cabinet, its place in the row, its best */
        char b[24];
        sp_buf[0] = 0;
        text_cat(sp_buf, GAMES[c]->name);
        text_cat(sp_buf, ", ");
        sp_num(sp_buf, c + 1);
        text_cat(sp_buf, " OF ");
        sp_num(sp_buf, NGAMES);
        GAMES[c]->best(b);
        if (b[0]) {
            text_cat(sp_buf, ". ");
            text_cat(sp_buf, b);
        }
        ann_later(sp_buf, ANN_NOW, 200u, plat_ms());
    }
}

static void boardwalk_knob(int role, int steps)   /* K1 the sea, K2 the tunes */
{
    char t[8];
    uint8_t *v = role == EN_K1 ? &sav.surf : &sav.tunes;
    *v = (uint8_t)clamp(*v + 5 * steps, 0, 100);
    int_text(*v, t);
    text_cat(t, "%");
    ui.hud[0] = 0;
    text_cat(ui.hud, role == EN_K1 ? "SEA " : "TUNES ");
    text_cat(ui.hud, t);
    ui.hud_until = plat_ms() + 1200u;
    snd_lock();
    if (role == EN_K1)
        bw_surf(sav.surf);
    else
        bw_attract(ui.tune_cab >= 0 ? sav.tunes : 0);
    snd_unlock();
    save_dirty();
    app_readout(role == EN_K1 ? "SEA" : "TUNES", t);
}

static void step_up(void)                /* PLAY: into the cabinet in front */
{
    const game_t *g = GAMES[sav.cab];
    if (!g->enter) {
        app_say(g->name, "SOON");
        return;
    }
    tune_fade();
    ui.game = g;
    g->enter();
    surf_for(g);
    if (ann_on()) {                      /* a new screen: all of it */
        g->where(sp_buf);
        ann_say(sp_buf, 0, ANN_NOW);
    }
}

static void step_down(void)              /* HOME: back to the boardwalk */
{
    if (ui.game && ui.game->leave)
        ui.game->leave();
    ui.game = 0;
    surf_for(0);
    tune_fade();
    if (ann_on()) {
        boardwalk_where(sp_buf);
        ann_say(sp_buf, 0, ANN_NOW);
    }
}

/* the keys are read by the 1 kHz tick, so each press has its time to the millisecond however long the screen takes
 * to draw: a game that must answer at once has them there (key_in_tick); the others get them here, from a queue */
#define KQ 32u
static uint32_t kq[KQ];                  /* key << 1 | down, << 8 above: the ms (its low 23 bits) */
static volatile uint32_t kq_w, kq_r;
static uint32_t tick_keys, kq_ms[KQ];

static void keys_tick(uint32_t now)      /* the tick */
{
    uint32_t keys = plat_keys(), ch = keys ^ tick_keys, k;
    const game_t *g = ui.game;
    if (!ch)
        return;
    tick_keys = keys;
    for (k = 0; k < NKEYS; k++) {
        if (!(ch >> k & 1u))
            continue;
        if (g && g->key_in_tick) {
            if (g->key)
                g->key((int)k, (int)(keys >> k & 1u), now);
        } else if (kq_w - kq_r < KQ) {
            kq[kq_w % KQ] = k << 1 | (keys >> k & 1u);
            kq_ms[kq_w % KQ] = now;
            kq_w++;
        }
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), k;
    const game_t *g = ui.game;
    while (kq_r != kq_w) {               /* the keys, as the tick saw them */
        uint32_t e = kq[kq_r % KQ], ms = kq_ms[kq_r % KQ];
        kq_r++;
        if (g && g->key && !g->key_in_tick)
            g->key((int)(e >> 1), (int)(e & 1u), ms);
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        int down = (btn & bit) != 0;
        if (!((btn ^ ui.btn) & bit))
            continue;
        if (g) {
            if (k == B_HOME && down)
                step_down();
            else if (g->button)
                g->button((int)k, down);
            g = ui.game;
        } else if (down) {
            if (k == B_OCTDN)
                walk(-1);
            else if (k == B_OCTUP)
                walk(1);
            else if (k == B_PLAY)
                step_up();
            g = ui.game;
        }
    }
    for (k = 0; k < NE; k++) {
        int32_t e = plat_enc((int)k);
        if (!e)
            continue;
        if (g && g->enc)
            g->enc((int)k, (int)e);
        else if (!g && (k == EN_SELECT || k == EN_ALGO))
            walk(e > 0 ? 1 : -1);
        else if (!g && (k == EN_K1 || k == EN_K2))
            boardwalk_knob((int)k, (int)e);
    }
    ui.btn = btn;
}

void app_input_only(void) { input(); }

void app_tick(void)
{
    const game_t *g = ui.game, *c;
    uint32_t now = plat_ms();
    keys_tick(now);
    if (g) {
        if (g->tick)
            g->tick(now);
        return;
    }
    /* on the boardwalk: the attract tune of the cabinet in front, a quarter second after the row stops */
    c = GAMES[sav.cab];
    if (ui.tune_cab != sav.cab) {
        if (now - ui.moved_ms < 250u || !c->tune)
            return;
        ui.tune_cab = sav.cab;
        ui.tune_i = 0;
        ui.tune_next = now;
        bw_attract(sav.tunes);
    }
    if ((int32_t)(now - ui.tune_next) >= 0) {
        uint8_t k = c->tune[ui.tune_i];
        if (k)
            bw_attract_note(k, 80, c->tune_timbre);
        ui.tune_i = (ui.tune_i + 1) % c->tune_len;
        ui.tune_next += c->tune_step + (ui.tune_i == 0 ? 4u * c->tune_step : 0u);   /* a breath before it again */
    }
}

void app_ui_init(void)
{
    ui.pos = sav.cab * 256;
    ui.tune_cab = -1;
    surf_for(0);
    lcd_fill(0, 0, 240, 240, G_PLANK);
}

static void lights(void)
{
    const game_t *g = ui.game;
    uint32_t b, keys = 0, mid = 0;
    plat_glow(1);
    if (g) {
        b = 1u << B_HOME | 1u << B_PLAY;
        plat_play_red((g == &GAME_BELLS && bl.ringing) || (g == &GAME_TILES && tl.state == TP_PLAY) ||
                      (g == &GAME_PITCH && (pt.state == PS_ASK || pt.state == PS_ANSWERED)));
        if (g->keys_lit)
            keys = g->keys_lit(&mid);
    } else {
        b = (sav.cab > 0 ? 1u << B_OCTDN : 0u) | (sav.cab < NGAMES - 1 ? 1u << B_OCTUP : 0u) |
            (GAMES[sav.cab]->enter ? 1u << B_PLAY : 0u);
        plat_play_red(0);
    }
    plat_leds(b, keys);
    plat_lights_mid(0, mid & ~keys);
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    input();
    lights();
    ann_tick(now);
    /* save when nothing is ringing (a flash write holds the sound a moment) */
    if (dirty && now - dirty_ms > 1500u && !(ui.game == &GAME_BELLS && bl.ringing) &&
        !(ui.game == &GAME_TILES && tl.state == TP_PLAY) && !(ui.game == &GAME_PITCH && pt.state == PS_ASK)) {
        dirty = 0;
        if (plat_store_save(OBJ_SAVE, &sav, sizeof sav) != 0)
            app_warn("NOT SAVED", "THE FLASH DID NOT TAKE IT");
    }
    if (ann_where()) {                   /* the reader asks: the whole screen */
        if (ui.game)
            ui.game->where(sp_buf);
        else
            boardwalk_where(sp_buf);
        ann_say(sp_buf, 0, ANN_NOW);
    }
    if (now - ui.drawn_ms < 33u)         /* ~30 frames a second */
        return;
    ui.drawn_ms = now;
    if (ui.game) {
        ui.game->frame(now);
    } else {
        int32_t to = sav.cab * 256, d = to - ui.pos;   /* the row slides, easing in */
        ui.pos += d / 3 + (d > 0) - (d < 0);
        if ((d > 0 && ui.pos > to) || (d < 0 && ui.pos < to))
            ui.pos = to;
        scene_draw(GAMES, NGAMES, ui.pos, now, now < ui.hud_until ? ui.hud : 0);
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "game"))           /* 0 the boardwalk, 1 + the cabinet */
        *got = ui.game ? sav.cab + 1 : 0;
    else if (!strcmp(what, "cab"))
        *got = sav.cab;
    else if (!strcmp(what, "ringing"))
        *got = bl.ringing;
    else if (!strcmp(what, "score"))     /* the last touch's striking, -1 none */
        *got = bl.score;
    else if (!strcmp(what, "keys_lit") || !strcmp(what, "keys_mid")) {   /* the game's key lights */
        uint32_t mid = 0, lit = ui.game && ui.game->keys_lit ? ui.game->keys_lit(&mid) : 0u;
        *got = (int)(what[5] == 'l' ? lit : mid);
    } else if (!strcmp(what, "clashes"))   /* Bells: pulls far too soon */
        *got = bl.clashes;
    else if (!strcmp(what, "blows"))     /* your blows judged */
        *got = bl.blows;
    else if (!strcmp(what, "method"))
        *got = sav.bells.method;
    else if (!strcmp(what, "bell"))
        *got = sav.bells.bell;
    else if (!strcmp(what, "best"))
        *got = sav.bells.best[sav.bells.method];
    else if (!strcmp(what, "surf"))
        *got = sav.surf;
    else if (!strcmp(what, "tune_cab"))  /* the cabinet whose tune plays, -1 none */
        *got = ui.tune_cab;
    else if (!strcmp(what, "pitch_state"))   /* 0 standing, 1 asking, 2 answered, 3 over */
        *got = pt.state;
    else if (!strcmp(what, "pitch_target"))  /* the key to find */
        *got = pt.target;
    else if (!strcmp(what, "pitch_score"))
        *got = pt.score;
    else if (!strcmp(what, "pitch_lives"))
        *got = pt.lives;
    else if (!strcmp(what, "pitch_level"))
        *got = pt.level;
    else if (!strcmp(what, "pitch_err"))     /* the duel: cents off */
        *got = (int)pt.err;
    else if (!strcmp(what, "best_ear"))
        *got = sav.pitch.best_ear;
    else if (!strcmp(what, "tiles_state"))   /* 0 standing, 1 playing, 2 done */
        *got = tl.state;
    else if (!strcmp(what, "tiles_pct"))
        *got = tl.pct;
    else if (!strcmp(what, "tiles_played"))
        *got = tl.notes_played;
    else if (!strcmp(what, "tiles_notes"))
        *got = tl.nnotes;
    else if (!strcmp(what, "clunks_stray"))  /* Tiles: presses that found no tile */
        *got = tl.stray;
    else if (!strcmp(what, "tiles_missed")) {
        int i, n = 0;
        for (i = 0; i < tl.ntiles; i++)
            n += tl.tile_state[i] == TS_MISS;
        *got = n;
    }
    else
        return 0;
    return 1;
}

/* the test player:
 *   answer right|wrong      Pitch EAR: the key of the note asked, or the one above it
 *   autotune on|off         Pitch TUNE: the knobs turned against the drift, as a good ear would
 *   autotiles all|half|off  Tiles: every tile hit on time, or every other one left */
static int host_autotune, host_autotiles;
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)b;
    (void)c;
    (void)d;
    if (!strcmp(cmd, "answer")) {
        pitch_key(!strcmp(a, "right") ? pt.target : (pt.target + 1) % NKEYS, 1, plat_ms());
        return 1;
    }
    if (!strcmp(cmd, "autotune")) {
        host_autotune = !strcmp(a, "on");
        return 1;
    }
    if (!strcmp(cmd, "autotiles")) {
        host_autotiles = !strcmp(a, "all") ? 1 : !strcmp(a, "half") ? 2 : 0;
        return 1;
    }
    return 0;
}

void app_host_ms(void)
{
    static const uint8_t LANE_KEY[4] = {2, 9, 16, 23};   /* a white key in each lane */
    uint32_t now = plat_ms();
    if (host_autotune && ui.game == &GAME_PITCH && pt.state == PS_ASK && now % 50u == 0u) {
        if (pt.err > 1.0f)
            pitch_enc(EN_K2, -1);
        else if (pt.err < -1.0f)
            pitch_enc(EN_K2, 1);
    }
    if (host_autotiles && ui.game == &GAME_TILES && tl.state == TP_PLAY) {
        int32_t t = (int32_t)(now - tl.t0);
        int i;
        for (i = 0; i < tl.ntiles; i++)
            if (tl.tile_state[i] == TS_WAIT && tl.tile_t[i] == t && (host_autotiles == 1 || !(i & 1)))
                tiles_key(LANE_KEY[tl.tile_lane[i]], 1, now);
    }
}

/* every screen, knob value and message as a screen reader gets them (the "speakdump" command): KIND <tab> text, for
 * fm1-reader's tools/audit.mjs. The settings are walked through and put back */
static void sp_out(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }
void app_speak_dump(void)
{
    static const char *const MSG[] = {"LOOK TO, TREBLE'S GOING", "STAND", "STAND, STRUCK 87%", "THAT'S ALL",
                                      "THAT'S ALL, STRUCK 92%", "LISTEN", "RIGHT", "RIGHT. LEVEL UP, NO REFERENCE",
                                      "NO, IT WAS A4", "TOO SLOW, IT WAS C#4", "NO, IT WAS F#3",
                                      "GAME OVER, SCORE 7", "GAME OVER. NEW BEST, SCORE 12", "TUNE IT",
                                      "OUT OF TUNE, SHARP, 12.3 SECONDS IN TUNE",
                                      "OUT OF TUNE, FLAT, 0.4 SECONDS IN TUNE", "STOPPED, 31.0 SECONDS IN TUNE",
                                      "STOPPED", "SONG OVER, 87% PLAYED", "SONG OVER. NEW BEST, 100% PLAYED"};
    save_t keep = sav;
    const game_t *keep_game = ui.game;
    int i, v;
    char t[48];
    for (i = 0; i < NGAMES; i++) {       /* the boardwalk at each cabinet */
        sav.cab = (uint8_t)i;
        boardwalk_where(sp_buf);
        sp_out("SCREEN", sp_buf);
    }
    for (v = 0; v <= 100; v += 50) {
        int_text(v, t);
        text_cat(t, "%");
        sp_join("SEA", t);
        sp_out("READOUT", sp_buf);
        sp_join("TUNES", t);
        sp_out("READOUT", sp_buf);
    }
    /* Bells: every method, every bell, the paces; ringing, listening */
    for (i = 0; i < RING_NMETHODS; i++) {
        sav.bells.method = (uint8_t)i;
        method_name(t);
        sp_join("METHOD", t);
        sp_out("READOUT", sp_buf);
        bells_enter();
        bells_where(sp_buf);
        sp_out("SCREEN", sp_buf);
    }
    for (v = 0; v <= 8; v++) {
        t[0] = 0;
        if (v)
            int_text(v, t);
        sp_join(v ? "BELL" : "LISTENING", t);
        sp_out("READOUT", sp_buf);
    }
    for (v = PACE_MIN; v <= PACE_MAX; v += 8) {
        int_text(v * 10, t);
        text_cat(t, " MS");
        sp_join("PACE", t);
        sp_out("READOUT", sp_buf);
    }
    sav.bells.method = 4;
    sav.bells.best[4] = 87;
    for (v = 0; v <= 2; v++) {
        sav.bells.bell = (uint8_t)v;
        bells_enter();
        bells_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        bl.ringing = 1;
        bl.blows = v;
        bl.err_sum = 10;
        bells_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        bl.ringing = 0;
    }
    /* Pitch */
    for (v = 1; v <= EAR_LEVELS; v++) {
        ear_level_text(v, t);
        sp_join("LEVEL", t);
        sp_out("READOUT", sp_buf);
    }
    sp_join("GAME", "EAR");
    sp_out("READOUT", sp_buf);
    sp_join("GAME", "TUNE");
    sp_out("READOUT", sp_buf);
    sav.pitch.best_ear = 9;
    sav.pitch.best_tune = 141;
    for (i = 0; i < 2; i++) {
        sav.pitch.mode = (uint8_t)i;
        for (v = PS_STAND; v <= PS_OVER; v++) {
            pt.state = v;
            pt.score = 7;
            pt.lives = 2;
            pt.level = 3;
            pt.in_tune_ms = 12345;
            pitch_where(sp_buf);
            sp_out("SCREEN", sp_buf);
        }
    }
    pt.state = PS_STAND;
    /* Tiles */
    for (i = 0; i < NSONGS; i++) {
        sp_join("SONG", SONGS[i].name);
        sp_out("READOUT", sp_buf);
    }
    for (i = 0; i < 3; i++) {
        sp_join("LEVEL", LEVEL_NAME[i]);
        sp_out("READOUT", sp_buf);
    }
    sp_join("TEMPO", "85%");
    sp_out("READOUT", sp_buf);
    sav.tiles.best[0] = 100;
    for (v = TP_STAND; v <= TP_DONE; v++) {
        tiles_build();
        tl.state = v;
        tl.notes_played = 20;
        tl.pct = 87;
        tl.perfect = 30;
        tl.best_combo = 12;
        tiles_where(sp_buf);
        sp_out("SCREEN", sp_buf);
    }
    tl.state = TP_STAND;
    for (i = 0; i < (int)(sizeof MSG / sizeof MSG[0]); i++)
        sp_out("MSG", MSG[i]);
    sp_join("NOT SAVED", "THE FLASH DID NOT TAKE IT");
    sp_out("WARN", sp_buf);
    sav = keep;
    ui.game = keep_game;
}
#endif
