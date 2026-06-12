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

//
// Like GenerateAot, but builds a real control-flow graph: it discovers every
// reachable instruction address (following TagInstr's branch/fall-through edges),
// gives each its own ICpuBlock, and wires them with Branch / CondBranch (the latter
// driven by TranslateCond). The backend's optimizer then sees the whole CFG. This
// requires a backend that implements the ICpuEmitter block ops (e.g. LLVM); on a
// backend that stubs them it returns the stub's error.
//
HRESULT GenerateAotCfg (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                        CPU_ADDR Entry, CPU_ADDR End,
                        OUT ICpuCode **ppCode, OUT UINT32 *pInstrCount);

//
// A static call edge discovered in a region: a CALL at Site to Callee, returning to
// ReturnPoint (the instruction after the CALL). The call graph is static because
// CALL targets are known at translate time; an edge/call-count trace weights these
// by how often each caller ran (see PerfTrace / ProfiledAot).
//
typedef struct _CPU_CALL_EDGE {
    CPU_ADDR Site;
    CPU_ADDR Callee;
    CPU_ADDR ReturnPoint;
} CPU_CALL_EDGE;

// Collect the static call edges reachable in [Entry, End). Writes up to MaxEdges
// into pEdges and returns the total found.
UINT32 CollectCallEdges (ICpuArchitecture *pArch, CPU_ADDR Entry, CPU_ADDR End,
                         OUT CPU_CALL_EDGE *pEdges, UINT32 MaxEdges);

//
// One inlined callee for GenerateAotCfgInlined: the callee body [Callee, CalleeEnd)
// is embedded at the call site -- the CALL elides its return-address push and falls
// straight into the callee, and the callee's RET branches directly to ReturnPoint
// (no pop, no dispatcher). Valid for a single-call-site leaf callee.
//
typedef struct _CPU_INLINE_SITE {
    CPU_ADDR Callee;
    CPU_ADDR CalleeEnd;
    CPU_ADDR ReturnPoint;
} CPU_INLINE_SITE;

// Like GenerateAotCfg, but inlines every callee named in pInline (profile-guided).
HRESULT GenerateAotCfgInlined (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                               CPU_ADDR Entry, CPU_ADDR End,
                               CPU_INLINE_SITE CONST *pInline, UINT32 InlineCount,
                               OUT ICpuCode **ppCode, OUT UINT32 *pInstrCount);

} // namespace LibCPU

#endif // LIBCPU_AOTGENERATOR_H
