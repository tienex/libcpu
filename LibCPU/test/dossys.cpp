/** @file  Knowledge-library demo: a DOS program's INT 21h calls become host libc calls. */
#include "RunDosSyscall.h"
#include "LibCPU/Loader.h"
#include <cstdio>
using namespace LibCPU;
int main (int argc, char **argv) {
    if (argc < 3) {
        std::printf ("usage: %s <path-to.backend> <knowledge.xml>\n", argv[0]);
        return 2;
    }
    ICpuBackend *pBackend = LoadBackendBundle (argv[1]);
    if (!pBackend) { std::printf ("failed to load bundle '%s'\n", argv[1]); return 2; }
    int Result = RunDosSyscallDemo (pBackend, argv[2]);
    pBackend->Release ();
    return Result;
}
