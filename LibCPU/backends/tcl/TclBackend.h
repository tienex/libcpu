/** @file
  Tcl backend for LibCPU: emit Tcl and run it on tclsh. The guest RAM and register
  file are marshalled through a temp file (the byte list B holds the RAM bytes then
  the CPU_STATE bytes).

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_TCLBACKEND_H
#define LIBCPU_TCLBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateTclBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_TCLBACKEND_H
