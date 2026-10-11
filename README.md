# FM-1 Windsynth

Custom firmware that turns the **M-VAVE FM-1** into a synthesizer for **any MIDI wind controller**
(EWI, NuRAD, Aerophone, Sylphyo and the rest), with voices taken from Mutable Instruments Plaits.
It runs in the browser first, as the firmware's own C code compiled to WebAssembly, and then on the
FM-1 itself.

> **Status: in development (0.0.x).** A fork of FiMba-1 that still plays FiMba's kalimba while the
> wind voice is built. The voice (Plaits' VA-VCF engine) is in, tested against upstream Plaits, and
> gets wired to breath and bite next. Nothing is released yet. The plan is in
> [docs/PLAN.md](docs/PLAN.md).

## The controls (planned defaults)

| Control | Default | Options |
| --- | --- | --- |
| Breath | CC2, to filter cutoff | A matrix that also reaches amp, resonance and engine parameters; aftertouch or another CC as the source |
| Bite | Pitch bend, ±2 semitones | Bend range adjustable |
| Glide | CC5 sets glide time | Another CC as the source |

## Building and testing

See [BUILDING.md](BUILDING.md). In short: `python3 tools/build.py` builds the FM-1 package,
`web/emu/build.sh` builds the browser version, and `tests/run_tests.sh` runs everything.

## Credits

FM-1 Windsynth is a fork of [FiMba-1](https://github.com/jadamsowers/fm1-fimba) by Adam Sowers,
which grew from [FoMni](https://github.com/charlesvestal/fm1-fomni) and
[X0X](https://github.com/charlesvestal/fm1-x0x) by Charles Vestal, both built on
[Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton Instruments). The voices
will come from [Plaits](https://github.com/pichenettes/eurorack) by Émilie Gillet (MIT). FiMba's
original README, which documents the inherited kalimba, is in
[docs/FIMBA-README.md](docs/FIMBA-README.md).

Licensing: GPL-3.0-only; see [LICENSING.md](LICENSING.md). Not affiliated with M-VAVE.
