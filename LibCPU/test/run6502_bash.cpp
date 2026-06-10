#include "Run6502.h"
#include "BashBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateBashBackend (); int r = Run6502Program (b); b->Release (); return r; }
