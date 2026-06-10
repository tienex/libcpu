/** @file
  Z shell backend for LibCPU: emit a zsh script and run it on zsh. The guest RAM
  and register file are marshalled through a temp file -- read with od into the
  (1-indexed) integer array d, written back with printf.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_ZSHBACKEND_H
#define LIBCPU_ZSHBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateZshBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_ZSHBACKEND_H
