**FM-1 Windsynth**: a breath-controlled synthesizer firmware, for any MIDI wind controller, for the M-VAVE FM-1.

> **Beta.** Windsynth runs on the FM-1, installed with the web updater. Custom firmware is installed at
> your own risk, so have the way back ready first (below). Its package differs from FoMni's only in
> the app, checked byte by byte in this release's build.

### Files

| File | What it is |
|---|---|
| `windsynth-VERSION.fwsc` | The firmware package, to install on the FM-1 |
| `Windsynth.html` | The browser version in one file: open it from disk, no FM-1 needed |

Or play it online: <https://jcroft.github.io/fm1-windsynth/>

### Installing

1. **Get the way back ready.** Download M-VAVE's stock firmware (FM-1 V15, "PC Firmware" at
   m-vave.com/download) and keep it. If Windsynth fails to start twice, the FM-1 comes up in safe mode
   (no sound, USB on), where you can reinstall. After four failed starts it enters the chip's own
   update mode, which the package can't overwrite, and `tools/fm1_rescue.py` puts stock firmware
   back. The install page's "Install the stock firmware" (and FoMni's web installer) can also restore it.
2. **Install from the web page**: <https://jcroft.github.io/fm1-windsynth/#install> (Chrome or Edge):
   connect the FM-1 by USB and press Install. Or **over USB from the command line**, from a checkout
   of this repository (`pip install -r requirements.txt`):
   ```
   python3 tools/fm1_install.py --info                   # finds the FM-1, writes nothing
   python3 tools/fm1_install.py windsynth-VERSION.fwsc
   ```
   Or hold OCT− and OCT+ for 5 seconds for update mode, and use any Felucca-family installer.
3. The splash screen shows the version, and the Setup page (GLO) shows the audio load. If it's
   high, or if you hear dropouts, please open an issue.

The [README](https://github.com/jcroft/fm1-windsynth#readme) explains every key, button and knob.

FM-1 Windsynth is GPL-3.0-only, and its source is in this repository; see
[LICENSING.md](https://github.com/jcroft/fm1-windsynth/blob/main/LICENSING.md). The `.fwsc` and
`Windsynth.html` contain Mutable Instruments' Plaits and stmlib, Copyright Emilie Gillet, under the
[MIT licence](https://github.com/jcroft/fm1-windsynth/blob/main/firmware/third_party/mutable/LICENSE).
The `.fwsc` also contains three files from JieLi's AC79 SDK (`uboot.boot`, `cfg_tool.bin`,
`eq_cfg_hw.bin`; contents unmodified), under the
[Apache License 2.0](https://github.com/jcroft/fm1-windsynth/blob/main/LICENSES/Apache-2.0.txt).
