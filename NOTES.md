# Boardwalk

An arcade firmware for the FM-1: a row of game cabinets on a seaside boardwalk. Private.
Brainstormed 2026-10-08 (session c0938ee9), dropped the same afternoon for Life, which became its own
firmware (`fm1-life`). Picked up again 2026-10-28 on fm1-base (MPL-2.0): Bells runs (listen and ring), the
boardwalk menu runs, Pitch and Tiles are cabinets with attract screens but no game yet.

## Status (2026-10-28)

- **Built on fm1-base** (submodule `base/`, at fm1-base `main` 4587fda), laid out as AMB-1 / CTL-1: `./build.sh`
  (`PYTHON=/opt/homebrew/bin/python3.14`, podman running), `sh tests/run_tests.sh`. Identity **FM-1_12**
  (dev FM-1_1200), USB "Boardwalk FM-1". No Bluetooth, no USB audio out.
- Eight cabinets: **Bells** (tower, handbells, conducting), **Pitch** (EAR and TUNE), **Tiles** (five songs),
  **Echo** (rhythm call and response), **Chord Code**, **Parrot** (inspired by Simon), **Moles** (whack-a-mole),
  **Ducks** (a shooting gallery), **Gull** (inspired by Flappy Bird: FLAP, SING, HOLD),
  **Claw**, **Striker**, **Balloons**, **Fishing**, **Fortune**, **Teacups**, **Bumpers**, **Coaster** (17 in all).
  Velocity and pressure (lib/touch) were tried and are out: neither steady enough on the FM-1 to play by. Each cabinet has a three-word blurb (game_t.blurb) under the boardwalk, and the one
  in front blinks PRESS PLAY. A design and copy pass (2026-10-28, one reviewer a game) is in the git log.
  Keys are read by the 1 kHz tick (exact times; Bells, Tiles and Echo answer in the tick itself); the black keys
  show what comes next (Bells: your place; Tiles: the next lane). The boardwalk plays each
  cabinet's attract tune quietly over the sea, fading as the row moves; K1 the sea, K2 the tunes. Gulls call.
- The row's order (2026-10-28): Striker, Moles, Balloons, Tiles, Gull, Ducks, Teacups, Fortune, Parrot, Bumpers,
  Echo, Claw, Pitch, Fishing, Chord Code, Coaster, Bells: two quick, obvious ones first, then the kinds taking turns
  (timing, reflex, ear, rhythm, a toy to rest on), the deeper ones further along, the bell tower at the pier's end.
  Scenarios walk to their cabinet by count (`turn SELECT n`), so a new order changes their counts.
- Screen reader: every screen on WHERE, knob values as they settle, answers at once; fm1-reader's audit is clean
  (`app/words.tsv`, `app/words.allow`, `app/web/words/`). The words link points at
  charlesvestal.github.io/fm1-boardwalk/words/, which does not exist yet (the repo is private).
- Browser emulator builds (`./build.sh --web`), tested in Node.
- Image ~244 KB; RAM ~36 of 96 KB; pool 206 of 336 KB (the plate ~139 KB, the canvas 69 KB).
- **Not yet on the hardware**: the render's CPU, the frame rate of full-screen redraws, how the bells, tones and
  gulls sound through the speaker, whether the bass is audible on it, and whether Tiles' timing feels right
  (input is polled between frames; key times in ms from plat_ms).
- Tests (14): the ringing core (ten plain courses true and at their lengths), the engine (every timbre, the
  attract group, the tones, a gull; `app/build/tests/rounds.wav` to listen to), scenarios (the boardwalk, Bells
  listening and striking, Pitch EAR and TUNE, Tiles played whole and half, the reader), the emulator, the audit.
  A test player (`answer`, `autotune`, `autotiles` in the scenarios) plays Pitch and Tiles.

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

## Design §1: Architecture (built 2026-10-28, on fm1-base)

```
app/
  app.h               the contract with fm1-base, the store (OBJ_SAVE at 0xFC000), save_t
  app.c               the shell: boardwalk <-> game, button routing, saving, lights; the unity build
  art.c               drawing in two 240 x 120 bands; golden hour's colours; the tower bell
  scene.c             the boardwalk: sky, sun, wheel, gulls, sea, bulbs, planks, the cabinets
  games/game.h        the game interface
  games/bells.c       Bells: Listen + Ring
  games/ringing.c/.h  pure C: place notation -> rows, the methods, the striking score (host-tested)
  dsp/bw.c, bw.h      the sound engine (audio interrupt, -O2)
tests/host/           ringing_test.c, bw_test.c
tests/scenarios/*.bw  the app in fm1-base's simulator
```

```c
typedef struct {
    const char *name;                     /* the marquee */
    uint16_t cab;                         /* the cabinet's colour */
    void (*enter)(void), (*leave)(void);  /* enter 0: a cabinet not open yet */
    void (*frame)(uint32_t ms);           /* logic and drawing, main loop, ~30 fps */
    void (*key)(int k, int down, uint32_t ms);
    void (*button)(int b, int down);
    void (*enc)(int role, int steps);
    void (*tick)(uint32_t ms);            /* 1 kHz timer interrupt (APP_TICK): Bells rings the tower's bells here */
    void (*attract)(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ms);
    void (*best)(char *buf);              /* "BEST 94%" */
    uint32_t (*keys_lit)(void);
} game_t;
```

The sound engine takes commands from the main loop and from the tick; the main loop's are posted with the tick
locked out (`snd_lock`), so the queue keeps one producer at a time.

- **Controls:** in the menu, OCT− / OCT+ (or turning SELECT) scroll the cabinets and PLAY enters a game; K1 the sea, K2 the attract tunes' level. In a game, HOME goes back to the boardwalk and everything else belongs to the game. MASTER is always volume. fm1-base's safety stays (REC held at power-on = update mode).
- **Sound engine `bw`** (audio interrupt): commands (`bw_bell(slot, note, vel)`, `bw_bell_pan`, `bw_blip(note, vel)`, `bw_surf(level)`, `bw_bells_damp`) drained at the start of each block. Bells: 8 slots, 7 partials each (hum, prime, tierce, quint, nominal, superquint, octave nominal) as decaying resonators. Blip: 4 FM plucks. Surf: noise with a 7 s swell. One Dattorro plate (AMB-1's). The attract tunes are their own group of notes with a level (`bw_attract`), faded out as the row moves and in again a quarter second after it stops. Notes come in six timbres (sine, pluck, piano, chime, bass, clunk); two sustained tones for Pitch's duel; a gull now and then while the sea is up.
- **Drawing:** two 240 × 120 bands per frame on gfx.c's canvas, ~30 fps (to check on the device).
- **Saving:** one object (`save_t`): format, the cabinet in front, the surf level, Bells' method / bell / pace and its best per method, room for the others. Saved 1.5 s after a change, never while bells ring (a flash write holds the sound).
- **Identity:** FM-1_12. Own USB name.

Bells as built: K1 method (ten: Plain Hunt, Plain Bob and Grandsire Doubles; Plain Hunt, Plain Bob, Kent TB and Cambridge S Minor; Plain Hunt, Plain Bob and Cambridge S Major), K2 your bell (LISTEN or 1..n), K3 pace (160–320 ms a blow). PLAY rings 4 rows of rounds, the plain course, 2 rows of rounds, or stands. Any key strikes your bell; a blow a gap late is a miss. The bells' notes: the top of a major scale down to the tenor.

## Pitch (built 2026-10-28)

- **EAR**: a note plays, you press its key; 3 lives, 8 s to answer, 5 right in a row goes up a level. Levels: 1 white
  keys C4–C5 with a C4 reference first (sine); 2 every key C4–C5 (sine, reference); 3 no reference (piano); 4 the
  whole keyboard (piano); 5 chimes; 6 any timbre. PLAY hears it again. Best: notes named in one game.
- **TUNE**: a reference tone and a second one that drifts (a random walk in its speed, faster as time goes on);
  K1 turns it 2 cents a detent, K2 half a cent. Within 4 cents counts as in tune; past 25 cents it is over. The
  screen shows two forks and the time in tune, never the error. Best: tenths of a second in tune.
- K1 the game, K2 EAR's starting level.

## Tiles (built 2026-10-28)

- Songs written out in `games/tiles.c` (public domain): Ode to Joy, Twinkle Twinkle, Frere Jacques, Greensleeves,
  Fur Elise (the opening), each with a simple bass line. MIDI loading is still to come.
- Lanes: the white keys in fours (F–B, C–F, G–C, D–G); any key in a lane counts. Tiles: HARD one per note, NORMAL at
  most one a beat, EASY one per two beats; a tile carries the notes until the next. Its lane follows the contour.
- Windows: 60 ms perfect, 150 ms good; a press with no tile near is a clunk and breaks the combo. The tile's first
  note sounds when hit (early too); its other notes at their times. The bass and a count-in from the tick.
- K1 song, K2 level, K3 tempo (60–140 %). Best: % of notes played, per song.

## Echo (built 2026-10-28)

- A groove (kick, hat) runs; a bar of woodblock plays a call, the next bar you answer on any key (a clap), then the
  next call, without stopping. Each of the call's hits takes your nearest free hit: within 40 ms whole, 90 ms 70 %,
  150 ms 35 %; an extra hit -50 %. 70 % passes; four passes go up a level (quarters, eighths, off the beat,
  sixteenths, anything); three misses and it is over. Calls written out in `games/echo.c` (six a level).
- K1 tempo (70-140 bpm), K2 the level to start at. Best: answers passed.

## Bells' other ways (built 2026-10-28)

- K4 MODE: TOWER (one bell), HANDBELLS (a pair: the left half of the keys the first bell, the right half the second;
  K2 the pair), CONDUCTING (Plain Bob only: K1 skips to Doubles, Minor, Major). Conducting: a target length is picked
  from the touches `ring_pb_touches` finds (every length up to 8 leads that some sequence of plain / bob / single
  brings round; Minor 3 5 6 8, Doubles 2 4 6 8, Major 5 7), with one way there kept as the hint. The left half of the
  keys calls BOB, the right half SINGLE (again: taken back); rows are worked out 3 ahead, so a call must come before
  then. Round at the length: a touch counted (`sav.touches`); round early, or not round by target + 2 leads: said,
  with the hint ("bob, bob, bob would do it").

## Chord Code, Parrot, Moles, Ducks (built 2026-10-28)

- **Chord Code**: a hidden chord (L1 major / minor root position C4-B4, L2 any triad any inversion, L3 sevenths);
  pick its notes, judged when you have picked as many: exact teal, right note other octave orange. 6 guesses, 5
  chords; score 7 - guesses each. PLAY hears it again.
- **Parrot** (inspired by Simon; the name is not ours to use): E A C# E on the four lanes, pads labelled with their
  keys; one more each round, a little faster; every five rounds the tones change key; a wrong pad or 4 s idle ends
  it, said why. K1 speed.
- **Moles** (whack-a-mole): moles on the 11 pentatonic white keys, each a step or two from the last, over a bass
  line (a good run is a tune), on a 112 bpm groove's eighths; gold = 3; a stray whack -1; faster as the 45 s run out.
  K1 level.
- **Ducks** (a shooting gallery): ducks on three rows quack their note as they come into view (by ear, again
  midway); press its key to shoot; a shot at nothing plays your note as a dull click and jams the gun 1 s (JAMMED!).
  L1-2 names on the ducks, L3-4 by ear; the keys never light. 20 ducks.

## Gull (built 2026-10-28)

- FLAP: any key flaps (gravity 520 px/s², a flap -185 px/s); each flap sings a C major pentatonic note set by the
  gull's height. SING: the held key is the height (F3 at the sea, G5 at the top; let go and it sinks 45 px/s); each
  gap is a white key's note, chimed as its posts come on; level 1 names it on the post, level 2 by ear. Posts 128 px
  apart, faster by 1.5 px/s a post; FLAP's gaps narrow as the score climbs. The keys never light.
- The save grew (sav.gull after rsv); a save from before it loads (offsetof(save_t, gull) bytes), the rest default.

## Claw (built 2026-10-28)

- The case sits over the keys (a column a key, key_x): a key moves the claw over it (a soft whirr at its pitch), PLAY
  drops. Toys each sing a note (hummed as the claw comes over one); the machine wishes for one (sparkle + its note,
  again every 6 s): 10 tickets for it, 3 for another; big toys are triads centred on their middle (third), x2.
  Grip: dead on 92 % (big 75), a key off 45 % (25); a slip drops it on the way up. 10 coins. K1 level: 1 notes shown
  (on the toys and the wish), 2 by ear, 3 chords. The key under the claw glows (where it is, never where to go).
- Knobs that choose a game's way are MODE everywhere (Pitch, Gull: was GAME; Bells' K4).

## Striker (built 2026-10-28)
- High Striker as a timing game: a meter swings up and down the board; any key strikes, the puck flies to where the
  meter was at that ms. The machine asks for a mark: on it 10, one off 6, two off 3; the bell asked and rung +5. Ten
  strikes. K1: 1 TWO MARKS (2.8 s swing), 2 ONE MARK (1.8 s), 3 UNEVEN (speed changes every half swing). Levels 1-2
  ask each target twice. Standing, a key is a practice strike. Best: points (sav.more[SV_STRIKER]).

## Balloons (built 2026-10-28)
- Hold a key to pump (the lit C4-C5 pad), let go to tie it off on the dashed ring: 10 / 6 / 3 / 1 within 1.5 / 3 / 5
  / 8 px; a hidden pop point 7-16 px past the ring (4-9 on L3), a wobble and squeaks as it nears. The ring's note
  plays first; the stretch tone reaches it at the ring. Size from the key's held time to the ms. K1: STEADY,
  SURGING, TIGHT. 10 balloons; best points (sav.more[SV_BALLOON]).

## Fishing (built 2026-10-28)
- The pond over the keys; each fish a note, humming as it passes under the float. A key casts; a fish nibbles, the
  float dips with its note: press that note's key within 0.9 / 0.7 / 0.45 s. Reel by tapping with the line's tug
  (from the tick); too fast tightens the line (SNAP), no tap lets it run; any knob reels slowly. 10 casts or 90 s;
  score the weight (sav.more[SV_FISH]). K1: NOTES SHOWN, BY EAR, FUSSY FISH.

## Fortune (built 2026-10-28)
- A toy: Barnaby Brass, a brass automaton seer (gear crown, turned-up moustache; an original character, no
  caricature), answers a question of up to 8 notes (PLAY): the ball swirls, then your phrase echoed, turned upside
  down and brought home, a bass under it; a card slides out: one of 32 fortunes by a hash of the notes (the same
  question, the same card) and a lucky note. K1 his voice (chime, piano, bells); best = fortunes told.

## Teacups (built 2026-10-28)
- A 60 s ride to a fairground waltz in C (from the tick). Any key on a beat to tap spins the cup (perfect ±40 ms,
  good ±90); off the beat it wobbles; a beat left out costs; drag 1.5 + 6 %/s. Top speed (85+): a countermelody, the
  bulbs and keys chase. Score = tenths at the top + the peak. Any knob spins it a little (to 70). K1: ONE A BAR, EVERY
  BEAT, FAST WALTZ. TC_LAG to set on the FM-1.

## Bumpers (built 2026-10-28)
- The dodgems from above: your car drives on; left white keys steer left, right ones right, sharper towards the
  ends; black keys honk; SELECT steers too. The other cars sing a chord's tones; a bump scores its place in a rising
  run, root-third-fifth within 2 s is CHORD +5; bumped from behind -1. 60 s. K1: SLOW, BUSY (one chases you), TRICKY
  (they swerve). Best: points (sav.more[SV_BUMPERS]).

## Coaster (built 2026-10-28)
- The track is the melody: rail height = pitch, each run's notes named on their markers; play each as the front car
  reaches it (±190 ms): right = speed for the climb ahead, wrong = brakes; a stop on a climb rolls back and ends it.
  K1: 1 C major scales (6 runs), 2 other keys and arpeggios (8), 3 longer runs, sevenths (10). Best: metres
  (sav.more[SV_COASTER]). The keys never light.

## More games still

From the list: **a puzzle** (Lumines-like, cleared lines play chords). New: none queued.

To set on the FM-1: Echo's EC_LAG and Tiles' TL_DRAW_AHEAD (key-to-sound and screen lag), the mix (each game's
loudest ~-26 dBFS in the simulator), whether a save on the boardwalk is heard.

Next: on the FM-1 (CPU, frame rate, the sound, Tiles' feel); then MIDI songs for Tiles (the user's own, as the
notes above plan), and Bells' handbells and conducting.

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
