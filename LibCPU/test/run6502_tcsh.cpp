#include "Run6502.h"
#include "TcshBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateTcshBackend (); int r = Run6502Program (b); b->Release (); return r; }
