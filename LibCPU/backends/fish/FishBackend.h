/** @file
  fish shell backend for LibCPU: emit a fish script and run it on fish. The guest
  RAM and register file are marshalled through a temp file -- read with od into the
  (1-indexed) integer array d, written back with printf.

  fish's "math" has no bitwise operators, so bitwise AND/OR/XOR are synthesized
  from arithmetic (bit-by-bit), masks use modulo, and shifts use powers of two.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_FISHBACKEND_H
#define LIBCPU_FISHBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateFishBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_FISHBACKEND_H
