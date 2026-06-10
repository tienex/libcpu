/** @file
  Korn shell backend for LibCPU: emit a ksh script and run it on ksh. The guest
  RAM and register file are marshalled through a temp file -- read with od into the
  integer array d, written back with printf.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_KSHBACKEND_H
#define LIBCPU_KSHBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateKshBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_KSHBACKEND_H
