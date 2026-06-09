/** @file
  libgccjit backend for LibCPU. Implements ICpuEmitter by building libgccjit
  rvalues/blocks and ICpuCode by compiling + running them. IR-builder pattern
  (like the LLVM backend); same interfaces, so any frontend runs through it.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_GCCJITBACKEND_H
#define LIBCPU_GCCJITBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

//
// Create the libgccjit backend. Returned reference is owned (Release when done).
//
ICpuBackend *CreateGccJitBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_GCCJITBACKEND_H
