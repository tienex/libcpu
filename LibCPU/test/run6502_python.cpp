#include "Run6502.h"
#include "PythonBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreatePythonBackend (); int r = Run6502Program (b); b->Release (); return r; }
