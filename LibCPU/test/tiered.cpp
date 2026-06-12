/** @file  Adaptive tiered engine demo: load two backend bundles (cheap, optimizing). */
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
    int Result = RunTieredDemo (pT0, pT1);
    pT1->Release ();
    pT0->Release ();
    return Result;
}
