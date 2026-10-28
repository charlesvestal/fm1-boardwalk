/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Fishing: the fishing pond: cast over a key, hook a fish as it bites, reel it in. Not written yet: a cabinet on the boardwalk, not open (PLAY there says SOON). Its saved
 * level, mode and best are sav.more[SV_FISH]. Part of the unity build. */
#include "game.h"

static void fish_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    (void)ms;
    cv_rect(x + w / 4, y + h / 3, w / 2, h / 3, RGB(50, 130, 170));
}

static void fish_best(char *t)
{
    t[0] = 0;
    if (sav.more[SV_FISH].best) {
        text_cat(t, "BEST ");
        int_text(sav.more[SV_FISH].best, t + 5);
    }
}

#ifdef PLAT_HOST
/* the host simulator: the game's own script commands (1 = handled) and checks (1 = known), a call every
 * simulated millisecond (a test player), and its lines for the speakdump (sp_out) */
static int fish_host_cmd(const char *cmd, const char *a)
{
    (void)cmd;
    (void)a;
    return 0;
}
static int fish_expect(const char *what, int *got)
{
    (void)what;
    (void)got;
    return 0;
}
static void fish_host_ms(uint32_t now) { (void)now; }
static void fish_speak_dump(void) {}
#endif

static const game_t GAME_FISH = {"FISHING", RGB(50, 130, 170), 0, 0, 0, 0, 0, 0, 0, fish_attract, fish_best, 0, 0, 0, 0, 0, 0,
                                  0, 0, "HOOK A BIG ONE", 0};
