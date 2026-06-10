/** @file  Load a backend BUNDLE and AOT-translate+run the whole CHIP-8 program. */
#include "RunAotChip8.h"
#include "LibCPU/Loader.h"
#include <cstdio>
using namespace LibCPU;
int main (int argc, char **argv) {
    if (argc < 2) { std::printf ("usage: %s <path-to.backend>\n", argv[0]); return 2; }
    ICpuBackend *pBackend = LoadBackendBundle (argv[1]);
    if (!pBackend) { std::printf ("failed to load bundle '%s'\n", argv[1]); return 2; }
    int Result = RunAotChip8Program (pBackend);
    pBackend->Release ();
    return Result;
}
