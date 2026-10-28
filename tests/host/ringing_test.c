/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The ringing core (app/games/ringing.c) on the host: every method's plain course comes round at its known length,
 * true (no row twice), every change a legal one (a bell moves at most one place); the notation's parts; the
 * handstroke gap; the striking score. */
#include <stdio.h>
#include <string.h>
#include "ringing.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); \
                                       printf("\n"); fails++; } } while (0)

static void row_text(const uint8_t *row, int n, char *t)
{
    int i;
    for (i = 0; i < n; i++)
        t[i] = (char)('1' + row[i]);
    t[n] = 0;
}

/* the plain course: its length, true, and each change legal */
static void course(int m, int want)
{
    static uint8_t seen[1024][RING_MAX];
    const ring_method_t *me = &RING_METHODS[m];
    ring_t r;
    int n = 0, i, j;
    CHECK(ring_start(&r, me), "%s %d: bad notation", me->name, me->stage);
    if (!r.nlead)
        return;
    memcpy(seen[n++], r.row, RING_MAX);
    do {
        uint8_t before[RING_MAX];
        memcpy(before, r.row, RING_MAX);
        ring_next(&r);
        for (i = 0; i < r.bells; i++) {
            int d = ring_place(&r, i) - (int)(memchr(before, i, RING_MAX) ? (uint8_t *)memchr(before, i, RING_MAX) - before : -9);
            CHECK(d >= -1 && d <= 1, "%s %d: bell %d jumped %d at change %d", me->name, me->stage, i + 1, d, r.at);
        }
        CHECK(r.row[r.bells - 1] == r.bells - 1 || !(me->stage & 1), "%s: the cover moved", me->name);
        if (!r.round)
            for (j = 0; j < n; j++)
                if (!memcmp(seen[j], r.row, RING_MAX)) {
                    char t[RING_MAX + 1];
                    row_text(r.row, r.bells, t);
                    CHECK(0, "%s %d: row %s again at change %d", me->name, me->stage, t, r.at);
                    return;
                }
        if (n < 1024)
            memcpy(seen[n++], r.row, RING_MAX);
    } while (!r.round && r.at < 1000);
    CHECK(r.at == want, "%s %d: came round after %d changes, want %d", me->name, me->stage, r.at, want);
}

int main(void)
{
    ring_change_t c[RING_LEAD_MAX];
    uint8_t row[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    char t[9];
    static const int LEN[] = {10, 12, 40, 30, 60, 120, 120, 16, 112, 224};   /* the plain courses, in RING_METHODS' order */
    int i;
    int32_t ideal[4] = {0, 200, 400, 600}, hit[4];

    /* the notation */
    CHECK(ring_lead("x16x16x16,12", 6, c, RING_LEAD_MAX) == 12, "Plain Bob Minor's lead is 12");
    CHECK(c[0] == 0 && c[1] == 0x21 && c[10] == 0 && c[11] == 0x03, "x16 ... x 12");
    CHECK(ring_lead("3,1.5.1.5.1", 5, c, RING_LEAD_MAX) == 10, "Grandsire Doubles' lead is 10");
    CHECK(ring_lead("3.1.5.3.1.3.1.3.5.1.3.1", 5, c, RING_LEAD_MAX) == 12, "Stedman's six-pair: no palindrome");
    CHECK(ring_lead("x", 5, c, RING_LEAD_MAX) == 0, "x on odd bells");
    CHECK(ring_lead("13", 6, c, RING_LEAD_MAX) == 0, "13 on six leaves a single bell between");
    CHECK(ring_lead("17", 6, c, RING_LEAD_MAX) == 0, "no seventh place on six");
    ring_lead("5", 5, c, 4);
    CHECK(c[0] == 0x10, "5 on five: only fifth's place");
    ring_lead("1", 6, c, 4);
    CHECK(c[0] == 0x21, "1 on six implies 16");
    ring_apply(row, 6, 0);
    row_text(row, 6, t);
    CHECK(!strcmp(t, "214365"), "x from rounds: %s", t);

    for (i = 0; i < RING_NMETHODS; i++)
        course(i, LEN[i]);

    /* conducting Plain Bob: the calls, and the touches they make */
    {
        uint8_t found[RING_TOUCH_MAX + 1], calls[RING_TOUCH_MAX + 1][RING_TOUCH_MAX];
        ring_change_t call[3];
        int l, k, n;
        CHECK(ring_pb_calls(&RING_METHODS[4], call) && call[0] == 0x03 && call[1] == 0x09 && call[2] == 0x0F,
              "Plain Bob Minor: 12, 14, 1234");
        CHECK(!ring_pb_calls(&RING_METHODS[6], call), "Cambridge has no Plain Bob calls");
        for (k = 0; k < RING_NMETHODS; k++) {
            if (!ring_pb_calls(&RING_METHODS[k], call))
                continue;
            n = ring_pb_touches(&RING_METHODS[k], found, calls);
            printf("  %s %d: touches of", RING_METHODS[k].name, RING_METHODS[k].stage);
            for (l = 1; l <= RING_TOUCH_MAX; l++)
                if (found[l])
                    printf(" %d", l);
            printf(" leads\n");
            CHECK(n > 1, "%s %d: more than one touch", RING_METHODS[k].name, RING_METHODS[k].stage);
            /* each touch found: rung out change by change, it comes round first at its length */
            for (l = 1; l <= RING_TOUCH_MAX; l++) {
                ring_t r;
                int lead, i, first = -1;
                if (!found[l])
                    continue;
                ring_start(&r, &RING_METHODS[k]);
                for (lead = 0; lead < l && first < 0; lead++)
                    for (i = 0; i < r.nlead; i++) {
                        ring_change_t save = r.lead[r.nlead - 1];
                        r.lead[r.nlead - 1] = call[calls[l][lead]];
                        ring_next(&r);
                        r.lead[r.nlead - 1] = save;
                        if (r.round && first < 0) {
                            first = lead + 1;
                            CHECK(i == r.nlead - 1, "%d leads: round mid-lead", l);
                        }
                    }
                CHECK(first == l, "%s %d, %d leads: came round at lead %d", RING_METHODS[k].name, RING_METHODS[k].stage,
                      l, first);
            }
        }
        ring_pb_touches(&RING_METHODS[4], found, calls);
        CHECK(found[5], "Plain Bob Minor's plain course: 5 leads");
        CHECK(found[3] && calls[3][0] == RING_BOB && calls[3][1] == RING_BOB && calls[3][2] == RING_BOB,
              "Plain Bob Minor: three bobs, three leads");
    }

    /* the handstroke gap: rows 0 and 1 (hand, back) without a gap between, then a gap before row 2 */
    CHECK(ring_time(0, 0, 6, 200) == 0 && ring_time(1, 0, 6, 200) == 1200 && ring_time(2, 0, 6, 200) == 2600,
          "the handstroke gap");

    /* striking */
    CHECK(ring_score(ideal, ideal, 4, 200) == 100, "perfect");
    for (i = 0; i < 4; i++)
        hit[i] = ideal[i] + (i & 1 ? 50 : -50);
    CHECK(ring_score(hit, ideal, 4, 200) == 50, "50 ms off on a 200 ms gap: %d", ring_score(hit, ideal, 4, 200));
    for (i = 0; i < 4; i++)
        hit[i] = ideal[i] + 500;
    CHECK(ring_score(hit, ideal, 4, 200) == 0, "way off");

    printf(fails ? "ringing: %d failed\n" : "ringing: ok\n", fails);
    return fails != 0;
}
