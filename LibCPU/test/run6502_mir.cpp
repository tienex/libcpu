/** @file  Run the 6502 program on the MIR backend (any-to-any). */
#include "Run6502.h"
#include "MirBackend.h"
using namespace LibCPU;
int main (void) {
    ICpuBackend *pBackend = CreateMirBackend ();
    int Result = Run6502Program (pBackend);
    pBackend->Release ();
    return Result;
}
