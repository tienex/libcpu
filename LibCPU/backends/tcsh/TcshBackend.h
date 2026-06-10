/** @file
  C shell (tcsh) backend for LibCPU: emit a tcsh script and run it on tcsh. The
  guest RAM and register file are marshalled through a temp file -- read with od
  into the (1-indexed) integer array d, written back with printf.

  tcsh has no functions, so the accessors are inlined per operation; arithmetic
  uses tcsh's "@" expressions, which support bitwise and shift operators.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_TCSHBACKEND_H
#define LIBCPU_TCSHBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateTcshBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_TCSHBACKEND_H
