#include "Run6502.h"
#include "JvmBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateJvmBackend (); int r = Run6502Program (b); b->Release (); return r; }
