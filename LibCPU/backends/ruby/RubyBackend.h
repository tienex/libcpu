/** @file
  Ruby backend for LibCPU: emit Ruby and run it on the ruby interpreter. The guest
  RAM and register file are marshalled through a temp file (the global byte array
  $d holds the RAM bytes then the CPU_STATE bytes).

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_RUBYBACKEND_H
#define LIBCPU_RUBYBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateRubyBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_RUBYBACKEND_H
