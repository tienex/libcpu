/** @file
  SLJIT backend for LibCPU. Implements ICpuEmitter by lowering each opaque value
  onto a stack temp-slot and emitting SLJIT machine ops, and ICpuCode by running
  the generated native code. This is the register/assembler codegen pattern
  (contrast the LLVM IR-builder backend); it implements the same interfaces, so
  any frontend runs through it unchanged.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_SLJITBACKEND_H
#define LIBCPU_SLJITBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

//
// Create the SLJIT JIT backend. Returned reference is owned (Release when done).
//
ICpuBackend *CreateSljitBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_SLJITBACKEND_H
