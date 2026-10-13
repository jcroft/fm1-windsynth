# Changelog

A release is a push to `main` that changes `VERSION` to a version with no tag yet. CI then makes the
release build, tags the commit `vX.Y.Z` and publishes the GitHub Release with that version's notes,
taken from its section below. Add the section in the same pull request as the `VERSION` change.

## 0.0.21

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

## 0.6.0

- **Reset to the defaults:** hold SAVE for 1.5 s, then press SAVE again to confirm (anything else, or
  waiting 5 s, keeps everything). Every sound setting goes back to its default; the Setup page (Tune,
  MIDI, Theme) stays as it is.
- **MIDI out is off by default.** Turn it on on the Setup page (GLO, KNOB 3). Settings you've already
  saved keep what they had.
- **Wah needs a sound hole:** on the Board and None bodies, Wah and Wah rate are greyed out and don't
  turn ("NO HOLE: BOX OR GOURD").
- **The middle of the kalimba glows:** the key under the lowest tine (white key 9 in Tine, 8 in
  Mirror; in Keyboard, the key that plays middle C) is dimly lit, and lights fully while it rings.

## 0.5.2

- **Wah is how much wah.** With Wah rate on, the hand now flutters from open to the Wah setting and
  back, so Wah 100 is the full sweep and low settings a gentle one. In 0.5.1 it was the other way
  round: the flutter filled what Wah left open, so Wah 1 swept the whole way and Wah 100 didn't move.
- The fastest Wah rates (up to ~8 Hz) now sweep fully instead of being smoothed away.

## 0.5.1

- **The wah is now heard.** Covering the sound hole (ENV → Body: Wah, the mod wheel, pressure, the Hole
  key, or Wah rate) muffles the sound like a vowel closing: bright with the hole open, dark and a little
  quieter covered, with a vowel-like peak sweeping through the tines on the way. Before, it only moved a
  resonance below the lowest tine, and barely changed the sound. Box and Gourd have a hole; Board and
  None don't.
- The hand glides over the hole in about 20 ms, so the Hole key covers and uncovers it without a click.
- **Wah rate**'s lowest setting reads **Off** (it was "Hand"): the hole moves only when you move it.

## 0.5.0

- **Theme**, a new knob on the Setup page (GLO, KNOB 4): **Light**, the pale wood and dark ink as
  before, or **Dark**, near-black with cream and amber. In Dark the knob bars and the chips turn cream
  and brown, so amber only means a tine sounding or a knob touched. The tines keep their metal in both.
  The screen changes at once, and the theme is saved with your settings.
- The Setup page's header shows the firmware's version, beside the CPU load.
- Saved settings carry over; Theme starts on Light.

## 0.4.1

- **Tine is now left-first**, as on a 17-key kalimba in C. C4 sits on white key 9, the next note up
  (D4) is on its left, and the scale alternates outward: D6 on the far left, C6 on the far right
  ([#1](https://github.com/jadamsowers/fm1-fimba/issues/1)).
- **Mirror**, a new Layout value (Keys page, KNOB 1), keeps the old right-first arrangement: C4 on
  key 8, D4 on its right.
- Saved settings carry over. A project saved on Tine plays the new way; pick Mirror for the old one.

## 0.4.0

- **16 voices** (up from 12): fewer tines cut short when long-ringing steel tines and patterns overlap.
  Upper overtones that have died away are skipped, so ordinary playing costs less.
- **Worn**: an old, played-in kalimba. Each tine gets its own quirks: a little out of tune (up to ±12
  cents), a longer or shorter sustain, a faster or slower beat, uneven brightness and level.
- **Pages by button**: each page button owns its pages; press it again for the next (dots in the
  header show which). HOME: Tine → **Character** (Worn, Glide, Tuning, Release). ENV: Body.
  FX: Space → Color. LFO: Grain → Spread. SEL: Keys → Play. SEQ: Pattern. GLO: Setup.
- **ARP**: tap to start or stop the pattern; **hold** to open its settings.
- Settings saved by earlier versions carry over; new settings start at their defaults.

## 0.3.0

- The web page installs FiMba-1 on the FM-1 over USB, and offers the highest release.
- It runs on the FM-1: the first install on real hardware boots and plays.

## 0.2.0

- CI builds the firmware package and checks it against FoMni's; a version tag makes a release.

## 0.1.0

- The first release.
