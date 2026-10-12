// SPDX-License-Identifier: GPL-3.0-only
// The reference: upstream Plaits' engines as Plaits itself builds them for a desktop (-DTEST, the
// system's <algorithm> and libm, no shim), rendering the fixed sweep to a file of floats.
//   wind_ref <out.f32> [ENGINE]  (wind_voice.h's engine number: 0 VA-VCF, the default, 1 2-op FM, 2 6-op FM
//                                 (bank 0), 3 granular formant, 4 waveshaping, 5 virtual analog)
#include <stdio.h>
#include <string.h>
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine2/six_op_engine.h"
#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine/waveshaping_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/resources.h"
#include <stdlib.h>
#include "wind_sweep.h"

using namespace plaits;

int main(int argc, char **argv)
{
    static char arena[16384];
    static VirtualAnalogVCFEngine vcf;
    static FMEngine fm;
    static SixOpEngine six;
    static GrainEngine grain;
    static WaveshapingEngine shape;
    static VirtualAnalogEngine va;
    Engine *all[] = {&vcf, &fm, &six, &grain, &shape, &va};
    int id = argc > 2 ? (strcmp(argv[2], "fm") ? atoi(argv[2]) : 1) : 0;
    Engine &e = *all[id >= 0 && id < 6 ? id : 0];
    stmlib::BufferAllocator a(arena, sizeof arena);
    FILE *f = argc > 1 ? fopen(argv[1], "wb") : NULL;
    if (!f)
        return 2;
    for (int i = 0; i < 6; i++) {       /* every engine, in wind_voice's order: the random numbers then match */
        all[i]->Init(&a);
        all[i]->Reset();
    }
    if (id == 2)
        e.LoadUserData(fm_patches_table[0]);
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
