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

#include <cstdio>
#include <map>
#include <set>
#include <vector>

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

HRESULT
GenerateAotCfg (ICpuArchitecture *pArch, ICpuBackend *pBackend,
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
    // 1. Discover every reachable instruction address by following the edges
    //    TagInstr reports: fall-through (NextPc) and branch target (NewPc).
    //
    std::set<CPU_ADDR>    Pcs;
    std::vector<CPU_ADDR> Work;
    Work.push_back (Entry);
    while (!Work.empty ()) {
        CPU_ADDR Pc = Work.back ();
        Work.pop_back ();
        if (Pc < Entry || Pc >= End || Pcs.count (Pc) != 0) {
            continue;
        }
        Pcs.insert (Pc);

        UINT32   Tag;
        CPU_ADDR NewPc;
        CPU_ADDR NextPc;
        if (FAILED (pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc))) {
            continue;
        }
        if (Tag & (TagContinue | TagConditional)) {
            Work.push_back (NextPc);
        }
        if (Tag & (TagBranch | TagConditional | TagCall)) {
            Work.push_back (NewPc);
        }
    }
    if (Pcs.empty ()) {
        return E_FAIL;
    }

    //
    // 2. One block per reachable address, plus a shared exit block. Raw pointers
    //    (ComPtr is move-only) released after the compile.
    //
    std::map<CPU_ADDR, ICpuBlock *> Blocks;
    for (CPU_ADDR Pc : Pcs) {
        char Name[24];
        std::snprintf (Name, sizeof (Name), "pc_%04llx", (unsigned long long) Pc);
        ICpuBlock *pBlock = nullptr;
        Emitter->CreateBlock (Name, &pBlock);
        Blocks[Pc] = pBlock;
    }
    ICpuBlock *pExit = nullptr;
    Emitter->CreateBlock ("exit", &pExit);

    auto Target = [&] (CPU_ADDR Pc) -> ICpuBlock * {
        auto It = Blocks.find (Pc);
        return (It != Blocks.end ()) ? It->second : pExit;
    };

    //
    // 3. The emitter starts in its own "entry" block: jump from there to the
    //    program entry, then fill and terminate each instruction block.
    //
    Emitter->Branch (Target (Entry));

    UINT32 Count = 0;
    for (CPU_ADDR Pc : Pcs) {
        Emitter->SetInsertBlock (Blocks[Pc]);
        pArch->TranslateInstr (Pc, Emitter);
        Count++;

        UINT32   Tag;
        CPU_ADDR NewPc;
        CPU_ADDR NextPc;
        pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc);

        if (Tag & TagConditional) {
            ComPtr<ICpuValue> Cond;
            if (SUCCEEDED (pArch->TranslateCond (Pc, Emitter, &Cond)) && Cond != nullptr) {
                Emitter->CondBranch (Cond, Target (NewPc), Target (NextPc));
            } else {
                Emitter->Branch (Target (NextPc));   // no condition available: fall through
            }
        } else if (Tag & (TagBranch | TagCall)) {
            Emitter->Branch (Target (NewPc));
        } else {
            Emitter->Branch (Target (NextPc));        // TagContinue / TagReturn / end
        }
    }

    if (pInstrCount != nullptr) {
        *pInstrCount = Count;
    }
    hr = pBackend->Compile (Emitter, ppCode);

    for (auto CONST &Pair : Blocks) {
        if (Pair.second != nullptr) {
            Pair.second->Release ();
        }
    }
    if (pExit != nullptr) {
        pExit->Release ();
    }
    return hr;
}

} // namespace LibCPU
