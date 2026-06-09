/** @file
  MIR backend for LibCPU (Vladimir Makarov's MIR, MIT). Implements ICpuEmitter by
  building MIR's 3-address register IR and ICpuCode by JIT-compiling it (MIR_gen).
  IR-builder pattern; same interfaces, so any frontend runs through it.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_MIRBACKEND_H
#define LIBCPU_MIRBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateMirBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_MIRBACKEND_H
