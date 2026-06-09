#include "Run6502.h"
#include "SwiftBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateSwiftBackend (); int r = Run6502Program (b); b->Release (); return r; }
