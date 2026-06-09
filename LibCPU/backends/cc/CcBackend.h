/** @file
  "cc" backend for LibCPU: the textual-emit mechanism. ICpuEmitter emits C source
  for the translation unit; ICpuCode compiles it with the system C compiler into a
  shared object and dlopen()s it. (libtcc is the in-memory variant of this same
  mechanism, on arches its backend supports.)

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_CCBACKEND_H
#define LIBCPU_CCBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateCcBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_CCBACKEND_H
