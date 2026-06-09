/** @file
  AsmJit backend for LibCPU: an in-process JIT using AsmJit's a64 Compiler
  (virtual registers + register allocation). Emits AArch64 machine code directly.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_ASMJITBACKEND_H
#define LIBCPU_ASMJITBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateAsmjitBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_ASMJITBACKEND_H
