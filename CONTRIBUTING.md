# Contributing to FM-1 Windsynth

Firmware that turns the M-VAVE FM-1 into a synth for any MIDI wind controller, with Plaits voices.
It runs in the browser first, then on the device. Issues and pull requests are welcome.

## The control spec

These are the defaults the instrument is built around. A change to one is a design change, so open an
issue first.

- Breath is CC2 by default and goes to filter cutoff by default. A mod matrix also reaches amp,
  resonance and engine parameters. Aftertouch or any other CC can be the breath source.
- Bite sends pitch bend: ±2 semitones by default, adjustable.
- CC5 sets glide time by default; another CC can be chosen.
- It's for any windsynth, not one model.

## Where things are

- `firmware/src/wind/`: the instrument. `wind_voice.cc` and `wind_voice.h` (the C seam to the Plaits
  engines), `wind_ctl` (MIDI, breath, bite, glide, the note stack), `wind_matrix` (the 4 Mod slots),
  `wind_fx` (the effects) and `wind_app` (the parameters and the audio block that ties them together).
  `shim/` stands in for libm and the C++ library, so the C++ builds freestanding.
- `firmware/third_party/mutable/`: Plaits and stmlib, vendored unmodified and pinned by
  `MANIFEST.sha256`. Never edit them; wrap them in `firmware/src/wind/`.
- `firmware/src/app/`: the UI (`ui.c`: pages, screen, panel keys), the saved project, the device main.
  `host/windsynth_host.c` runs the whole app from a script (`tests/scenarios/*.scn`); `web/emu/` is
  the same compiled to WebAssembly, with the page.
- Inherited from the FiMba-1 fork, and through it from Felucca, X0X and FoMni: the platform
  (`firmware/hal/`, `usb.c`, `ota.c` and so on). `LICENSING.md` says where each part comes from.
  The FiMba history at the bottom of `CHANGELOG.md` and the conventions in that code are reference,
  not rules.

## Code

- C99 for the app and C++11 for the wind voice, float only. There's no libm or C++ runtime on the device,
  so use `dsp/fastmath.h` (from `firmware/src/wind/`, through `wind_math.h`). No exceptions, RTTI or static constructors. Build with
  `-ffp-contract=off`, so the host and browser builds compute the same samples as the device.
- Hardware register access stays in `firmware/hal/`; `tools/build.py` checks this.
- Saved settings must keep loading, so add new enum values and parameters at the end.
- New files carry `SPDX-License-Identifier: GPL-3.0-only`, except in `firmware/src/wind/`, which is
  MPL-2.0: files there carry `SPDX-License-Identifier: MPL-2.0` and include nothing from outside the
  directory but `wind_math.h`'s maths, the vendored Mutable code and the C library
  (`tools/check_wind_license.py` checks).

## Build and test

- Device: `python3 tools/build.py` (`JIELI_TOOLCHAIN`, `AC79_SDK`; see `BUILDING.md`).
- Browser: `web/emu/build.sh` (Emscripten). Host simulator: `host/build_host.sh`.
- Everything: `tests/run_tests.sh`. It has to pass before a push. Parts that need the JieLi
  toolchain and SDK, Emscripten or Chrome are skipped, with a note, when those are missing; CI runs
  them all. Add tests with each feature, and break each new check once to see it fail.

## Pull requests

- Work on a branch and open a PR against `main`; CI has to be green before it merges.
- Commit subjects: `Area: what changed`.
- Describe a change players will notice in the PR, and leave `VERSION` and `CHANGELOG.md` to the
  maintainer, who adds them when the change is released.
- `VERSION` stays at `0.0.x` during development. CI builds and tests those versions but releases
  nothing. A version past 0.0.x, merged to main, releases automatically: tag, GitHub Release with
  its `CHANGELOG.md` section, and the site at https://jcroft.github.io/fm1-windsynth/. Never tag or
  release by hand.

By contributing, you agree that your contribution is licensed under GPL-3.0-only, as the rest of the
project is, or under MPL-2.0 for changes to `firmware/src/wind/`.
