#include "Run6502.h"
#include "ClrBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateClrBackend (); int r = Run6502Program (b); b->Release (); return r; }
