#include "RunChip8.h"
#include "ClrBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateClrBackend (); int r = RunChip8Program (b); b->Release (); return r; }
