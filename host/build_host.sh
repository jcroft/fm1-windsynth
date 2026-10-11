#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the Windsynth host simulator (the whole app on this machine) into build/host/windsynth_host.
# Same sources as the firmware, -ffp-contract=off like the device (no fused multiply-add).
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
mkdir -p build/gen build/host
[ -f build/gen/felucca_font.h ] || "$PY" tools/gen_font.py build/gen/felucca_font.h >/dev/null
# the wind voice (C++), freestanding as on the device: it links with $CC, no C++ runtime
WIND=$(sh tools/wind_cxx.sh "${CXX:-c++}" build/host/wind)
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter \
    -DOM_HOST -Ifirmware/src -Ibuild/gen -o build/host/windsynth_host host/windsynth_host.c \
    firmware/src/wind/wind_ctl.c firmware/src/wind/wind_matrix.c firmware/src/wind/wind_app.c firmware/src/wind/wind_fx.c $WIND -lm
echo "host: build/host/windsynth_host"
