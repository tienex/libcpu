/** @file
  PowerShell backend for LibCPU: emit a PowerShell script and run it on pwsh. The
  guest RAM and register file are marshalled through a temp file -- read and
  written as a byte array with [System.IO.File].

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_PWSHBACKEND_H
#define LIBCPU_PWSHBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreatePwshBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_PWSHBACKEND_H
