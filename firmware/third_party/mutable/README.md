# Plaits and stmlib (vendored)

The subset of Mutable Instruments' code the wind voice builds, copied **unmodified** from upstream.
Our changes live outside this directory: `firmware/src/wind/wind_voice.cc` wraps the engines and
`firmware/src/wind/shim/` stands in for the C++ library and libm, so the code builds freestanding.

| Code | Upstream | Commit |
| --- | --- | --- |
| `plaits/` | <https://github.com/pichenettes/eurorack> | `08460a69a7e1f7a81c5a2abcc7189c9a6b7208d4` (2023-08-16) |
| `stmlib/` | <https://github.com/pichenettes/stmlib> | `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec` (2023-05-30), the eurorack commit's submodule |

Copyright Emilie Gillet, MIT licence (`LICENSE`, and the header of every file).

`MANIFEST.sha256` holds every file's hash; `tests/run_tests.sh` checks it, so an edit here fails the
tests. To add an engine, copy its files (and only the headers it includes) from the same commits, then
regenerate the manifest from this directory:

    find plaits stmlib -type f | sort | xargs sha256sum > MANIFEST.sha256
