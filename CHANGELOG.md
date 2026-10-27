# Changelog

A release is a push to `main` that changes `VERSION` to a version with no tag yet. CI then makes the
release build, tags the commit `vX.Y.Z` and publishes the GitHub Release with that version's notes,
taken from its section below. Add the section in the same pull request as the `VERSION` change.

## 0.0.33

- **No breath, no sound.** A phrase with no breath behind it is now silent, the way a wind patch whose
  level follows the breath is. A controller can send stray notes at its breath threshold with the breath
  near zero (a NuRAD's open C#, for one); those used to sound quietly, at full level through the closed
  filter. The level opens over the first sliver of breath (breath CC 0 to 10, whatever the Curve), and
  holds what it reached until the next phrase, so releases and a breath tapering off sound as before.
  Above that sliver nothing changes: breath still only opens the filter, and the panel keys play at
  full breath.
- The gate waits until the breath source has been heard. A keyboard with no breath, or a controller
  sending its breath on a CC that isn't Breath CC (the Setup page names the CC that's moving), plays as
  before.
- A setup that sounds at zero breath on purpose (breath to Amp at a negative amount) now starts silent.

## 0.0.32

From a player's report (a NuRAD sending breath on CC74, with Breath CC still on CC2):

- **When breath comes on another CC, the Setup page says so.** If your controller is streaming a CC
  that isn't Breath CC, or aftertouch, and Breath CC itself is quiet, the Breath row reads "CC74
  moving" (or "Press moving"). Turn Breath CC (the first knob) to it. The browser page shows the same
  under its meters. It counts only what the MIDI ch setting hears, and a stream (a knob nudged isn't one).
- **A MIDI note played over a held panel key gets the breath that came with it.** The keys still play at
  full breath, but only their own notes now. A stray note from a controller (the NuRAD's open C#, for one)
  used to sound at full breath while a key was down. It still takes the voice while it lasts (the last
  note wins), at the breath that came, so a controller's stray notes are quiet but not silent: breath
  opens the filter; it doesn't gate the sound.
- Your settings and presets come back as they were.

## 0.0.31

- **The speaker goes quiet while a computer records the FM-1 over USB.** A new page, Setup 2 (GLO
  pressed again), has one setting, Speaker. On Auto, the default, the FM-1's own output fades out while
  a computer records its USB audio, and fades back when the recording stops. The computer still gets
  everything. On keeps the speaker on. The page shows whether a computer is recording and whether the
  speaker is on.
- The headphone jack goes quiet with the speaker. One output feeds both, and nothing in the FM-1 turns
  the speaker off on its own: M-VAVE's own firmware has no control for it either.
- "Recording" means an app on the computer is taking the FM-1's USB audio. Some apps do that whenever
  the FM-1 is their input, even with nothing armed; Setup 2 shows it, and Speaker On keeps the sound.
- Your settings and presets come back as they were, with Speaker on Auto.

## 0.0.30

- **Breath CC and Glide CC move to Setup**, beside the MIDI channel: they say how your controller is
  wired, so they sit with the rest of the rig. The Breath page is now Curve, Attack and Release; the
  Pitch page Bend, Glide and Glide mode.
- **Transpose and Tune move to Pitch 2**, beside CC depth.
- Nothing else changes: your settings, presets and sound come back as they were.

## 0.0.29

The Filter, Noise, Breath, Pitch and LFO pages each draw their own picture in the middle of the screen:

- **Filter:** the filter's response, frequency across and level up, at Cutoff, Reso and Slope (it follows
  the note you play, as the filter does). While something moves the filter, breath or a Mod slot or the
  LFO, a second curve in the accent shows where it is now, with its frequency.
- **Noise:** the noise filter's response for its Type (low-pass, band-pass, high-pass or between), at Freq
  above the note (marked) and Reso, with the Level as bars under it.
- **Breath:** the Curve, with the breath coming in on it as you blow (and its value), and a plot of the
  last moment of breath, as it came in and as it comes out of Attack and Release.
- **Pitch:** the phrase, your pitch over the last few seconds as you play it, with each note's start
  marked and named, glides and bites in the line and your breath under it; beside it the note, the bite
  in semitones, and the glide time (in the accent while a glide moves).
- **Pitch 2:** the bite on a ruler of the Bend range, and the glide: how long the pitch takes to reach a
  new note at Glide alone, with all of CC depth added (dashed), and now, with what the glide CC adds.
- **LFO:** one cycle of the shape with where the LFO is in it now (as faded in, and in full), the fade,
  and the value of what it moves.
- **LFO 2:** the LFO over the last few seconds inside its fade, with a mark where a note started it again
  (Sync Note).

## 0.0.28

- **Factory presets.** The first 18 slots now hold factory presets, named on the screen as PRESETS turns
  to them. All of them play the same way as the init sound: breath to cutoff +75, the same attack and
  release, bend ±2, 4 ms of glide and no LFO. Only the tone changes.
  - **The basics** (1 to 5): Simple (the init sound), Simple Square, Simple Pulse, Simple Sub and
    Simple Triangle.
  - **Famous wind leads:** Truth (T-Square's Lyricon sound: two saws through a 12 dB filter, then
    delays and a hall), EWI Doubler (the late-80s EWI sound), and three of Michael Brecker's EWI
    sounds: Brecker Rays, Sentimental (after "In a Sentimental Mood") and Brecker Octave. Also Lyricon.
  - **One for each other engine:** FM Reed, DX Brass, DX CS-80, Formant Vox, CZ Lead, Air Pipe and
    Fold Lead.
- Saving over a factory preset makes the slot yours: it shows its number instead of a name. A slot you
  haven't saved over keeps following the factory preset, so retuned presets in later versions reach it.
- **From an earlier version:** presets you saved stay as they are. A slot that still held the init sound
  (never saved, or saved unchanged) now holds its factory preset. If you were playing such a slot, you
  come back to its factory preset rather than the init sound.

## 0.0.27

The FX pages show what each effect does to the sound, instead of the note and the bars:

- **The chain**, along the top of all five: DIST, MOD, DELAY and VERB in the order the sound goes
  through them, this page's lit, and a bar under each one whose Mix is up (full at 50, where the effect
  is at full level).
- **Distortion:** the Type's curve at this Drive, with a dot where the note you're playing lands on it,
  and a sine wave before and after it, Tone's darkening included.
- **Modulation:** how the left and right delay times sweep over the last few seconds (Phaser: its
  notches), moving while you play.
- **Delay:** the echoes laid out in time from Time and Feedback, the dry note first. Ping-pong goes left,
  right, left (Time past its longest, about 1.1 s, says "max"); Tape and Analog get darker each repeat. Each
  echo lights as it sounds.
- **Reverb:** the tail falling away in dB over a time scale from 50 ms to 80 s, so Decay moves where it
  reaches -60 dB from 0.2 s all the way to 60 s; it starts after the Pre-delay, and a dashed line shows
  the highs, which fall faster as Tone goes down. It shows the note feeding it while you play, then a marker runs
  down the tail.
- **Reverb 2:** the room, sized by Size and the Type, its walls moving with Mod; the pre-delay on the
  direct path, Shimmer's octaves climbing, and Low end as a bass line above or below flat.

An effect whose Mix is 0 draws in grey. The pictures come from the effects' own numbers, so they match
what you hear. The pictures only redraw when something on them changes, and moving parts step at most
30 times a second.

## 0.0.26

New screens for the Mod and Setup pages:

- **Mod 1-4: the routing, live.** The slot is drawn as a cable from its source to its destination: the
  source's level as you play it, the amount on the cable (thicker and brighter the harder you blow), and
  the destination's knob value with a mark where it is now, "40 → 67" (the "now" is every route
  together, not this slot alone). A destination that does nothing on the engine playing (Harmonics on
  FM, for one) is drawn greyed out, like an off slot. Under it, everything modulating the sound at the
  moment: the Mod slots in use, the LFO and the Filter page's Breath, each with its source's level, and
  which slots are off. With no note the LFO rests there, so the screen holds still.
- **Setup: a tuner and a controller monitor.** The note you're sounding, big, with a needle and how many
  cents off it is, with Transpose, Tune and the bite applied. Under it, what your controller is sending
  as it arrives: the breath CC (or "Press", aftertouch, if that's the breath source), pitch bend, the
  glide CC and the last note with its velocity, each with a dot that lights as a value comes in. It
  shows every MIDI channel and which channel each value came on, marked in orange when the MIDI ch
  setting isn't listening to it: a controller on the wrong channel shows up here. Changing the breath
  source or the glide CC clears that row until the new one sends.

## 0.0.25

- **Engine page: a scope.** The middle of the screen shows the engine's wave as you play: two periods of
  the note, locked to the pitch the voice plays, with the last two traces fading behind the newest. A
  steady wave holds still; one that moves by itself (VA's two detuned oscillators beating, a 6-op patch's
  envelopes) moves on the screen too. The note and the engine's tone control (its live value, after
  breath and the Mod slots) are named above it. Quiet, it shows a flat line and "Blow a note", and stops
  redrawing. It shows the voice before the effects and before MASTER: its size is the sound's, and it
  shows even with MASTER down.

## 0.0.24

- **Stage, on PLAY.** One screen for playing: the note and how far off it is, breath (with its recent
  peak), bite, glide and the output as live meters, over twelve knobs in three rows: Filter (Breath,
  Cutoff, Reso, Noise), the engine's own four, and the effects' mixes. KNOB 1-4 turn the lit row, SELECT
  moves the row, and the dials carry the same live ghosts as the knob bars. PLAY again (or EDIT) goes back to
  the page you came from; another page button goes to its page. SELECT goes round the rows, and the
  row is remembered.
- **The keys show your breath on Stage.** They light dim from the left as you blow, with the note you're
  playing lit on top.

## 0.0.23

- **Live ghosts on the knobs.** A knob that something is moving shows an orange tick at its live value,
  joined to where it's set: Cutoff (the Filter's Breath, Mod slots, the LFO), Reso, Morph, Timbre,
  Harmonics on the smooth engines, Aux, Noise Level, and Glide and CC depth while the glide CC adds to
  the glide. Knobs nothing moves look as before. While you turn a knob, its tick takes the text colour.
- **Glide CC: choosing another CC, or Off, lets go of the old one.** The glide used to stay as long as the
  old CC had last set it until the new one moved.

## 0.0.22

Fixes from a full code review (eight reviewers, each finding checked by two skeptics):

- **Delay: no more bursts of noise.** Switching the delay to Ping-pong with Time above about 1.1 s read
  outside the delay line for most of a second, and Times that are multiples of 17 ms read one sample past
  its end once a trip. Both played whatever memory sat beside the line, and fed it back into the echoes.
- **Distortion: no click when it comes on or goes off.** Its dry path started with 5 samples of silence
  and jumped back in time when it went off; now the dry slides to and from the distortion's latency.
- **LFO to Amp with Fade:** a note now starts at full level, and the tremolo fades in from there (it
  started 6 dB down).
- **Mod slots:** two Bite (or other bipolar) to Amp slots no longer multiply two negative gains back to
  full level at full bite down. Panel keys play at a fixed velocity (100), so Velocity to Amp no longer
  mutes them at power-on or follows the last MIDI note.
- **Screen:** small bites and Tune changes redraw the cents and the bite bar; notes below MIDI 0 show the
  right octave, and notes above 119 both digits; messages and the knob highlight no longer stick after
  49.7 days of uptime. The splash says "A windsynth for the FM-1".
- **Saving:** a pending edit is saved before an update over USB or entering update mode, and a failed
  autosave waits for the next quiet spell instead of erasing flash every frame.
- **Browser:** changing the MIDI input silences held notes on every channel, and a store saved by a newer
  build can no longer overwrite the emulator's memory.
- **Tests and CI:** the published page is now opened and played in Chrome in CI (it was skipped), the
  effects and maths run under the address and undefined-behaviour sanitizers, the host's restart forgets
  its cached presets as the device does, the Setup page's screen check no longer depends on the host's
  speed, and FiMba-1's old changelog sections can no longer be released as Windsynth's.

## 0.0.21

- **The web page says it isn't released yet.** It says it's for trying Windsynth in the browser, with
  the FM-1 firmware to follow once it has been tested on the real thing. The link to the source is
  gone (the repository is private), and the installer is folded away under "Have a test build?" until
  a release exists. Then it opens by itself and the notice goes.

## 0.0.20

- **The Engine page follows the engine.** Turning ALGORITHM on the Engine page now always redraws its
  knobs. Before, it could keep showing the previous engine's knob names, from Waveshape onward, whose
  knobs start at the same values as Formant's. The engine itself was changing; only the screen was stale.
- The breath, cutoff and output bars now redraw on every change that shows, not just larger ones.

## 0.0.19

- **Filter and Engine pages.** HOME's first page is now Filter: Cutoff, Resonance, Slope and Breath.
  The engine's own three knobs (Harmonics, Timbre and Morph, named for what they do on it) and Aux are
  together on the Engine page, whose header names the engine. The ALGORITHM knob picks the engine.
- **Breath to cutoff is a knob.** The Filter page's Breath sets how far breath opens the cutoff
  (+75 to start, −100 to +100), always connected, so all four Mod slots are free and start off. A sound
  saved earlier that sent breath to the cutoff from a Mod slot has that amount moved onto the knob and
  the slot turned off, so it plays as it did.

## 0.0.18

- **An LFO.** The LFO key now holds two LFO pages: Shape (Sine, Triangle, Saw, Square, S&H), Rate
  (0.05 to 20 Hz, 5 Hz to start), Dest and Amount, then Fade (how long it takes to come in after a note
  starts from silence) and Sync (Free, or Note: each new phrase restarts it). Its destinations are the
  Mod slots' plus Pitch, where Amount is in cents. Amount starts at 0, so it's off until you turn it up.
- **Harmonics and Aux are Mod destinations.** Harmonics moves on Formant, Waveshape, VA and Harmonic,
  where it changes smoothly. On Classic, FM, 6-op and PD it would jump in steps, so there it reads
  "Harm off" and does nothing.
- **Pages rearranged.** The Pitch pages moved to the SEQ key, where the Keys page was. Key breath and the
  Octave knob are gone: the panel keys play at full breath, and OCT- / OCT+ still move them.

## 0.0.17

- **Aux: each engine's second output.** Every Plaits engine but 6-op has a second output (AUX), and
  the Engine page's fourth knob now mixes it in: 0, the normal sound, to 100, AUX alone. On Classic it
  is the filter's high-pass; on FM, a sub-oscillator; on Formant, CZ-style resonant waves; on
  Waveshape, another folding curve; on VA, the synced-oscillator variant; on PD, the free-running
  version; on Harmonic, an organ-drawbar subset. 6-op's AUX is its main output, so it has no Aux knob.
  Sounds saved before load with Aux at 0.

## 0.0.16

- **A noise layer for any engine.** HOME's third page, Noise, mixes Plaits' noise in with the engine:
  Level (0, off, to start), Type (low-pass through band-pass to high-pass), Freq (the noise filter's
  pitch, in semitones from the note, +24 to start) and Reso. It goes in ahead of the main filter, so
  breath on cutoff opens it with the tone, and it plays and stops with the note. Noise is also a new
  Mod destination: breath to Noise blows it in harder, and a negative amount gives soft notes a breathy
  edge.

## 0.0.15

- **Every engine gets Classic's filter, exactly.** The other engines went through the same two filter
  stages as Classic, but clean. Classic drives its filter: it soft-clips into the first stage, between
  the stages and in the second, and eases off the drive as resonance rises. Ours now does all of that,
  so FM, Formant, Waveshape, VA, PD and Harmonic sound as warm through the filter as Classic does, and
  resonance growls rather than whistles. A test holds ours to Classic's, sample for sample.
- **The init sound has a touch of reverb**: Cloud at Mix 3, with Decay 3.5 s, Tone 60, Pre-delay
  20 ms, Size 50 and Mod 50. Presets never saved start from it too.

## 0.0.14

- **Distortion and Modulation**, ahead of the delay and the reverb, so FX runs Distortion, Modulation,
  Delay, Reverb (and Reverb 2), the order the sound goes through them. Distortion has Overdrive,
  Distort, Fuzz, Fold and Crush, with Drive and Tone; it runs at twice the rate so high notes don't
  alias, and keeps its level near the dry sound's. Modulation has Chorus, Ensemble, Flanger and Phaser,
  with Rate and Depth, and turns the sound stereo. Both start at Mix 0, so saved sounds are unchanged.

## 0.0.13

- **Two more Plaits engines**, after VA on ALGORITHM: **PD**, phase distortion as on Casio's CZ
  (Ratio, Amount, and Asym on Morph), and **Harmonic**, Plaits' additive engine (Bumps, Peak, and
  Shape on Morph). Breath on cutoff works on both as on the others.

## 0.0.12

- **Glide is a set time, and CC5 adds to it.** Glide on the Pitch page is now the glide time in
  milliseconds, 4 ms by default. The glide CC (CC5) adds to it, up to CC depth (400 ms) at full and
  nothing at 0, so the glide never gets shorter than Glide. The knob no longer moves with the CC.
  Saved sounds come back with Glide at 4 ms: before, it held wherever the CC last left it.
- **6-op fixed.** It played about 2.3 semitones flat and rough, because Plaits' 6-op needs every block
  the same length and got blocks of 12 and then 4. It's in tune and clean now. Its Env (Morph) at 25,
  Morph's default, now plays the patch's own envelope times; before, 25 made them four times faster.
- **Swarm is gone.** It didn't work on a windsynth. A sound saved on Swarm comes back on Classic.

## 0.0.11

- **A smoother reverb tail**, Cloud's above all. The tail used to bloom, then fall away in steps
  as its sound went round the tank, and at the end fade into a faint hiss. Now the input goes into
  the whole tank and the outputs are taken from all round it, so the tail falls evenly for as long
  as Decay says, and its lines keep floating-point detail, so it fades all the way out to silence.

## 0.0.10

- **Five more Plaits engines**, on ALGORITHM or the Engine page: **6-op** FM (Plaits' 96 DX7
  patches, picked by name, starting on BRASS 6 BC), **Formant** (granular formants, vocal),
  **Waveshape**, **VA** (two detuned oscillators) and **Swarm** (eight sawtooths). Each goes through
  the same filter, so breath on cutoff works as before.
- **Knobs named for the engine.** Ratio, Index and Morph take each engine's own names (6-op: Patch,
  Bright, Env; Waveshape: Shape, Fold, Asym, and so on). Classic's Engine page no longer shows Ratio
  and Index, which do nothing on it.
- 6-op starts its envelopes with each breath; slurred notes keep the gesture going.

## 0.0.9

- **Shimmer, rebuilt** toward Valhalla Shimmer and BigSky's. The whole tail now goes through a
  cleaner octave-up shifter (longer, smoother grains) and back into the reverb's input, so it blooms
  and climbs an octave at a time, high-passed so the low end stays clear. On Reverb 2, the Mod knob
  becomes **Shimmer** while the type is Shimmer: how much climbs (50 by default; near the top it
  sustains on its own).
- **CC depth starts at 400 ms** (it was 2 s) and is set in milliseconds, up to 2 s. A depth you set in
  0.0.8 comes back as the same time.

## 0.0.8

- **Glide CC depth.** Press LFO twice for Pitch 2. CC depth sets the glide time CC5 (or your glide
  CC) reaches at full, so the whole CC range can cover, say, no glide to 200 ms. It's 2 s by default,
  as before, and it's your rig's setting, so presets don't change it.

## 0.0.7

- **Reverb and delay**, on the new FX pages (press FX, then again for more). Both are stereo, the
  delay feeds the reverb, and both start at Mix 0, so the init sound stays dry. A preset keeps its
  effects.
- **Reverb**: five types on one tank. Cloud is the big, blooming ambient one, after BigSky's Cloud;
  Hall, Plate and Room are smaller; Shimmer is Cloud with an octave-up shimmer. Mix, Decay (0.2 s to
  60 s), Tone, then Pre-delay, Size, Mod and Low end on Reverb 2.
- **Delay**: Digital, Tape (wow, flutter, darker repeats), Analog (darker still) or Ping-pong (left
  at Time, right at twice it). Mix, Time (20 ms to 2 s) and Feedback; Time changes glide.
- A project saved by 0.0.5 now comes back on its preset. 0.0.6 opened those on P01.

## 0.0.6

- **A second engine: FM.** Plaits' 2-operator FM, with its sub-oscillator left out, through the same
  filter as Classic, so breath opens it the same way. Pick it on the new Engine page (press HOME
  twice) or with the ALGORITHM knob. Ratio sets the modulator (0.5 to 16 times the note), Index how
  much FM, and Morph on the Voice page is its feedback (none at 50). A preset keeps its engine.
- **A new Mod destination, Timbre:** the engine's own timbre, FM's index. Breath to Timbre makes FM
  brighten as you blow harder. Classic has no timbre of its own, so there it does nothing.

## 0.0.5

- **64 presets.** Turn PRESETS to pick one (P01 to P64; each starts as the init sound). Press SAVE,
  turn PRESETS if you want another slot, and press SAVE again to keep the sound there. The header shows
  the preset, with a `*` when you've changed it. A preset holds the sound; breath source and curve,
  the glide CC, the Keys page and Setup are your rig's and stay as they are.
- PRESETS no longer transposes: Transpose is on the Setup page.
- Holding SAVE now gives the init sound, and keeps the rig's settings (breath source and curve, glide
  CC, Keys) as well as Setup.

## 0.0.4

- **The init sound is one saw**, with breath on the cutoff and nothing else: Morph starts at 25
  (Classic's pure saw; at 30 it blended in a square), Resonance at 0, and Mod 2 to 4 are off (and
  start on Cutoff rather than showing Amp, Reso and Morph). Settings you've already saved keep what
  they had: Factory reset (or hold SAVE) to hear the new start.
- The browser page's Breath slider is gone: play it from your windsynth.

## 0.0.3

- **It plays.** Blow your windsynth into it (USB or the MIDI jack, any channel by default): breath on
  CC2 opens the filter, the bite bends ±2 semitones, CC5 sets the glide time. Overlapping notes play
  legato, last note wins. Breath can come from aftertouch or any CC instead, with a curve.
- **Four Mod slots** send breath, bite, the mod wheel, pressure or velocity to cutoff, resonance, amp
  or morph. Mod 1 is Breath → Cutoff.
- **New pages and screen:** Voice, Breath, Pitch, Mod 1–4, Keys and Setup; the screen shows the note,
  the bite and bars for breath, cutoff and output. The FM-1's keys play too.
- **The kalimba is gone**, and a saved FiMba project no longer loads (you start from the defaults).
- **In the browser:** pick which MIDI input plays, drag a Breath slider if you have no windsynth at
  hand, and watch what the instrument hears and does under the panel.

## 0.0.2

- **Plaits is inside.** The wind voice is built: Plaits' "Classic" engine (VA-VCF) with its filter,
  a gate envelope, VCA and soft clip, in tune at the FM-1's sample rate. It renders exactly as
  upstream Plaits does, in the browser too, and the device build links it. You can't play it yet:
  the next step (M2) connects it to your windsynth's breath, bite and keys.

## 0.0.1

- **FM-1 Windsynth starts here**, as a fork of FiMba-1 0.6.0. It still plays FiMba's kalimba while
  the wind voice is built; see `docs/PLAN.md`. The USB name is now "Windsynth FM-1".
- Versions `0.0.x` are development builds: nothing is released until `VERSION` moves past them.

The history below is FiMba-1's.

## FiMba-1 0.6.0

- **Reset to the defaults:** hold SAVE for 1.5 s, then press SAVE again to confirm (anything else, or
  waiting 5 s, keeps everything). Every sound setting goes back to its default; the Setup page (Tune,
  MIDI, Theme) stays as it is.
- **MIDI out is off by default.** Turn it on on the Setup page (GLO, KNOB 3). Settings you've already
  saved keep what they had.
- **Wah needs a sound hole:** on the Board and None bodies, Wah and Wah rate are greyed out and don't
  turn ("NO HOLE: BOX OR GOURD").
- **The middle of the kalimba glows:** the key under the lowest tine (white key 9 in Tine, 8 in
  Mirror; in Keyboard, the key that plays middle C) is dimly lit, and lights fully while it rings.

## FiMba-1 0.5.2

- **Wah is how much wah.** With Wah rate on, the hand now flutters from open to the Wah setting and
  back, so Wah 100 is the full sweep and low settings a gentle one. In 0.5.1 it was the other way
  round: the flutter filled what Wah left open, so Wah 1 swept the whole way and Wah 100 didn't move.
- The fastest Wah rates (up to ~8 Hz) now sweep fully instead of being smoothed away.

## FiMba-1 0.5.1

- **The wah is now heard.** Covering the sound hole (ENV → Body: Wah, the mod wheel, pressure, the Hole
  key, or Wah rate) muffles the sound like a vowel closing: bright with the hole open, dark and a little
  quieter covered, with a vowel-like peak sweeping through the tines on the way. Before, it only moved a
  resonance below the lowest tine, and barely changed the sound. Box and Gourd have a hole; Board and
  None don't.
- The hand glides over the hole in about 20 ms, so the Hole key covers and uncovers it without a click.
- **Wah rate**'s lowest setting reads **Off** (it was "Hand"): the hole moves only when you move it.

## FiMba-1 0.5.0

- **Theme**, a new knob on the Setup page (GLO, KNOB 4): **Light**, the pale wood and dark ink as
  before, or **Dark**, near-black with cream and amber. In Dark the knob bars and the chips turn cream
  and brown, so amber only means a tine sounding or a knob touched. The tines keep their metal in both.
  The screen changes at once, and the theme is saved with your settings.
- The Setup page's header shows the firmware's version, beside the CPU load.
- Saved settings carry over; Theme starts on Light.

## FiMba-1 0.4.1

- **Tine is now left-first**, as on a 17-key kalimba in C. C4 sits on white key 9, the next note up
  (D4) is on its left, and the scale alternates outward: D6 on the far left, C6 on the far right
  ([#1](https://github.com/jadamsowers/fm1-fimba/issues/1)).
- **Mirror**, a new Layout value (Keys page, KNOB 1), keeps the old right-first arrangement: C4 on
  key 8, D4 on its right.
- Saved settings carry over. A project saved on Tine plays the new way; pick Mirror for the old one.

## FiMba-1 0.4.0

- **16 voices** (up from 12): fewer tines cut short when long-ringing steel tines and patterns overlap.
  Upper overtones that have died away are skipped, so ordinary playing costs less.
- **Worn**: an old, played-in kalimba. Each tine gets its own quirks: a little out of tune (up to ±12
  cents), a longer or shorter sustain, a faster or slower beat, uneven brightness and level.
- **Pages by button**: each page button owns its pages; press it again for the next (dots in the
  header show which). HOME: Tine → **Character** (Worn, Glide, Tuning, Release). ENV: Body.
  FX: Space → Color. LFO: Grain → Spread. SEL: Keys → Play. SEQ: Pattern. GLO: Setup.
- **ARP**: tap to start or stop the pattern; **hold** to open its settings.
- Settings saved by earlier versions carry over; new settings start at their defaults.

## FiMba-1 0.3.0

- The web page installs FiMba-1 on the FM-1 over USB, and offers the highest release.
- It runs on the FM-1: the first install on real hardware boots and plays.

## FiMba-1 0.2.0

- CI builds the firmware package and checks it against FoMni's; a version tag makes a release.

## FiMba-1 0.1.0

- The first release.
