#include "Run6502.h"
#include "LightningBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateLightningBackend (); int r = Run6502Program (b); b->Release (); return r; }
