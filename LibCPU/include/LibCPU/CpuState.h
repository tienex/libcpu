/** @file
  The default LibCPU runtime register-file layout shared by simple backends.

  A frontend addresses registers by index and flags by the generic CPU_FLAG
  enum; CPU_STATE is the concrete storage those map onto, passed as pGRF to
  ICpuCode::Execute. Both the interpreter backend and the LLVM backend operate
  on this identical layout, so a guest can be run by either, interchangeably.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_CPUSTATE_H
#define LIBCPU_CPUSTATE_H

#include "LibCPU/Base.h"

typedef struct _CPU_STATE {
    UINT64 Reg[32];   // general registers, addressed by index
    UINT8  Flag[8];   // condition flags, addressed by CPU_FLAG
    UINT64 Pc;        // program counter
} CPU_STATE;

//
// Byte offsets of the fields within CPU_STATE (used by codegen backends that
// compute addresses into the raw GRF pointer).
//
#define CPU_STATE_REG_OFFSET   ((UINT32) 0)            // Reg[0]
#define CPU_STATE_FLAG_OFFSET  ((UINT32) (32 * 8))     // Flag[0]
#define CPU_STATE_PC_OFFSET    ((UINT32) (32 * 8 + 8)) // Pc

#endif // LIBCPU_CPUSTATE_H
