#include "Run6502.h"
#include "AsmjitBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateAsmjitBackend (); int r = Run6502Program (b); b->Release (); return r; }
