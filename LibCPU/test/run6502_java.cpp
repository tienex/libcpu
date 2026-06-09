#include "Run6502.h"
#include "JavaBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateJavaBackend (); int r = Run6502Program (b); b->Release (); return r; }
