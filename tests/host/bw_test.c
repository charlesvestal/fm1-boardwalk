/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The sound engine (app/dsp/bw.c) on the host: silence at rest, a bell rings and dies away, a damp stops it, the
 * blip and the surf sound, nothing clips hard or goes NaN, and the render's cost. Writes OUT/rounds.wav: rounds and
 * the plain course of Plain Bob Minor (from app/games/ringing.c), to listen to.
 *   bw_test OUT_DIR */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "bw.h"
#include "ringing.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); \
                                       printf("\n"); fails++; } } while (0)
#define BLK 256u
#define FULL 8388607.0f

static int32_t buf[2 * BLK];
static FILE *wav;
static uint32_t wav_n;

static float block(void)                 /* one block; its peak, 0..1 of full scale */
{
    float peak = 0.0f;
    uint32_t i;
    bw_render(buf, BLK, 4096u);
    for (i = 0; i < 2 * BLK; i++) {
        float v = fabsf((float)buf[i]) / FULL;
        if (v != v)
            peak = 99.0f;
        else if (v > peak)
            peak = v;
        if (wav) {
            int16_t s = (int16_t)(buf[i] >> 8);
            fwrite(&s, 2, 1, wav);
            wav_n++;
        }
    }
    return peak;
}
static float run_ms(int ms)              /* the peak over ms */
{
    float p = 0.0f;
    int k, nb = ms * 44100 / 1000 / (int)BLK;
    for (k = 0; k < nb; k++) {
        float b = block();
        p = b > p ? b : p;
    }
    return p;
}

static void wav_header(uint32_t frames)
{
    uint32_t v;
    uint16_t h;
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav);
    v = 36 + frames * 4;
    fwrite(&v, 4, 1, wav);
    fwrite("WAVEfmt ", 1, 8, wav);
    v = 16;
    fwrite(&v, 4, 1, wav);
    h = 1;
    fwrite(&h, 2, 1, wav);
    h = 2;
    fwrite(&h, 2, 1, wav);
    v = 44100;
    fwrite(&v, 4, 1, wav);
    v = 44100 * 4;
    fwrite(&v, 4, 1, wav);
    h = 4;
    fwrite(&h, 2, 1, wav);
    h = 16;
    fwrite(&h, 2, 1, wav);
    fwrite("data", 1, 4, wav);
    v = frames * 4;
    fwrite(&v, 4, 1, wav);
}

int main(int argc, char **argv)
{
    static const int NOTE[6] = {81, 79, 77, 76, 74, 72};   /* a ring of six in C: treble A5 .. tenor C5 */
    float p;
    int i;
    clock_t t0;
    char path[512];

    bw_init();
    CHECK(run_ms(200) == 0.0f, "silence at rest");

    bw_bell(0, 72, 110);
    p = run_ms(100);
    CHECK(p > 0.05f && p < 0.99f, "a bell rings: peak %.3f", (double)p);
    run_ms(3000);
    CHECK(bw_bell_level(0) > 0.0f && bw_bell_level(0) < 0.5f, "it dies away: %.3f", (double)bw_bell_level(0));
    bw_bells_damp();
    run_ms(500);
    CHECK(bw_bell_level(0) < 1e-4f, "a damp stops it: %.5f", (double)bw_bell_level(0));
    run_ms(4000);                        /* the plate empties */

    bw_blip(72, 100);
    p = run_ms(100);
    CHECK(p > 0.05f, "a blip: %.3f", (double)p);
    for (i = 0; i < BW_NTIMBRES; i++) {  /* every timbre sounds and dies away */
        bw_note(60, 100, i);
        p = run_ms(100);
        CHECK(p > 0.02f && p < 0.99f, "timbre %d: %.3f", i, (double)p);
        run_ms(3000);
    }
    run_ms(4000);
    CHECK(run_ms(100) < 0.002f, "the notes died away");
    for (i = 0; i < BW_NDRUMS; i++) {    /* every drum sounds, and is short */
        bw_drum(i, 110);
        p = run_ms(60);
        CHECK(p > 0.02f && p < 0.99f, "drum %d: %.3f", i, (double)p);
        run_ms(600);
        CHECK(run_ms(50) < 0.01f, "drum %d dies away", i);
    }
    run_ms(4000);
    bw_attract(0);                       /* an attract note at level 0: silent */
    run_ms(300);
    bw_attract_note(72, 120, BW_CHIME);
    CHECK(run_ms(200) < 0.002f, "attract at 0 is silent");
    bw_attract(100);
    bw_attract_note(72, 120, BW_CHIME);
    CHECK(run_ms(200) > 0.02f, "attract at 100 sounds");
    run_ms(6000);
    bw_tone(0, 6900, 60);                /* two tones 3 cents apart, then off */
    bw_tone(1, 6903, 60);
    p = run_ms(1000);
    CHECK(p > 0.05f && p < 0.99f, "two tones: %.3f", (double)p);
    bw_tone(0, 6900, 0);
    bw_tone(1, 6903, 0);
    run_ms(5000);
    CHECK(run_ms(100) < 0.002f, "the tones stop");
    bw_gull();
    p = run_ms(1000);
    CHECK(p > 0.01f, "a gull: %.3f", (double)p);
    run_ms(4000);
    bw_surf(80);
    run_ms(4000);
    p = run_ms(4000);
    CHECK(p > 0.01f && p < 0.9f, "the surf: %.3f", (double)p);
    bw_surf(0);
    run_ms(2000);

    for (i = 0; i < 8; i++)              /* every bell at once, hard: loud but bounded */
        bw_bell(i, 60 + 2 * i, 127);
    p = run_ms(200);
    CHECK(p < 1.0f && p > 0.3f, "eight bells at once: %.3f", (double)p);
    bw_panic();
    run_ms(4000);

    /* the render's cost: eight bells ringing, the surf, the plate */
    for (i = 0; i < 8; i++)
        bw_bell(i, 60 + 2 * i, 100);
    bw_surf(60);
    t0 = clock();
    run_ms(10000);
    printf("bw: 10 s of eight bells, surf and plate in %.0f ms on this machine\n",
           (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC);
    bw_panic();
    bw_surf(0);
    run_ms(3000);

    /* rounds, the plain course of Plain Bob Minor and rounds again, to listen to */
    if (argc > 1) {
        ring_t ring;
        int r, k;
        ring_start(&ring, &RING_METHODS[4]);
        snprintf(path, sizeof path, "%s/rounds.wav", argv[1]);
        wav = fopen(path, "wb");
        if (wav) {
            wav_header(0);
            for (i = 0; i < 6; i++)
                bw_bell_pan(i, -40 + 16 * i);
            for (r = 0; r < 4 + 60 + 4; r++) {
                if (r >= 4 && r < 64)
                    ring_next(&ring);
                if (r && !(r & 1))
                    run_ms(220);     /* the handstroke gap */
                for (k = 0; k < 6; k++) {
                    bw_bell(ring.row[k], NOTE[ring.row[k]] - 12, 100);
                    run_ms(220);
                }
            }
            run_ms(5000);
            wav_header(wav_n / 2);
            fclose(wav);
            printf("bw: wrote %s\n", path);
        }
    }

    printf(fails ? "bw: %d failed\n" : "bw: ok\n", fails);
    return fails != 0;
}
