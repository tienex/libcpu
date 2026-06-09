/** @file
  QBE backend for LibCPU: textual-emit AOT via QBE's SSA IL. ICpuEmitter emits
  QBE IL; ICpuCode runs `qbe` to produce assembly, assembles+links it with the
  system compiler into a shared object, and dlopen()s it.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_QBEBACKEND_H
#define LIBCPU_QBEBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateQbeBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_QBEBACKEND_H
