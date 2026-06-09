/** @file
  GNU Lightning backend for LibCPU: an in-process register/temp-slot JIT. Each
  ICpuValue is a stack slot; ops load operands into scratch GPRs, compute, and
  store back. jit_emit() produces a native function pointer.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_LIGHTNINGBACKEND_H
#define LIBCPU_LIGHTNINGBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateLightningBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_LIGHTNINGBACKEND_H
