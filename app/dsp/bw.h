/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Boardwalk's sound engine (its own -O2 unit). The main loop posts commands; the render (the audio interrupt)
 * takes them at the start of each block, so a command is heard within a block (5.8 ms).
 *
 * Sources: bells (modal: hum, prime, tierce, quint, nominal and two above it, each its own decay), notes (two-operator
 * FM in a few timbres, for the games and the cabinets' attract tunes), two sustained tones (Pitch's tuning duel), and
 * the surf (filtered noise with a slow swell, a gull now and then). One plate reverb (Dattorro's) under all of them.
 * The attract tunes' notes are a group with its own level, faded as the boardwalk's cabinets change. */
#pragma once
#include <stdint.h>

#define BW_BELLS 8
#define BW_NOTES 8
#define BW_TONES 2
#define BW_RATE 44100

void bw_init(void);
void bw_render(int32_t *lr, uint32_t n, uint32_t gain_q12);   /* audio interrupt: n stereo frames, 24-bit */
void bw_panic(void);                     /* everything quiet now (audio interrupt or with it held off) */

/* commands (main loop) */
void bw_bell(int bell, int note, int vel);   /* strike bell slot 0..BW_BELLS-1 at MIDI note (its nominal), vel 1..127 */
void bw_bell_bright(int bell, int note, int vel);   /* the same, a little brighter: your own bell */
void bw_bell_pan(int bell, int pan);         /* -64 left .. +63 right */
enum { BW_SINE, BW_PLUCK, BW_PIANO, BW_CHIME, BW_BASS, BW_CLUNK, BW_SAW, BW_NTIMBRES };
void bw_note(int note, int vel, int timbre); /* a note, dying away by itself */
void bw_attract_note(int note, int vel, int timbre);   /* one of the attract tunes' notes (bw_attract's level) */
void bw_attract(int level);                  /* the attract tunes' level, 0..100, faded over ~0.2 s */
void bw_blip(int note, int vel);             /* bw_note(note, vel, BW_PLUCK) */
void bw_tone(int slot, int cents, int level);   /* a sustained tone: slot 0..BW_TONES-1, pitch in cents (MIDI note x
                                                 * 100: 6900 is A4), level 0..100 (0: off); pitch and level glide */
enum { BW_KICK, BW_HAT, BW_BLOCK_HI, BW_BLOCK_LO, BW_CLAP, BW_NDRUMS };
void bw_drum(int drum, int vel);             /* a drum: Echo's groove, its calls and your answers */
void bw_surf(int level);                     /* 0..100: the sea, and its gulls */
void bw_gull(void);                          /* a gull calls now */
void bw_bells_damp(void);                    /* every bell stopped (a ringer's hand on the rope: back to the menu) */

/* for the screen and tests */
uint32_t bw_cmds_dropped(void);              /* commands that found the queue full */
float bw_bell_level(int bell);               /* how loud a bell still rings, 0..~1 */
