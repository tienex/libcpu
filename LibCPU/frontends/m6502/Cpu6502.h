/** @file
  MOS 6502 frontend for LibCPU (initial slice).

  Implements ICpuArchitecture by emitting instruction semantics purely through
  ICpuEmitter -- it references no code generator. The same frontend therefore
  runs on the interpreter backend or any LLVM backend unchanged.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_CPU6502_H
#define LIBCPU_CPU6502_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

//
// Register indices the 6502 frontend uses (map onto the backend's register file).
//
enum {
    Reg6502A = 0,   // accumulator
    Reg6502X = 1,
    Reg6502Y = 2,
    Reg6502S = 3    // stack pointer
};

//
// Create the 6502 frontend. Returned reference is owned (Release when done).
//
ICpuArchitecture *Create6502 (VOID);

} // namespace LibCPU

#endif // LIBCPU_CPU6502_H
