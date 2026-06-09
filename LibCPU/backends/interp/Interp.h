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

namespace LibCPU {

//
// Register-file storage the interpreter executes against. A frontend addresses
// registers by index (0..) and flags by the generic CPU_FLAG enum; this struct
// is the concrete storage those indices and flags map onto. (Passed as pGRF to
// ICpuCode::Execute.)
//
typedef struct _INTERP_STATE {
    UINT64 Reg[32];
    UINT8  Flag[8];
    UINT64 Pc;
} INTERP_STATE;

//
// Create the interpreter backend. Returned reference is owned (Release when done).
//
ICpuBackend *CreateInterpreterBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_INTERP_H
