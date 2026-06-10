/** @file
  Bash backend for LibCPU: emit a bash script and run it on bash. The guest RAM
  and register file are marshalled through a temp file -- read with od into an
  integer array, written back with printf.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_BASHBACKEND_H
#define LIBCPU_BASHBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateBashBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_BASHBACKEND_H
