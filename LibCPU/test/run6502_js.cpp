#include "Run6502.h"
#include "JsBackend.h"
using namespace LibCPU;
int main (void) { ICpuBackend *b = CreateJsBackend (); int r = Run6502Program (b); b->Release (); return r; }
