// SPDX-License-Identifier: GPL-3.0-only
// The reference: upstream Plaits' VA-VCF engine as Plaits itself builds it for a desktop (-DTEST,
// the system's <algorithm> and libm, no shim), rendering the fixed sweep to a file of floats.
//   wind_ref <out.f32>
#include <stdio.h>
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "wind_sweep.h"

using namespace plaits;

int main(int argc, char **argv)
{
    static char arena[16];
    static VirtualAnalogVCFEngine e;
    stmlib::BufferAllocator a(arena, sizeof arena);
    FILE *f = argc > 1 ? fopen(argv[1], "wb") : NULL;
    if (!f)
        return 2;
    e.Init(&a);
    e.Reset();
    for (int s = 0; s < SWEEP_STEPS; s++) {
        EngineParameters p;
        float out[12], aux[12];
        bool env;
        p.trigger = TRIGGER_UNPATCHED;
        p.accent = 1.0f;
        sweep_params(s, &p.note, &p.timbre, &p.morph, &p.harmonics);
        for (int i = 0; i < SWEEP_LEN; i += 12) {
            e.Render(p, out, aux, 12, &env);
            fwrite(out, sizeof(float), 12, f);
        }
    }
    return fclose(f) ? 1 : 0;
}
