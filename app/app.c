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

#ifdef PLAT_HOST
static void sp_out(const char *kind, const char *t) { printf("%s\t%s\n", kind, t); }   /* the speakdump's lines */
#endif

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
#include "games/echo.c"
#include "games/chord.c"
#include "games/simon.c"
#include "games/whack.c"
#include "games/gallery.c"
#include "games/gull.c"
#include "games/claw.c"
#include "games/striker.c"
#include "games/balloon.c"
#include "games/fish.c"
#include "games/fortune.c"
#include "games/teacups.c"
#include "games/bumpers.c"
#include "games/coaster.c"
#include "scene.c"

/* --------------------------------------------------------------- cabinets --- */
/* the row: two quick, obvious ones first, then the kinds taking turns (timing, reflex, ear, rhythm, a toy to
 * rest on), the deeper ones further along, and the bell tower at the pier's end */
static const game_t *const GAMES[] = {&GAME_STRIKER, &GAME_WHACK, &GAME_BALLOON, &GAME_TILES, &GAME_GULL,
                                      &GAME_GALLERY, &GAME_TEACUPS, &GAME_FORTUNE, &GAME_SIMON, &GAME_BUMPERS,
                                      &GAME_ECHO, &GAME_CLAW, &GAME_PITCH, &GAME_FISH, &GAME_CHORD,
                                      &GAME_COASTER, &GAME_BELLS};
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
    sav.surf = 40;                       /* the sea under the tunes, not over them */
    sav.tunes = 60;
    sav.bells.method = 4;                /* Plain Bob Minor */
    sav.bells.bell = 0;                  /* listening, the first time */
    sav.bells.pace = 22;
    sav.pitch.level = 1;
    sav.tiles.level = 1;
    sav.tiles.tempo = 100;
    sav.echo.tempo = 100;
    sav.echo.level = 1;
    sav.chord.level = sav.whack.level = sav.gallery.level = 1;
    sav.simon.level = 2;
    sav.gull.level = 1;
}

/* ---------------------------------------------------------------- contract --- */
void app_init(void)
{
    bw_init();
    {
        int n;
        memset(&sav, 0, sizeof sav);
        n = plat_store_load(OBJ_SAVE, &sav, sizeof sav);
        if ((n != (int)sizeof sav && n != (int)offsetof(save_t, gull) && n != (int)offsetof(save_t, more)) ||
            sav.magic != SAVE_MAGIC || sav.format != SAVE_FORMAT)   /* (an older save ends where a part begins) */
            save_defaults();
        if (!sav.gull.level)             /* (a save from before Gull) */
            sav.gull.level = 1;
        for (n = 0; n < 8; n++)          /* (from before the later games) */
            if (!sav.more[n].level)
                sav.more[n].level = 1;
    }
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
    if (!sav.echo.tempo)                 /* a save from before Echo */
        sav.echo.tempo = 100;
    sav.echo.tempo = (uint8_t)clamp(sav.echo.tempo, 70, 140);
    sav.echo.level = (uint8_t)clamp(sav.echo.level, 1, EC_LEVELS);
    sav.chord.level = (uint8_t)clamp(sav.chord.level, 1, CC_LEVELS);   /* (0 in a save from before them: 1) */
    sav.whack.level = (uint8_t)clamp(sav.whack.level, 1, WM_LEVELS);
    sav.gallery.level = (uint8_t)clamp(sav.gallery.level, 1, GL_LEVELS);
    sav.simon.level = (uint8_t)clamp(sav.simon.level, 1, 3);
    sav.bells.mode %= 3u;
    sav.gull.mode %= 3u;
    sav.gull.level = (uint8_t)clamp(sav.gull.level, 1, 2);
    sav.claw.level = (uint8_t)clamp(sav.claw.level, 1, CL_LEVELS);
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
    text_cat(t, GAMES[sav.cab]->blurb);
    text_cat(t, ", ");
    sp_num(t, sav.cab + 1);
    text_cat(t, " OF ");
    sp_num(t, NGAMES);
    GAMES[sav.cab]->best(b);
    if (b[0]) {
        text_cat(t, ". ");
        text_cat(t, b);
    }
    text_cat(t, ". PLAY STEPS UP, THE OCTAVE BUTTONS WALK. KNOB 1, SEA ");
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
        text_cat(sp_buf, GAMES[c]->blurb);
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
    if (dirty) {                         /* (the music stops here anyway) */
        dirty = 0;
        if (plat_store_save(OBJ_SAVE, &sav, sizeof sav) != 0)
            app_warn("NOT SAVED", "THE FLASH DID NOT TAKE IT");
    }
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

#ifdef APP_TOUCH
static uint8_t sh_touch_on;              /* lib/touch on: the keys come from it, in the main loop (input). (Not
                                          * touch_live / touch_held: lib/touch.c's own, in the same unity build) */
static uint32_t sh_touch_keys;
#else
#define sh_touch_on 0
#endif

static void keys_tick(uint32_t now)      /* the tick */
{
    uint32_t keys = plat_keys(), ch = keys ^ tick_keys, k;
    const game_t *g = ui.game;
    if (sh_touch_on || !ch)
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

#ifdef APP_TOUCH
static void touch_follow(void)           /* touch on while the game in front wants it, off otherwise */
{
    int want = ui.game && ui.game->touch && ui.game->touch();
    if (want == sh_touch_on)
        return;
    touch_on(want);
    sh_touch_on = (uint8_t)want;
    sh_touch_keys = 0;
    tick_keys = plat_keys();
}
#endif

static void input(void)
{
    uint32_t btn = plat_buttons(), k;
    const game_t *g = ui.game;
#ifdef APP_TOUCH
    touch_follow();
    if (sh_touch_on) {                    /* the keys from touch: each press and release, with how it was played */
        uint32_t keys, ch, now = plat_ms();
        touch_poll();
        keys = touch_keys();
        ch = keys ^ sh_touch_keys;
        sh_touch_keys = keys;

        for (k = 0; k < NKEYS; k++)
            if ((ch >> k & 1u) && g && g->key)
                g->key((int)k, (int)(keys >> k & 1u), now);
    }
#endif
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
            walk((int)e);                /* as far as it turned */
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
        if (k)                           /* each sound at about the same level as the chime */
            bw_attract_note(k, c->tune_timbre == BW_CHIME ? 80 : c->tune_timbre == BW_SAW ? 95 : 105, c->tune_timbre);
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
        plat_play_red(g->playing && g->playing());
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
    /* saved when nothing is playing (a flash write holds the sound a moment): in a game, 2.5 s after a change (a
     * round's closing chimes ring out first); on the
     * boardwalk (the sea and the tunes play on), after 10 s left alone, or on stepping into a game */
    if (dirty && now - dirty_ms > (ui.game ? 2500u : 10000u) && !(ui.game && ui.game->playing && ui.game->playing())) {
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
    if (striker_expect(what, got) || balloon_expect(what, got) || fish_expect(what, got) || fortune_expect(what, got) || teacups_expect(what, got) || bumpers_expect(what, got) || coaster_expect(what, got))   /* the later games' own */
        return 1;
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
    else if (!strcmp(what, "bells_result"))  /* conducting: 0 none, 1 round on target, 2 round off it, 3 not round */
        *got = bl.result;
    else if (!strcmp(what, "touches"))
        *got = sav.touches;
    else if (!strcmp(what, "bells_mode"))
        *got = sav.bells.mode;
    else if (!strcmp(what, "claw_state"))    /* 0 standing, 1 ready, 2 dropping, 3 out of coins */
        *got = cw.state;
    else if (!strcmp(what, "claw_tickets"))
        *got = cw.tickets;
    else if (!strcmp(what, "claw_coins"))
        *got = cw.coins;
    else if (!strcmp(what, "gull_state"))    /* 0 standing, 1 flying, 2 over */
        *got = gu.state;
    else if (!strcmp(what, "gull_score"))
        *got = gu.score;
    else if (!strcmp(what, "simon_state"))   /* 0 standing, 1 showing, 2 yours, 3 over */
        *got = sm.state;
    else if (!strcmp(what, "simon_len"))
        *got = sm.len;
    else if (!strcmp(what, "whack_state"))
        *got = wm.state;
    else if (!strcmp(what, "whack_score"))
        *got = wm.score;
    else if (!strcmp(what, "whack_stray"))
        *got = wm.stray;
    else if (!strcmp(what, "gallery_state"))
        *got = gl.state;
    else if (!strcmp(what, "gallery_score"))
        *got = gl.score;
    else if (!strcmp(what, "chord_state"))   /* 0 standing, 1 guessing, 2 solved (or given), 3 over */
        *got = cc.state;
    else if (!strcmp(what, "chord_score"))
        *got = cc.score;
    else if (!strcmp(what, "chord_tries"))
        *got = cc.tries;
    else if (!strcmp(what, "echo_state"))    /* 0 standing, 1 playing, 2 over */
        *got = ec.state;
    else if (!strcmp(what, "echo_score"))
        *got = ec.score;
    else if (!strcmp(what, "echo_lives"))
        *got = ec.lives;
    else if (!strcmp(what, "echo_pct"))      /* the last answer, % */
        *got = ec.last_pct;
    else if (!strcmp(what, "echo_level"))
        *got = ec.level;
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
static int host_autoclaw, host_autogull, host_mash, host_autoring, host_autoconduct, host_autotune, host_autotiles, host_autoecho, host_autosimon, host_autowhack, host_autoshoot, host_autochord;
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)b;
    (void)c;
    (void)d;
    if (striker_host_cmd(cmd, a) || balloon_host_cmd(cmd, a) || fish_host_cmd(cmd, a) || fortune_host_cmd(cmd, a) || teacups_host_cmd(cmd, a) || bumpers_host_cmd(cmd, a) || coaster_host_cmd(cmd, a))   /* the later games' own */
        return 1;
    if (!strcmp(cmd, "answer")) {
        pitch_key(!strcmp(a, "right") ? pt.target : (pt.target + 1) % NKEYS, 1, plat_ms());
        return 1;
    }
    if (!strcmp(cmd, "autotune")) {
        host_autotune = !strcmp(a, "on");
        return 1;
    }
    if (!strcmp(cmd, "autoclaw")) {     /* autoclaw on|off: the claw over the wished toy, dropped */
        host_autoclaw = !strcmp(a, "on");
        return 1;
    }
    if (!strcmp(cmd, "autogull")) {     /* autogull on|off: Gull flown through every gap */
        host_autogull = !strcmp(a, "on");
        return 1;
    }
    if (!strcmp(cmd, "mash")) {         /* mash on|off: every key, every 320 ms (a game must not reward it) */
        host_mash = !strcmp(a, "on");
        return 1;
    }
    if (!strcmp(cmd, "autoring")) {     /* autoring on|off: Bells, your bells struck on time */
        host_autoring = !strcmp(a, "on");
        return 1;
    }
    if (!strcmp(cmd, "autoconduct")) {  /* autoconduct on|off: Bells conducting, the calls that bring it round */
        host_autoconduct = !strcmp(a, "on");
        return 1;
    }
    if (!strcmp(cmd, "autosimon")) {    /* autosimon on|off: every sequence played back */
        host_autosimon = !strcmp(a, "on");
        return 1;
    }
    if (!strcmp(cmd, "autowhack")) {    /* autowhack on|off: every mole whacked */
        host_autowhack = !strcmp(a, "on");
        return 1;
    }
    if (!strcmp(cmd, "autoshoot")) {    /* autoshoot on|off: every duck shot midway */
        host_autoshoot = !strcmp(a, "on");
        return 1;
    }
    if (!strcmp(cmd, "autochord")) {    /* autochord right|wrong|off: every guess the chord, or never */
        host_autochord = !strcmp(a, "right") ? 1 : !strcmp(a, "wrong") ? 2 : 0;
        return 1;
    }
    if (!strcmp(cmd, "autoecho")) {     /* autoecho on|off: every call answered on time */
        host_autoecho = !strcmp(a, "on");
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
    striker_host_ms(now);
    balloon_host_ms(now);
    fish_host_ms(now);
    fortune_host_ms(now);
    teacups_host_ms(now);
    bumpers_host_ms(now);
    coaster_host_ms(now);
    if (host_autotune && ui.game == &GAME_PITCH && pt.state == PS_ASK && now % 50u == 0u) {
        if (pt.err > 1.0f)
            pitch_enc(EN_K2, -1);
        else if (pt.err < -1.0f)
            pitch_enc(EN_K2, 1);
    }
    if (host_autoring && ui.game == &GAME_BELLS && bl.ringing && bl.mode != BM_CONDUCT) {
        int32_t t = (int32_t)(now - bl.t0);
        int h;
        for (h = 0; h < 2; h++)
            if (bl.mine[h] >= 0 && bl.my_next[h] < bl.nrows &&
                blow_time(bl.my_next[h], row_place(bl.my_next[h], bl.mine[h])) == t)
                bells_key(h ? 20 : 2, 1, now);
    }
    if (host_autoconduct && ui.game == &GAME_BELLS && bl.ringing && bl.mode == BM_CONDUCT && !bl.complete &&
        bl.call == RING_PLAIN && bl.leads < bl.target && bl.hint[bl.leads] != RING_PLAIN &&
        bl.ring.nlead - 1 - bl.ring.at % bl.ring.nlead <= 6)
        bells_key(bl.hint[bl.leads] == RING_BOB ? 2 : 20, 1, now);
    if (host_autosimon && ui.game == &GAME_SIMON && sm.state == SS_YOURS && now - sm.yours_ms >= 250u)
        simon_key(LANE_KEY[sm.seq[sm.pos]], 1, now);
    if (host_autowhack && ui.game == &GAME_WHACK && now % 120u == 0u) {
        int k;
        for (k = 0; k < NKEYS; k++)
            if (wm.up_until[k])
                whack_key(k, 1, now);
    }
    if (host_autoclaw && ui.game == &GAME_CLAW && cw.state == CW_READY && cw.wish >= 0 && now % 100u == 0u) {
        int k = cw.toy[cw.wish].key;
        if (cw.col != k)
            claw_key(k, 1, now);
        else if (cw.cx - (float)key_x(k) < 1.0f && (float)key_x(k) - cw.cx < 1.0f)
            claw_drop(now);
    }
    if (host_autogull && ui.game == &GAME_GULL && gu.state == GS2_FLY) {
        int i, next = -1;
        for (i = 0; i < GU_POSTS; i++)   /* the next post ahead of the gull */
            if (gu.post_x[i] + GU_POST_W >= GU_X - GU_R && (next < 0 || gu.post_x[i] < gu.post_x[next]))
                next = i;
        if (next >= 0 && gu.mode == 0 && gu.vy > 0.0f && gu.y > gu.post_gap[next] + 6) {
            gull_key(9, 1, now);
            gull_key(9, 0, now);
        }
        if (next >= 0 && gu.mode == GU_HOLD) {   /* hold below the gap (and falling), let go above it */
            int want = gu.y + gu.vy * 0.15f > (float)gu.post_gap[next];
            if (want && !(gu.keys >> 9 & 1u))
                gull_key(9, 1, now);
            else if (!want && (gu.keys >> 9 & 1u))
                gull_key(9, 0, now);
        }
        if (next >= 0 && gu.mode == 1 && gu.held != gu.post_key[next]) {
            if (gu.held >= 0)
                gull_key(gu.held, 0, now);
            gull_key(gu.post_key[next], 1, now);
        }
    }
    if (host_mash && ui.game && ui.game->key && now % 320u == 0u) {
        int k;
        for (k = 0; k < NKEYS; k++)
            ui.game->key(k, 1, now);
    }
    if (host_autoshoot && ui.game == &GAME_GALLERY && gl.state == GS_PLAY && now % 50u == 0u) {
        int i;
        for (i = 0; i < GL_ON; i++)
            if (gl.duck[i].on && !gl.duck[i].hit && gl.duck[i].x > 80 * 16 && gl.duck[i].x < 160 * 16)
                gallery_key(gl.duck[i].key, 1, now);
    }
    if (host_autochord && ui.game == &GAME_CHORD && cc.state == CS_GUESS && !cc.judge_ms && !cc.secret_left &&
        now - cc.shown_ms > 1000u && now - cc.secret_ms > 200u) {
        int i;
        for (i = 0; i < cc.n; i++)
            chord_key(host_autochord == 1 ? cc.secret[i] : (cc.secret[i] + 1) % NKEYS, 1, now);
    }
    if (host_autoecho && ui.game == &GAME_ECHO && ec.state == ES_PLAY && (int32_t)(now - ec.t0) >= 0) {
        int32_t t = (int32_t)(now - ec.t0), bar = t / bar_ms(), st = t % bar_ms();
        if (bar >= 2 && !(bar & 1) && st % ec.step == 0 && (call_at(bar) >> (st / ec.step) & 1u))
            echo_key(9, 1, now);
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
    for (v = 0; v < BM_N; v++) {         /* Bells' three ways, standing and ringing */
        sav.bells.mode = (uint8_t)v;
        sav.bells.bell = 1;
        sav.bells.method = 4;
        sp_join("MODE", MODE_NAME[v]);
        sp_out("READOUT", sp_buf);
        bells_enter();
        bells_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        bl.ringing = 1;
        bl.target = 3;
        bells_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        bl.call = RING_BOB;
        bells_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        bl.call = RING_PLAIN;
        bl.ringing = 0;
    }
    pair_text(2, t);
    sp_join("BELLS", t);
    sp_out("READOUT", sp_buf);
    bl.mode = BM_CONDUCT;
    bl.leads = bl.target = 3;
    bl.hint[0] = bl.hint[1] = bl.hint[2] = RING_BOB;
    for (v = BR_ROUND; v <= BR_LOST; v++) {
        bl.result = v;
        bl.leads = v == BR_EARLY ? 5 : 3;
        conduct_said(sp_buf);
        sp_out("MSG", sp_buf);
    }
    bl.result = BR_NONE;
    sav.bells.mode = 0;
    /* the new cabinets, standing and playing */
    sp_join("SPEED", "MEDIUM");
    sp_out("READOUT", sp_buf);
    for (v = SS_STAND; v <= SS_OVER; v++) {
        sm.state = v;
        sm.len = 4;
        simon_where(sp_buf);
        sp_out("SCREEN", sp_buf);
    }
    sm.state = SS_STAND;
    for (v = WS_STAND; v <= WS_OVER; v++) {
        wm.state = v;
        whack_where(sp_buf);
        sp_out("SCREEN", sp_buf);
    }
    wm.state = WS_STAND;
    for (i = 1; i <= GL_LEVELS; i++) {
        sav.gallery.level = (uint8_t)i;
        gl.state = GS_STAND;
        gallery_where(sp_buf);
        sp_out("SCREEN", sp_buf);
    }
    for (v = GS_PLAY; v <= GS_OVER; v++) {
        gl.state = v;
        gallery_where(sp_buf);
        sp_out("SCREEN", sp_buf);
    }
    gl.state = GS_STAND;
    for (i = 1; i <= CC_LEVELS; i++) {
        sav.chord.level = (uint8_t)i;
        cc.state = CS_STAND;
        chord_where(sp_buf);
        sp_out("SCREEN", sp_buf);
    }
    cc.n = 3;
    cc.secret[0] = 7;
    cc.secret[1] = 11;
    cc.secret[2] = 15;
    for (v = CS_GUESS; v <= CS_OVER; v++) {
        cc.state = v;
        chord_where(sp_buf);
        sp_out("SCREEN", sp_buf);
    }
    cc.state = CS_STAND;
    sav.echo.tempo = 100;
    sav.echo.level = 2;
    for (v = ES_STAND; v <= ES_OVER; v++) {
        ec.state = v;
        echo_where(sp_buf);
        sp_out("SCREEN", sp_buf);
    }
    ec.state = ES_STAND;
    striker_speak_dump();
    balloon_speak_dump();
    fish_speak_dump();
    fortune_speak_dump();
    teacups_speak_dump();
    bumpers_speak_dump();
    coaster_speak_dump();
    for (i = 1; i <= CL_LEVELS; i++) {   /* Claw: each level standing; playing, over */
        char lv[24];
        sav.claw.level = (uint8_t)i;
        cw.state = CW_STAND;
        claw_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        int_text(i, lv);
        text_cat(lv, ", ");
        text_cat(lv, CL_LEVEL_NAME[i - 1]);
        sp_join("LEVEL", lv);
        sp_out("READOUT", sp_buf);
    }
    cw.tickets = 23;
    cw.coins = 4;
    cw.state = CW_READY;
    claw_where(sp_buf);
    sp_out("SCREEN", sp_buf);
    cw.state = CW_OVER;
    claw_where(sp_buf);
    sp_out("SCREEN", sp_buf);
    cw.state = CW_STAND;
    for (i = 0; i < 2; i++) {            /* Gull: both games, standing, flying, over */
        sav.gull.mode = (uint8_t)i;
        sp_join("MODE", GU_MODE[i]);
        sp_out("READOUT", sp_buf);
        for (v = GS2_STAND; v <= GS2_OVER; v++) {
            gu.state = v;
            gu.score = 7;
            gull_where(sp_buf);
            sp_out("SCREEN", sp_buf);
        }
    }
    sp_join("LEVEL", "1, NOTES SHOWN");
    sp_out("READOUT", sp_buf);
    sp_join("LEVEL", "2, BY EAR");
    sp_out("READOUT", sp_buf);
    gu.state = GS2_STAND;
    /* Pitch */
    for (v = 1; v <= EAR_LEVELS; v++) {
        ear_level_text(v, t);
        sp_join("LEVEL", t);
        sp_out("READOUT", sp_buf);
    }
    sp_join("MODE", "EAR");
    sp_out("READOUT", sp_buf);
    sp_join("MODE", "TUNE");
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
