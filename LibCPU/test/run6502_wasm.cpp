#include "Run6502.h"
#include "WasmBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateWasmBackend (); int r = Run6502Program (b); b->Release (); return r; }
