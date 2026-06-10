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

    //
    // Self-modifying-code support (used by the AOT path). The host sets the
    // watched code region [CodeStart, CodeEnd); the store write-barrier sets
    // CodeDirty when a guest write lands inside it; a per-block guard, finding
    // CodeDirty set, records the block's address in TrapPc and returns ExecSmc so
    // the host can re-translate from there. CodeStart == CodeEnd keeps it inert
    // (no write is ever "in code"), so non-SMC runs are unaffected.
    //
    UINT64 CodeStart; // watched code region, low bound (inclusive)
    UINT64 CodeEnd;   // watched code region, high bound (exclusive)
    UINT64 TrapPc;    // block PC where a guard fired, else CPU_SMC_NO_TRAP
    UINT8  CodeDirty; // set by the store write-barrier
} CPU_STATE;

//
// Byte offsets of the fields within CPU_STATE (used by codegen backends that
// compute addresses into the raw GRF pointer).
//
#define CPU_STATE_REG_OFFSET       ((UINT32) 0)             // Reg[0]
#define CPU_STATE_FLAG_OFFSET      ((UINT32) (32 * 8))      // Flag[0]
#define CPU_STATE_PC_OFFSET        ((UINT32) (32 * 8 + 8))  // Pc
#define CPU_STATE_CODESTART_OFFSET ((UINT32) (32 * 8 + 16)) // CodeStart
#define CPU_STATE_CODEEND_OFFSET   ((UINT32) (32 * 8 + 24)) // CodeEnd
#define CPU_STATE_TRAPPC_OFFSET    ((UINT32) (32 * 8 + 32)) // TrapPc
#define CPU_STATE_CODEDIRTY_OFFSET ((UINT32) (32 * 8 + 40)) // CodeDirty

// Sentinel stored in TrapPc when no SMC guard has fired.
#define CPU_SMC_NO_TRAP            (~UINT64_C (0))

#endif // LIBCPU_CPUSTATE_H
