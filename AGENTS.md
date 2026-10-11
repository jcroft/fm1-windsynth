# FM-1 Windsynth: notes for agents

Firmware that turns the M-VAVE FM-1 into a synth for any MIDI wind controller, with Plaits voices.
Browser first, then the device. The plan and its milestones (M0 to M5) are in `docs/PLAN.md`.

## The control spec (settled: build it, don't reopen it)

- Breath is CC2 by default and goes to filter cutoff by default. A mod matrix also reaches amp,
  resonance and engine parameters. Aftertouch or any other CC can be the breath source.
- Bite sends pitch bend: ±2 semitones by default, adjustable.
- CC5 sets glide time by default; another CC can be chosen.
- It's for any windsynth, not one model.

## Where things are

- `firmware/src/wind/`: our code. `wind_voice.cc` and `wind_voice.h` (the C seam to the Plaits
  engines), and `wind_ctl` and `wind_matrix` from M2. `shim/` stands in for libm and the C++ library,
  so the C++ builds freestanding.
- `firmware/third_party/mutable/`: Plaits and stmlib, vendored unmodified and pinned by
  `MANIFEST.sha256`. Never edit them; wrap them in `firmware/src/wind/`.
- Inherited from the FiMba-1 fork and still in place until replaced: `firmware/src/dsp/kalimba.*`,
  `firmware/src/app/` (UI, project and panel), `host/`, `web/emu/`, and the platform (`firmware/hal/`,
  `usb.c`, `ota.c` and so on). Treat FiMba's docs and conventions as reference, not rules.

## Code

- C99 for the app and C++11 for the wind voice, float only. There's no libm or C++ runtime on the device,
  so use `dsp/fastmath.h`. No exceptions, RTTI or static constructors. Build with
  `-ffp-contract=off`, so the host and browser builds compute the same samples as the device.
- Hardware register access stays in `firmware/hal/`; `tools/build.py` checks this.
- Saved settings must keep loading, so add new enum values and parameters at the end.

## Build and test

- Device: `python3 tools/build.py` (`JIELI_TOOLCHAIN`, `AC79_SDK`; see `BUILDING.md`).
- Browser: `web/emu/build.sh` (Emscripten). Host simulator: `host/build_host.sh`.
- Everything: `tests/run_tests.sh`. It has to pass before a push. Add tests with each feature,
  and break each new check once to see it fail.

## Workflow

- Work on a branch, open a PR, and drive CI to green.
- Then merge it yourself; don't wait for a human to merge. To take a PR out of draft, use
  the GitHub API (`update_pull_request`, draft false); `gh pr ready` doesn't work here.
- Commit subjects: `Area: what changed`.
- `VERSION` stays at `0.0.x` during development. CI builds and tests those versions but releases
  nothing. A version past 0.0.x, merged to main, releases automatically: tag, GitHub Release with
  its `CHANGELOG.md` section, and the site at https://jcroft.github.io/fm1-windsynth/. Never tag or
  release by hand.
