/** @file  Run the 6502 program on the libgccjit backend (any-to-any). */
#include "Run6502.h"
#include "GccJitBackend.h"
using namespace LibCPU;
int main (void) {
    ICpuBackend *pBackend = CreateGccJitBackend ();
    int Result = Run6502Program (pBackend);
    pBackend->Release ();
    return Result;
}
