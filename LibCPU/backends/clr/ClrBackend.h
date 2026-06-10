/** @file
  In-process CLR (.NET) backend for LibCPU: generate raw CIL by hand and JIT it in
  process through Reflection.Emit (DynamicMethod) on an embedded CoreCLR runtime --
  no csc, no source compilation per instruction. A tiny managed helper (compiled
  once as infrastructure) calls DynamicILInfo.SetCode on the emitted IL; the host
  reaches it through the nethost/hostfxr hosting API.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_CLRBACKEND_H
#define LIBCPU_CLRBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateClrBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_CLRBACKEND_H
