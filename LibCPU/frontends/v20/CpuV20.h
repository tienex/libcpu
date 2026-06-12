/** @file
  NEC V20/V30 (uPD70108 / uPD70116) frontend for LibCPU (initial slice).

  The V20/V30 are 8086-software-compatible (the V20 has an 8-bit bus like the 8088,
  the V30 a 16-bit bus like the 8086; the instruction semantics are identical) plus
  a handful of NEC-only instructions under the 0F prefix. This slice implements a
  representative 16-bit 8086 subset -- enough real control flow (Jcc, JMP, LOOP) to
  exercise the backends' CFG path on an actual CPU -- plus the NEC bit instructions
  SET1/CLR1/NOT1/TEST1, which a stock 8086 does not have.

  Addressing is flat (the offset is used directly as the linear RAM address); only
  register-direct ModR/M (mod=11) and the direct-address MOV forms are modelled.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_CPUV20_H
#define LIBCPU_CPUV20_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

//
// 16-bit register indices, in 8086 encoding order (ModR/M reg/rm and B8+r). They
// map directly onto the backend register file.
//
enum {
    RegV20AX = 0, RegV20CX = 1, RegV20DX = 2, RegV20BX = 3,
    RegV20SP = 4, RegV20BP = 5, RegV20SI = 6, RegV20DI = 7,

    // Segment registers, in 8086 sreg-encoding order (ES,CS,SS,DS) at indices 8..11.
    // A memory access forms its 20-bit linear address as segment * 16 + offset; with
    // a segment register left at 0 the access is unsegmented (offset == linear).
    RegV20ES = 8, RegV20CS = 9, RegV20SS = 10, RegV20DS = 11
};

//
// Create the V20/V30 frontend. Returned reference is owned (Release when done).
// CodeSeg is the code segment (CS): instructions are fetched from CS * 16 + IP, so
// the AOT driver's program counter is the 16-bit IP offset within that segment.
// CodeSeg 0 (the no-argument form) is flat addressing. The host should also set the
// guest CS register (Reg[RegV20CS]) to the same value so MOV-from-CS reads it back.
//
ICpuArchitecture *CreateV20 (UINT16 CodeSeg);
ICpuArchitecture *CreateV20 (VOID);

// Re-point a V20 frontend's code segment (CS), used by the host's far-JMP/CALL resume
// loop after the guest reloads CS:IP.
VOID SetV20CodeSegment (ICpuArchitecture *pArch, UINT16 Cs);

} // namespace LibCPU

#endif // LIBCPU_CPUV20_H
