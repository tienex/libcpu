#include "Run6502.h"
#include "QbeBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateQbeBackend (); int r = Run6502Program (b); b->Release (); return r; }
