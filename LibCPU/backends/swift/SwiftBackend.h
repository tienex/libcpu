/** @file
  Swift backend for LibCPU: emit Swift and run it on the swift toolchain. The
  guest RAM and register file are marshalled through a temp file (the Foundation
  Data buffer d holds the RAM bytes then the CPU_STATE bytes).

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_SWIFTBACKEND_H
#define LIBCPU_SWIFTBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateSwiftBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_SWIFTBACKEND_H
