/** @file  System-level emulation demo: an 8086 machine with a device bus + timer IRQ. */
#include "RunSystem.h"
#include "LibCPU/Loader.h"
#include <cstdio>
using namespace LibCPU;
int main (int argc, char **argv) {
    if (argc < 2) {
        std::printf ("usage: %s <path-to.backend>\n", argv[0]);
        return 2;
    }
    ICpuBackend *pBackend = LoadBackendBundle (argv[1]);
    if (!pBackend) { std::printf ("failed to load bundle '%s'\n", argv[1]); return 2; }
    int Result = RunSystemDemo (pBackend);
    pBackend->Release ();
    return Result;
}
