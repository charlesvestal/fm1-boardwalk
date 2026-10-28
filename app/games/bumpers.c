/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Bumpers: bumper cars: steer with the keys, bump the others, each bump a note. Not written yet: a cabinet on the boardwalk, not open (PLAY there says SOON). Its saved
 * level, mode and best are sav.more[SV_BUMPERS]. Part of the unity build. */
#include "game.h"

static void bumpers_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    (void)ms;
    cv_rect(x + w / 4, y + h / 3, w / 2, h / 3, RGB(240, 150, 40));
}

static void bumpers_best(char *t)
{
    t[0] = 0;
    if (sav.more[SV_BUMPERS].best) {
        text_cat(t, "BEST ");
        int_text(sav.more[SV_BUMPERS].best, t + 5);
    }
}

#ifdef PLAT_HOST
/* the host simulator: the game's own script commands (1 = handled) and checks (1 = known), a call every
 * simulated millisecond (a test player), and its lines for the speakdump (sp_out) */
static int bumpers_host_cmd(const char *cmd, const char *a)
{
    (void)cmd;
    (void)a;
    return 0;
}
static int bumpers_expect(const char *what, int *got)
{
    (void)what;
    (void)got;
    return 0;
}
static void bumpers_host_ms(uint32_t now) { (void)now; }
static void bumpers_speak_dump(void) {}
#endif

static const game_t GAME_BUMPERS = {"BUMPERS", RGB(240, 150, 40), 0, 0, 0, 0, 0, 0, 0, bumpers_attract, bumpers_best, 0, 0, 0, 0, 0, 0,
                                  0, 0, "BUMP IN TUNE", 0};
