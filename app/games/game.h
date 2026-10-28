/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* A game: one cabinet on the boardwalk. The shell (shell.c) owns the boardwalk, HOME, MASTER and saving; while a
 * game is in front, every other control is the game's. Part of the unity build. */
#pragma once
#include <stdint.h>

typedef struct {
    const char *name;                    /* the marquee */
    uint16_t cab;                        /* the cabinet's colour */
    void (*enter)(void), (*leave)(void);
    void (*frame)(uint32_t ms);          /* logic and drawing, main loop */
    void (*key)(int k, int down, uint32_t ms);   /* ms: when the press was seen (input runs between frames too) */
    void (*button)(int b, int down);
    void (*enc)(int role, int steps);
    void (*tick)(uint32_t ms);           /* 1 kHz, the timer interrupt: what has to be on time */
    void (*attract)(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms);   /* the cabinet's little screen,
                                                                                    * drawn on the canvas */
    void (*best)(char *buf);             /* "BEST 94%", or "" */
    uint32_t (*keys_lit)(void);          /* the keys to light */
    const uint8_t *tune;                 /* its attract tune on the boardwalk: MIDI notes, 0 a rest, looped */
    uint8_t tune_len;
    uint16_t tune_step;                  /* ms a step */
    uint8_t tune_timbre;                 /* bw.h BW_* */
    void (*where)(char *t);              /* the whole screen for a screen reader, as a sentence (ANN_MAX) */
} game_t;
