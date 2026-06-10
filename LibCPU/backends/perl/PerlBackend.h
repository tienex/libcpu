/** @file
  Perl backend for LibCPU: emit Perl and run it on the perl interpreter. The guest
  RAM and register file are marshalled through a temp file (the array @d holds the
  RAM bytes then the CPU_STATE bytes).

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_PERLBACKEND_H
#define LIBCPU_PERLBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreatePerlBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_PERLBACKEND_H
