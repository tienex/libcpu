#include "RunChip8.h"
#include "JvmBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateJvmBackend (); int r = RunChip8Program (b); b->Release (); return r; }
