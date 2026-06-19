/** @file
  Shadow CFG -- universal control-flow + machine capabilities for ANY backend.

  PROBLEM. Driving a full machine (the M24 system emulation) needs more than a backend's
  per-instruction data ops: it needs a control-flow graph (basic blocks + branches +
  dispatch) and the machine "trap" capabilities -- port I/O (ICpuSystemEmitter), software
  interrupts/syscalls (ICpuSyscallEmitter), the cycle clock (ICpuClockEmitter), and the
  self-modifying-code / indirect-branch hooks (ICpuSmcEmitter). Implementing all of that
  natively in every backend is why only the register-assemblers (asmjit/sljit/lightning)
  and the IR JITs drive the machine today; the ~30 textual / managed / scripting backends
  implement only the straight-line ICpuEmitter and return E_NOTIMPL for Branch/CondBranch.

  INSIGHT. None of those features actually require new backend code. Every one of them is
  expressible as STRAIGHT-LINE data ops that write fields of CPU_STATE and then return to
  the host:

    - a conditional branch  =  next-PC = Select(cond, takenPc, fallPc); store PC; return
    - a port out            =  store IoCtrl/IoPort/IoData + resume-PC;       return
    - an INT / syscall       =  store the vector + resume-PC;                 return
    - an indirect branch/RET =  store the runtime target into PC;             return
    - the cycle clock        =  add N to the cycle counter (a plain add)
    - SMC                    =  the store already goes through guest memory; the host
                               checks dirty pages between blocks

  The host run loop (System) already dispatches block-by-block: translate [Pc, End), run,
  read the next Pc (or a trap), repeat. So the "CFG" is a host-side DISPATCH PROTOCOL, not a
  backend feature -- it can live ONCE here and be synthesized for any backend.

  DESIGN. ShadowBackend wraps an inner ICpuBackend and presents a fully machine-capable
  backend. Its emitter (ShadowEmitter) implements the FULL ICpuEmitter plus every machine
  capability, lowering each to the inner backend's minimal straight-line ops:

    - Data ops (ConstInt/Get-PutRegister/Load/Store/BinaryOp/UnaryOp/Compare/Cast/Flags)
      forward verbatim to the inner emitter -- the core every backend already implements.
    - Select, if the inner backend lacks it, is synthesized arithmetically
      (b + cond*(a-b) over the op set).
    - Branch/CondBranch and the trap capabilities terminate the unit: they store the next
      guest PC (and any trap fields) into CPU_STATE via the inner ops, and the unit ends.
      The host dispatches to the successor -- exactly as it already does for the interp.
    - An intra-instruction loop (e.g. the V20 REP string ops, which the frontend lowers as
      an internal Header/Body/Done block loop) is lowered by HOST-REDISPATCH: the back-edge
      becomes "set PC = this instruction's address, return", so the host re-runs the block
      one iteration at a time. The loop-carried state (CX/SI/DI...) lives in registers, so
      re-execution continues correctly. No inner control flow is needed.

  GenerateShadowUnit translates ONE guest basic block this way -- so there are never any
  intra-unit join points across instructions; all control flow exits to the host. Correct
  for any backend whose straight-line core works (proved by the M x N frontend/backend
  matrix), at the cost of a host round-trip per block (the "correct but slow" universal
  path). Backends with a native CFG keep using GenerateAotCfg (the fast path); System falls
  back to the shadow path when a backend's emitter has no native control flow.

  GENERALIZATION. This is capability synthesis: define the MINIMAL CORE a backend must
  implement, then synthesize each optional capability from it. A backend declares only what
  it does natively; the shadow layer fills the rest. A minimal correct backend therefore
  yields a fully-featured one -- and a single conformance check against the synthesized path
  guarantees every core-passing backend can drive the machine.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_SHADOWCFG_H
#define LIBCPU_SHADOWCFG_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

//
// Shadow ABI. A shadow-compiled unit cannot write the dedicated CPU_STATE dispatch fields
// (TrapPc/IoCtrl/SyscallVector) -- those need capabilities the minimal backend lacks -- so it
// funnels its outcome through reserved GENERAL REGISTERS, written with the core PutRegister op.
// System's shadow-dispatch reads them after the unit runs. The V20 guest uses register indices
// 0..11; indices 28..31 are reserved here and never touched by the frontend.
//
#define SHADOW_REG_STATUS   ((UINT32) 28)   // SHADOW_ST_* (+ width<<8 for port ops); 0 = plain next-PC
#define SHADOW_REG_NEXTPC   ((UINT32) 29)   // the next/resume guest PC (IP offset), as TrapPc would carry
#define SHADOW_REG_A        ((UINT32) 30)   // operand: port / INT vector / trap reason / new CS
#define SHADOW_REG_B        ((UINT32) 31)   // operand: port-out data

// SHADOW_REG_STATUS low byte -- how System should dispatch after the unit runs.
#define SHADOW_ST_NEXT      ((UINT64) 0)    // continue at NEXTPC (branch / fall-through / indirect / RET)
#define SHADOW_ST_PORTOUT   ((UINT64) 1)    // OUT: A=port, B=data, width in bits 8.. ; resume at NEXTPC
#define SHADOW_ST_PORTIN    ((UINT64) 2)    // IN:  A=port, width in bits 8.. ; System writes AL/AX; resume NEXTPC
#define SHADOW_ST_SYSCALL   ((UINT64) 3)    // INT n: A=vector; resume at NEXTPC
#define SHADOW_ST_SYSTRAP   ((UINT64) 4)    // privileged control (HLT/STI/CLI/PUSHF/...): A=reason; resume NEXTPC
#define SHADOW_ST_FARJUMP   ((UINT64) 5)    // far transfer: A=new CS, NEXTPC=new IP

//
// Probe whether a backend can build a native CFG (multi-block branches). Creates a throwaway
// emitter and tests Branch/CondBranch support; a backend that returns E_NOTIMPL needs the
// shadow path. The result is intended to be cached by the caller (it is per-backend).
//
bool BackendHasNativeCfg (ICpuArchitecture *pArch, ICpuBackend *pBackend);

//
// Translate ONE guest basic block starting at Entry through pInner using only its minimal
// straight-line emitter, synthesizing control flow and the machine trap capabilities as
// described above. The produced ICpuCode runs the block and leaves the next guest PC (or a
// trap) in CPU_STATE for the host to dispatch. End bounds the decode window. Returns E_FAIL
// if the block cannot be decoded.
//
HRESULT GenerateShadowUnit (ICpuArchitecture *pArch, ICpuBackend *pInner,
                            CPU_ADDR Entry, CPU_ADDR End, OUT ICpuCode **ppCode);

} // namespace LibCPU

#endif // LIBCPU_SHADOWCFG_H
