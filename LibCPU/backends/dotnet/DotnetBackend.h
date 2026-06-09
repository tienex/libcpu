/** @file
  .NET backend for LibCPU: emit C#, compile to MSIL with Roslyn (csc), and run it
  on the CLR (mono/dotnet). The guest RAM and register file are marshalled through
  a temp file.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_DOTNETBACKEND_H
#define LIBCPU_DOTNETBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateDotnetBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_DOTNETBACKEND_H
