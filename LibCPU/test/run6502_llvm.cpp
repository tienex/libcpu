/** @file
  Run the 6502 program on the LLVM JIT backend -- same frontend, different
  backend (any-to-any).
**/
#include "Run6502.h"
#include "LlvmBackend.h"

using namespace LibCPU;

int main (void) {
    ICpuBackend *pBackend = CreateLlvmBackend ();
    int Result = Run6502Program (pBackend);
    pBackend->Release ();
    return Result;
}
