/** @file
  x86/8086 system-trap reasons -- the CPU "personality" contract for the x86 family.

  The generic machine (System) understands only CPU-neutral traps (port I/O, halt, interrupt
  enable; see CpuState.h's CPU_IO_*). Everything specific to the x86 real-mode model -- the FLAGS
  layout, the interrupt-vector table, the IRET stack frame, the BCD/string privileged ops -- is a
  trap whose reason lives in the architecture range (>= CPU_IO_ARCH_BASE). To keep these out of the
  generic CPU_IO_ space (and out of System), they are scoped in their own x86 namespace but keep the
  familiar CPU_IO_ names: LibCPU::X86::CPU_IO_IRET, ::CPU_IO_PUSHF, ... The generic machine forwards
  such a trap, with its IoData payload, to the installed x86 personality (InstallX86System); System
  itself contains no x86 knowledge.

  (The x86 CPU frontend, being an x86 CPU, may reference these directly; they are emitted via
  ICpuSystemEmitter::EmitSystemTrap[Value].)

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_X86TRAP_H
#define LIBCPU_X86TRAP_H

#include "LibCPU/CpuState.h"

namespace LibCPU {
namespace X86 {

// x86 privileged/system operations, carried as CPU_IO reasons in the architecture range. Scoped
// under LibCPU::X86 so the generic CPU_IO_ space stays CPU-neutral.
enum {
    CPU_IO_IRET  = (int) CPU_IO_ARCH_BASE + 0,   // pop IP:CS:FLAGS, resume (restores IF)
    CPU_IO_PUSHF = (int) CPU_IO_ARCH_BASE + 1,   // push FLAGS (host supplies the IF bit)
    CPU_IO_POPF  = (int) CPU_IO_ARCH_BASE + 2,   // pop FLAGS (host restores the IF bit)
    CPU_IO_INTO  = (int) CPU_IO_ARCH_BASE + 3,   // INT 4 iff OF
    CPU_IO_BOUND = (int) CPU_IO_ARCH_BASE + 4,   // INT 5 iff IoData != 0 (index out of range)
    CPU_IO_INSB  = (int) CPU_IO_ARCH_BASE + 5,   // IN byte from DX -> [ES:DI]; advance DI
    CPU_IO_INSW  = (int) CPU_IO_ARCH_BASE + 6,   // IN word
    CPU_IO_OUTSB = (int) CPU_IO_ARCH_BASE + 7,   // OUT byte [DS:SI] -> DX; advance SI
    CPU_IO_OUTSW = (int) CPU_IO_ARCH_BASE + 8,   // OUT word
    // REP forms: the host repeats the transfer CX times (advancing SI/DI), then CX = 0. A single
    // emitted block cannot loop a trapping op, so the whole string move is done in the host.
    CPU_IO_REP_INSB  = (int) CPU_IO_ARCH_BASE + 9,
    CPU_IO_REP_INSW  = (int) CPU_IO_ARCH_BASE + 10,
    CPU_IO_REP_OUTSB = (int) CPU_IO_ARCH_BASE + 11,
    CPU_IO_REP_OUTSW = (int) CPU_IO_ARCH_BASE + 12
};

} // namespace X86
} // namespace LibCPU

#endif // LIBCPU_X86TRAP_H
