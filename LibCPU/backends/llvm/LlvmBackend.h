/** @file
  LLVM backend for LibCPU. Implements ICpuEmitter by building LLVM IR and
  ICpuCode by JIT-compiling it (ORC LLJIT). Targets recent LLVM (developed
  against 22) with LLVM_VERSION_MAJOR guards intended to span the installed range.

  It implements the SAME ICpuEmitter/ICpuBackend interfaces as the interpreter,
  so any frontend runs through it unchanged.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_LLVMBACKEND_H
#define LIBCPU_LLVMBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

//
// Create the LLVM JIT backend. Returned reference is owned (Release when done).
//
ICpuBackend *CreateLlvmBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_LLVMBACKEND_H
