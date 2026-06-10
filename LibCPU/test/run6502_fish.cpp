#include "Run6502.h"
#include "FishBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateFishBackend (); int r = Run6502Program (b); b->Release (); return r; }
