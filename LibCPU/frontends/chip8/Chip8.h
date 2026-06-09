/** @file
  CHIP-8 frontend for LibCPU (ALU/load/store slice).

  A second frontend, written purely against ICpuEmitter -- it runs on every
  backend unchanged, demonstrating the any-frontend x any-backend matrix.

  Register mapping onto the backend register file:
    V0..VF -> Reg[0..15]   (8-bit; VF is the flag register)
    I      -> Reg[16]      (16-bit index)

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_CHIP8_H
#define LIBCPU_CHIP8_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

enum { Chip8RegI = 16, Chip8RegVF = 15 };

ICpuArchitecture *CreateChip8 (VOID);

} // namespace LibCPU

#endif // LIBCPU_CHIP8_H
