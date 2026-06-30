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
// Pic: when TRUE, emit a POSITION-INDEPENDENT unit (code addresses materialized relative to a runtime
// CodeBase, see ICpuSegmentedCode::SetPicTranslation) so the artifact is valid at any load address and
// can be cached/persisted by code content. The default (FALSE) emits absolute addresses.
HRESULT GenerateAotCfg (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                        CPU_ADDR Entry, CPU_ADDR End,
                        OUT ICpuCode **ppCode, OUT UINT32 *pInstrCount, BOOLEAN Pic = FALSE);

//
// Translate the single instruction at Pc for use by the $exec / XCT run-loop handler.
// Arranges for CPU_STATE.DispPc to hold the resolved successor address on normal exit:
//
//   TagConditional (skip): DispPc = NewPc (skip target) if condition TRUE,
//                                   NextPc (fall-through) if condition FALSE.
//   TagBranch      (JMP):  DispPc = NewPc (static jump target).
//   TagContinue    (fall): DispPc = NextPc.
//   TagTrap (IOT/HLT/indirect): TrapPc and SyscallVector are set by the Syscall /
//                               IndirectBranch path; DispPc is not meaningful.
//
// Requires the backend to expose ICpuSmcEmitter (SetDispatchTarget). Returns E_NOTIMPL
// when the backend does not support it; the caller should fall back to GenerateAotCfg.
//
HRESULT GenerateAotExecOne (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                            CPU_ADDR Pc, OUT ICpuCode **ppCode);

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

// Collect the static call edges reachable in [Entry, End), descending through callees
// (so nested calls are included). Writes up to MaxEdges and returns the total found.
UINT32 CollectCallEdges (ICpuArchitecture *pArch, CPU_ADDR Entry, CPU_ADDR End,
                         OUT CPU_CALL_EDGE *pEdges, UINT32 MaxEdges);

// Like CollectCallEdges, but does NOT descend into callees -- it reports only the
// caller's TOP-LEVEL (direct) call sites of [Entry, End). Profile-guided inlining
// uses this so it inlines top-level sites and lets the AOT driver recurse the rest.
UINT32 CollectDirectCallEdges (ICpuArchitecture *pArch, CPU_ADDR Entry, CPU_ADDR End,
                               OUT CPU_CALL_EDGE *pEdges, UINT32 MaxEdges);

//
// One inlined CALL site for GenerateAotCfgInlined: the CALL at Site to Callee is
// expanded by embedding a PRIVATE copy of the callee's body (duplicate-and-
// specialize) -- the CALL elides its return-address push and falls straight into
// that copy, and the copy's RET branches directly to ReturnPoint (no pop, no
// dispatcher). Listing the same Callee under several Sites gives each its own copy.
//
typedef struct _CPU_INLINE_SITE {
    CPU_ADDR Site;          // the CALL pc
    CPU_ADDR Callee;        // callee entry
    CPU_ADDR ReturnPoint;   // where this site's copy of the callee returns
} CPU_INLINE_SITE;

// Like GenerateAotCfg, but inlines every callee named in pInline (profile-guided).
HRESULT GenerateAotCfgInlined (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                               CPU_ADDR Entry, CPU_ADDR End,
                               CPU_INLINE_SITE CONST *pInline, UINT32 InlineCount,
                               OUT ICpuCode **ppCode, OUT UINT32 *pInstrCount, BOOLEAN Pic = FALSE);

// Build an INSTRUMENTED CFG for edge profiling: each CALL block bumps
// CPU_STATE.EdgeCount[i] (i = the call site's discovery order). The discovered call
// sites are written to pSites (slot i -> that site) and *pSiteCount. The host runs
// the build, then reads EdgeCount[i] for the TRUE frequency of edge pSites[i].
// Requires a backend exposing ICpuProfileEmitter (returns E_NOTIMPL otherwise).
HRESULT GenerateAotCfgProfiling (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                                 CPU_ADDR Entry, CPU_ADDR End,
                                 OUT ICpuCode **ppCode,
                                 OUT CPU_CALL_EDGE *pSites, UINT32 MaxSites, OUT UINT32 *pSiteCount);

} // namespace LibCPU

#endif // LIBCPU_AOTGENERATOR_H
