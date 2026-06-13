/** @file
  Native AOT: compile a guest program to a STANDALONE host executable.

  The ordinary AOT path produces an artifact that runs inside LibCPU, operating on a
  CPU_STATE blob in memory (registers accessed by offset). Native AOT instead emits a
  self-contained C translation unit -- guest registers become C LOCALS, so the host C
  compiler's register allocator maps them onto real host machine registers, and the
  guest RAM is a static array initialised with the program image. Compiled by the host
  cc with -O2, the result is an ordinary executable that runs with NO dependency on
  LibCPU at all.

  Control flow: each reachable guest instruction is a C label; fall-through and
  branches are goto; a CALL pushes and jumps to the callee, a RET pops and jumps
  through a dispatch switch back to the return label. Instructions that need a host
  (far jump, INT/syscall, port I/O) are not representable in a standalone binary and
  terminate the program -- native AOT targets self-contained computation.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_NATIVEAOT_H
#define LIBCPU_NATIVEAOT_H

#include "LibCPU/ICpu.h"
#include <string>

namespace LibCPU {

typedef struct _LC_NATIVE_OPTIONS {
    bool     DumpResult;     // emit code to print a 16-bit memory word at exit
    CPU_ADDR ResultAddr;     // the word's guest address
} LC_NATIVE_OPTIONS;

// Generate a standalone C program translating [Entry, End) of pArch's code memory.
// pImage/ImageLen are the guest bytes embedded into the executable. Returns the C
// source, or an empty string on failure.
std::string GenerateNativeC (ICpuArchitecture *pArch, UINT8 CONST *pImage, UINT32 ImageLen,
                               CPU_ADDR Entry, CPU_ADDR End, LC_NATIVE_OPTIONS CONST &Opt);

// Compile C source to a standalone native executable at pOutExe using the host C
// compiler ($CC, else "cc"). Returns false and fills *pError on failure.
bool CompileNative (std::string CONST &Source, CHAR8 CONST *pOutExe, std::string *pError);

} // namespace LibCPU

#endif // LIBCPU_NATIVEAOT_H
