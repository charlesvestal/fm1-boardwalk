# Boardwalk (parked)

An arcade firmware for the FM-1: a row of game cabinets on a seaside boardwalk. Private.
Brainstormed 2026-10-08 (session c0938ee9), dropped the same afternoon for Life, which became its own
firmware (`fm1-life`). No code was written; this folder holds the decisions and the mockups.

Mockups in `mockups/` (open in a browser):
- `cabinets.html`: the opening screen, cabinets in front of the boardwalk; ← → slide the row. **Golden hour (B) was picked.**
- `look.html`: first ideas for the game-select menu.
- `ring-screen.html`: three ways to show Bells' Ring screen (blue line, tower, timeline).
- `ring-screen-v2.html`: the chosen mix, **layout A**: six swinging tower bells across the top, the blue line underneath.

## Decided

- **Games for v1, as of 2026-10-08:** Bells + Simon. Life became its own firmware. Later cabinets were Snake, Breaker, Pong, Songbird.
  **Revised 2026-10-09:** keep Bells; Simon and Snake aren't good enough as games. Add a perfect-pitch / ear trainer. Still to choose: the rest (see "Game ideas").
- **Look:** golden hour. The selected cabinet sits in the middle at full size, with its neighbours shrunk and dimmed; the Ferris wheel turns, gulls fly over, waves roll, the bulb strings twinkle. Each cabinet has a marquee, a small screen playing the game's attract animation and the best score.
- **Menu sound:** both together. Surf and gulls run underneath (filtered noise with a slow swell, as in Eno's breathing voices), and the selected cabinet's attract tune plays quietly on top, crossfading over ~0.5 s as you scroll. Left alone, the device is an ambient box.
- **Each game:** at most one settings page of four knobs.
- **Code structure:** one engine, games as plug-ins (below).

## Bells (change ringing)

n bells (6 or 8) ring in rows; between rows bells may only swap with neighbours. Methods are written in place
notation (Plain Bob Minor: `x16x16x16,12`), so hundreds fit in a few KB. Keep the handstroke gap (the pause
every other row).

- **v1 (option A):** *Listen* (the tower rings by itself, with the blue line drawn) and *Ring* (you take one bell on a key and are scored on striking, i.e. how evenly spaced you are, which is how real bands are judged). About 8 methods from Plain Hunt to Cambridge Surprise, on 6 or 8 bells.
- **Later:** handbells (two bells on two keys), conducting (calling Bob / Single to bring the bells back to rounds).
- **Screen:** six tower bells hang across the top in bell order, mouth-up at rest, swinging a full circle as they strike (one way at handstroke, back at backstroke; the header shows which). Countdown pips under your bell, then "NOW". The blue line scrolls underneath.
- **Sound:** modal FM / additive bells with the classic partials (hum, prime, tierce, quint, nominal), long decays.
- Check the licence of the Central Council of Church Bell Ringers' method library before shipping more than a curated handful.

## Design §1: Architecture (agreed, not built)

```
firmware/src/
  app/ui.c            the shell: boardwalk menu ↔ game, button routing, saving, glow LEDs
  app/boardwalk.c     the opening scene: sky, wheel, sea, planks, cabinets, OCT± scrolling
  games/game.h        the game interface
  games/bells.c       Bells: Listen + Ring
  games/ringing.h     pure C: place notation → rows, methods table, striking score (host-tested)
  games/<name>.c      one file per further game
  dsp/bw.c, bw.h      the sound engine (audio interrupt)
```

```c
typedef struct {
    const char *name;                     /* the marquee */
    uint16_t cab, cab_tint;               /* cabinet colours */
    void (*enter)(void), (*leave)(void);
    void (*frame)(uint32_t ms);           /* logic, main loop */
    void (*key)(int k, int down, uint32_t t_us);   /* timestamped: Bells scores timing */
    void (*button)(int b, int down);
    void (*enc)(int role, int steps);
    void (*draw)(void);                   /* full game screen */
    void (*attract_draw)(int x, int y, int w, int h);  /* the cabinet's little screen */
    void (*attract_sound)(int on);        /* start / stop its quiet tune */
    void (*score_text)(char *buf);        /* "best 94%" */
} game_t;
```

- **Controls:** in the menu, OCT− / OCT+ scroll the cabinets and PLAY or SELECT enters a game. In a game, HOME goes back to the boardwalk and everything else belongs to the game. MASTER is always volume. Felucca's safety stays (OCT− + OCT+ held 5 s = update mode).
- **Sound engine `bw`** (audio interrupt): the main loop posts commands (`bw_bell(bell, strike)`, `bw_blip(note, vel)`, `bw_surf(level)`, `bw_attract_gain(g)`), and the render drains them at the start of each block, like FoMni's `om_cmd`. Voices: bell (~8, modal), blip (short FM / pulse), surf (noise + swell, gull chirps). One plate reverb (Dattorro). Cabinet crossfade = a gain ramp per source.
- **Drawing:** gfx.c has a canvas strip (up to 240×144), no full framebuffer. The scene redraws in two bands per frame, ~25 fps. First check whether a full 240×240 canvas fits in the shared RAM pool (115 KB of 336 KB) next to the reverb.
- **Saving:** one object (3.75 KB): format version, last cabinet, settings (volume, surf level), each game's best scores and progress.
- **Identity:** FM-1_4 went to Life; the next free digit is 2. Own USB name.

Still to design: the rest of Bells, the other games, the boardwalk scene, testing.

## Game ideas (2026-10-09)

Liked so far: **Bells**, a **pitch trainer**, and a **Piano Tiles cabinet** (below). Simon and Snake are out (not real games).

Other candidates:
- **Pitch** has two parts. *Perfect pitch*: a note plays, you hit the key; streaks and speed; levels widen the range, change the timbre, drop the reference note. *Tuning duel*: a second tone drifts out of tune, you bring it back with an encoder by ear (until the beating stops), and the drift speeds up.
- **Chord Code**: Mastermind with chords. A hidden 3–4-note chord plays; you guess on the keys; the LEDs say right note / right note, wrong octave. Deduction plus ear training.
- **Echo**: call-and-response rhythm mini-games over a groove (like Rhythm Heaven), scored in ms like Bells.
- **A puzzle**: Tetris / Lumines-like, where cleared lines play chords on the beat.

### Tiles (the rhythm cabinet)

**Why not Guitar Hero or Synthesia:** a 581 KB slot can't hold recorded backing tracks, so songs must be MIDI that the FM-1 plays itself.
Guitar Hero / Rock Band charts are timing maps over a recording (5-fret notes carry no pitch, and the band lives in the audio).
Synthesia-style falling notes use the real pitches, which is piano practice and too hard for a game.

**The model is Piano Tiles / Magic Tiles** (iOS / Android): four lanes, and a tile's lane has nothing to do with its pitch.
Each hit plays the *next* notes of the melody, so you feel like you're playing the song with four keys.

- **Input:** four lanes on four keys.
- **Sound:** a piano / bell voice for the melody; the backing from MIDI, maybe on Polly's GM engine (Sonivox EAS, 200 KB set). Check that it fits the slot next to the games, and the CPU with the screen drawing.
- **Hit and miss:** a hit sounds the melody notes that tile carries; a miss drops the lead out with a clunk (the Guitar Hero feel).
- **Difficulty:** how many notes each tile carries and how dense the tiles are. Easy has tiles only on strong beats, each sounding several notes; Hard has nearly a tile per note.
- **Charts:** generated automatically from the melody track. A hand-made chart track can override it, in Rock Band's MIDI lane convention (fixed note ranges per lane and difficulty), so it's authorable in any DAW.
- **Songs:** public-domain MIDI (folk, ragtime, classical) shipped with it; anything else loaded by the user, as with fm1-j2me's JAR.

Prior art to read (licences not checked yet; none runs on the device as is):
- [MIDI_Tiles_Reader](https://github.com/kaisubr/MIDI_Tiles_Reader): Android Piano Tiles that plays your own MIDI files. Closest to the idea.
- [Piano Tiles 2 song converter](https://gist.github.com/miodevs/1285be56b32b19cf83f3aee9b71ddb14): shows how songs become tiles.
- [Vega-77/piano-tiles](https://github.com/Vega-77/piano-tiles): web Piano Tiles with a backing track.
- [midi2clonehero](https://github.com/apvilkko/midi2clonehero), [ConvertHero](https://github.com/Dirtmigurt/ConvertHero): MIDI part → 5-lane chart, automatically (the reduction step).
- [Midi-Hero](https://github.com/jpmc3630/midi-hero): builds a drum rhythm game from MIDI files.
- For timing scores: StepMania (MIT) and OpenTaiko (MIT). For the format: YARG (LGPL-3.0) reads Rock Band / Clone Hero MIDI charts.
