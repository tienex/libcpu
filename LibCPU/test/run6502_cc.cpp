/** @file  Run the 6502 program on the cc (emit-C) backend (any-to-any). */
#include "Run6502.h"
#include "CcBackend.h"
using namespace LibCPU;
int main (void) {
    ICpuBackend *pBackend = CreateCcBackend ();
    int Result = Run6502Program (pBackend);
    pBackend->Release ();
    return Result;
}
