#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Compile the wind voice (C++: wind_voice.cc and its vendored Plaits units) for the host or the web.
#   tools/wind_cxx.sh <c++ compiler> <out dir> [extra flags]  ->  prints the object files
# Freestanding, as on the device (tools/build.py WIND_UNITS, WIND_CXXFLAGS): no exceptions, RTTI,
# static constructors or libm (firmware/src/wind/shim), so the objects link with a C compiler and no
# C++ runtime. -ffp-contract=off like the rest of the DSP.
set -e
CXX="$1"; OUT="$2"; shift 2
mkdir -p "$OUT"
for u in firmware/src/wind/wind_voice.cc \
         firmware/third_party/mutable/plaits/dsp/engine2/virtual_analog_vcf_engine.cc \
         firmware/third_party/mutable/plaits/dsp/engine/fm_engine.cc \
         firmware/third_party/mutable/plaits/dsp/engine2/six_op_engine.cc \
         firmware/third_party/mutable/plaits/dsp/fm/algorithms.cc \
         firmware/third_party/mutable/plaits/dsp/fm/dx_units.cc \
         firmware/third_party/mutable/plaits/dsp/engine/grain_engine.cc \
         firmware/third_party/mutable/plaits/dsp/engine/waveshaping_engine.cc \
         firmware/third_party/mutable/plaits/dsp/engine/virtual_analog_engine.cc \
         firmware/third_party/mutable/plaits/dsp/engine2/phase_distortion_engine.cc \
         firmware/third_party/mutable/plaits/dsp/engine/additive_engine.cc \
         firmware/third_party/mutable/stmlib/utils/random.cc \
         firmware/third_party/mutable/plaits/resources.cc \
         firmware/third_party/mutable/stmlib/dsp/units.cc; do
    o="$OUT/$(basename "$u" .cc).o"
    $CXX -std=c++11 -O2 -ffp-contract=off -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics \
        -fno-use-cxa-atexit -DTEST -Wall -Ifirmware/src/wind/shim -Ifirmware/third_party/mutable -Ifirmware/src \
        "$@" -c "$u" -o "$o"
    printf '%s ' "$o"
done
