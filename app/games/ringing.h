/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Change ringing, pure C (no platform): methods in place notation, the rows they ring, and how evenly a ringer
 * strikes. Host-tested by tests/host/ringing_test.c.
 *
 * Place notation: a change is the places that stay ("16": bells in 1st and 6th place stay, the others swap in
 * pairs) or "x" (every pair swaps); changes are separated by "." or by an "x" between them. A "," splits the
 * notation into parts, each one rung forward and back (a palindrome on its last change): "x16x16x16,12" is Plain Bob
 * Minor's lead, x16x16x16x16x16x then 12. Places 1..9, 0 = 10, E = 11, T = 12; a stage up to RING_MAX. */
#pragma once
#include <stdint.h>

#define RING_MAX 8                       /* bells */
#define RING_LEAD_MAX 48                 /* changes in a lead */

typedef struct {
    const char *name;
    uint8_t stage;                       /* working bells; an odd stage rings a cover tenor behind */
    const char *pn;
} ring_method_t;

extern const ring_method_t RING_METHODS[];
extern const int RING_NMETHODS;

/* a change: bit p set = place p (0-based) stays */
typedef uint16_t ring_change_t;

/* the lead's changes from a method's notation: how many (0 = bad notation) */
int ring_lead(const char *pn, int stage, ring_change_t *out, int max);
/* one change on a row of n bells (bell numbers 0..n-1): every pair not held swaps */
void ring_apply(uint8_t *row, int n, ring_change_t c);

/* a method rung from rounds: the row now, which change comes next, and whether it has come round */
typedef struct {
    ring_change_t lead[RING_LEAD_MAX];
    int nlead, stage, bells;             /* bells: stage, plus the cover on odd stages */
    uint8_t row[RING_MAX];
    int at;                              /* changes rung since rounds */
    int round;                           /* 1: back in rounds after a change (the plain course is over) */
} ring_t;

int ring_start(ring_t *r, const ring_method_t *m);   /* rounds; 0 = bad method */
void ring_next(ring_t *r);                           /* the next change */
int ring_place(const ring_t *r, int bell);           /* where a bell is in the row (0-based) */

/* conducting Plain Bob: the lead end's change for a plain lead, a bob and a single (call[0..2]); 0 = the method is
 * not Plain Bob on 5, 6 or 8 */
#define RING_TOUCH_MAX 8                 /* leads */
enum { RING_PLAIN, RING_BOB, RING_SINGLE };
int ring_pb_calls(const ring_method_t *m, ring_change_t call[3]);
/* every touch length up to RING_TOUCH_MAX leads that a sequence of calls brings round (first comes round at that
 * lead): found[l] = 1, with one such sequence in calls[l][0..l-1] (RING_PLAIN / BOB / SINGLE); index 0 unused. How
 * many lengths were found */
int ring_pb_touches(const ring_method_t *m, uint8_t found[RING_TOUCH_MAX + 1],
                    uint8_t calls[RING_TOUCH_MAX + 1][RING_TOUCH_MAX]);

/* striking: blows at times t (ms) against where they belonged (ideal, ms); n blows.
 * The score is 100 for perfect, 0 for an average error of half a gap or more (gap: ms between blows) */
int ring_score(const int32_t *t, const int32_t *ideal, int n, int32_t gap_ms);

/* the time of a blow: row r (0-based, from the first row rung, a handstroke), place p, bells n, gap ms; each
 * handstroke after the first (rows 2, 4, ...) starts one gap late: the handstroke gap */
static inline int32_t ring_time(int32_t row, int32_t place, int32_t n, int32_t gap)
{
    return (row * n + place + row / 2) * gap;
}
