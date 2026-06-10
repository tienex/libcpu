#include "RunChip8.h"
#include "TcshBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateTcshBackend (); int r = RunChip8Program (b); b->Release (); return r; }
