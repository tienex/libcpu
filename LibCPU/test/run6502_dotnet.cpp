#include "Run6502.h"
#include "DotnetBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateDotnetBackend (); int r = Run6502Program (b); b->Release (); return r; }
