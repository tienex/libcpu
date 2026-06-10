#include "RunChip8.h"
#include "CPythonBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateCPythonBackend (); int r = RunChip8Program (b); b->Release (); return r; }
