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

    //
    // System-level emulation. A device-bus instruction (port IN/OUT) or a privileged
    // control instruction (IRET/HLT/STI/CLI) records its kind in IoCtrl and traps to
    // TrapPc; the host machine (System) reads IoCtrl/IoPort/IoData, drives the
    // emulated device or performs the control action, and resumes. IoCtrl == CPU_IO_NONE
    // means no system trap is pending (so a plain control-flow trap is unaffected).
    //
    UINT64 IoCtrl;          // CPU_IO_* reason in the low 8 bits, access width in the next 8
    UINT64 IoPort;          // I/O port (for OUT/IN)
    UINT64 IoData;          // data written (OUT); the host writes the read value back to AL/AX (IN)

    // A monotonically increasing measure of executed work (interpreter: one per micro-op).
    // It is the machine's time base: the host feeds it to time-driven peripherals (e.g. the
    // 8253 PIT) so a counter latched twice -- as a BIOS timer-calibration loop does -- shows a
    // delta proportional to the work between the latches, instead of a fixed per-read step. Last
    // field of the struct so the codegen field offsets above stay stable; not used by the JITs.
    UINT64 Cycles;
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
#define CPU_STATE_IOCTRL_OFFSET    ((UINT32) (32 * 8 + 96 + 32 * 8)) // IoCtrl (after SyscallVector)
#define CPU_STATE_IOPORT_OFFSET    ((UINT32) (32 * 8 + 104 + 32 * 8))// IoPort
#define CPU_STATE_IODATA_OFFSET    ((UINT32) (32 * 8 + 112 + 32 * 8))// IoData
#define CPU_STATE_CYCLES_OFFSET    ((UINT32) (32 * 8 + 120 + 32 * 8))// Cycles (after IoData)

// Number of per-call-site edge counters (profiling instrumentation).
#define CPU_PROFILE_SLOTS          ((UINT32) 32)

// Default guest RAM size when CPU_STATE.RamSize is 0 (the legacy 16-bit space).
#define CPU_RAM_DEFAULT            ((UINT64) 0x10000)

// Sentinel stored in TrapPc when no SMC guard has fired.
#define CPU_SMC_NO_TRAP            (~UINT64_C (0))

// Sentinel stored in SyscallVector when no guest system call is pending.
#define CPU_NO_SYSCALL            (~UINT64_C (0))

// CPU_STATE.IoCtrl reason codes (low byte). The access width (8/16) is the next byte.
//
// These are the GENERIC, CPU-neutral traps a machine understands: port I/O, halt, and the
// interrupt-enable controls. Anything CPU-architecture-specific (an x86 IRET/PUSHF/POPF/INTO/
// BOUND/INS/OUTS, a different CPU's privileged ops, ...) is NOT defined here: it traps with a
// reason at or above CPU_IO_ARCH_BASE, which the generic machine forwards verbatim (with IoData)
// to its installed CPU "personality" (e.g. X86Trap.h / X86System) without interpreting it.
#define CPU_IO_NONE               ((UINT64) 0)   // no system trap pending
#define CPU_IO_OUT                ((UINT64) 1)   // port write: IoPort, IoData
#define CPU_IO_IN                 ((UINT64) 2)   // port read: IoPort; host writes the value back
#define CPU_IO_HLT                ((UINT64) 4)   // halt until the next interrupt
#define CPU_IO_STI                ((UINT64) 5)   // enable interrupts
#define CPU_IO_CLI                ((UINT64) 6)   // disable interrupts
#define CPU_IO_ARCH_BASE          ((UINT64) 0x80) // reasons >= this are CPU-personality-defined (opaque here)
#define CPU_IO_REASON(c)          ((UINT32) ((c) & 0xFF))
#define CPU_IO_WIDTH(c)           ((UINT32) (((c) >> 8) & 0xFF))
#define CPU_IO_MAKE(reason, w)    (((UINT64) (reason)) | (((UINT64) (w)) << 8))

#endif // LIBCPU_CPUSTATE_H
