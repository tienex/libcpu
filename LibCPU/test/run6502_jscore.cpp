#include "Run6502.h"
#include "JscoreBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateJscoreBackend (); int r = Run6502Program (b); b->Release (); return r; }
