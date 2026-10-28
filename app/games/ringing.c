/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Change ringing: see ringing.h. The methods' notations are the Central Council's (methods.cccbr.org.uk), facts
 * about the methods, written out here. */
#include "ringing.h"

const ring_method_t RING_METHODS[] = {
    {"PLAIN HUNT", 5, "5.1.5.1.5,1"},
    {"PLAIN HUNT", 6, "x16x16x16,16"},
    {"PLAIN BOB", 5, "5.1.5.1.5,125"},
    {"GRANDSIRE", 5, "3,1.5.1.5.1"},
    {"PLAIN BOB", 6, "x16x16x16,12"},
    {"KENT", 6, "34x34.16x12x16x12x16,16"},
    {"CAMBRIDGE", 6, "x36x14x12x36x12x56,12"},
    {"PLAIN HUNT", 8, "x18x18x18x18,18"},
    {"PLAIN BOB", 8, "x18x18x18x18,12"},
    {"CAMBRIDGE", 8, "x38x14x1258x36x14x58x16x78,12"},
};
const int RING_NMETHODS = (int)(sizeof RING_METHODS / sizeof RING_METHODS[0]);

static int place_of(char ch)                 /* a place's character: 0-based, -1 = not one */
{
    if (ch >= '1' && ch <= '9')
        return ch - '1';
    if (ch == '0')
        return 9;
    if (ch == 'E' || ch == 'e')
        return 10;
    if (ch == 'T' || ch == 't')
        return 11;
    return -1;
}

/* the changes of one part, s..e, into out: how many, -1 = bad */
static int part(const char *s, const char *e, int stage, ring_change_t *out, int max)
{
    int n = 0;
    while (s < e) {
        ring_change_t c = 0;
        if (*s == '.') {
            s++;
            continue;
        }
        if (*s == 'x' || *s == 'X' || *s == '-') {
            s++;
        } else {
            int p, q, lo = stage, hi = -1;
            while (s < e && (p = place_of(*s)) >= 0) {
                if (p >= stage)
                    return -1;
                c |= (ring_change_t)(1u << p);
                lo = p < lo ? p : lo;
                hi = p > hi ? p : hi;
                s++;
            }
            if (!c)
                return -1;
            /* the places implied: below the lowest place an even number of bells swap, above the highest too */
            if (lo & 1)
                c |= 1u;
            if ((stage - 1 - hi) & 1)
                c |= (ring_change_t)(1u << (stage - 1));
            for (p = 0; p < stage;) {    /* and between the places, the bells swap in pairs: every gap even */
                if (c >> p & 1u) {
                    p++;
                    continue;
                }
                for (q = p; q < stage && !(c >> q & 1u); q++)
                    ;
                if ((q - p) & 1)
                    return -1;
                p = q;
            }
        }
        if (!c && (stage & 1))           /* x on an odd stage: no such change */
            return -1;
        if (n >= max)
            return -1;
        out[n++] = c;
    }
    return n;
}

int ring_lead(const char *pn, int stage, ring_change_t *out, int max)
{
    int n = 0;
    if (stage < 2 || stage > RING_MAX)
        return 0;
    while (*pn) {
        const char *e = pn;
        int k, i;
        while (*e && *e != ',')
            e++;
        k = part(pn, e, stage, out + n, max - n);
        if (k <= 0)
            return 0;
        if (*e == ',' || n) {            /* a part of a symmetric notation: forward and back */
            if (n + 2 * k - 1 > max)
                return 0;
            for (i = 0; i < k - 1; i++)
                out[n + k + i] = out[n + k - 2 - i];
            k = 2 * k - 1;
        }
        n += k;
        pn = *e ? e + 1 : e;
    }
    return n;
}

void ring_apply(uint8_t *row, int n, ring_change_t c)
{
    int p = 0;
    while (p < n) {
        if (c >> p & 1u || p + 1 >= n) {
            p++;
            continue;
        }
        {
            uint8_t t = row[p];
            row[p] = row[p + 1];
            row[p + 1] = t;
        }
        p += 2;
    }
}

int ring_start(ring_t *r, const ring_method_t *m)
{
    int i;
    r->stage = m->stage;
    r->bells = m->stage + (m->stage & 1);
    r->nlead = ring_lead(m->pn, m->stage, r->lead, RING_LEAD_MAX);
    for (i = 0; i < RING_MAX; i++)
        r->row[i] = (uint8_t)i;
    r->at = 0;
    r->round = 0;
    return r->nlead > 0;
}

void ring_next(ring_t *r)
{
    int i;
    ring_apply(r->row, r->stage, r->lead[r->at % r->nlead]);   /* the cover stays behind */
    r->at++;
    for (i = 0; i < r->stage && r->row[i] == i; i++)
        ;
    r->round = i == r->stage;
}

int ring_place(const ring_t *r, int bell)
{
    int i;
    for (i = 0; i < r->bells; i++)
        if (r->row[i] == bell)
            return i;
    return -1;
}

int ring_score(const int32_t *t, const int32_t *ideal, int n, int32_t gap_ms)
{
    int32_t sum = 0, half = gap_ms / 2;
    int i;
    if (n <= 0 || half <= 0)
        return 0;
    for (i = 0; i < n; i++) {
        int32_t d = t[i] - ideal[i];
        d = d < 0 ? -d : d;
        sum += d > half ? half : d;
    }
    return (int)(100 - (100 * sum + n * half / 2) / (n * half));
}

int ring_pb_calls(const ring_method_t *m, ring_change_t call[3])
{
    static const char *const PN[2][3] = {{"125", "145", "123"}, {"12", "14", "1234"}};   /* doubles; minor, major */
    int i, odd = m->stage & 1;
    if (m->name[0] != 'P' || m->name[6] != 'B' || (m->stage != 5 && m->stage != 6 && m->stage != 8))
        return 0;
    for (i = 0; i < 3; i++)
        if (ring_lead(PN[!odd][i], m->stage, &call[i], 1) != 1)
            return 0;
    return 1;
}

/* a lead from rounds with each call at its end: the row it leaves, as a permutation (where each place's bell came
 * from). A lead from any row is that row taken through the same permutation */
static void lead_perm(const ring_method_t *m, const ring_change_t call[3], int c, uint8_t *perm)
{
    ring_t r;
    int i;
    ring_start(&r, m);
    r.lead[r.nlead - 1] = call[c];
    for (i = 0; i < r.nlead; i++)
        ring_next(&r);
    for (i = 0; i < RING_MAX; i++)
        perm[i] = r.row[i];
}

static int touch_search(int stage, const uint8_t perm[3][RING_MAX], const uint8_t *lh, int depth, uint8_t *seq,
                        uint8_t found[], uint8_t calls[][RING_TOUCH_MAX])
{
    int c, i, n = 0;
    if (depth >= RING_TOUCH_MAX)
        return 0;
    for (c = 0; c < 3; c++) {
        uint8_t next[RING_MAX];
        int round = 1;
        for (i = 0; i < stage; i++) {
            next[i] = lh[perm[c][i]];
            round &= next[i] == i;
        }
        seq[depth] = (uint8_t)c;
        if (round) {
            if (!found[depth + 1]) {
                found[depth + 1] = 1;
                for (i = 0; i <= depth; i++)
                    calls[depth + 1][i] = seq[i];
                n++;
            }
        } else {
            n += touch_search(stage, perm, next, depth + 1, seq, found, calls);
        }
    }
    return n;
}

int ring_pb_touches(const ring_method_t *m, uint8_t found[RING_TOUCH_MAX + 1],
                    uint8_t calls[RING_TOUCH_MAX + 1][RING_TOUCH_MAX])
{
    ring_change_t call[3];
    uint8_t perm[3][RING_MAX], lh[RING_MAX], seq[RING_TOUCH_MAX];
    int i;
    for (i = 0; i <= RING_TOUCH_MAX; i++)
        found[i] = 0;
    if (!ring_pb_calls(m, call))
        return 0;
    for (i = 0; i < 3; i++)
        lead_perm(m, call, i, perm[i]);
    for (i = 0; i < RING_MAX; i++)
        lh[i] = (uint8_t)i;
    return touch_search(m->stage, (const uint8_t(*)[RING_MAX])perm, lh, 0, seq, found, calls);
}
