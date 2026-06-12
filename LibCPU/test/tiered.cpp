/** @file  Tiered execution demos: JIT (online adaptive) and AOT (profile-guided). */
#include "RunTiered.h"
#include "LibCPU/Loader.h"
#include <cstdio>
using namespace LibCPU;
int main (int argc, char **argv) {
    if (argc < 3) {
        std::printf ("usage: %s <tier0.backend> <tier1.backend>\n", argv[0]);
        return 2;
    }
    ICpuBackend *pT0 = LoadBackendBundle (argv[1]);
    ICpuBackend *pT1 = LoadBackendBundle (argv[2]);
    if (!pT0 || !pT1) { std::printf ("failed to load bundles\n"); return 2; }

    int Jit = RunTieredDemo (pT0, pT1);
    int Aot = RunProfiledAotDemo (pT0, pT1, "/tmp/libcpu-v20.trace");
    int Inl = RunInlineDemo (pT0, pT1, "/tmp/libcpu-v20-edges.trace");

    pT1->Release ();
    pT0->Release ();
    return (Jit == 0 && Aot == 0 && Inl == 0) ? 0 : 1;
}
