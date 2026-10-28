/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Fortune: the fortune machine: a brass seer with a crystal ball answers the notes you play with a tune and a card. Not written yet: a cabinet on the boardwalk, not open (PLAY there says SOON). Its saved
 * level, mode and best are sav.more[SV_FORTUNE]. Part of the unity build. */
#include "game.h"

static void fortune_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    (void)ms;
    cv_rect(x + w / 4, y + h / 3, w / 2, h / 3, RGB(120, 80, 150));
}

static void fortune_best(char *t)
{
    t[0] = 0;
    if (sav.more[SV_FORTUNE].best) {
        text_cat(t, "BEST ");
        int_text(sav.more[SV_FORTUNE].best, t + 5);
    }
}

#ifdef PLAT_HOST
/* the host simulator: the game's own script commands (1 = handled) and checks (1 = known), a call every
 * simulated millisecond (a test player), and its lines for the speakdump (sp_out) */
static int fortune_host_cmd(const char *cmd, const char *a)
{
    (void)cmd;
    (void)a;
    return 0;
}
static int fortune_expect(const char *what, int *got)
{
    (void)what;
    (void)got;
    return 0;
}
static void fortune_host_ms(uint32_t now) { (void)now; }
static void fortune_speak_dump(void) {}
#endif

static const game_t GAME_FORTUNE = {"FORTUNE", RGB(120, 80, 150), 0, 0, 0, 0, 0, 0, 0, fortune_attract, fortune_best, 0, 0, 0, 0, 0, 0,
                                  0, 0, "HEAR YOUR FUTURE", 0};
