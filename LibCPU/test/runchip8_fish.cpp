#include "RunChip8.h"
#include "FishBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateFishBackend (); int r = RunChip8Program (b); b->Release (); return r; }
