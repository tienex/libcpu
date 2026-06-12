/** @file  Disk-cached translation demo: translate once, reload from disk thereafter. */
#include "RunCache.h"
#include "LibCPU/Loader.h"
#include <cstdio>
using namespace LibCPU;
int main (int argc, char **argv) {
    if (argc < 3) {
        std::printf ("usage: %s <path-to.backend> <cache-dir>\n", argv[0]);
        return 2;
    }
    ICpuBackend *pBackend = LoadBackendBundle (argv[1]);
    if (!pBackend) { std::printf ("failed to load bundle '%s'\n", argv[1]); return 2; }
    int Result = RunCacheDemo (pBackend, argv[2]);
    pBackend->Release ();
    return Result;
}
