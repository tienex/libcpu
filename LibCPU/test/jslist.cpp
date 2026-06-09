/** @file  Query the js backend (ICpuJsEngines) and list discovered engines. */
#include "JsBackend.h"
#include "JsEngines.h"
#include <cstdio>
using namespace LibCPU;
int main (void) {
    ICpuBackend *pB = CreateJsBackend ();
    ICpuJsEngines *pE = nullptr;
    if (FAILED (pB->QueryInterface (IID_ICpuJsEngines, (void **) &pE))) { std::printf ("no ICpuJsEngines\n"); return 1; }
    UINT32 N = pE->GetEngineCount ();
    std::printf ("discovered %u JS engines:\n", N);
    for (UINT32 I = 0; I < N; I++) {
        JS_ENGINE_INFO Info; pE->GetEngineInfo (I, &Info);
        std::printf ("  [%u] %-6s %-13s %s\n", I, Info.Name, Info.Family, Info.Path);
    }
    std::printf ("default selected: %u\n", pE->GetSelectedEngine ());
    pE->Release (); pB->Release (); return 0;
}
