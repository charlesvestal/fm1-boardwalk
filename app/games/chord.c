/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Chord Code: Mastermind with chords. A hidden chord plays (PLAY hears it again); pick its notes on the keys (a key
 * again takes it back). When you have picked as many as it has, the guess is judged: each note exact (teal), the
 * right note in the wrong octave (orange), or not in it (grey). Six guesses a chord, five chords a game; a chord
 * found sooner scores more, and more on the higher levels. Found or not, the chord is named.
 *
 * Its notes are always somewhere from C4 to G5 (the keys below are dimmed). Levels: 1 major and minor triads, root
 * position; 2 any triad (major, minor, diminished, augmented) in any inversion; 3 four-note chords (major, minor and
 * dominant sevenths).
 *
 * Standing: K1 the level; PLAY starts. Part of the unity build. */
#include "game.h"

#define CC_LEVELS 3
#define CC_TRIES 6
#define CC_CHORDS 5
#define CC_MAX 4                         /* notes in a chord */
#define CC_LOW 7                         /* the lowest key a chord can use: C4 */

enum { CS_STAND, CS_GUESS, CS_SOLVED, CS_OVER };
enum { CR_NONE, CR_EXACT, CR_OCTAVE };
enum { CK_MAJ, CK_MIN, CK_DIM, CK_AUG, CK_MAJ7, CK_MIN7, CK_DOM7 };
static struct {
    int state, level, chord, tries, score, n, new_best, kind, root, found, gain;
    uint8_t secret[CC_MAX];              /* keys, low to high */
    uint8_t guess[CC_TRIES][CC_MAX], mark[CC_TRIES][CC_MAX];
    uint32_t picked;                     /* keys picked for the guess being made */
    uint32_t judge_ms, shown_ms, next_ms, secret_ms;
    int secret_left;                     /* the secret's notes still to play, one after another, then together */
} cc;

static const char *const CC_LEVEL_NAME[CC_LEVELS] = {"MAJOR AND MINOR", "TRIADS, INVERTED", "SEVENTH CHORDS"};
static const char *const CC_KIND[7] = {"MAJOR", "MINOR", "DIMINISHED", "AUGMENTED", "MAJOR SEVENTH", "MINOR SEVENTH",
                                       "SEVENTH"};

static void chord_name(char *t)          /* "F# MINOR SEVENTH" */
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    t[0] = 0;
    text_cat(t, N[cc.root]);
    text_cat(t, " ");
    text_cat(t, CC_KIND[cc.kind]);
}

static int picks(void)
{
    uint32_t m = cc.picked;
    int c = 0;
    while (m) {
        c += (int)(m & 1u);
        m >>= 1;
    }
    return c;
}

static void cc_new_chord(uint32_t now)
{
    static const int8_t TRIADS[4][3] = {{0, 4, 7}, {0, 3, 7}, {0, 3, 6}, {0, 4, 8}};
    static const int8_t SEVENTHS[3][4] = {{0, 4, 7, 11}, {0, 3, 7, 10}, {0, 4, 7, 10}};
    int root = CC_LOW + (int)rnd(12), i, j, inv;   /* C4..B4 as keys */
    int8_t iv[CC_MAX];
    if (cc.level == 3) {
        cc.kind = CK_MAJ7 + (int)rnd(3);
        cc.n = 4;
        for (i = 0; i < 4; i++)
            iv[i] = SEVENTHS[cc.kind - CK_MAJ7][i];
    } else {
        cc.kind = cc.level == 1 ? (int)rnd(2) : (int)rnd(4);
        cc.n = 3;
        for (i = 0; i < 3; i++)
            iv[i] = TRIADS[cc.kind][i];
        if (cc.level == 2)               /* an inversion: the lowest notes up an octave (each as likely) */
            for (inv = (int)rnd(3), i = 0; i < inv; i++)
                iv[i] += 12;
    }
    cc.root = (53 + root) % 12;
    for (i = 0; i < cc.n; i++) {         /* above G5, an octave down: still the chord, no note twice */
        int k = root + iv[i];
        while (k > NKEYS - 1)
            k -= 12;
        cc.secret[i] = (uint8_t)k;
    }
    for (i = 1; i < cc.n; i++)           /* low to high */
        for (j = i; j > 0 && cc.secret[j - 1] > cc.secret[j]; j--) {
            uint8_t t = cc.secret[j];
            cc.secret[j] = cc.secret[j - 1];
            cc.secret[j - 1] = t;
        }
    cc.tries = 0;
    cc.picked = 0;
    cc.judge_ms = 0;
    cc.state = CS_GUESS;
    cc.secret_ms = now + 400u;
    cc.secret_left = cc.n + 1;
}

static void cc_sound(uint32_t now)       /* the secret: its notes one by one, then together */
{
    int i;
    if (!cc.secret_left || (int32_t)(now - cc.secret_ms) < 0)
        return;
    snd_lock();
    if (cc.secret_left > 1)
        bw_note(53 + cc.secret[cc.n - cc.secret_left + 1], 75, BW_PIANO);
    else
        for (i = 0; i < cc.n; i++)
            bw_note(53 + cc.secret[i], 62, BW_PIANO);
    snd_unlock();
    cc.secret_left--;
    cc.secret_ms = now + (cc.secret_left == 1 ? 600u : 350u);
}

static void chord_go(void)
{
    char t[24] = "FIND ";
    rnd_state ^= plat_ms() * 2654435761u;
    cc.level = clamp(sav.chord.level, 1, CC_LEVELS);
    cc.chord = 0;
    cc.score = cc.found = 0;
    cc_new_chord(plat_ms());
    int_text(cc.n, t + 5);
    text_cat(t, " NOTES");
    app_say("LISTEN", t);
}

static void cc_judge(uint32_t now)
{
    int i, j, exact = 0, t = cc.tries;
    char n[6];
    for (i = 0, j = 0; i < NKEYS && j < cc.n; i++)
        if (cc.picked >> i & 1u)
            cc.guess[t][j++] = (uint8_t)i;
    for (j = 0; j < cc.n; j++) {
        cc.mark[t][j] = CR_NONE;
        for (i = 0; i < cc.n; i++) {
            if (cc.guess[t][j] == cc.secret[i])
                cc.mark[t][j] = CR_EXACT;
            else if ((cc.guess[t][j] - cc.secret[i]) % 12 == 0 && cc.mark[t][j] != CR_EXACT)
                cc.mark[t][j] = CR_OCTAVE;
        }
        exact += cc.mark[t][j] == CR_EXACT;
    }
    cc.tries++;
    cc.picked = 0;
    cc.shown_ms = now;
    snd_lock();                          /* your guess, together */
    for (j = 0; j < cc.n; j++)
        bw_note(53 + cc.guess[t][j], 60, BW_PIANO);
    snd_unlock();
    if (exact == cc.n) {
        cc.gain = (CC_TRIES + 1 - cc.tries) * cc.level;
        cc.score += cc.gain;
        cc.found++;
        cc.state = CS_SOLVED;
        cc.next_ms = now + 2800u;
        cc.secret_ms = now + 500u;       /* its notes again, as a flourish */
        cc.secret_left = cc.n + 1;
        chord_name(sp_buf + 0);          /* (sp_buf free here: said at once) */
        {
            char m[40] = "FOUND IT, ";
            text_cat(m, sp_buf);
            app_say(m, "");
        }
        return;
    }
    sp_buf[0] = 0;                       /* each note: right, another octave, or not in it */
    for (j = 0; j < cc.n; j++) {
        note_text(53 + cc.guess[t][j], n);
        text_cat(sp_buf, n);
        text_cat(sp_buf, cc.mark[t][j] == CR_EXACT ? " RIGHT" : cc.mark[t][j] == CR_OCTAVE ? " OTHER OCTAVE" :
                                                                                         " NOT IN IT");
        text_cat(sp_buf, j + 1 < cc.n ? ". " : ".");
    }
    if (ann_on())
        ann_say(sp_buf, 0, ANN_NOW);
    if (cc.tries >= CC_TRIES) {
        char m[48] = "OUT OF GUESSES. IT WAS ";
        cc.gain = 0;
        cc.state = CS_SOLVED;            /* not found: shown, named, and on to the next */
        cc.next_ms = now + 3200u;
        cc.secret_ms = now + 900u;
        cc.secret_left = cc.n + 1;
        chord_name(m + text_len(m));
        app_say(m, "");
    } else {
        cc.secret_ms = now + 900u;       /* the secret again, to compare */
        cc.secret_left = 1;
    }
}

static void chord_key(int k, int down, uint32_t ms)
{
    int c;
    if (!down)
        return;
    snd_lock();
    bw_note(53 + k, 75, BW_PIANO);
    snd_unlock();
    if (cc.state != CS_GUESS)
        return;
    cc.picked ^= 1u << k;                /* a pick, or taken back (in the pause before judging, too) */
    c = picks();
    cc.judge_ms = c == cc.n ? ms + 600u : 0u;   /* a moment to see it (or take one back), then judged */
}

static void chord_button(int b, int down)
{
    if (!down || b != B_PLAY)
        return;
    if (cc.state == CS_STAND || cc.state == CS_OVER)
        chord_go();
    else if (cc.state == CS_GUESS) {     /* hear it again */
        cc.secret_ms = plat_ms();
        cc.secret_left = cc.n + 1;
    }
}

static void chord_enc(int role, int steps)
{
    char t[32];
    if (role != EN_K1 || cc.state == CS_GUESS || cc.state == CS_SOLVED)
        return;
    sav.chord.level = (uint8_t)clamp(sav.chord.level + steps, 1, CC_LEVELS);
    int_text(sav.chord.level, t);
    text_cat(t, ", ");
    text_cat(t, CC_LEVEL_NAME[sav.chord.level - 1]);
    app_readout("LEVEL", t);
    cc.state = CS_STAND;
    save_dirty();
}

static void chord_enter(void) { cc.state = CS_STAND; }
static void chord_leave(void) { cc.state = CS_STAND; }

static uint16_t chord_key_col(int k)
{
    int i;
    if (cc.picked >> k & 1u)
        return RGB(150, 120, 200);       /* a pick (lilac: on a black key too) */
    if (cc.state == CS_SOLVED || cc.state == CS_OVER)
        for (i = 0; i < cc.n; i++)
            if (cc.secret[i] == k)
                return G_TEAL;
    if (cc.state == CS_GUESS && k < CC_LOW)
        return mix(G_MUTE, G_PAPER, 128);   /* never in a chord */
    return 0;
}

static void chord_draw(uint32_t now)
{
    static const uint16_t MARK_COL[3] = {RGB(150, 140, 130), RGB(62, 154, 176), RGB(242, 163, 58)};
    int32_t y0, i, j;
    char s[40];
    (void)now;
    for (y0 = 0; y0 < 240; y0 += BAND_H) {
        band(y0, G_PAPER);
        cv_rect(0, 0, 240, 18, RGB(90, 70, 140));
        cv_text(6, 3, &FONT_XS, "CHORD CODE", G_PAPER);
        if (cc.state == CS_STAND) {
            cv_text(14, 30, &FONT_XS, "K1 LEVEL", G_PLANK2);
            int_text(sav.chord.level, s);
            cv_text(94, 28, &FONT_B, s, G_INK);
            cv_text(94, 46, &FONT_XS, CC_LEVEL_NAME[clamp(sav.chord.level, 1, CC_LEVELS) - 1], G_PLANK2);
            cv_text(14, 76, &FONT_XS, "HEAR A CHORD, FIND ITS NOTES.", G_PLANK2);
            for (i = 0; i < 3; i++) {    /* the marks, as a key */
                static const char *const W[3] = {"RIGHT", "OTHER OCTAVE", "NOT IN IT"};
                static const int8_t X[3] = {14, 70, 160};
                fill_circle(X[i] + 4, 98, 4, MARK_COL[i == 0 ? 1 : i == 1 ? 2 : 0]);
                cv_text(X[i] + 12, 91, &FONT_XS, W[i], G_PLANK2);
            }
            cv_text(14, 112, &FONT_XS, "SIX GUESSES A CHORD, FIVE CHORDS.", G_PLANK2);
            if (sav.chord.best) {
                text_cat((s[0] = 0, s), "BEST ");
                int_text(sav.chord.best, s + 5);
                cv_text(14, 140, &FONT_B, s, G_INK);
            }
        } else {
            if (cc.state == CS_OVER)
                text_cat((s[0] = 0, s), "5 CHORDS");
            else {
                text_cat((s[0] = 0, s), "CHORD ");
                int_text(cc.chord + 1, s + 6);
                text_cat(s, " OF 5");
            }
            cv_text(234 - text_w(&FONT_XS, s), 3, &FONT_XS, s, G_PAPER);
            text_cat((s[0] = 0, s), "SCORE ");
            int_text(cc.score, s + 6);
            cv_text(14, 24, &FONT_B, s, G_INK);
            if (cc.state == CS_SOLVED) { /* found it, or not */
                s[0] = 0;
                if (cc.gain) {
                    text_cat(s, "FOUND IT! +");
                    int_text(cc.gain, s + text_len(s));
                } else {
                    text_cat(s, "IT WAS");
                }
                cv_text(226 - text_w(&FONT_B, s), 24, &FONT_B, s, cc.gain ? G_TEAL : G_RED);
            }
            for (j = 0; j < CC_TRIES; j++) { /* the guesses so far, each note marked; the one being made */
                int32_t y = 46 + j * 23;
                int now_row = j == cc.tries && cc.state == CS_GUESS;
                cv_rect(40, y, 160, 19, j < cc.tries || now_row ? G_PAPER2 : mix(G_PAPER2, G_PAPER, 160));
                if (now_row) {
                    int k, c = 0;
                    for (k = 0; k < NKEYS && c < cc.n; k++)
                        if (cc.picked >> k & 1u) {
                            note_text(53 + k, s);
                            text_mid(40 + c * 40 + 24, y + 2, &FONT_XS, s, G_INK);
                            c++;
                        }
                    for (; c < cc.n; c++)
                        text_mid(40 + c * 40 + 24, y + 2, &FONT_XS, "-", G_PLANK2);
                }
                for (i = 0; i < cc.n && j < cc.tries; i++) {
                    int32_t x = 40 + i * 40 + 20;
                    fill_circle(x - 14, y + 9, 4, MARK_COL[cc.mark[j][i]]);
                    note_text(53 + cc.guess[j][i], s);
                    text_mid(x + 4, y + 2, &FONT_XS, s, G_INK);
                }
            }
            if (cc.state == CS_SOLVED) { /* what it was: named (its keys are teal below) */
                chord_name(s);
                text_mid(120, 184, &FONT_B, s, G_TEAL);
            } else if (cc.state == CS_OVER) {
                text_cat((s[0] = 0, s), "FOUND ");
                int_text(cc.found, s + 6);
                text_cat(s, " OF 5");
                text_mid(120, 184, &FONT_B, s, G_INK);
                text_mid(120, 24, &FONT_B, cc.new_best ? "NEW BEST!" : "", G_RED);
            }
        }
        keyboard(202, 18, chord_key_col);
        cv_rect(0, 220, 240, 20, G_LILAC);
        if (cc.state == CS_GUESS)
            cv_text(8, 224, &FONT_XS, "KEY AGAIN: UNDO", G_PLANK2);
        {
            const char *f = cc.state == CS_GUESS ? "PLAY: HEAR AGAIN" : cc.state == CS_OVER ? "PLAY: AGAIN" :
                            cc.state == CS_STAND ? "PLAY: START" : "";
            cv_text(232 - text_w(&FONT_XS, f), 224, &FONT_XS, f, G_INK);
        }
        band_send(y0);
    }
}

static void chord_frame(uint32_t now)
{
    cc_sound(now);
    if (cc.judge_ms && (int32_t)(now - cc.judge_ms) >= 0) {
        cc.judge_ms = 0;
        cc_judge(now);
    }
    if (cc.state == CS_SOLVED && (int32_t)(now - cc.next_ms) >= 0) {
        if (cc.chord + 1 >= CC_CHORDS) {
            char t[16] = "SCORE ";
            cc.state = CS_OVER;
            cc.new_best = cc.score > sav.chord.best;
            if (cc.new_best) {
                sav.chord.best = (uint16_t)cc.score;
                save_dirty();
            }
            int_text(cc.score, t + 6);
            app_say(cc.new_best ? "GAME OVER. NEW BEST" : "GAME OVER", t);
        } else {
            char t[16] = "CHORD ";
            cc.chord++;
            cc_new_chord(now);
            int_text(cc.chord + 1, t + 6);
            text_cat(t, " OF 5");
            app_say(t, "");
        }
    }
    chord_draw(now);
}

static void chord_attract(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms)
{
    static const uint16_t C[3] = {RGB(62, 154, 176), RGB(242, 163, 58), RGB(150, 140, 130)};
    int i, j, row = (int)(ms / 700u % 4u);
    for (j = 0; j < 3; j++)
        for (i = 0; i < 3; i++)
            if (j <= row)
                fill_circle(x + w * (2 * i + 1) / 6, y + h * (2 * j + 1) / 6, 3, C[(i + j + (j == row)) % 3]);
}

static void chord_best(char *t)
{
    t[0] = 0;
    if (sav.chord.best) {
        text_cat(t, "BEST ");
        int_text(sav.chord.best, t + 5);
    }
}

static uint32_t chord_keys_lit(uint32_t *mid)   /* your picks lit; after a guess, its exact notes lit, the octave ones mid */
{
    uint32_t keys = cc.picked, now = plat_ms();
    int j, t = cc.tries - 1;
    *mid = 0;
    if (cc.state == CS_STAND)
        return 0;
    if (t >= 0 && now - cc.shown_ms < 1500u && !cc.picked)
        for (j = 0; j < cc.n; j++) {
            if (cc.mark[t][j] == CR_EXACT)
                keys |= 1u << cc.guess[t][j];
            else if (cc.mark[t][j] == CR_OCTAVE)
                *mid |= 1u << cc.guess[t][j];
        }
    return keys;
}

static void chord_where(char *t)
{
    int j;
    t[0] = 0;
    if (cc.state == CS_STAND) {
        text_cat(t, "CHORD CODE. KNOB 1, LEVEL ");
        sp_num(t, sav.chord.level);
        text_cat(t, ", ");
        text_cat(t, CC_LEVEL_NAME[clamp(sav.chord.level, 1, CC_LEVELS) - 1]);
        text_cat(t, ". HEAR A CHORD, FIND ITS NOTES ON THE KEYS. SIX GUESSES A CHORD. ");
        if (sav.chord.best) {
            text_cat(t, "BEST ");
            sp_num(t, sav.chord.best);
            text_cat(t, ". ");
        }
        text_cat(t, "PLAY STARTS.");
        return;
    }
    if (cc.state == CS_OVER) {
        text_cat(t, "CHORD CODE. GAME OVER, SCORE ");
        sp_num(t, cc.score);
        text_cat(t, cc.new_best ? ". NEW BEST. FOUND " : ". FOUND ");
        sp_num(t, cc.found);
        text_cat(t, " OF 5. PLAY STARTS AGAIN.");
        return;
    }
    text_cat(t, "CHORD CODE. CHORD ");
    sp_num(t, cc.chord + 1);
    text_cat(t, " OF 5, SCORE ");
    sp_num(t, cc.score);
    if (cc.state == CS_GUESS) {
        text_cat(t, ". GUESS ");
        sp_num(t, cc.tries + 1);
        text_cat(t, " OF 6. PICK ");
        sp_num(t, cc.n);
        text_cat(t, " NOTES, ");
        sp_num(t, picks());
        text_cat(t, " PICKED. PLAY HEARS IT AGAIN.");
    } else {
        char n[24];
        chord_name(n);
        text_cat(t, ". IT WAS ");
        text_cat(t, n);
        for (j = 0; j < cc.n; j++) {
            note_text(53 + cc.secret[j], n);
            text_cat(t, j ? " " : ", ");
            text_cat(t, n);
        }
        text_cat(t, ".");
    }
}

static int chord_playing(void) { return cc.state == CS_GUESS || cc.state == CS_SOLVED; }

static const uint8_t CHORD_TUNE[] = {60, 64, 67, 0, 62, 65, 69, 0, 59, 62, 67, 0, 60, 64, 67, 72, 0, 0};

static const game_t GAME_CHORD = {"CHORD CODE", RGB(90, 70, 140), chord_enter, chord_leave, chord_frame, chord_key,
                                  chord_button, chord_enc, 0, chord_attract, chord_best, chord_keys_lit,
                                  CHORD_TUNE, sizeof CHORD_TUNE, 220, BW_PIANO, chord_where, 0, chord_playing, "CRACK THE CHORD"};
