/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Boardwalk's sound engine (its own -O2 unit). The main loop posts commands; the render (the audio interrupt)
 * takes them at the start of each block, so a command is heard within a block (5.8 ms).
 *
 * Sources: bells (modal: hum, prime, tierce, quint, nominal and two above it, each its own decay), blips (a short
 * FM pluck, for the games' notes and the menus), and the surf (filtered noise with a slow swell, and a gull now and
 * then). One plate reverb (Dattorro's) under all of them. */
#pragma once
#include <stdint.h>

#define BW_BELLS 8
#define BW_BLIPS 4
#define BW_RATE 44100

void bw_init(void);
void bw_render(int32_t *lr, uint32_t n, uint32_t gain_q12);   /* audio interrupt: n stereo frames, 24-bit */
void bw_panic(void);                     /* everything quiet now (audio interrupt or with it held off) */

/* commands (main loop) */
void bw_bell(int bell, int note, int vel);   /* strike bell slot 0..BW_BELLS-1 at MIDI note (its nominal), vel 1..127 */
void bw_bell_pan(int bell, int pan);         /* -64 left .. +63 right */
void bw_blip(int note, int vel);             /* a short pluck */
void bw_surf(int level);                     /* 0..100 */
void bw_bells_damp(void);                    /* every bell stopped (a ringer's hand on the rope: back to the menu) */

/* for the screen and tests */
uint32_t bw_cmds_dropped(void);              /* commands that found the queue full */
float bw_bell_level(int bell);               /* how loud a bell still rings, 0..~1 */
