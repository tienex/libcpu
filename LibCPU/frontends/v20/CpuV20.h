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
    RegV20SP = 4, RegV20BP = 5, RegV20SI = 6, RegV20DI = 7
};

//
// Create the V20/V30 frontend. Returned reference is owned (Release when done).
//
ICpuArchitecture *CreateV20 (VOID);

} // namespace LibCPU

#endif // LIBCPU_CPUV20_H
