/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Fortune: the fortune machine. Barnaby Brass, a brass automaton seer with a gear for a crown and a turned-up
 * moustache, sits in a glass booth with his crystal ball. You ask him a question in notes: play up to eight on the
 * keys (each heard as you go, and written on the question slip), then PLAY. His eyes glow, the ball swirls, a drone
 * and a shower of chimes, and he answers with a tune grown from yours: your phrase echoed, then turned upside down,
 * then brought home to its first note, a bass under it. A card slides out of the slot with your fortune (read out)
 * and a lucky note.
 *
 * A toy, not a contest: the same question always gets the same answer (the card and the lucky note come from the
 * notes), and the best is how many fortunes he has told. PLAY (or a key) asks again. K1 his voice: chime, piano or
 * bells. Saved in sav.more[SV_FORTUNE]: mode the voice, best the fortunes told. Part of the unity build. */
#include "game.h"

#define FO_MAX 8                         /* notes in a question */
#define FO_THINK 1700                    /* ms of looking into the ball before the answer */
#define FO_STEP 230                      /* ms a note of the answer */
#define FO_SLIT 159                      /* the card slot's y */
#define FO_CARD_H 60                     /* the card, all the way out */
#define FO_SLIDE 900                     /* ms to slide out */
#define FO_EVS 40

#define FO_CAB RGB(120, 80, 150)
#define FO_BRASS RGB(214, 168, 82)
#define FO_BRASS_D RGB(150, 104, 48)
#define FO_BRASS_L RGB(252, 220, 140)
#define FO_WOOD RGB(92, 52, 78)
#define FO_WOOD_D RGB(62, 34, 56)
#define FO_GLASS0 RGB(46, 28, 50)
#define FO_GLASS1 RGB(88, 52, 66)
#define FO_BALL RGB(84, 64, 132)
#define FO_MIST RGB(200, 180, 240)

enum { FS_ASK, FS_THINK, FS_ANSWER, FS_CARD };
enum { FE_SPARKLE, FE_VOICE, FE_BASS, FE_DRONE, FE_HUSH };
enum { FV_CHIME, FV_PIANO, FV_BELLS, FV_N };

static const char *const FO_VOICE[FV_N] = {"CHIME", "PIANO", "BELLS"};

/* the cards: warm, playful, seaside and music; never health, money or worse */
static const char *const FORTUNE[] = {
    "A TUNE YOU HUM TODAY WILL FOLLOW YOU HOME.",
    "THE NEXT WAVE BRINGS GOOD NEWS.",
    "A GULL WILL SING YOUR SONG BACK TO YOU.",
    "SOMEONE NEAR YOU IS HUMMING THE SAME TUNE.",
    "YOUR NEXT CHORD WILL BE A HAPPY ONE.",
    "A FRIEND WILL SOON ASK YOU FOR A SONG.",
    "THE TIDE IS TURNING YOUR WAY.",
    "LOOK UP AT SUNSET. THE SKY HAS A SURPRISE FOR YOU.",
    "A SMALL SONG WILL MAKE A BIG DAY.",
    "YOU WILL DANCE WHEN NO ONE IS LOOKING, AND IT WILL BE WONDERFUL.",
    "THE BIG WHEEL WILL WAVE TO YOU TONIGHT.",
    "A SHELL ON THE SAND HOLDS A MELODY MEANT FOR YOU.",
    "YOUR LUCKY RHYTHM IS THREE SHORT AND ONE LONG.",
    "A WRONG NOTE WILL TURN OUT TO BE THE RIGHT ONE.",
    "SOMEONE WILL WHISTLE YOUR TUNE BEFORE THE WEEK IS OUT.",
    "THE CASTLE YOU BUILD IN THE SAND WILL STAND TALL.",
    "THE BAND WILL PLAY THE SONG YOU LOVE BEST.",
    "AN OLD SONG WILL FEEL NEW AGAIN.",
    "THE BEST ICE CREAM IS JUST AROUND THE CORNER.",
    "YOUR LAUGH WILL START A CHORUS.",
    "A KITE WILL CATCH THE WIND YOU NEED.",
    "TODAY YOUR STEPS WILL KEEP PERFECT TIME.",
    "A NEW FRIEND IS ONE HELLO AWAY.",
    "THE MOON WILL HUM ALONG WITH YOU TONIGHT.",
    "THE STARS AGREE. YOU ARE IN TUNE.",
    "PLAY IT AGAIN. IT GETS BETTER EVERY TIME.",
    "THE SEA HAS SAVED A SUNNY DAY FOR YOU.",
    "A SONG YOU FORGOT WILL COME BACK TO YOU.",
    "YOU WILL HEAR BELLS, AND THEY WILL RING FOR YOU.",
    "THE LIGHTS ON THE PIER WILL SHINE JUST FOR YOU TONIGHT.",
    "SOMEONE WILL SMILE AT YOUR HUMMING.",
    "A SAILOR WILL TEACH YOU A SONG WORTH KEEPING.",
};
#define FO_NFORT ((int)(sizeof FORTUNE / sizeof FORTUNE[0]))

static struct {
    volatile int state;
    int n;                               /* the question's notes */
    uint8_t q[FO_MAX];                   /* MIDI */
    int idx, lucky;                      /* the answer: its card, its lucky note (MIDI) */
    uint32_t key_ms;                     /* a note asked: his eyes flicker */
    uint32_t nudge_ms;                   /* PLAY with no question: the slip shakes (0 never) */
    uint32_t t0;                         /* PLAY: the answer's clock */
    int32_t card_at, end_at;             /* ms after t0: the card slides out; the answer over */
    int said, mark;                      /* the card read out; a card remembered (the tests) */
    /* the answer's sounds, played by the tick */
    int nev;
    volatile int ev;
    int32_t ev_t[FO_EVS];
    uint8_t ev_kind[FO_EVS], ev_note[FO_EVS];
    volatile int cur;                    /* the answer's note sounding now (MIDI, 0 none) */
    volatile uint32_t cur_ms;
    int bell;                            /* the next bell slot (BELLS) */
    char lines[4][40];                   /* the card, wrapped */
    int nlines;
} fo;

static int fo_voice(void) { return sav.more[SV_FORTUNE].mode % FV_N; }

static void fo_play(int note, int vel)   /* a note in his voice (the caller holds the sound engine) */
{
    int v = fo_voice();
    if (v == FV_BELLS) {
        bw_bell(fo.bell, note + 12, vel);
        fo.bell = (fo.bell + 1) % BW_BELLS;
    } else {
        bw_note(note, vel, v == FV_PIANO ? BW_PIANO : BW_CHIME);
    }
}

static uint32_t fo_hash(void)            /* the question, as a number (FNV-1a): the same notes, the same answer */
{
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < fo.n; i++)
        h = (h ^ fo.q[i]) * 16777619u;
    return (h ^ (uint32_t)fo.n) * 16777619u;
}

static int fo_lucky(uint32_t h)          /* the note asked most; among equals, one picked by the question */
{
    int i, j, most = 0, cnt[FO_MAX], nbest = 0, pick;
    for (i = 0; i < fo.n; i++) {
        cnt[i] = 0;
        for (j = 0; j < fo.n; j++)
            cnt[i] += fo.q[j] == fo.q[i];
        if (cnt[i] > most)
            most = cnt[i];
    }
    for (i = 0; i < fo.n; i++)           /* each note once: its first place */
        if (cnt[i] == most) {
            for (j = 0; j < i && fo.q[j] != fo.q[i]; j++)
                ;
            nbest += j == i;
        }
    pick = (int)((h >> 11) % (uint32_t)nbest);
    for (i = 0; i < fo.n; i++)
        if (cnt[i] == most) {
            for (j = 0; j < i && fo.q[j] != fo.q[i]; j++)
                ;
            if (j == i && pick-- == 0)
                return fo.q[i];
        }
    return fo.q[0];
}

static void fo_wrap(const char *s)       /* the card's text in lines that fit it */
{
    int n = 0, len = 0, i;
    char word[40];
    fo.nlines = 0;
    fo.lines[0][0] = 0;
    for (;;) {
        n = 0;
        while (*s == ' ')
            s++;
        if (!*s)
            break;
        while (*s && *s != ' ' && n < 38)
            word[n++] = *s++;
        word[n] = 0;
        len = (int)text_len(fo.lines[fo.nlines]);
        if (len) {
            char t[80];
            t[0] = 0;
            text_cat(t, fo.lines[fo.nlines]);
            text_cat(t, " ");
            text_cat(t, word);
            if (len + 1 + n > 36 || text_w(&FONT_XS, t) > 196) {
                if (fo.nlines == 3)
                    break;
                fo.lines[++fo.nlines][0] = 0;
            } else {
                text_cat(fo.lines[fo.nlines], " ");
            }
        }
        text_cat(fo.lines[fo.nlines], word);
    }
    fo.nlines++;
    for (i = fo.nlines; i < 4; i++)
        fo.lines[i][0] = 0;
}

static int fo_fold(int n)                /* a note brought onto the keys by octaves */
{
    while (n < 53)
        n += 12;
    while (n > 53 + NKEYS - 1)
        n -= 12;
    return n;
}

static void fo_ev(int32_t t, int kind, int note)
{
    if (fo.nev >= FO_EVS)
        return;
    fo.ev_t[fo.nev] = t;
    fo.ev_kind[fo.nev] = (uint8_t)kind;
    fo.ev_note[fo.nev] = (uint8_t)note;
    fo.nev++;
}

static void fortune_ask(void)            /* PLAY: he looks into the ball and answers */
{
    static const uint8_t SPARKLE[8] = {96, 91, 93, 88, 89, 84, 86, 81};
    uint32_t h = fo_hash();
    int i, t, root = fo.q[0], bass = 36 + fo.q[0] % 12;
    fo.idx = (int)(h % (uint32_t)FO_NFORT);
    fo.lucky = fo_lucky(h);
    fo_wrap(FORTUNE[fo.idx]);
    fo.nev = 0;
    fo.ev = 0;
    fo.cur = 0;
    fo.said = 0;
    fo_ev(0, FE_DRONE, bass + 12);
    for (i = 0; i < 8; i++)              /* the ball wakes: a shower of chimes */
        fo_ev(120 + i * 190, FE_SPARKLE, SPARKLE[i]);
    t = FO_THINK;
    fo_ev(t, FE_BASS, bass);             /* your phrase, echoed */
    for (i = 0; i < fo.n; i++, t += FO_STEP)
        fo_ev(t, FE_VOICE, fo.q[i]);
    fo.card_at = t;
    if (fo.n > 1) {                      /* turned upside down about its first note */
        fo_ev(t, FE_BASS, bass + 7);
        for (i = 1; i < fo.n; i++, t += FO_STEP)
            fo_ev(t, FE_VOICE, fo_fold(2 * root - fo.q[i]));
    }
    t += FO_STEP / 2;                    /* and home */
    fo_ev(t, FE_BASS, bass);
    fo_ev(t, FE_VOICE, root);
    fo_ev(t, FE_VOICE, fo_fold(root + 12) == root + 12 ? root + 12 : root);
    fo_ev(t + 1400, FE_HUSH, 0);
    fo.end_at = t + 900;
    fo.t0 = plat_ms();
    fo.state = FS_THINK;                 /* (last: the tick plays the list from here) */
    app_say("BARNABY LOOKS INTO THE BALL", "");
}

static void fortune_clear(void)
{
    fo.n = 0;
    fo.idx = -1;
    fo.lucky = 0;
    fo.state = FS_ASK;
}

static void fortune_tick(uint32_t now)   /* the answer's sounds, on time */
{
    int32_t t;
    if (fo.state != FS_THINK && fo.state != FS_ANSWER)
        return;
    t = (int32_t)(now - fo.t0);
    while (fo.ev < fo.nev && t >= fo.ev_t[fo.ev]) {
        int n = fo.ev_note[fo.ev];
        switch (fo.ev_kind[fo.ev]) {
        case FE_SPARKLE:
            bw_note(n, 45, BW_CHIME);
            break;
        case FE_VOICE:
            fo_play(n, 105);
            if (n <= 53 + NKEYS - 1) {
                fo.cur = n;
                fo.cur_ms = now;
            }
            break;
        case FE_BASS:
            bw_note(n, 100, BW_BASS);
            break;
        case FE_DRONE:                   /* a low hum, a fifth over it */
            bw_tone(0, n * 100, 22);
            bw_tone(1, (n + 7) * 100 + 3, 12);
            break;
        default:
            bw_tone(0, 0, 0);
            bw_tone(1, 0, 0);
        }
        fo.ev++;
    }
}

static void fortune_key(int k, int down, uint32_t ms)
{
    (void)ms;
    if (!down || fo.state == FS_THINK || fo.state == FS_ANSWER)
        return;
    if (fo.state == FS_CARD)             /* a new question */
        fortune_clear();
    snd_lock();
    fo_play(53 + k, 100);
    snd_unlock();
    fo.key_ms = plat_ms();
    if (fo.n < FO_MAX)
        fo.q[fo.n++] = (uint8_t)(53 + k);
}

static void fortune_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (fo.state == FS_CARD) {
        fortune_clear();
        app_say("ASK AGAIN", "UP TO 8 NOTES, THEN PRESS PLAY");
    } else if (fo.state == FS_ASK) {
        if (fo.n) {
            fortune_ask();
        } else {
            fo.nudge_ms = plat_ms() | 1u;
            app_say("PLAY A FEW NOTES FIRST", "THEN PRESS PLAY");
        }
    }
}

static void fortune_enc(int role, int steps)
{
    if (role != EN_K1 || fo.state == FS_THINK || fo.state == FS_ANSWER)
        return;
    sav.more[SV_FORTUNE].mode = (uint8_t)((sav.more[SV_FORTUNE].mode + FV_N * 4 + steps) % FV_N);
    save_dirty();
    snd_lock();
    fo_play(fo.n ? fo.q[0] : 72, 90);    /* a taste of it */
    snd_unlock();
    app_readout("VOICE", FO_VOICE[fo_voice()]);
}

static void fortune_hush(void)
{
    snd_lock();
    bw_tone(0, 0, 0);
    bw_tone(1, 0, 0);
    snd_unlock();
}

static void fortune_enter(void) { fortune_clear(); }

static void fortune_leave(void)
{
    fortune_clear();
    fortune_hush();
    snd_lock();
    bw_bells_damp();
    snd_unlock();
}

static void fo_said(char *t)             /* the card, as said */
{
    char n[8];
    t[0] = 0;
    text_cat(t, FORTUNE[fo.idx]);
    text_cat(t, " LUCKY NOTE ");
    note_text(fo.lucky, n);
    text_cat(t, n);
}

static void fortune_step(uint32_t now)   /* the answer's stages, by its clock */
{
    int32_t t = (int32_t)(now - fo.t0);
    if (fo.state == FS_THINK && t >= FO_THINK)
        fo.state = FS_ANSWER;
    if (fo.state == FS_ANSWER && t >= fo.card_at && !fo.said) {
        static char said[ANN_MAX + 1];   /* (not sp_buf: app_say builds in that) */
        fo.said = 1;
        if (sav.more[SV_FORTUNE].best < 65535u)
            sav.more[SV_FORTUNE].best++;  /* (saved when the tune is over) */
        save_dirty();
        tickets_win(1);                  /* tickets: one a fortune told */
        fo_said(said);
        app_say(said, "");
    }
    if (fo.state == FS_ANSWER && t >= fo.end_at) {
        fo.state = FS_CARD;
        snd_lock();                      /* the lucky note, on its own once the tune is over */
        fo_play(fo.lucky, 95);
        snd_unlock();
    }
}

/* ----------------------------------------------------------------- drawing --- */
static void thick_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)   /* 3 px */
{
    int d;
    for (d = -1; d <= 1; d++) {
        cv_line(x0 + d, y0, x1 + d, y1, c);
        cv_line(x0, y0 + d, x1, y1 + d, c);
    }
}

static void fo_gear(int32_t cx, int32_t cy, float rot)   /* his crown: a gear, turning while he thinks */
{
    int i;
    for (i = 0; i < 10; i++) {
        float a = rot + (float)i * 0.6283185f;
        fill_circle(cx + (int32_t)(12.0f * fm_cosf(a)), cy + (int32_t)(12.0f * fm_sinf(a)), 3, FO_BRASS_D);
    }
    fill_circle(cx, cy, 11, FO_BRASS_D);
    fill_circle(cx, cy, 8, FO_BRASS);
    fill_circle(cx, cy, 3, RGB(62, 154, 176));   /* a teal gem at its hub */
}

static void fo_ball(int32_t bx, int32_t by, uint32_t now, int busy)   /* the crystal ball, swirling */
{
    int a, j;
    float spin = (float)(now % 100000u) * (busy ? 0.0062f : 0.0011f);
    int32_t glow = busy ? 150 + (int32_t)(60.0f * fm_sinf((float)(now % 100000u) * 0.008f)) : 40;
    if (busy) {                          /* a halo */
        ring_circle(bx, by, 20, mix(FO_GLASS1, FO_MIST, glow / 2));
        ring_circle(bx, by, 22, mix(FO_GLASS1, FO_MIST, glow / 4));
    }
    fill_circle(bx, by, 17, mix(FO_BALL, RGB(130, 110, 190), glow));
    for (a = 0; a < 3; a++)              /* the mist: three arms of a spiral */
        for (j = 2; j < 16; j++) {
            float ang = (float)a * 2.0943951f + (float)j * 0.42f - spin, rr = (float)j * 1.0f;
            int32_t px = bx + (int32_t)(rr * fm_cosf(ang)), py = by + (int32_t)(rr * 0.85f * fm_sinf(ang));
            uint16_t c = mix(FO_MIST, C_WHITE, busy ? 256 - j * 14 : 0);
            c = mix(mix(FO_BALL, RGB(130, 110, 190), glow), c, busy ? 230 - j * 6 : 120 - j * 5);
            cv_rect(px, py, 2, 2, c);
        }
    ring_circle(bx, by, 17, mix(FO_BALL, FO_MIST, 140));
    fill_circle(bx - 7, by - 7, 3, mix(FO_MIST, C_WHITE, 160));   /* the glint */
    cv_rect(bx - 12, by + 16, 25, 3, FO_BRASS_D);   /* its stand */
    cv_rect(bx - 8, by + 14, 17, 3, FO_BRASS);
}

static void fo_seer(uint32_t now, int busy) /* Barnaby Brass, in his booth */
{
    float tt = (float)(now % 100000u) * 0.001f;
    int32_t t = (int32_t)(now - fo.t0), nod = 0, hy, y, hx = 120;
    int glow, blink;
    if (busy)                            /* a slow sway as he thinks */
        nod = (int32_t)(2.0f * fm_sinf(tt * 3.0f));
    if (fo.state == FS_ANSWER && t >= fo.card_at && t < fo.card_at + 500)
        nod = 4 - (t - fo.card_at) * 4 / 500 + 1;   /* the nod: there you are */
    if (fo.state == FS_CARD && now - fo.t0 < (uint32_t)fo.end_at + 400u)
        nod = 1;
    hy = 82 + nod;
    /* the body: round brass shoulders, a plum waistcoat, rivets */
    for (y = 104; y < 158; y++) {
        int32_t hw = y < 116 ? 36 - (116 - y) * (116 - y) / 7 : 36 + (y - 116) / 6, v = y - 104;
        cv_rect(hx - hw, y, 2 * hw + 1, 1, FO_BRASS_D);
        cv_rect(hx - hw + 2, y, 2 * hw - 3, 1, y < 108 ? FO_BRASS_L : FO_BRASS);
        if (v > 13)
            v = 13;
        cv_rect(hx - v, y, 2 * v + 1, 1, FO_WOOD);   /* the waistcoat, opening down from the collar */
        if (v > 2)
            cv_rect(hx - 1, y, 3, 1, G_PAPER2);   /* the shirt front */
    }
    fill_circle(hx - 28, 120, 2, FO_BRASS_D);
    fill_circle(hx + 28, 120, 2, FO_BRASS_D);
    cv_rect(hx - 6, hy + 18, 13, 6, FO_BRASS_D);   /* the neck */
    for (y = 0; y < 7; y++) {            /* a bow tie: two wings, narrowing to the knot */
        int32_t w = 8 - (y < 3 ? 3 - y : y - 3);
        cv_rect(hx - 8, 103 + y, w, 1, G_RED);
        cv_rect(hx + 9 - w, 103 + y, w, 1, G_RED);
    }
    fill_circle(hx, 106, 2, G_RED2);
    fo_gear(hx, hy - 22, busy ? tt * 1.6f : 0.15f);
    fill_circle(hx, hy, 20, FO_BRASS_D);   /* the head */
    fill_circle(hx, hy, 19, FO_BRASS);
    fill_circle(hx - 6, hy - 8, 5, FO_BRASS_L);   /* a shine */
    fill_circle(hx - 19, hy + 2, 3, FO_BRASS_D);   /* his ears: bolts */
    fill_circle(hx + 19, hy + 2, 3, FO_BRASS_D);
    /* the eyes: glowing while he looks, a flicker when a note is asked, a blink now and then */
    glow = busy ? 1 : (fo.state == FS_ASK && now - fo.key_ms < 220u) ? 1 : 0;
    blink = !busy && now % 4100u < 130u;
    {
        int s;
        for (s = -1; s <= 1; s += 2) {
            int32_t ex = hx + s * 8, ey = hy - 3;
            if (blink) {
                cv_rect(ex - 4, ey, 9, 2, FO_WOOD_D);
                continue;
            }
            fill_circle(ex, ey, 5, FO_WOOD_D);
            if (glow) {
                fill_circle(ex, ey, 4, G_SUN);
                fill_circle(ex, ey, 2, C_WHITE);
            } else {
                fill_circle(ex, ey, 4, G_PAPER);
                fill_circle(ex + (fo.state == FS_CARD ? 0 : 1), ey + 1, 2, G_INK);
            }
        }
    }
    /* the moustache, turned up at the ends; a smile under it */
    thick_line(hx - 1, hy + 8, hx - 10, hy + 9, G_INK);
    thick_line(hx + 1, hy + 8, hx + 10, hy + 9, G_INK);
    thick_line(hx - 10, hy + 9, hx - 15, hy + 4, G_INK);
    thick_line(hx + 10, hy + 9, hx + 15, hy + 4, G_INK);
    fill_circle(hx - 15, hy + 3, 2, G_INK);
    fill_circle(hx + 15, hy + 3, 2, G_INK);
    cv_line(hx - 4, hy + 13, hx, hy + 14, FO_WOOD_D);
    cv_line(hx, hy + 14, hx + 4, hy + 13, FO_WOOD_D);
    fill_circle(hx - 12, hy + 12, 2, mix(FO_BRASS, G_RED, 90));   /* rosy cheeks */
    fill_circle(hx + 12, hy + 12, 2, mix(FO_BRASS, G_RED, 90));
    /* the table and the ball */
    cv_rect(40, 150, 160, 8, FO_WOOD_D);
    cv_rect(40, 150, 160, 1, FO_BRASS_D);
    fo_ball(hx, 131, now, busy);
    /* the arms and hands: resting by the ball, or circling over it */
    {
        int s;
        for (s = -1; s <= 1; s += 2) {
            int32_t px, py;
            if (busy) {
                float a = tt * 4.0f + (s > 0 ? 3.14159f : 0.0f);
                px = hx + s * 18 + (int32_t)(6.0f * fm_cosf(a));
                py = 112 + (int32_t)(5.0f * fm_sinf(a));
            } else {
                px = hx + s * 26;
                py = 145;
            }
            thick_line(hx + s * 30, 112, px, py, FO_BRASS_D);
            fill_circle(px, py, 5, FO_BRASS_D);
            fill_circle(px, py, 4, FO_BRASS_L);
        }
    }
}

static uint16_t fortune_key_col(int k)   /* the question's keys; the answer's note; the lucky one */
{
    int i;
    if (fo.state == FS_ANSWER && fo.cur && fo.cur == 53 + k && plat_ms() - fo.cur_ms < 200u)
        return G_SUN;
    if (fo.state == FS_ASK)
        for (i = 0; i < fo.n; i++)
            if (fo.q[i] == 53 + k)
                return RGB(170, 130, 210);
    return 0;
}

static void fortune_draw(uint32_t now)
{
    int32_t y0, i, y;
    int busy = fo.state == FS_THINK || fo.state == FS_ANSWER;
    int32_t t = (int32_t)(now - fo.t0), out = 0;   /* the card: how far out */
    char s[48];
    if (fo.state == FS_CARD)
        out = FO_CARD_H;
    else if (fo.state == FS_ANSWER && t >= fo.card_at)
        out = clamp((t - fo.card_at) * FO_CARD_H / FO_SLIDE, 0, FO_CARD_H);
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, G_SKY1);
        for (y = 18; y < 198; y += 6)    /* the golden hour behind the booth */
            cv_rect(0, y, 240, 6, mix(G_SKY0, G_SKY1, (y - 18) * 256 / 180));
        cv_rect(0, 0, 240, 18, FO_CAB);
        cv_text(6, 3, &FONT_XS, "FORTUNE", G_PAPER);
        s[0] = 0;
        text_cat(s, "TOLD ");
        int_text(sav.more[SV_FORTUNE].best, s + 5);
        cv_text(234 - text_w(&FONT_XS, s), 3, &FONT_XS, s, G_PAPER);
        /* the booth: wood, a sign, bulbs up the sides */
        round_rect(22, 22, 196, 176, 10, FO_WOOD);
        round_rect(34, 25, 172, 24, 6, FO_WOOD_D);
        text_mid(120, 29, &FONT_B, "BARNABY BRASS", busy ? G_SUN : FO_BRASS_L);
        for (i = 0; i < 13; i++) {
            int on = busy ? ((int32_t)(now / 110u) + i) % 3 == 0 : ((int32_t)(now / 700u) + i) % 2 == 0;
            fill_circle(28, 34 + i * 13, 2, on ? G_BULB : FO_WOOD_D);
            fill_circle(211, 34 + i * 13, 2, on ? G_BULB : FO_WOOD_D);
        }
        /* the glass and the dim, warm inside */
        for (y = 52; y < 158; y += 2)
            cv_rect(36, y, 168, 2, mix(FO_GLASS0, busy ? RGB(110, 60, 110) : FO_GLASS1, (y - 52) * 256 / 106));
        fo_seer(now, busy);
        cv_line(44, 120, 70, 60, mix(FO_GLASS1, G_PAPER, 60));   /* the glass's glints */
        cv_line(48, 124, 74, 64, mix(FO_GLASS1, G_PAPER, 40));
        cv_line(182, 150, 198, 116, mix(FO_GLASS1, G_PAPER, 50));
        cv_rect(34, 50, 172, 2, FO_BRASS_D);   /* the window's frame */
        cv_rect(34, 156, 172, 2, FO_BRASS_D);
        /* the slot, and the question slip under it */
        cv_rect(30, FO_SLIT - 1, 180, 4, G_SCREEN);
        cv_rect(30, FO_SLIT - 2, 180, 1, FO_BRASS_D);
        if (out < FO_CARD_H) {
            uint32_t nd = fo.nudge_ms && !fo.n ? now - fo.nudge_ms : 9999u;   /* PLAY too soon: a shake */
            int32_t jx = nd < 400u ? (nd / 50u & 1u ? 3 : -3) : 0;
            round_rect(36 + jx, 167, 168, 28, 4, G_PAPER2);
            if (!fo.n) {                 /* how to ask */
                text_mid(120 + jx, 169, &FONT_XS, "ASK WITH UP TO 8 NOTES,", nd < 900u ? G_RED : G_INK);
                text_mid(120 + jx, 182, &FONT_XS, "THEN PRESS PLAY.", nd < 900u ? G_RED : G_INK);
            } else {
                int line;
                for (line = 0; line < 2 && line * 4 < fo.n; line++) {
                    s[0] = 0;
                    for (i = line * 4; i < fo.n && i < line * 4 + 4; i++) {
                        if (s[0])
                            text_cat(s, "  ");
                        note_text(fo.q[i], s + text_len(s));
                    }
                    text_mid(120, fo.n > 4 ? 169 + line * 13 : 175, &FONT_XS, s, G_INK);
                }
            }
        }
        keyboard(198, 20, fortune_key_col);
        if (out > 0) {                   /* the card, out of the slot (nothing of it above the slit) */
            int32_t top = FO_SLIT + 2 + out - FO_CARD_H, cy;
            cv_y0 = clamp(FO_SLIT + 2 - y0, 0, BAND_H);
            round_rect(16, top, 208, FO_CARD_H - 2, 5, G_PAPER);
            cv_rect(19, top + 3, 202, 1, FO_CAB);
            cv_rect(19, top + FO_CARD_H - 6, 202, 1, FO_CAB);
            cy = top + 5 + (3 - fo.nlines) * 6;
            for (i = 0; i < fo.nlines; i++)
                text_mid(120, cy + i * 12, &FONT_XS, fo.lines[i], G_INK);
            s[0] = 0;
            text_cat(s, "LUCKY NOTE ");
            note_text(fo.lucky, s + text_len(s));
            text_mid(120, top + 41, &FONT_XS, s, FO_CAB);
            fill_circle(30, top + 47, 2, G_ORANGE);   /* little stars at its corners */
            fill_circle(210, top + 47, 2, G_ORANGE);
            cv_y0 = 0;
        }
        cv_rect(0, 220, 240, 20, G_LILAC);
        s[0] = 0;
        text_cat(s, "K1 VOICE: ");
        text_cat(s, FO_VOICE[fo_voice()]);
        cv_text(FOOT_L, FOOT_Y, &FONT_XS, s, G_PLANK2);
        {
            const char *f = fo.state == FS_CARD ? "PLAY: ASK AGAIN" : fo.state == FS_ASK ? (fo.n ? "PLAY: ASK" : "") :
                                                                                       "LISTEN";
            cv_text(FOOT_R - text_w(&FONT_XS, f), FOOT_Y, &FONT_XS, f, G_INK);
        }
        band_send(y0);
    }
}

static void fortune_frame(uint32_t now)
{
    fortune_step(now);
    fortune_draw(now);
}

static void fortune_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    int32_t bx = x + w / 2, by = y + h - h / 3, r = h / 4, a, j;
    float spin = (float)(ms % 100000u) * 0.004f;
    fill_circle(bx, y + h / 4, r * 2 / 3, FO_BRASS);   /* his head, eyes aglow */
    cv_rect(bx - r / 3 - 1, y + h / 4 - 1, 2, 2, G_SUN);
    cv_rect(bx + r / 3, y + h / 4 - 1, 2, 2, G_SUN);
    fill_circle(bx, by, r, FO_BALL);
    for (a = 0; a < 3; a++)
        for (j = 1; j < r; j += 2) {
            float ang = (float)a * 2.0943951f + (float)j * 0.5f - spin;
            cv_pset(bx + (int32_t)((float)j * fm_cosf(ang)), by + (int32_t)((float)j * fm_sinf(ang)), FO_MIST);
        }
}

static void fortune_best(char *t)
{
    t[0] = 0;
    if (sav.more[SV_FORTUNE].best) {
        text_cat(t, "TOLD ");
        int_text(sav.more[SV_FORTUNE].best, t + 5);
    }
}

static uint32_t fortune_keys_lit(uint32_t *mid)   /* the question's keys at the mid level; the answer's note bright;
                                                    * the lucky note on the card */
{
    uint32_t lit = 0;
    int i;
    *mid = 0;
    if (fo.state == FS_ASK)
        for (i = 0; i < fo.n; i++)
            *mid |= 1u << (fo.q[i] - 53);
    if (fo.state == FS_ANSWER && fo.cur && plat_ms() - fo.cur_ms < 200u)
        lit = 1u << (fo.cur - 53);
    if (fo.state == FS_CARD && fo.lucky >= 53 && fo.lucky < 53 + NKEYS)
        lit = 1u << (fo.lucky - 53);
    return lit;
}

static void fortune_where(char *t)
{
    int i;
    t[0] = 0;
    if (fo.state == FS_ASK) {
        text_cat(t, "FORTUNE. BARNABY BRASS, THE SEER. ");
        if (fo.n) {
            text_cat(t, "YOUR QUESTION ");
            for (i = 0; i < fo.n; i++) {
                if (i)
                    text_cat(t, ", ");
                note_text(fo.q[i], t + text_len(t));
            }
            text_cat(t, ". PLAY ASKS. ");
        } else {
            text_cat(t, "ASK WITH UP TO 8 NOTES, THEN PRESS PLAY. ");
        }
        text_cat(t, "KNOB 1, VOICE ");
        text_cat(t, FO_VOICE[fo_voice()]);
        text_cat(t, ".");
        if (sav.more[SV_FORTUNE].best) {
            text_cat(t, " ");
            sp_num(t, sav.more[SV_FORTUNE].best);
            text_cat(t, sav.more[SV_FORTUNE].best == 1 ? " FORTUNE TOLD." : " FORTUNES TOLD.");
        }
        return;
    }
    if (fo.state != FS_CARD) {
        text_cat(t, "FORTUNE. BARNABY LOOKS INTO THE BALL. LISTEN.");
        return;
    }
    text_cat(t, "FORTUNE. THE CARD SAYS, ");
    fo_said(t + text_len(t));
    text_cat(t, ". PLAY ASKS AGAIN.");
}

static int fortune_playing(void)         /* the answer, and the lucky note ringing after it (the save waits: a flash
                                          * write holds the sound) */
{
    return fo.state == FS_THINK || fo.state == FS_ANSWER ||
           (fo.state == FS_CARD && plat_ms() - fo.t0 < (uint32_t)fo.end_at + 3000u);
}

#ifdef PLAT_HOST
/* the host simulator: the game's own script commands (1 = handled) and checks (1 = known), a call every
 * simulated millisecond (a test player), and its lines for the speakdump (sp_out)
 *   fortune_mark            remember the card on show (expect fortune_same 1: the same card again) */
static int fortune_host_cmd(const char *cmd, const char *a)
{
    (void)a;
    if (!strcmp(cmd, "fortune_mark")) {
        fo.mark = fo.idx;
        return 1;
    }
    return 0;
}
static int fortune_expect(const char *what, int *got)
{
    if (!strcmp(what, "fortune_state"))  /* 0 asking, 1 looking, 2 answering, 3 the card */
        *got = fo.state;
    else if (!strcmp(what, "fortune_notes"))
        *got = fo.n;
    else if (!strcmp(what, "fortune_card"))   /* the card's number, -1 none */
        *got = fo.idx;
    else if (!strcmp(what, "fortune_same"))
        *got = fo.idx >= 0 && fo.idx == fo.mark;
    else if (!strcmp(what, "fortune_lucky"))   /* MIDI */
        *got = fo.lucky;
    else if (!strcmp(what, "fortune_told"))
        *got = sav.more[SV_FORTUNE].best;
    else if (!strcmp(what, "fortune_voice"))
        *got = fo_voice();
    else if (!strcmp(what, "fortune_maxlines")) {   /* the most lines a card takes (3 fit) */
        int i, m = 0;
        for (i = 0; i < FO_NFORT; i++) {
            fo_wrap(FORTUNE[i]);
            m = fo.nlines > m ? fo.nlines : m;
        }
        if (fo.idx >= 0)
            fo_wrap(FORTUNE[fo.idx]);
        *got = m;
    }
    else
        return 0;
    return 1;
}
static void fortune_host_ms(uint32_t now) { (void)now; }
static void fortune_speak_dump(void)
{
    int i, keep_n = fo.n, keep_state = fo.state, keep_idx = fo.idx, keep_lucky = fo.lucky;
    uint8_t keep_mode = sav.more[SV_FORTUNE].mode;
    static const uint8_t Q[3] = {60, 64, 67};
    for (i = 0; i < FV_N; i++)
        sp_out("READOUT", (sp_join("VOICE", FO_VOICE[i]), sp_buf));
    sav.more[SV_FORTUNE].mode = 0;
    fo.state = FS_ASK;
    fo.n = 0;
    fortune_where(sp_buf);
    sp_out("SCREEN", sp_buf);
    fo.n = 3;
    memcpy(fo.q, Q, 3);
    fortune_where(sp_buf);
    sp_out("SCREEN", sp_buf);
    fo.state = FS_THINK;
    fortune_where(sp_buf);
    sp_out("SCREEN", sp_buf);
    fo.state = FS_CARD;
    fo.lucky = 61;
    for (i = 0; i < FO_NFORT; i++) {     /* every card, on the screen and as said */
        fo.idx = i;
        fortune_where(sp_buf);
        sp_out("SCREEN", sp_buf);
        fo_said(sp_buf);
        sp_out("MSG", sp_buf);
    }
    sp_out("MSG", "BARNABY LOOKS INTO THE BALL");
    sp_out("MSG", (sp_join("ASK AGAIN", "UP TO 8 NOTES, THEN PRESS PLAY"), sp_buf));
    sp_out("MSG", (sp_join("PLAY A FEW NOTES FIRST", "THEN PRESS PLAY"), sp_buf));
    fo.n = keep_n;
    fo.state = keep_state;
    fo.idx = keep_idx;
    fo.lucky = keep_lucky;
    sav.more[SV_FORTUNE].mode = keep_mode;
}
#endif

static const uint8_t FORTUNE_TUNE[] = {69, 72, 76, 81, 0, 79, 76, 72, 0, 74, 77, 81, 84, 0, 81, 0};

static const game_t GAME_FORTUNE = {"FORTUNE", FO_CAB, fortune_enter, fortune_leave, fortune_frame, fortune_key,
                                    fortune_button, fortune_enc, fortune_tick, fortune_attract, fortune_best,
                                    fortune_keys_lit, FORTUNE_TUNE, sizeof FORTUNE_TUNE, 200, BW_CHIME,
                                    fortune_where, 0, fortune_playing, "HEAR YOUR FUTURE", 0};
