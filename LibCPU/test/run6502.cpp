/** @file
  Run the 6502 program on the interpreter backend (no LLVM).
**/
#include "Run6502.h"
#include "Interp.h"

using namespace LibCPU;

int main (void) {
    ICpuBackend *pBackend = CreateInterpreterBackend ();
    int Result = Run6502Program (pBackend);
    pBackend->Release ();
    return Result;
}
