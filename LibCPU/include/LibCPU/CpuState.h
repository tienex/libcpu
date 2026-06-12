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
    UINT64 CodeStart;       // watched code region, low bound (inclusive)
    UINT64 CodeEnd;         // watched code region, high bound (exclusive)
    UINT64 TrapPc;          // block PC where a guard fired, else CPU_SMC_NO_TRAP
    UINT8  CodeDirty[32];   // per-page dirty bitmap, ONE BIT per page: for page p,
                            // bit (p & 7) of CodeDirty[p >> 3]; p = addr >> 8
    UINT64 DispPc;          // indirect-branch dispatch target (in-artifact scratch):
                            // an indirect branch writes its runtime target here, then
                            // the artifact's dispatcher routes to the matching block
    UINT64 EdgeCount[32];   // per-call-site execution counters: an instrumented
                            // (profiling) build bumps EdgeCount[i] each time the i-th
                            // CALL site runs, giving TRUE edge frequencies
    UINT64 RamSize;         // size of the guest RAM the host passes to Execute. A
                            // marshalling backend (cpython/jvm/clr) copies this many
                            // bytes; 0 means the legacy default (64 KiB). In-process
                            // backends address pRAM directly and ignore it.
    UINT64 SyscallVector;   // a pending guest system call (e.g. a DOS INT number): an
                            // INT/SVC instruction records its vector here and traps to
                            // TrapPc (the instruction after it); the host's knowledge-
                            // library dispatcher reads it, performs the native call, and
                            // resumes. CPU_NO_SYSCALL means none pending.
} CPU_STATE;

// Self-modifying-code page granularity: 256-byte pages over the 16-bit address
// space (256 pages), tracked as a bit-per-page bitmap (256 bits = 32 bytes). A
// write dirties only its own page's bit, so only blocks in that page re-translate.
#define CPU_SMC_PAGE_SHIFT  ((UINT32) 8)
#define CPU_SMC_PAGE_COUNT  ((UINT32) 256)
#define CPU_SMC_DIRTY_BYTES ((UINT32) 32)

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
#define CPU_STATE_DISPPC_OFFSET    ((UINT32) (32 * 8 + 72)) // DispPc (after CodeDirty[32])
#define CPU_STATE_EDGECOUNT_OFFSET ((UINT32) (32 * 8 + 80)) // EdgeCount[0] (after DispPc)
#define CPU_STATE_RAMSIZE_OFFSET   ((UINT32) (32 * 8 + 80 + 32 * 8)) // RamSize (after EdgeCount[32])
#define CPU_STATE_SYSCALL_OFFSET   ((UINT32) (32 * 8 + 88 + 32 * 8)) // SyscallVector (after RamSize)

// Number of per-call-site edge counters (profiling instrumentation).
#define CPU_PROFILE_SLOTS          ((UINT32) 32)

// Default guest RAM size when CPU_STATE.RamSize is 0 (the legacy 16-bit space).
#define CPU_RAM_DEFAULT            ((UINT64) 0x10000)

// Sentinel stored in TrapPc when no SMC guard has fired.
#define CPU_SMC_NO_TRAP            (~UINT64_C (0))

// Sentinel stored in SyscallVector when no guest system call is pending.
#define CPU_NO_SYSCALL            (~UINT64_C (0))

#endif // LIBCPU_CPUSTATE_H
