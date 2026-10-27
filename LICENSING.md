# FM-1 Windsynth licensing

FM-1 Windsynth is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`,
full text in `LICENSE`). Copyright (C) 2026 Jeff Croft, for the wind instrument and the changes made
here; the parts that came from other projects keep their authors' copyright, as the tables below say. It is a fork of [FiMba-1](https://github.com/jadamsowers/fm1-fimba), which is
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

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Barlow Semi Condensed (The Barlow Project Authors), the UI face | SIL OFL 1.1 | `assets/fonts/BarlowSemiCondensed-*.ttf`, `assets/fonts/Barlow-OFL.txt` |
| Terminus (Dimitar Toshkov Zhekov), an alternative font set | SIL OFL 1.1 | `assets/fonts/ter-u*.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| The wind instrument: `firmware/src/wind/` (voice, control, matrix, app), the UI's pages, `tools/wind_cxx.sh`, `tools/check_vendor.py`, `tests/host/wind_*`, `tests/scenarios/` | FM-1 Windsynth, Copyright (C) 2026 Jeff Croft | GPL-3.0-only |
| Plaits and stmlib, a subset, unmodified: `firmware/third_party/mutable/` (provenance and hashes in its `README.md`) | [Mutable Instruments](https://github.com/pichenettes/eurorack), Copyright Emilie Gillet | MIT (`firmware/third_party/mutable/LICENSE`) |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package, so every released `.fwsc` contains them, unmodified; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. "M-VAVE" and "FM-1" are
trademarks of their respective owners. FM-1 Windsynth is independent firmware, not affiliated with, endorsed
by or supported by any of them.

## Radio

FM-1 Windsynth never enables the Bluetooth / Wi-Fi radio of the hardware.
