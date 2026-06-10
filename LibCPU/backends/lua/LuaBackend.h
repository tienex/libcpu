/** @file
  Lua backend for LibCPU: emit Lua and run it on the lua interpreter. The guest
  RAM and register file are marshalled through a temp file (the 1-indexed table d
  holds the RAM bytes then the CPU_STATE bytes).

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_LUABACKEND_H
#define LIBCPU_LUABACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateLuaBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_LUABACKEND_H
