#include "Run6502.h"
#include "CPythonBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateCPythonBackend (); int r = Run6502Program (b); b->Release (); return r; }
