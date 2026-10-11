# FM-1 Windsynth: Engineering Plan

2026-10-11 · Live, editable version: https://claude.ai/code/artifact/aca75da1-b485-4f2a-8c45-d8e446879030

## Goal and scope

We build a monophonic, Plaits-based windsynth as FM-1 firmware and develop it in the browser first. The browser build runs the same C/C++ code that will later be installed on the device. You play it from your windsynth over Web MIDI on day one, before the FM-1 arrives.

**In v1**

- One mono voice with last-note priority and glide.
- Two engines: Plaits' VA-VCF (classic waveshapes into a filter) first, then Plaits' VA with our own post-filter.
- Breath control: CC2 by default, routed to filter cutoff, with a 4-slot mod matrix that can also reach amp, resonance and the Plaits macros. Aftertouch and other CCs can stand in for breath.
- Bite as pitch bend: 14-bit, ±2 semitones by default, adjustable.
- Glide time on CC5 by default, assignable to any CC.
- Presets saved to the simulated flash, plus a minimal set of on-device screen pages.
- The browser simulator page with Web MIDI, deployed from CI.
- A device package (`.fwsc`) built in CI from day one, so the hardware path never rots.

**Not in v1:** the custom UI (later, built on the simulator's JS API), engines beyond the first two, polyphony, a sequencer, Bluetooth MIDI and effects beyond what the base fork already has.

## Architecture

We fork **FiMba-1** and swap its kalimba for a wind voice. FiMba is the leanest single-instrument firmware on the Felucca platform. It already has the platform seam (`plat.h`), a small app layer (`ui`, `project`, `panel`), a one-file DSP (`dsp/kalimba.c`), safe boot, a browser emulator and CI that builds and checks the device package. Everything is GPL-3.0.

_Diagram: control and audio flow, three new modules on the reused platform (see the live doc)._

The same three modules run in the browser and on the FM-1; only the bottom band and the audio output differ between the two builds.

**Where each piece comes from**

| Piece | Source | Change |
| --- | --- | --- |
| HAL: keys, encoders, LEDs, screen, I2S audio, flash, watchdog | Felucca via FiMba (`firmware/hal/`) | none |
| USB-MIDI + audio, TRS MIDI in, OTA loader, storage | Felucca via FiMba (`usb.c`, `midi_uart.c`, `ota.c`, `storage.c`) | none |
| App skeleton, safe boot, `.fwsc` region check, web installer | FiMba (`app/`, `tools/check_fwsc.py`, `web/`) | UI pages rewritten for the wind voice |
| Browser emulator (Emscripten + AudioWorklet + Web MIDI) | FiMba `web/emu/`, itself from X0X/Felucca | builds C++ too; adds a MIDI device picker and a dev overlay |
| Float maths, CPU performance page, second core if needed | X0X (`dsp/fastmath.h`, perf page, `hal/fm1_cpu1.h`) | copied as needed |
| Synth engines | Plaits + stmlib, vendored unmodified and pinned by hash | none: wrappers live in our code |
| Engine wrapper patterns, resampler | Lunar Modulator (MIT), as reference | reimplemented small |

**Repository layout (new code in bold)**

- `firmware/hal/`, `firmware/src/*.c`: platform, untouched.
- `firmware/src/app/`: `plat.h` seam, `main_fm1.c`, `ui.c`, `project.c` (presets and settings), `panel.c`.
- **`firmware/src/wind/wind_ctl.c`**: MIDI to control state (note stack, breath, bend, glide), conditioning and curves.
- **`firmware/src/wind/wind_matrix.c`**: the 4-slot mod matrix, sources to destinations.
- **`firmware/src/wind/wind_voice.cc` + `wind_voice.h`**: a C API over the Plaits engines, plus pitch, glide, VCA and post-filter.
- **`third_party/plaits/`, `third_party/stmlib/`**: vendored subset with a hash manifest.
- `host/`: host simulator plus a **`wind-render`** CLI (MIDI file in, WAV out).
- `web/emu/`: browser build.

**The C seam between app and voice** keeps C++ inside one file:

```c
typedef struct {            /* control state, per 64-frame block */
    float note;             /* MIDI note + bend + glide, in semitones */
    float gate;             /* 0 or 1 */
    float cutoff, res, amp; /* 0..1 after the matrix */
    float timbre, morph, harmonics;
} wind_frame_t;

void wv_init(void);
void wv_engine(uint32_t id);
void wv_render(const wind_frame_t *f, int32_t *out_lr, uint32_t frames);
```

**Threading:** v1 renders on core 0 inside the audio interrupt, as FiMba does. MIDI is parsed once per 64-frame block (1.45 ms). We move the render to core 1 with X0X's code only if profiling on the device asks for it.

## Voice design

One mono voice: a Plaits engine into a VCA, with pitch built from note, bend and glide. Breath shapes it through the matrix.

**Signal chain:** note stack → pitch (note + bend + glide + transpose + tune) → Plaits engine (float, 12-sample blocks) → post-filter (only for engines without their own) → VCA → DC blocker → soft clip → master gain (MASTER pot) → 24-bit I2S, the same signal on L and R.

**Engines in v1**

| Id | Engine | Filter | How Cutoff / Resonance reach it |
| --- | --- | --- | --- |
| 0 | Plaits VA-VCF (`engine2/virtual_analog_vcf_engine`) | its own 2-stage SVF, key-tracked | Cutoff → TIMBRE. Resonance → HARMONICS, on the half set by a 12/24 dB **Slope** setting: 12 dB uses HARMONICS 0.625→1.0 (resonance 0→max), 24 dB uses 0.375→0.0. This follows the engine's own maths |
| 1 | Plaits VA (`engine/virtual_analog_engine`) | none in the engine | our post-filter: one `stmlib::Svf` low-pass, the same class VA-VCF uses, key-tracked the same way |

MORPH and HARMONICS (and TIMBRE on engine 1) are knobs and matrix destinations. Each engine keeps Plaits' own meaning for them.

**Gate and amplitude.** Windsynths send note-on when breath crosses their threshold and note-off when it stops. The voice keeps a short gate envelope (5 ms attack, 60 ms release, both settings) as the VCA's base. Your default routes breath only to cutoff, so dynamics come from the filter and the gate gives on/off. A Breath → Amp slot multiplies the VCA by breath for full breath dynamics; I'd ship a preset with both routes.

**Pitch.** Note stack of 10 with last-note priority: releasing a note falls back to the previous held note without retriggering. Bend adds ±range semitones. Glide is constant-time and linear in semitones, from the current pitch to the new note. By default it applies only when a note arrives while another is still sounding (legato), the usual windsynth convention; a setting makes it always on.

**Sample rate.** v1 runs Plaits at the FM-1's 44,117.6 Hz with a pitch offset of +1.414 semitones (12 · log2(47,872.34 / 44,117.6)) to correct its tuning tables. Breath is the envelope in these two engines, so the 8.5 % longer internal time constants don't matter. We add Lunar's resampler approach when we add engines with built-in decays (string, modal, drums).

**Velocity** is ignored in v1. Windsynths derive it from the initial breath, which we already track continuously.

**Naming.** Mutable Instruments asks that derivative works not use its module names, so the UI calls the engines by their sound ("Classic", "Dual Osc") and credits Plaits on an About page.

## Control spec

Three inputs come from the windsynth (breath, bite and glide), each with a default you set and an override. Breath feeds a 4-slot matrix; bite and glide are fixed routes.

### Inputs

| Input | Default | Options | Conditioning |
| --- | --- | --- | --- |
| Breath | CC2 | any CC 0–119 by number, or channel aftertouch | optional 14-bit pairing (CC n + CC n+32, e.g. CC2 + CC34) · threshold 0–20 % · curve (linear, soft, hard, S) · gain 50–200 % · smoothing 0–20 ms, default 5 ms |
| Bite → pitch bend | pitch bend message, ±2 semitones | range 0–12 semitones | 14-bit, 2 ms smoothing |
| Glide time | CC5, 0–127 → 0–2 s, exponential | any CC, or off (uses the panel's glide knob) | CC65 (portamento on/off) honoured when received |
| Mod A, Mod B | off | any CC, or channel aftertouch | the same curve and smoothing as breath |
| MIDI channel | omni | 1–16 | CC121 resets controllers; CC123 and CC120 silence the voice |

Mod A and Mod B are two free matrix sources, so aftertouch or another CC can modulate something alongside breath.

### Matrix

Four slots, each **Source → Destination × Amount** (−100 % to +100 %). Several slots on one destination add, except Amp, which multiplies.

- **Sources:** Breath, Mod A, Mod B, Bend, Note (key tracking, C4 = 0).
- **Destinations:** Cutoff, Resonance, Amp, Timbre, Morph, Harmonics, Glide time.

**Default slots**

| Slot | Source | Destination | Amount |
| --- | --- | --- | --- |
| 1 | Breath | Cutoff | +100 % |
| 2 | off | Amp | — |
| 3 | off | Resonance | — |
| 4 | off | Morph | — |

Slots 2–4 come pre-set to the destinations you'll want most, so turning one on is a single change.

**How the destinations behave**

- **Cutoff:** base cutoff (knob, 0–1) + amount × source, clamped to 0–1. Full scale is about 10 octaves above the note on VA-VCF.
- **Resonance:** base + amount × source, clamped to 0–1, then mapped per engine (see Voice design).
- **Amp:** a positive amount opens the VCA with the source, and a negative amount closes it. At +100 % breath 0 is silent and breath 1 is full.
- **Timbre, Morph, Harmonics:** base knob + amount × source, straight into Plaits' 0–1 parameters.
- **Glide time:** scales the glide time set by CC5 or the knob.

### Order of work in each 64-frame block

1. Drain the MIDI queue: USB, TRS and Web MIDI all land in the same queue. Update the note stack, raw breath, bend and CC values.
2. Condition the inputs (threshold, curve, gain), then run the one-pole smoothing per sample inside the block so cutoff never steps.
3. Run the matrix and fill `wind_frame_t`.
4. Render the voice.

### Testing without a windsynth

The FM-1's keys play notes at a fixed "key breath" level, 80 % by default. The browser page adds a breath slider, plus the space bar as a breath with a short ramp.

## Browser simulator

The browser build is our main development surface until the FM-1 arrives, and the base for the custom UI later. It starts as FiMba's `web/emu/`: the firmware's own sources compiled to WebAssembly and run in an AudioWorklet against a simulated FM-1, with Web MIDI input feeding the same queue as USB.

**What we add**

- **C++ in the build:** `em++` compiles the vendored Plaits files and `wind_voice.cc`; the C files stay on `emcc`. The same flags as the device: `-fno-exceptions -fno-rtti -fno-threadsafe-statics` and no fused multiply-add, so host and device produce the same samples.
- **MIDI device picker and channel filter,** remembered in the browser. Today the page listens to every input.
- **Dev overlay** beside the panel: live meters for breath (raw and conditioned), bend, glide time, the current note and each matrix destination, plus the render time per block on your computer.
- **A small JS API** on the page (`wind.get()`, `wind.set(path, value)`, `wind.onState(cb)`). It is the hook the custom UI will use later, so that UI never reaches into the firmware's memory directly.

**Deployment:** CI publishes the page to GitHub Pages on every merge to `main`, as FiMba does, with the latest device package beside it for the web installer.

**Limits to remember:** the simulator runs at your computer's speed, so it says nothing about the FM-1's CPU headroom. It also has no TRS MIDI timing. Both get checked on the device at M4.

## Milestones

Six milestones; M0 to M3 need no hardware. Each ends at a check anyone can run.

1. **M0 Fork and build.** Fork FiMba into the new repo, rename it and keep CI green: host tests, the browser page and the `.fwsc` package built with JieLi's toolchain on Linux.
   - Exit: the deployed page plays FiMba's kalimba from our repo, and CI produces a package that `check_fwsc.py` passes.
2. **M1 Plaits inside.** Vendor the Plaits and stmlib subset with a hash manifest. Write `wind_voice.cc` and its C seam with VA-VCF, and wire C++ into all three builds (host, Emscripten, pi32v2).
   - Exit: the host render of a fixed note sweep matches upstream Plaits within 1 LSB, and the pi32v2 build links with no C++ runtime.
3. **M2 Wind control.** Write `wind_ctl.c` and `wind_matrix.c`, replace the kalimba with the wind voice, and add the MIDI picker and dev overlay.
   - Exit: you play it from your windsynth in the browser. CC2 opens the cutoff, bite bends ±2 semitones, CC5 sets glide, and the parser and matrix unit tests pass.
4. **M3 Playable instrument.** Add engine 1 with the post-filter, settings pages on the FM-1 screen, presets (8 to start) saved to flash and the JS API.
   - Exit: settings and presets survive a reload, and every control in the spec is reachable from the panel.
5. **M4 On the device** (once the FM-1 arrives). Back up the flash with an RP2040 Transporter, install the package, then measure CPU per engine and MIDI-to-sound latency over USB and TRS.
   - Exit: it plays from both inputs, worst-case CPU is under 50 % of one core, and latency is under 6 ms.
6. **M5 More engines and the custom UI.** Add the resampler, then waveshaping, 2-op FM, formant/speech, wavetable, chords and string/modal. Start the custom UI on the JS API.
   - Exit: set per engine as we go.

## Testing

Most of the confidence comes from host tests and golden renders, because the same code runs in every build. The device only adds CPU, latency and TRS timing.

| Layer | What it checks | How |
| --- | --- | --- |
| Unit (host, C) | MIDI parsing (running status, 14-bit bend, CC MSB/LSB pairing, omni and channel), note stack, curves, threshold, smoothing step response, glide timing, matrix sums and clamps | the base fork's test runner, `tests/run_tests.sh` |
| Plaits reference | our wrapper against upstream Plaits for the same parameters | Lunar's method: same compiled objects on both sides, compare WAVs within 1 LSB |
| Golden renders | scripted MIDI → WAV, hash compared with `tests/golden.txt` | `wind-render`. Renders are rewritten deliberately with `GOLDEN_UPDATE=1`, as Felucca does |
| Performance fixtures | your recorded EWI `.mid` files replayed: no clicks at note changes, no zipper on cutoff, levels in range | `wind-render` plus simple checks (peak, step energy between blocks) |
| Browser | page loads, MIDI injected through a fake Web MIDI input makes sound, overlay updates | headless Chromium (Playwright), extending FiMba's `emu_test.mjs` |
| Device build | pi32v2 compile and link, `.fwsc` regions byte-identical to FiMba's outside the app, instruction-count cost budget per render function | CI on Linux, with Felucca's `target_budget` approach as a CPU proxy before hardware |
| Device (M4) | boot, CPU per engine, latency, TRS under a dense breath stream | the X0X-style performance page, plus a loopback recording for latency |

We also try booting each CI package in [fm1-emulator](https://github.com/simonjohansson/fm1-emulator) (QEMU with a pi32v2 CPU). If that works headless in CI, it catches crashes on the real instruction set before anything is installed.

## Risks and defaults

**Defaults I picked (change any of them)**

- Base fork: FiMba-1 rather than Felucca or X0X, because it is already a lean single-instrument firmware with safe boot and package checks. The project is GPL-3.0 as a result.
- Repo: [jcroft/fm1-windsynth](https://github.com/jcroft/fm1-windsynth), as you set it.
- I read "app" in your note as **amp**.
- Glide only on legato notes, with a setting to make it always on.
- No resampler until we add engines with built-in decays.

**Risks**

| Risk | Effect | Mitigation |
| --- | --- | --- |
| C++ through JieLi's clang 4.0.1 for pi32v2 fails or miscompiles | the device build blocks | M1 links the pi32v2 build in CI before any control work. The desktop test already needed no C++ runtime. Fallback: hand-port the two VA engines to C (about 400 lines) |
| CPU per Plaits engine on pi32v2 is unknown | heavy engines may not fit | instruction-count budgets in CI as a proxy, measured at M4, with core 1 as the reserve |
| 7-bit breath steps audibly on cutoff | zipper noise | per-sample smoothing, plus CC2/CC34 14-bit pairing when the controller sends it |
| A dense breath stream over TRS MIDI (31,250 baud, about 1,000 messages a second) | lag or dropped notes | running status, draining the UART ring every block, measured at M4. USB has no such limit |
| The FM-1 scene moves weekly; FiMba's base may change under us | merge churn | pin the fork point and pull platform fixes deliberately |
