/** @file  Query the cc backend (ICpuCcCompilers) and list discovered compilers. */
#include "CcBackend.h"
#include "CcCompilers.h"
#include <cstdio>
using namespace LibCPU;
int main (void) {
    ICpuBackend *pB = CreateCcBackend ();
    ICpuCcCompilers *pCC = nullptr;
    if (FAILED (pB->QueryInterface (IID_ICpuCcCompilers, (void **) &pCC))) {
        std::printf ("cc backend does not expose ICpuCcCompilers\n"); return 1;
    }
    CHAR8 CONST *Fam[] = {"unknown","clang","gcc","msvc","watcom","borland",
                          "intel","metrowerks","digitalmars","ibmxl","generic"};
    UINT32 N = pCC->GetCompilerCount ();
    std::printf ("discovered %u compilers:\n", N);
    for (UINT32 I = 0; I < N; I++) {
        CC_COMPILER_INFO Info;
        pCC->GetCompilerInfo (I, &Info);
        std::printf ("  [%2u] %-22s fam=%-11s host=%s target=%-22s %s\n",
                     I, Info.Name, Fam[Info.Family], Info.UsableForHost ? "yes" : "no ",
                     Info.Target[0] ? Info.Target : "-", Info.Version);
    }
    std::printf ("default selected index: %u\n", pCC->GetSelectedCompiler ());
    pCC->Release ();
    pB->Release ();
    return 0;
}
