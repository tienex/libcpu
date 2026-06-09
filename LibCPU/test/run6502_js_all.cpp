/** @file  Run the 6502 program on EVERY discovered JS engine. */
#include "Run6502.h"
#include "JsBackend.h"
#include "JsEngines.h"
#include <cstdio>
using namespace LibCPU;
int main (void) {
    ICpuBackend *pB = CreateJsBackend ();
    ICpuJsEngines *pE = nullptr;
    pB->QueryInterface (IID_ICpuJsEngines, (void **) &pE);
    UINT32 N = pE->GetEngineCount ();
    int Fails = 0;
    for (UINT32 I = 0; I < N; I++) {
        JS_ENGINE_INFO Info; pE->GetEngineInfo (I, &Info);
        pE->SelectEngine (I);
        std::printf ("\n=== engine: %s (%s) ===\n", Info.Name, Info.Family);
        if (Run6502Program (pB) != 0) Fails++;
    }
    pE->Release (); pB->Release ();
    std::printf ("\n%u engines, %d failures\n", N, Fails);
    return Fails;
}
