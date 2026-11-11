# FM-1 Windsynth

Custom firmware that turns the **M-VAVE FM-1** into a synthesizer for **any MIDI wind controller**
(EWI, NuRAD, Aerophone, Sylphyo and the rest), with voices taken from Mutable Instruments Plaits.
It runs in the browser first, as the firmware's own C code compiled to WebAssembly, and then on the
FM-1 itself.

> **Status: 0.1.0, the first release.** It plays: notes, breath, bite and glide from any windsynth
> drive a Plaits voice (eight engines), with 18 factory presets in 64 slots, a distortion, a modulation
> effect (chorus, ensemble, flanger, phaser), a delay and a reverb, in the browser and on the FM-1. It's
> tested on real FM-1 hardware for playability, performance and stability. Still to come: better
> presets, more testing of each synth engine, and improvements to the effects. Play it and install it
> at <https://jcroft.github.io/fm1-windsynth/>.

![Full breath on A3](docs/img/wind_voice.png) ![The Breath page](docs/img/wind_breath.png) ![Mod 1: breath to cutoff](docs/img/wind_mod.png)

## Playing

Plug in your windsynth (USB, or the FM-1's MIDI jack) and blow. It listens on every channel by
default. Last note wins: overlapping notes play legato, without a new attack, and letting one go
returns to the one still held. The FM-1's own keys play too, every key its printed note from F3, at
full breath (they're switches, so they sense no pressure). **OCT−** / **OCT+** shift them.

| Control | Default | Options |
| --- | --- | --- |
| Breath | CC2, to filter cutoff (the Filter page's Breath knob, +75); a phrase with no breath is silent (the level opens over the first sliver of breath, CC 0 to 10, once the breath source has been heard) | Aftertouch or any CC as the source; a curve; how much it opens the cutoff (Breath, −100 to +100); the Mod slots also send it to amp, resonance, morph and the rest |
| Bite | Pitch bend, ±2 semitones | 0 to 12 semitones |
| Glide | 4 ms; CC5 adds up to 400 ms to it, and at 0 adds nothing | The glide time itself (up to 2 s, or off); another CC, or none; how much the CC adds at full (CC depth, up to 2 s); glide on every note or legato only |

The screen shows the note (with its offset in cents while you bend), the bite, and bars for breath,
cutoff and output.

## Pages

A page button opens its page, and pressed again its next one; the four knobs set its four values. **EDIT** and the **SELECT** knob
step through every page.

| Button | Page | Knobs |
| --- | --- | --- |
| HOME | Filter | Cutoff, Resonance, Slope (12 or 24 dB), Breath (how far breath opens the cutoff, −100 to +100, +75 to start) |
| HOME again | Engine (the header names it) | The engine's own three knobs, named for what they do on it: Harmonics and Timbre (Classic has neither), Morph (Classic: the waveform, a saw up to 25, a square at 50, then a pulse that narrows and widens again, with a sub-octave below 20 and above 80; FM: the feedback, none at 50); then Aux: from the engine's main output (0) to its second output alone (100; not on 6-op, whose second output is the same). The ALGORITHM knob picks the engine. See the engines below. |
| HOME again | Noise | Level (0 is off), Type (low-pass through band-pass to high-pass), Freq (the noise filter's pitch, in semitones from the note), Reso |
| HOME again | Sample | Slot (Off, or one of the 32 samples; see Samples below), Level, Engine (the engine's own level: 0 plays the sample alone), Tune (±24 semitones). The page draws the sample, its loop, Start and where it plays |
| HOME again | Sample 2 | Start (where in the sample it starts), Loop (Off: once through; Loop; Ping-pong), Trigger (Phrase: it starts again with each phrase from silence; Note: with each new note too), Track (On: at the note you play; Off: at its own pitch) |
| ENV | Breath | Curve, Attack, Release |
| LFO | LFO | Shape (Sine, Triangle, Saw, Square, S&H), Rate (0.05 to 20 Hz, 5 Hz by default), Dest (the Mod slots' destinations, plus Pitch), Amount (−100 to +100; on Pitch, in cents; 0 by default, so it's off) |
| LFO again | LFO 2 | Fade (how long it takes to come in after a note starts from silence), Sync (Free, or Note: each new phrase restarts it) |
| SEL | Mod 1–4 (press again for the next) | Source (breath, bite, mod wheel, pressure, velocity), Destination (cutoff, resonance, amp, morph, timbre: the Engine page's third knob, FM's Index; noise: the noise layer's level; harmonics: the Engine page's second knob, on Formant, Waveshape, VA and Harmonic only, "Harm off" elsewhere; aux: the Aux mix; sample: the sample layer's level), Amount (−100 to +100) |
| SEQ | Pitch | Bend range, Glide (the glide time with the glide CC at 0: 4 ms by default, up to 2 s), Glide mode (always or legato) |
| SEQ again | Pitch 2 | CC depth: how much the glide CC adds to Glide at full (400 ms by default, up to 2 s), Transpose, Tune |
| GLO | Setup | Breath CC (CC2 by default, another CC, or aftertouch), Glide CC (CC5 by default, another CC, or off), MIDI channel (Omni by default), Theme (Light or Dark); the version. When your controller's breath arrives on another CC, the Breath row says which ("CC74 moving") |
| GLO again | Setup 2 | Speaker: Auto (the default) silences the FM-1's speaker while a computer records it over USB audio; On keeps it on. The headphone jack goes quiet with it: one output feeds both, and the FM-1 has no switch for the speaker alone. The page shows whether a computer is recording and whether the speaker is on |
| FX | Distortion | Type (Overdrive, Distort, Fuzz, Fold, Crush), Mix, Drive, Tone |
| FX again | Modulation | Type (Chorus, Ensemble, Flanger, Phaser), Mix, Rate (0.05 Hz to 10 Hz), Depth |
| FX again | Delay | Mode (Digital, Tape, Analog, Ping-pong), Mix, Time (20 ms to 2 s; Ping-pong to 1.1 s), Feedback |
| FX again | Reverb | Type (Cloud, Hall, Plate, Room, Shimmer), Mix, Decay (0.2 s to 60 s), Tone |
| FX again | Reverb 2 | Pre-delay (up to 500 ms), Size, Mod (how much the reverb moves; on Shimmer, Shimmer: how much of the tail climbs), Low end (the bass's decay, shorter or longer) |

The engines are Plaits' own, played by breath. Classic has its own filter, and every other engine goes
through an exact copy of it, drive and saturation included, so breath on cutoff works and sounds the
same way on all of them. A noise layer can go with any of them: Plaits' noise through its own
filter, which sweeps from low-pass to high-pass and follows the note, mixed in ahead of the main
filter, so breath on cutoff opens the air with the tone. Send breath to Noise on a Mod page to blow
it in harder, or a negative amount for a breathy edge on soft notes. Their knobs take the
engine's names.

| Engine | Engine page: Harmonics, Timbre | Engine page: Morph |
|---|---|---|
| Classic | (none) | the waveform |
| FM | Ratio (the modulator, 0.5 to 16 times the note), Index | Feedback |
| 6-op | Patch (96 DX7 patches, by name; BRASS 6 BC to start), Bright (the modulators' level) | Env (the envelopes' speed; 25, Morph's default, as the patch has it) |
| Formant | Ratio (between the two formants), Formant | Width |
| Waveshape | Shape (the shaper's curve), Fold | Asym (asymmetry) |
| VA | Detune (between the two oscillators), Square (pulse width, then sync) | Saw (triangle to saw) |
| PD (phase distortion) | Ratio (the distorting modulator's, to the note), Amount | Asym (asymmetry) |
| Harmonic (additive) | Bumps (how many peaks in the spectrum), Peak (the loudest harmonic) | Shape (wide and flat to narrow and peaked) |

6-op starts its envelopes at the start of each breath, not each note, so a slurred phrase stays one
gesture.

**OCT−** / **OCT+** move the panel's keys by octaves.

## Samples

A sample layer can go with any engine, as the noise does: one sample at a time, at the note you play,
looped, mixed in ahead of the filter. Its Level is a Mod destination, so breath can blow it in, and
the Engine knob turns the engine down, to 0 for the sample on its own (a sax, say). The FM-1 holds
32 samples, about 244 KB in all: 5.6 s of 16-bit sound at 22 kHz, or twice that as 8-bit mu-law.
One sample, with loops; no key zones.

To get them there, use the **Samples** section of the web page: add sound files (WAV, MP3, FLAC and
what else your browser plays), and it finds each one's pitch and a loop for it (a WAV's own root and loop, if it has them),
which you can change. They play in the browser version at once; **Send to FM-1** loads them over the
USB cable (Chrome or Edge), and **Read from FM-1** gets them back. From a computer's command line,
`tools/fm1_samples.py` does the same (`build`, `send`, `backup`, `info`). A preset keeps its slot
number, not the sample, so a preset whose slot is empty plays without it. Loading stops the sound
for a moment while the flash is written.

## Effects

The voice goes through four effects in order: Distortion, Modulation, Delay, Reverb. Modulation turns
it stereo, and the delay and the reverb are stereo out. Mix works the same on each: at 0 the effect is
off, at 50 you hear the dry sound and the effect at full level, and at 100 only the effect. All start
at 0 except the reverb, which the init sound has a touch of (Cloud at Mix 3, 3.5 s), and a preset
keeps its effects.

The distortion has five types. **Overdrive** is a soft, slightly asymmetric clip, **Distort** a hard
one, **Fuzz** hard on one side and soft on the other with more gain, **Fold** a wavefolder, and
**Crush** fewer bits and a lower sample rate. Drive sets how hard, and Tone is a low-pass after it.
The level stays near the dry sound's as Drive goes up, and all but Crush run at twice the rate, so
high notes don't alias. **Chorus** on Modulation is two moving taps, left and right; **Ensemble**
three, with a fast vibrato, like a string machine's; **Flanger** a short swept delay fed back; and
**Phaser** six swept allpasses fed back. Both sides move differently, which makes the stereo.

The reverb is one tank with five voicings. **Cloud** is the big ambient one, after Strymon's BigSky
Cloud: a long chain of moving diffusers in front of a large loop, so notes bloom rather than echo.
**Hall**, **Plate** and **Room** are smaller and plainer. **Shimmer** is Cloud whose tail goes through
an octave-up pitch shifter and back into its input, so it climbs an octave at a time, as Valhalla's
and BigSky's shimmers do; on Reverb 2 its Mod knob becomes Shimmer, how much climbs. Decay shows the reverb time. The delay's **Tape** mode wobbles and darkens
its repeats, **Analog** is darker still, and **Ping-pong** echoes left at Time and right at twice it.
Time changes glide, like a tape echo. Feedback at 100 repeats for ever, saturating instead of
growing.

## Presets

There are 64 presets, P01 to P64. The first 18 are factory presets, named on the screen as you turn
to them, and the rest start as the init sound. Turn **PRESETS** to play another; the header shows which, with a `*` once you've changed its sound. To keep a sound, press
**SAVE**: the header asks "SAVE TO P12". Turn **PRESETS** to pick another slot if you like, then
press **SAVE** again (any other button leaves the presets alone). What you're playing, edits
included, is also kept by itself a few seconds after you stop, so it comes back after a restart.

A preset holds the sound: the Filter, Engine, Noise, Sample, LFO, Pitch and Mod pages, the effects, and Attack and
Release. Your rig's settings stay put when you change presets: the breath curve, Pitch 2 (the glide CC's
depth, Transpose and Tune), the keys' octave and Setup (the breath and glide CCs, MIDI ch and Theme; the Speaker).
Hold **SAVE** for 1.5 s, then press it again, for the init sound (the preset slot is untouched until
you save).

## Building and testing

See [BUILDING.md](BUILDING.md). In short: `python3 tools/build.py` builds the FM-1 package,
`web/emu/build.sh` builds the browser version, and `tests/run_tests.sh` runs everything.

## Credits

FM-1 Windsynth is a fork of [FiMba-1](https://github.com/jadamsowers/fm1-fimba) by Adam Sowers,
which grew from [FoMni](https://github.com/charlesvestal/fm1-fomni) and
[X0X](https://github.com/charlesvestal/fm1-x0x) by Charles Vestal, both built on
[Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton Instruments). The voices
come from [Plaits](https://github.com/pichenettes/eurorack) by Émilie Gillet (MIT).

Copyright (C) 2026 Jeff Croft and the authors of the projects above. Licensing: GPL-3.0-only, with the
instrument code (`firmware/src/wind/`) also under MPL-2.0 so other firmware can reuse it; see
[LICENSING.md](LICENSING.md). Contributions are welcome; see [CONTRIBUTING.md](CONTRIBUTING.md).
Not affiliated with M-VAVE.
