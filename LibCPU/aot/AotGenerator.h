/** @file
  Ahead-of-time translation driver for LibCPU.

  AOT is not a separate code generator: it reuses the same translation architecture
  as the JIT path (ICpuArchitecture -> ICpuEmitter -> ICpuBackend). The only
  difference is granularity. The JIT translates and runs one instruction at a time,
  so each backend compile sees a single instruction. The AOT driver instead feeds
  an ENTIRE program into ONE emitter and calls Compile ONCE, so the chosen backend
  -- LLVM, GCCJIT, cc, Java, CLR, ... -- and its optimizer see the whole program
  together and can optimize across instruction boundaries (fold constants, promote
  register-file slots to SSA values, drop dead flag updates, ...). The backend (and
  thus the artifact: native code, C, JVM bytecode, CIL) is a free choice, exactly
  as it is for the JIT.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_AOTGENERATOR_H
#define LIBCPU_AOTGENERATOR_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

//
// Translate the whole program in [Entry, End) through the given backend into a
// single ICpuCode (one compile of the entire program). Returns S_OK and sets
// *ppCode on success. *pInstrCount, if non-null, receives the number of
// instructions folded into the one artifact.
//
HRESULT GenerateAot (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                     CPU_ADDR Entry, CPU_ADDR End,
                     OUT ICpuCode **ppCode, OUT UINT32 *pInstrCount);

} // namespace LibCPU

#endif // LIBCPU_AOTGENERATOR_H
