/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Boardwalk's sound engine (bw.h). One compilation unit, -O2. */
#include "bw.h"
#include "fastmath.h"

#if defined BW_HOST || defined PLAT_HOST
#define BW_POOL
#else
#define BW_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")
#define TWO_PI 6.2831853f

/* ---------------------------------------------------------------- commands --- */
/* one word each: op in the top byte, then two bytes of arguments. One producer (the main loop), one consumer
 * (the render) */
enum { C_BELL = 1, C_PAN, C_BLIP, C_SURF, C_DAMP };
#define CQ 64u
static uint32_t cq[CQ];
static volatile uint32_t cq_w, cq_r, cq_drop;

static void post(uint32_t op, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t w = cq_w;
    if (w - cq_r >= CQ) {
        cq_drop++;
        return;
    }
    cq[w % CQ] = op << 24 | (a & 0xFFu) << 16 | (b & 0xFFu) << 8 | (c & 0xFFu);
    BARRIER();
    cq_w = w + 1u;
}
void bw_bell(int bell, int note, int vel) { post(C_BELL, (uint32_t)bell, (uint32_t)note, (uint32_t)vel); }
void bw_bell_pan(int bell, int pan) { post(C_PAN, (uint32_t)bell, (uint32_t)(pan + 64), 0); }
void bw_blip(int note, int vel) { post(C_BLIP, (uint32_t)note, (uint32_t)vel, 0); }
void bw_surf(int level) { post(C_SURF, (uint32_t)level, 0, 0); }
void bw_bells_damp(void) { post(C_DAMP, 0, 0, 0); }
uint32_t bw_cmds_dropped(void) { return cq_drop; }

/* ------------------------------------------------------------------- bells --- */
/* A tuned bell's partials, against its nominal (the note it is heard at), as the founders tune them: the hum two
 * octaves below, the prime an octave below, the tierce a minor third above that, the quint, the nominal, and two
 * above it. Their ratios, how loud each is at the strike, and how long each rings (seconds to -60 dB, for a bell
 * at A4; lower bells ring longer). Each partial is a resonator: a
 * phasor turned a little every sample and shrunk by its decay, struck by adding to it. */
#define NPART 7
static const float PART_RATIO[NPART] = {0.25f, 0.5f, 0.6f, 0.75f, 1.0f, 1.5f, 2.0f};   /* hum, prime, tierce, quint,
                                                                                     * nominal, superquint, octave */
static const float PART_AMP[NPART] = {0.30f, 0.22f, 0.26f, 0.08f, 0.40f, 0.16f, 0.07f};
static const float PART_T60[NPART] = {9.0f, 5.0f, 4.0f, 2.5f, 3.2f, 1.4f, 0.7f};

typedef struct {
    float re[NPART], im[NPART];          /* the resonators */
    float c[NPART], s[NPART];            /* each one's turn a sample, times its decay */
    float amp[NPART];
    float pan_l, pan_r;
    int note;
    float level;                         /* an estimate, for the screen */
} bell_t;
static bell_t bell[BW_BELLS];
static uint8_t damping;                  /* blocks left of a quick fade (a damp) */

static void bell_tune(bell_t *b, int note)
{
    int p;
    float f = 440.0f * fm_exp2f((float)(note - 69) / 12.0f);
    float len = fm_clampf(fm_sqrtf(440.0f / f), 0.35f, 2.5f);   /* lower bells ring longer */
    for (p = 0; p < NPART; p++) {
        float fp = f * PART_RATIO[p], w, r;
        if (fp > 18000.0f) {             /* above hearing: off */
            b->c[p] = b->s[p] = b->amp[p] = 0.0f;
            continue;
        }
        w = TWO_PI * fp / (float)BW_RATE;
        r = fm_expf(-6.9077553f / (PART_T60[p] * len * (float)BW_RATE));   /* ln 1000: -60 dB over T60 */
        b->c[p] = r * fm_cosf(w);
        b->s[p] = r * fm_sinf(w);
        b->amp[p] = PART_AMP[p];
    }
    b->note = note;
}

static void bell_strike(int i, int note, int vel)
{
    bell_t *b = &bell[i];
    float v = (float)vel / 127.0f, g;
    int p;
    if (note != b->note) {               /* a new bell in this slot: tuned afresh, silent first */
        bell_tune(b, note);
        for (p = 0; p < NPART; p++)
            b->re[p] = b->im[p] = 0.0f;
    }
    g = v * v;                           /* a harder strike is louder and brighter */
    for (p = 0; p < NPART; p++) {
        float bright = p >= 4 ? 0.5f + 0.5f * v : 1.0f;
        b->im[p] += b->amp[p] * g * bright;
    }
    b->level = g;
}

/* ------------------------------------------------------------------- blips --- */
typedef struct {
    float ph, mph, inc, minc, env, idx;
} blip_t;
static blip_t blip[BW_BLIPS];
static int blip_next;

static void blip_start(int note, int vel)
{
    blip_t *b = &blip[blip_next];
    float f = 440.0f * fm_exp2f((float)(note - 69) / 12.0f);
    blip_next = (blip_next + 1) % BW_BLIPS;
    b->inc = f / (float)BW_RATE;
    b->minc = 2.0f * b->inc;
    b->env = 0.35f * (float)vel / 127.0f;
    b->idx = 2.5f;
    b->ph = b->mph = 0.0f;
}

/* -------------------------------------------------------------------- surf --- */
/* white noise through two one-pole lowpasses, its level swelling on a slow sine (a wave every ~7 s), and its
 * brightness with it: the wave breaks bright and draws back dark */
static uint32_t rng = 22222u;
static float surf_lp1, surf_lp2, surf_level, surf_target, swell_ph;
static inline float noise(void)
{
    rng = rng * 1664525u + 1013904223u;
    return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}

/* ------------------------------------------------- reverb: Dattorro's plate --- */
/* J. Dattorro, "Effect Design, Part 1", JAES 1997 (as in FoMni and AMB-1): a predelay, four input diffusers, then a
 * figure-eight tank of two halves, each a modulated allpass, a delay, a damping lowpass, an allpass and a delay,
 * feeding the other half. Stereo out from seven taps a side. The paper's lengths are for 29761 Hz; DS() scales them
 * to 44.1 kHz. */
#define DS(n) (((n) * 14818 + 5000) / 10000)          /* 44100 / 29761 */
#define PRE_N 882                                    /* 20 ms */
#define EXC DS(16)
enum { L_PRE, L_IN1, L_IN2, L_IN3, L_IN4, L_APL, L_D1L, L_AP2L, L_D2L, L_APR, L_D1R, L_AP2R, L_D2R, L_N };
static const int DL_LEN[L_N] = {PRE_N, DS(142), DS(107), DS(379), DS(277), DS(672) + EXC + 2, DS(4453), DS(1800),
                                DS(3720), DS(908) + EXC + 2, DS(4217), DS(2656), DS(3163)};
#define DL_TOTAL (PRE_N + DS(142) + DS(107) + DS(379) + DS(277) + DS(672) + DS(4453) + DS(1800) + DS(3720) + DS(908) + \
                  DS(4217) + DS(2656) + DS(3163) + 2 * (EXC + 2))
static float pl_buf[DL_TOTAL] BW_POOL;
typedef struct {
    float *b;
    int n, i;
} dl_t;
static dl_t dl[L_N];
static float pl_bw, pl_damp_l, pl_damp_r, lfo_s, lfo_c = 1.0f;
#define PL_DECAY 0.72f
#define PL_DD2 0.5f

static inline float tap(const dl_t *d, int k)
{
    int j = d->i - k;
    return d->b[j < 0 ? j + d->n : j];
}
static inline void push(dl_t *d, float x)
{
    d->b[d->i] = x;
    if (++d->i >= d->n)
        d->i = 0;
}
static inline float allpass(dl_t *d, int len, float x, float g)
{
    float z = tap(d, len), v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}
static inline float allpass_mod(dl_t *d, float len, float x, float g)
{
    int k = (int)len;
    float f = len - (float)k, z = tap(d, k) + (tap(d, k + 1) - tap(d, k)) * f, v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}

static inline void reverb(float in, float *ol, float *or_)
{
    float x, a, b, fb_l = tap(&dl[L_D2R], DS(3163)), fb_r = tap(&dl[L_D2L], DS(3720));
    push(&dl[L_PRE], in);
    x = tap(&dl[L_PRE], PRE_N);
    pl_bw += 0.6f * (x - pl_bw);
    x = allpass(&dl[L_IN1], DS(142), pl_bw, 0.75f);
    x = allpass(&dl[L_IN2], DS(107), x, 0.75f);
    x = allpass(&dl[L_IN3], DS(379), x, 0.625f);
    x = allpass(&dl[L_IN4], DS(277), x, 0.625f);
    {
        float s = lfo_s + 0.0001f * lfo_c, c = lfo_c - 0.0001f * s;
        lfo_s = s;
        lfo_c = c;
    }
    a = allpass_mod(&dl[L_APL], (float)DS(672) + (float)EXC * lfo_s, x + PL_DECAY * fb_l, -0.7f);
    push(&dl[L_D1L], a);
    b = tap(&dl[L_D1L], DS(4453));
    pl_damp_l += 0.45f * (b - pl_damp_l);
    b = allpass(&dl[L_AP2L], DS(1800), pl_damp_l * PL_DECAY, PL_DD2);
    push(&dl[L_D2L], b);
    a = allpass_mod(&dl[L_APR], (float)DS(908) + (float)EXC * lfo_c, x + PL_DECAY * fb_r, -0.7f);
    push(&dl[L_D1R], a);
    b = tap(&dl[L_D1R], DS(4217));
    pl_damp_r += 0.45f * (b - pl_damp_r);
    b = allpass(&dl[L_AP2R], DS(2656), pl_damp_r * PL_DECAY, PL_DD2);
    push(&dl[L_D2R], b);
    *ol = 0.6f * (tap(&dl[L_D1R], DS(266)) + tap(&dl[L_D1R], DS(2974)) - tap(&dl[L_AP2R], DS(1913)) +
                  tap(&dl[L_D2R], DS(1996)) - tap(&dl[L_D1L], DS(1990)) - tap(&dl[L_AP2L], DS(187)) -
                  tap(&dl[L_D2L], DS(1066)));
    *or_ = 0.6f * (tap(&dl[L_D1L], DS(353)) + tap(&dl[L_D1L], DS(3627)) - tap(&dl[L_AP2L], DS(1228)) +
                   tap(&dl[L_D2L], DS(2673)) - tap(&dl[L_D1R], DS(2111)) - tap(&dl[L_AP2R], DS(335)) -
                   tap(&dl[L_D2R], DS(121)));
}

static void plate_init(void)
{
    float *p = pl_buf;
    int k, i;
    for (k = 0; k < L_N; k++) {
        dl[k].b = p;
        dl[k].n = DL_LEN[k];
        dl[k].i = 0;
        p += DL_LEN[k];
    }
    for (i = 0; i < DL_TOTAL; i++)
        pl_buf[i] = 0.0f;
    pl_bw = pl_damp_l = pl_damp_r = 0.0f;
    lfo_s = 0.0f;
    lfo_c = 1.0f;
}

/* ------------------------------------------------------------------ engine --- */
void bw_init(void)
{
    int i;
    plate_init();
    for (i = 0; i < BW_BELLS; i++) {
        bell[i].note = -1;
        bell[i].pan_l = bell[i].pan_r = 0.7071f;
    }
    cq_w = cq_r = 0;
}

void bw_panic(void)
{
    int i, p;
    for (i = 0; i < BW_BELLS; i++) {
        for (p = 0; p < NPART; p++)
            bell[i].re[p] = bell[i].im[p] = 0.0f;
        bell[i].level = 0.0f;
    }
    for (i = 0; i < BW_BLIPS; i++)
        blip[i].env = 0.0f;
    surf_target = surf_level = 0.0f;
}

float bw_bell_level(int b) { return b >= 0 && b < BW_BELLS ? bell[b].level : 0.0f; }

static void commands(void)
{
    while (cq_r != cq_w) {
        uint32_t c = cq[cq_r % CQ], op = c >> 24, a = c >> 16 & 0xFFu, b = c >> 8 & 0xFFu, v = c & 0xFFu;
        BARRIER();
        cq_r++;
        if (op == C_BELL && a < BW_BELLS && b < 128u && v)
            bell_strike((int)a, (int)b, (int)v);
        else if (op == C_PAN && a < BW_BELLS) {
            float x = ((float)b - 64.0f) / 128.0f + 0.5f;   /* 0..1, equal power */
            bell[a].pan_l = fm_cosf(x * 1.5707963f);
            bell[a].pan_r = fm_sinf(x * 1.5707963f);
        } else if (op == C_BLIP && a < 128u)
            blip_start((int)a, (int)(b ? b : 100u));
        else if (op == C_SURF)
            surf_target = (float)(a > 100u ? 100u : a) * 0.01f;
        else if (op == C_DAMP)
            damping = 24;                /* ~140 ms */
    }
}

void bw_render(int32_t *lr, uint32_t n, uint32_t gain_q12)
{
    uint32_t k;
    int i, p;
    float gain = (float)gain_q12 * (1.0f / 4096.0f) * 8000000.0f, swell, bright;
    commands();
    if (damping) {                       /* a damp: every bell down ~6 dB a block */
        damping--;
        for (i = 0; i < BW_BELLS; i++)
            for (p = 0; p < NPART; p++) {
                bell[i].re[p] *= 0.5f;
                bell[i].im[p] *= 0.5f;
            }
    }
    /* the surf's swell, once a block */
    swell_ph += (float)n / ((float)BW_RATE * 7.3f);
    if (swell_ph >= 1.0f)
        swell_ph -= 1.0f;
    swell = 0.5f + 0.5f * fm_sin_turns(swell_ph);
    swell *= swell;
    bright = 0.03f + 0.10f * swell;
    surf_level += (surf_target - surf_level) * 0.02f;
    for (i = 0; i < BW_BELLS; i++) {     /* the level estimate, for the screen: the nominal's */
        bell_t *b = &bell[i];
        b->level = fm_flush(fm_sqrtf(b->re[4] * b->re[4] + b->im[4] * b->im[4]) / (PART_AMP[4] + 1e-6f));
    }
    for (k = 0; k < n; k++) {
        float l = 0.0f, r = 0.0f, wet = 0.0f, rl, rr, s;
        for (i = 0; i < BW_BELLS; i++) {
            bell_t *b = &bell[i];
            float sum = 0.0f;
            if (b->note < 0)
                continue;
            for (p = 0; p < NPART; p++) {
                float re = b->re[p], im = b->im[p];
                b->re[p] = re * b->c[p] - im * b->s[p];
                b->im[p] = re * b->s[p] + im * b->c[p];
                sum += b->im[p];
            }
            l += sum * b->pan_l;
            r += sum * b->pan_r;
        }
        wet += 0.35f * (l + r);
        for (i = 0; i < BW_BLIPS; i++) {
            blip_t *b = &blip[i];
            float m;
            if (b->env < 1e-5f)
                continue;
            b->mph += b->minc;
            b->mph -= (float)(int)b->mph;
            b->ph += b->inc;
            b->ph -= (float)(int)b->ph;
            m = fm_sin_turns(b->mph) * b->idx;
            s = fm_sin_turns(b->ph + m * 0.159155f) * b->env;
            b->env *= 0.99985f;
            b->idx *= 0.9997f;
            l += s;
            r += s;
            wet += 0.25f * s;
        }
        if (surf_level > 1e-4f) {
            surf_lp1 += bright * (noise() - surf_lp1);
            surf_lp2 += bright * (surf_lp1 - surf_lp2);
            s = surf_lp2 * surf_level * (0.15f + 0.85f * swell) * 2.5f;
            l += s;
            r += s * 0.9f;
            wet += 0.2f * s;
        }
        reverb(wet, &rl, &rr);
        l = fm_tanhf((l + 0.5f * rl) * 0.8f);
        r = fm_tanhf((r + 0.5f * rr) * 0.8f);
        lr[2 * k] = (int32_t)(l * gain);
        lr[2 * k + 1] = (int32_t)(r * gain);
    }
    for (i = 0; i < BW_BELLS; i++)       /* the resonators' tiny remainders, to zero (no denormals) */
        for (p = 0; p < NPART; p++) {
            bell[i].re[p] = fm_flush(bell[i].re[p]);
            bell[i].im[p] = fm_flush(bell[i].im[p]);
        }
}
