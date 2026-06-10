/** @file
  Ahead-of-time translation driver. See AotGenerator.h.

  The whole program is translated into one emitter and compiled once. Because the
  backends accumulate an instruction's emitted form (statements / IR / bytecode /
  CIL) and read & write the guest register file through GRF, feeding successive
  instructions into the same emitter produces a single artifact whose effects chain
  correctly -- and the backend's optimizer now sees every instruction at once.

  This straight-line driver follows fall-through edges (TagInstr's NextPc). Control
  flow (branches/loops) is the natural extension: create one ICpuBlock per
  instruction address and wire CreateBlock/Branch/CondBranch instead of relying on
  fall-through -- which is why the driver is expressed in terms of the same
  ICpuEmitter the JIT uses, not a bespoke emitter.
**/
#include "AotGenerator.h"
#include "LibCPU/PCom.h"

namespace LibCPU {

HRESULT
GenerateAot (ICpuArchitecture *pArch, ICpuBackend *pBackend,
             CPU_ADDR Entry, CPU_ADDR End,
             OUT ICpuCode **ppCode, OUT UINT32 *pInstrCount)
{
    if (pArch == nullptr || pBackend == nullptr || ppCode == nullptr) {
        return E_INVALIDARG;
    }
    *ppCode = nullptr;

    ComPtr<ICpuEmitter> Emitter;
    HRESULT hr = pBackend->CreateEmitter (pArch, &Emitter);
    if (FAILED (hr) || Emitter == nullptr) {
        return FAILED (hr) ? hr : E_FAIL;
    }

    //
    // Accumulate every instruction of the program into the SINGLE emitter. One
    // compile of the whole thing follows, so the backend optimizes across the
    // entire program rather than one instruction at a time.
    //
    UINT32   Count = 0;
    CPU_ADDR Pc    = Entry;
    while (Pc < End) {
        hr = pArch->TranslateInstr (Pc, Emitter);
        if (FAILED (hr)) {
            return hr;
        }
        Count++;

        UINT32   Tag;
        CPU_ADDR NewPc;
        CPU_ADDR NextPc;
        if (FAILED (pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc)) || NextPc <= Pc) {
            break;   // non-advancing or untaggable: stop the linear sweep
        }
        Pc = NextPc;
    }

    if (pInstrCount != nullptr) {
        *pInstrCount = Count;
    }
    return pBackend->Compile (Emitter, ppCode);
}

} // namespace LibCPU
