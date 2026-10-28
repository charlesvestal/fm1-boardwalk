/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Boardwalk: an arcade of music games on a golden-hour boardwalk. The app's half of the contract with fm1-base
 * (device/unity.c, host/plat_host.c), and what is saved. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "plat.h"
#include "bw.h"
#include "ringing.h"

#define APP_NAME "BOARDWALK"
#define APP_SPLASH_RGB RGB(247, 169, 139)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TICK                         /* 1 kHz: the tower rings its bells on time (games' app_tick) */
void app_tick(void);

/* the store: the save at 0xFC000, the panel calibration at 0xDC000 */
enum { OBJ_SAVE, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

#define SAVE_MAGIC 0x4B4C5742u           /* "BWLK" */
#define SAVE_FORMAT 1u
#define NMETHODS_MAX 16
typedef struct {
    uint32_t magic, format;
    uint8_t cab;                         /* the cabinet in front of you */
    uint8_t surf;                        /* the sea's level, 0..100 */
    uint8_t tunes;                       /* the attract tunes' level, 0..100 */
    uint8_t rsv0;
    struct {
        uint8_t method;                  /* RING_METHODS */
        uint8_t bell;                    /* yours: 0 listen, 1..n */
        uint8_t pace;                    /* the gap between blows, in 10 ms */
        uint8_t mode;                    /* 0 tower (one bell), 1 handbells (a pair), 2 conducting */
        uint8_t best[NMETHODS_MAX];      /* the best striking per method, %, 0 = not rung yet */
    } bells;
    struct {
        uint8_t mode;                    /* 0 ear (perfect pitch), 1 tune (the tuning duel) */
        uint8_t level;                   /* the level an ear game starts at, 1.. */
        uint16_t best_ear;               /* notes named in one game */
        uint16_t best_tune;              /* tenths of a second kept in tune */
        uint8_t rsv[2];
    } pitch;
    struct {
        uint8_t song, level, tempo, rsv; /* level 0 easy, 1 normal, 2 hard; tempo in % */
        uint8_t best[16];                /* % of the notes played, per song */
    } tiles;
    struct {
        uint8_t tempo, level;            /* bpm; the level to start at */
        uint16_t best;                   /* answers passed in one game */
    } echo;
    struct {
        uint8_t level, rsv;
        uint16_t best;
    } chord, whack, gallery, simon;      /* each: its level (Simon: its speed) and its best */
    uint16_t touches;                    /* Bells: touches conducted round on target */
    uint8_t hand_best[10];               /* Bells: the best striking per method on handbells */
    uint8_t conduct_len;                 /* Bells: the length a conducted touch is set (leads; 0 any) */
    uint8_t rsv[3];
    /* added since (a save that ends before here loads, these from their defaults) */
    struct {
        uint8_t mode, level;             /* 0 flap, 1 sing; level 1 names on the posts, 2 by ear */
        uint16_t best[2];                /* posts passed, per mode */
    } gull;
    struct {
        uint8_t level, rsv;
        uint16_t best;                   /* tickets in a round */
    } claw;
    uint8_t rsv2[12];                    /* room to grow */
} save_t;
extern save_t sav;
void save_dirty(void);                   /* saved at the next quiet moment */

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* the host simulator: checks, and a player for the tests (app.c) */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#define APP_HOST_MS
void app_host_ms(void);
#define APP_HOST_SPEAKDUMP
void app_speak_dump(void);
