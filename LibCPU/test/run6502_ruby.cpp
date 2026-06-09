#include "Run6502.h"
#include "RubyBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateRubyBackend (); int r = Run6502Program (b); b->Release (); return r; }
