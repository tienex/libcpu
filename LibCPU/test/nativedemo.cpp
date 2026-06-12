/** @file  Native-AOT demo: a guest program becomes a standalone host executable. */
#include "RunNative.h"
#include <cstdio>
using namespace LibCPU;
int main (int argc, char **argv) {
    CHAR8 CONST *pOut = (argc > 1) ? argv[1] : "/tmp/lcx_native_demo";
    return RunNativeAotDemo (pOut);
}
