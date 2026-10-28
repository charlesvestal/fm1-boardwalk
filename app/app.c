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

#include "ringing.c"
#include "art.c"
#include "games/bells.c"
#include "scene.c"

/* --------------------------------------------------------------- cabinets --- */
/* the cabinets still being built: a marquee and an attract screen, not open yet */
static void pitch_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    int32_t i, k = (int32_t)(ms / 600u % 8u);
    for (i = 0; i < 8; i++)              /* a keyboard, one key lit at a time */
        cv_rect(x + 3 + i * (w - 6) / 8, y + h / 3, (w - 6) / 8 - 1, h / 2, i == k ? RGB(126, 224, 184) : G_PAPER);
}
static void tiles_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    int32_t i;
    for (i = 0; i < 5; i++) {            /* tiles falling down four lanes */
        int32_t lane = (i * 3 + 1) % 4, ty = (int32_t)((ms / 12u + (uint32_t)i * 23u) % (uint32_t)(h + 12)) - 12;
        cv_rect(x + 2 + lane * (w - 4) / 4, y + clamp(ty, 0, h), (w - 4) / 4 - 2, clamp(ty + 12, 0, h) - clamp(ty, 0, h),
                RGB(242, 163, 58));
    }
}
static const game_t GAME_PITCH = {"PITCH", RGB(62, 154, 176), 0, 0, 0, 0, 0, 0, 0, pitch_attract, 0, 0};
static const game_t GAME_TILES = {"TILES", RGB(242, 163, 58), 0, 0, 0, 0, 0, 0, 0, tiles_attract, 0, 0};
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
    sav.bells.method = 4;                /* Plain Bob Minor */
    sav.bells.bell = 0;                  /* listening, the first time */
    sav.bells.pace = 22;
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
}
void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12) { bw_render(lr, n, gain_q12); }
void app_panic(void) { bw_panic(); }

/* --------------------------------------------------------------------- UI --- */
static struct {
    const game_t *game;                  /* in front of you; 0 on the boardwalk */
    int32_t pos;                         /* the row's position, cabinets x 256 (slides to sav.cab) */
    uint32_t keys, btn;
    char msg[2][24];
    uint32_t msg_until, drawn_ms;
} ui;

void app_say(const char *a, const char *b)
{
    char t[48];
    t[0] = 0;
    text_cat(t, a);
    if (b && b[0]) {
        text_cat(t, " ");
        text_cat(t, b);
    }
    if (ann_on())
        ann_say(t, 0, ANN_NOW);
}

static void surf_for(const game_t *g)    /* the sea: full on the boardwalk, under a game a quarter */
{
    snd_lock();
    bw_surf(g ? sav.surf / 4 : sav.surf);
    snd_unlock();
}

static void walk(int d)                  /* along the boardwalk */
{
    int c = clamp(sav.cab + d, 0, NGAMES - 1);
    if (c == sav.cab)
        return;
    sav.cab = (uint8_t)c;
    save_dirty();
    app_say(GAMES[c]->name, "");
}

static void step_up(void)                /* PLAY: into the cabinet in front */
{
    const game_t *g = GAMES[sav.cab];
    if (!g->enter) {
        app_say(g->name, "SOON");
        return;
    }
    ui.game = g;
    g->enter();
    surf_for(g);
    app_say(g->name, "");
}

static void step_down(void)              /* HOME: back to the boardwalk */
{
    if (ui.game && ui.game->leave)
        ui.game->leave();
    ui.game = 0;
    surf_for(0);
    app_say(APP_NAME, GAMES[sav.cab]->name);
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), now = plat_ms(), k;
    const game_t *g = ui.game;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys ^ ui.keys) & bit && g && g->key)
            g->key((int)k, (keys & bit) != 0, now);
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
    }
    ui.keys = keys;
    ui.btn = btn;
}

void app_input_only(void) { input(); }

void app_tick(void)
{
    const game_t *g = ui.game;
    if (g && g->tick)
        g->tick(plat_ms());
}

void app_ui_init(void)
{
    ui.pos = sav.cab * 256;
    surf_for(0);
    lcd_fill(0, 0, 240, 240, G_PLANK);
}

static void lights(void)
{
    const game_t *g = ui.game;
    uint32_t b, keys = 0;
    plat_glow(1);
    if (g) {
        b = 1u << B_HOME | 1u << B_PLAY;
        plat_play_red(g == &GAME_BELLS && bl.ringing);
        if (g->keys_lit)
            keys = g->keys_lit();
    } else {
        b = (sav.cab > 0 ? 1u << B_OCTDN : 0u) | (sav.cab < NGAMES - 1 ? 1u << B_OCTUP : 0u) |
            (GAMES[sav.cab]->enter ? 1u << B_PLAY : 0u);
        plat_play_red(0);
    }
    plat_leds(b, keys);
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    input();
    lights();
    ann_tick(now);
    /* save when nothing is ringing (a flash write holds the sound a moment) */
    if (dirty && now - dirty_ms > 1500u && !(ui.game == &GAME_BELLS && bl.ringing)) {
        dirty = 0;
        plat_store_save(OBJ_SAVE, &sav, sizeof sav);
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
        scene_draw(GAMES, NGAMES, ui.pos, now);
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
    else if (!strcmp(what, "blows"))     /* your blows judged */
        *got = bl.blows;
    else if (!strcmp(what, "method"))
        *got = sav.bells.method;
    else if (!strcmp(what, "bell"))
        *got = sav.bells.bell;
    else if (!strcmp(what, "best"))
        *got = sav.bells.best[sav.bells.method];
    else
        return 0;
    return 1;
}
#endif
