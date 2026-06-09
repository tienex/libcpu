/** @file
  Run the 6502 program on the SLJIT backend -- same frontend, register-style
  codegen backend (any-to-any).
**/
#include "Run6502.h"
#include "SljitBackend.h"

using namespace LibCPU;

int main (void) {
    ICpuBackend *pBackend = CreateSljitBackend ();
    int Result = Run6502Program (pBackend);
    pBackend->Release ();
    return Result;
}
