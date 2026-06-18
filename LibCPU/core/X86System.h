/** @file
  x86/8086 system personality.

  The generic machine (System) forwards every CPU-architecture-specific trap (reason >=
  CPU_IO_ARCH_BASE) to an installed handler. This is the x86 one: it implements the real-mode
  privileged/system operations -- IRET, PUSHF/POPF, INTO, BOUND, INS/OUTS (see X86Trap.h) -- working
  only through System's generic accessors (registers/flags via State(), physical memory, the
  interrupt-enable bit, vectored-interrupt raising, and the device-port bus). All x86 knowledge
  lives here, not in System.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_X86SYSTEM_H
#define LIBCPU_X86SYSTEM_H

#include "System.h"

namespace LibCPU {

// Install the x86 personality on a freshly created machine: routes the x86 system traps and
// hardware-interrupt delivery. Call once after constructing the System (and before Run) for any
// 8086/V20-class machine.
void InstallX86System (System &Sys);

// Seed a real-mode IVT entry (vector -> Seg:Off at physical 0). The x86 way to install an ISR
// before running; used by the test harness instead of the (now removed) System::SetIvt.
void X86SetIvt (System &Sys, UINT32 Vector, UINT16 Seg, UINT16 Off);

} // namespace LibCPU

#endif // LIBCPU_X86SYSTEM_H
