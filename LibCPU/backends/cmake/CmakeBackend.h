/** @file
  CMake textual-emit backend for LibCPU -- because CMake's script language is
  Turing-complete (math(EXPR) with bitwise ops, if/while/function, lists, file I/O).
  Emits a .cmake script and runs it with `cmake -P`; the guest RAM + register file are
  marshalled through a temp file (read with file(READ ... HEX), written back as a hex
  text file the host converts to bytes). Like the bash/cmd backends, it implements only
  the straight-line emitter -- the shadow CFG supplies control flow and the machine traps.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_CMAKEBACKEND_H
#define LIBCPU_CMAKEBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateCmakeBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_CMAKEBACKEND_H
