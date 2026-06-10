#include "Run6502.h"
#include "CmdBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateCmdBackend (); int r = Run6502Program (b); b->Release (); return r; }
