#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Windsynth host tests: the maths, the platform pieces kept from Felucca / X0X / FoMni, the instrument
# (the wind voice against upstream Plaits, wind control and the matrix, built without libm as on the
# device), and the whole firmware app in the simulator (tests/scenarios/*.scn: breath, bite, glide,
# pages, keys, persistence, effects, noise, samples), with screenshots and audio in build/scenarios/.
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
FAIL=0
OUT=build/host
mkdir -p "$OUT" build/scenarios
run() {
    name="$1"; shift
    if "$@" > "$OUT/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name (see $OUT/$name.log)"
        grep 'FAIL' "$OUT/$name.log" | head -10 | sed 's/^/       /'
        tail -15 "$OUT/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
run fastmath sh -c "$CC -O2 -ffp-contract=off -Wall -Wextra -Werror -o $OUT/fastmath_test tests/host/fastmath_test.c -lm && $OUT/fastmath_test"
run encoder sh -c "$CC -O2 -w -Ifirmware/hal -o $OUT/encoder_test tests/host/encoder_test.c && $OUT/encoder_test"
run uac sh -c "$CC -O2 -w -Ifirmware/src -Ifirmware/hal -o $OUT/uac_test tests/host/uac_test.c -lm && $OUT/uac_test"
run trs sh -c "$CC -O2 -w -Ifirmware/src -Ifirmware/hal -o $OUT/trs_test tests/host/trs_test.c && $OUT/trs_test"
# the wind voice: upstream Plaits renders the reference sweep (its own desktop build); ours, freestanding
# and linked by $CC with no -lm or C++ runtime as on the device, must match it and play in tune
CXX="${CXX:-c++}"
run wind sh -c "mkdir -p $OUT/wind && $CXX -O2 -ffp-contract=off -DTEST -Ifirmware/third_party/mutable -Itests/host \
    -o $OUT/wind/wind_ref tests/host/wind_ref.cc firmware/third_party/mutable/plaits/dsp/engine2/virtual_analog_vcf_engine.cc \
    firmware/third_party/mutable/plaits/dsp/engine/fm_engine.cc firmware/third_party/mutable/plaits/resources.cc \
    firmware/third_party/mutable/plaits/dsp/engine2/six_op_engine.cc firmware/third_party/mutable/plaits/dsp/fm/algorithms.cc \
    firmware/third_party/mutable/plaits/dsp/fm/dx_units.cc firmware/third_party/mutable/plaits/dsp/engine/grain_engine.cc \
    firmware/third_party/mutable/plaits/dsp/engine/waveshaping_engine.cc firmware/third_party/mutable/plaits/dsp/engine/virtual_analog_engine.cc \
    firmware/third_party/mutable/plaits/dsp/engine2/phase_distortion_engine.cc firmware/third_party/mutable/plaits/dsp/engine/additive_engine.cc \
    firmware/third_party/mutable/stmlib/utils/random.cc \
    firmware/third_party/mutable/stmlib/dsp/units.cc && \
    for e in 0 1 2 3 4 5 6 7; do $OUT/wind/wind_ref $OUT/wind/ref_\$e.f32 \$e || exit 1; done && \
    $CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -Ifirmware/src -Itests/host \
    -o $OUT/wind/wind_voice_test tests/host/wind_voice_test.c firmware/src/wind/wind_sample.c \$(sh tools/wind_cxx.sh $CXX $OUT/wind -Werror) && \
    $OUT/wind/wind_voice_test $OUT/wind $OUT/wind"
# wind control (MIDI, the note stack, breath, bite, glide), the matrix and the instrument; no libm
run wind-ctl sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -Ifirmware/src \
    -o $OUT/wind/wind_ctl_test tests/host/wind_ctl_test.c firmware/src/wind/wind_ctl.c firmware/src/wind/wind_matrix.c \
    firmware/src/wind/wind_app.c firmware/src/wind/wind_fx.c firmware/src/wind/wind_sample.c \$(sh tools/wind_cxx.sh $CXX $OUT/wind -Werror) && $OUT/wind/wind_ctl_test"
# the sample layer's player: the bank's checks, pitch, loops, start and trigger, mu-law; no libm
run wind-sample sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -Ifirmware/src \
    -o $OUT/wind/wind_sample_test tests/host/wind_sample_test.c firmware/src/wind/wind_sample.c && $OUT/wind/wind_sample_test"
# the effects: off is bit-exact, decay times, stereo, shimmer, echo times, Ping-pong, distortion level,
# latency and aliasing, modulation; no libm
run wind-fx sh -c "$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -Ifirmware/src \
    -c -o $OUT/wind/wind_fx.o firmware/src/wind/wind_fx.c && \
    $CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Werror -Ifirmware/src -o $OUT/wind/wind_fx_test \
    tests/host/wind_fx_test.c $OUT/wind/wind_fx.o && $OUT/wind/wind_fx_test"
# the same, and the maths, with every out-of-bounds read and undefined operation an error (where the
# compiler has the sanitizers)
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
if echo 'int main(void){return 0;}' | $CC $SAN -x c -o $OUT/san_probe - >/dev/null 2>&1; then
    run wind-fx-sanitized sh -c "$CC -O1 -g -ffp-contract=off -std=c99 $SAN -Ifirmware/src -o $OUT/wind/wind_fx_san \
        tests/host/wind_fx_test.c firmware/src/wind/wind_fx.c && $OUT/wind/wind_fx_san && \
        $CC -O1 -g -ffp-contract=off $SAN -o $OUT/fastmath_san tests/host/fastmath_test.c -lm && $OUT/fastmath_san"
else
    echo "  skip wind-fx-sanitized (no -fsanitize=address,undefined)"
fi
run vendor python3 tools/check_vendor.py
# the wind code is MPL-2.0 and builds on its own with the C library's maths, as other firmware would use it
# (the voice both with the system's headers and freestanding, through the shim)
run wind-mpl sh -c "python3 tools/check_wind_license.py && mkdir -p $OUT/wind-libm && \
    for u in wind_ctl wind_matrix wind_app wind_fx wind_sample; do \
        $CC -std=c99 -Wall -Wextra -Werror -DWIND_MATH_LIBM -c -o $OUT/wind-libm/\$u.o firmware/src/wind/\$u.c || exit 1; done && \
    $CXX -std=c++11 -Wall -Werror -DTEST -DWIND_MATH_LIBM -Ifirmware/third_party/mutable -c -o $OUT/wind-libm/wind_voice.o \
        firmware/src/wind/wind_voice.cc && sh tools/wind_cxx.sh $CXX $OUT/wind-libm -DWIND_MATH_LIBM -Werror >/dev/null"
run storage sh -c "$CC -O2 -o $OUT/storage_test tests/storage_test.c && $OUT/storage_test"
# the update path, against the firmware package (Felucca's tests; needs ./build.sh)
if [ -f build/windsynth.fwsc ]; then
    run ota-entry sh -c "$CC -o $OUT/ota_test tests/ota_test.c && $OUT/ota_test build/windsynth.fwsc"
    if [ -z "${AC79_SDK:-}" ]; then
        echo "  skip update-loader (needs AC79_SDK, as the build)"
    else
    run update-loader sh -c "head -c 100000 build/windsynth.bin > $OUT/old_app.bin && \
        python3 tools/fm1pkg_make.py $OUT/old_app.bin build/loader/ota.bin $OUT/old.fwsc >/dev/null && \
        $CC -o $OUT/ldr_test tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc build/windsynth.fwsc"
    fi
    run installer python3 tests/install_test.py
    # the package taken apart and every CRC checked; against FoMni's own package when one is there
    # (build/fomni-ref: see BUILDING.md, "Before installing")
    REF=""
    [ -f build/fomni-ref/build/omni.fwsc ] && REF="--ref build/fomni-ref/build/omni.fwsc"
    run package sh -c "python3 tools/check_fwsc.py build/windsynth.fwsc --app build/windsynth.bin $REF"
else
    echo "  skip update-path tests (no build/windsynth.fwsc: run ./build.sh)"
fi
run host-build sh host/build_host.sh
if command -v emcc >/dev/null 2>&1; then
    run emu sh -c "sh web/emu/build.sh >/dev/null 2>&1 && node tests/host/emu_test.mjs build/emu/windsynth.wasm"
    run wind-wasm node tests/host/wind_wasm_test.mjs build/emu/windsynth.wasm $OUT/wind/wind_sweep_0.f32
    CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
    [ -x "$CHROME" ] || CHROME="$(command -v google-chrome || command -v chromium || true)"
    # the installer as the page ships it, against a simulated FM-1
    run updater node tests/web/updater_test.mjs
    if [ -n "$CHROME" ]; then
        run standalone node tests/host/standalone_test.mjs "$CHROME"
        if [ -f build/windsynth.fwsc ]; then       # the installer's buttons, end to end (needs a firmware build)
            run installer-page node tests/web/installer_page_test.mjs "$CHROME"
        else
            echo "  skip installer-page (no build/windsynth.fwsc)"
        fi
    else
        echo "  skip standalone, installer-page (no Chrome; CI runs them in its browser job)"
    fi
else
    echo "  skip emu, wind-wasm, updater, standalone (no emcc)"
fi
# the sample bank tool (tools/fm1_samples.py), and the bank and its SysEx that sample.scn loads
run samples-tool python3 tests/samples_test.py build/samples
# the page's banks and SysEx: the same as the tool's
if command -v node >/dev/null 2>&1; then
    run samples-page node tests/web/samples_test.mjs build/samples
else
    echo "  skip samples-page (no node)"
fi
for s in tests/scenarios/*.scn; do
    n=$(basename "$s" .scn)
    mkdir -p "build/scenarios/$n"
    run "scenario-$n" build/host/windsynth_host "$s" "build/scenarios/$n"
done
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
