/** @file
  Intel 8080 / 8085 frontend for LibCPU.

  An 8-bit CPU with a flat 16-bit address space and fixed-length (1/2/3-byte) opcodes -- no
  ModR/M. The seven 8-bit registers A,B,C,D,E,H,L plus the 16-bit stack pointer map onto the
  backend register file; the flag bits S,Z,P,CY map onto the generic condition flags. The NEC
  V20/V30 can execute this instruction set in their 8080 emulation mode, so this frontend also
  serves that mode.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_CPUI8080_H
#define LIBCPU_CPUI8080_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

//
// Register file indices. A,B,C,D,E,H,L are 8-bit; SP is the 16-bit stack pointer. (The 8080
// opcode register field uses a different order -- B,C,D,E,H,L,M,A -- which the frontend maps
// onto these indices; M means the byte at [HL].)
//
enum {
    RegI8080A = 0, RegI8080B = 1, RegI8080C = 2, RegI8080D = 3,
    RegI8080E = 4, RegI8080H = 5, RegI8080L = 6, RegI8080SP = 7
};

// Create the 8080/8085 frontend. Returned reference is owned (Release when done).
ICpuArchitecture *CreateI8080 (VOID);

} // namespace LibCPU

#endif // LIBCPU_CPUI8080_H
