/** @file
  Interpreter backend for LibCPU.

  This backend implements ICpuEmitter by recording the emitted operations into a
  small linear IR, and ICpuCode by interpreting that IR against the guest RAM and
  register file. It links no code generator at all -- its existence proves the
  frontend<->backend boundary truly decouples frontends from LLVM.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_INTERP_H
#define LIBCPU_INTERP_H

#include "LibCPU/ICpu.h"
#include "LibCPU/CpuState.h"

namespace LibCPU {

//
// The interpreter executes against the shared CPU_STATE layout (CpuState.h);
// INTERP_STATE is kept as an alias for existing callers.
//
typedef CPU_STATE INTERP_STATE;

//
// Create the interpreter backend. Returned reference is owned (Release when done).
//
ICpuBackend *CreateInterpreterBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_INTERP_H
