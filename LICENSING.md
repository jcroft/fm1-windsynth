# FM-1 Windsynth licensing

FM-1 Windsynth is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`,
full text in `LICENSE`). The instrument code in `firmware/src/wind/` is also available on its own
under the Mozilla Public License 2.0 (see below). Copyright (C) 2026 Jeff Croft, for the wind instrument and the changes
made here; the parts that came from other projects keep their authors' copyright, as the tables below
say. It is a fork of [FiMba-1](https://github.com/jadamsowers/fm1-fimba), which is
built on FoMni (and through it X0X and Felucca), and keeps their licence. If you distribute FM-1
Windsynth, or firmware derived from it, you must give your recipients its
complete corresponding source under the same licence.

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| Platform: `firmware/hal/`, `firmware/loader/`, `firmware/src/{libc,lcd,gfx,usb,storage,ota,midi_uart}.c`, `firmware/crt0.S`, `firmware/app.ld`, `tools/` (build, package, install, rescue, font), `web/fm1*.js` | [Felucca](https://github.com/hugelton/Felucca), Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, as changed for [X0X](https://github.com/charlesvestal/fm1-x0x) and [FoMni](https://github.com/charlesvestal/fm1-fomni) (USB names changed here) | GPL-3.0-only |
| `firmware/src/app/{panel,plat_fm1,main_fm1}.c`, `firmware/src/main.c`, `host/windsynth_host.c`, `web/emu/`, the frame of `firmware/src/app/ui.c` (bands, knobs, autosave) and `project.c` | adapted from FoMni (itself from X0X and Felucca) | GPL-3.0-only |
| `firmware/src/dsp/fastmath.h` | FoMni / X0X | GPL-3.0-only |
| Platform tests (`tests/host/{fastmath,encoder,uac,trs}_test.c`, `tests/{storage,ota,ldr}_test.c`, `tests/install_test.py`) | FoMni / Felucca | GPL-3.0-only |
| The simulated FM-1 for the web installer's tests (`tests/web/fake_fm1.mjs`) and the installer panel's protocol use (`web/fm1ota.js`, `web/fm1pkg.js`, inlined into the page) | Felucca's `web/test_web.mjs` and update modules, by way of FoMni's web installer | GPL-3.0-only |
| The UI's look (its themes, bars and knob row) and the scenario runner in the simulator | [FiMba-1](https://github.com/jadamsowers/fm1-fimba), Adam Sowers | GPL-3.0-only |
| The wind instrument: `firmware/src/wind/` (voice, control, matrix, effects, app, the maths seam `wind_math.h` and `shim/`) | FM-1 Windsynth, Copyright (C) 2026 Jeff Croft | MPL-2.0 (`firmware/src/wind/LICENSE`, `LICENSES/MPL-2.0.txt`) |
| The UI's pages, `tools/wind_cxx.sh`, `tools/check_vendor.py`, `tools/check_wind_license.py`, `tests/host/wind_*`, `tests/scenarios/` | FM-1 Windsynth, Copyright (C) 2026 Jeff Croft | GPL-3.0-only |

## The instrument under MPL-2.0

`firmware/src/wind/` is under the Mozilla Public License 2.0, so other firmware can use it, including
firmware that links closed code such as a Bluetooth stack. The MPL is file-level: changes to these
files stay under the MPL, and the rest of a program that uses them can be under any licence.

In this firmware those files are combined with the GPL-3.0-only platform above, and the firmware as
a whole is distributed under GPL-3.0-only, which the MPL allows (its section 3.3). The rest of the
repository stays GPL-3.0-only; relicensing it would need its authors' consent.

The wind code takes its maths through `wind_math.h`. Here that is `firmware/src/dsp/fastmath.h`
(X0X, GPL-3.0-only), which is not part of the MPL code. Build with `-DWIND_MATH_LIBM` and the C
library's maths stands in. `tools/check_wind_license.py` keeps every file in `firmware/src/wind/`
under MPL-2.0 and checks that nothing else there reaches outside the directory.

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Barlow Semi Condensed (The Barlow Project Authors), the UI face | SIL OFL 1.1 | `assets/fonts/BarlowSemiCondensed-*.ttf`, `assets/fonts/Barlow-OFL.txt` |
| Terminus (Dimitar Toshkov Zhekov), an alternative font set | SIL OFL 1.1 | `assets/fonts/ter-u*.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| Plaits and stmlib ([Mutable Instruments](https://github.com/pichenettes/eurorack), Copyright Emilie Gillet), a subset, unmodified, compiled into the firmware and the browser version | MIT (`firmware/third_party/mutable/LICENSE`) | `firmware/third_party/mutable/` (provenance and hashes in its `README.md`) |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin`, read from your SDK checkout at build time and placed in the package, so every released `.fwsc` contains them (their contents unmodified; the last two are stored encrypted, as the whole app region is) | Apache-2.0 (`LICENSES/Apache-2.0.txt`) | not in this tree: <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. "M-VAVE" and "FM-1" are
trademarks of their respective owners. FM-1 Windsynth is independent firmware, not affiliated with, endorsed
by or supported by any of them.

## Radio

FM-1 Windsynth never enables the Bluetooth / Wi-Fi radio of the hardware.
