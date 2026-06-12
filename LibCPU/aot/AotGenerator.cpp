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
GenerateAotCfgInlined (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                       CPU_ADDR Entry, CPU_ADDR End,
                       CPU_INLINE_SITE CONST *pInline, UINT32 InlineCount,
                       OUT ICpuCode **ppCode, OUT UINT32 *pInstrCount)
{
    if (pArch == nullptr || pBackend == nullptr || ppCode == nullptr) {
        return E_INVALIDARG;
    }
    *ppCode = nullptr;

    // Inline-plan lookups: is this CALL target inlined? is this PC inside an inlined
    // callee (so its RET returns straight to the recorded continuation)?
    auto IsInlinedCall = [&] (CPU_ADDR Callee) -> bool {
        for (UINT32 I = 0; I < InlineCount; I++) {
            if (pInline[I].Callee == Callee) { return true; }
        }
        return false;
    };
    auto InlinedRetOf = [&] (CPU_ADDR Pc, CPU_ADDR *pRet) -> bool {
        for (UINT32 I = 0; I < InlineCount; I++) {
            if (Pc >= pInline[I].Callee && Pc < pInline[I].CalleeEnd) {
                *pRet = pInline[I].ReturnPoint;
                return true;
            }
        }
        return false;
    };

    ComPtr<ICpuEmitter> Emitter;
    HRESULT hr = pBackend->CreateEmitter (pArch, &Emitter);
    if (FAILED (hr) || Emitter == nullptr) {
        return FAILED (hr) ? hr : E_FAIL;
    }

    // Optional self-modifying-code support: if the emitter exposes it, a guard is
    // planted at each block entry (inert unless the host arms CPU_STATE.Code*).
    ICpuSmcEmitter *pSmc = nullptr;
    if (FAILED (Emitter->QueryInterface (IID_ICpuSmcEmitter, (VOID **) &pSmc))) {
        pSmc = nullptr;
    }

    //
    // 1. Discover every reachable instruction address by following the edges
    //    TagInstr reports: fall-through (NextPc) and branch target (NewPc).
    //
    std::set<CPU_ADDR>    Pcs;
    std::vector<CPU_ADDR> Work;
    bool                  HasIndirect = false;   // any block ends in an indirect transfer?
    Work.push_back (Entry);
    while (!Work.empty ()) {
        CPU_ADDR Pc = Work.back ();
        Work.pop_back ();
        // Bound only by End, NOT by Entry: a backward branch (loop) reaches PCs
        // below the entry/resume point, and those blocks must be in the CFG too.
        if (Pc >= End || Pcs.count (Pc) != 0) {
            continue;
        }
        Pcs.insert (Pc);

        UINT32   Tag;
        CPU_ADDR NewPc;
        CPU_ADDR NextPc;
        if (FAILED (pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc))) {
            continue;
        }
        CPU_ADDR RetDummy = 0;
        if ((Tag & TagReturn) && !InlinedRetOf (Pc, &RetDummy)) {
            HasIndirect = true;        // a non-inlined RET needs the dispatcher
        }
        if (Tag & (TagContinue | TagConditional | TagCall)) {
            Work.push_back (NextPc);   // CALL returns to NextPc, so it is reachable too
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

    // Indirect-branch dispatcher (built only when needed and the backend supports the
    // dispatch scratch): a chain of compare-blocks that reads the runtime target from
    // DispPc and routes to the matching instruction block; an unknown target falls to
    // IndirectBranch (host re-translate). N+1 blocks for N instruction blocks.
    std::vector<ICpuBlock *> Disp;
    ICpuBlock *pDispatch = pExit;
    if (HasIndirect && pSmc != nullptr) {
        for (UINT32 Idx = 0; Idx <= (UINT32) Blocks.size (); Idx++) {
            ICpuBlock *pB = nullptr;
            Emitter->CreateBlock ("disp", &pB);
            Disp.push_back (pB);
        }
        pDispatch = Disp[0];
    }

    //
    // 3. The emitter starts in its own "entry" block: jump from there to the
    //    program entry, then fill and terminate each instruction block. If the
    //    backend does not implement Branch (it stubs the block ops), bail cleanly
    //    rather than emit wrong linear code.
    //
    HRESULT BrHr = Emitter->Branch (Target (Entry));
    if (FAILED (BrHr)) {
        for (auto CONST &Pair : Blocks) {
            if (Pair.second != nullptr) { Pair.second->Release (); }
        }
        if (pExit != nullptr) { pExit->Release (); }
        if (pSmc != nullptr)  { pSmc->Release (); }
        return BrHr;   // e.g. E_NOTIMPL: this backend has no control-flow support
    }

    UINT32 Count = 0;
    for (CPU_ADDR Pc : Pcs) {
        Emitter->SetInsertBlock (Blocks[Pc]);
        if (pSmc != nullptr) {
            pSmc->EmitCodeGuard (Pc);   // trap here if code was modified since translation
        }

        // Tag first: an inlined CALL/RET skips its data effect (the push / the pop +
        // dispatch) entirely and is realized as a plain branch.
        UINT32   Tag;
        CPU_ADDR NewPc;
        CPU_ADDR NextPc;
        pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc);

        bool     InlinedCall = (Tag & TagCall) != 0 && IsInlinedCall (NewPc);
        CPU_ADDR RetPt       = 0;
        bool     InlinedRet  = (Tag & TagReturn) != 0 && InlinedRetOf (Pc, &RetPt);

        if (!InlinedCall && !InlinedRet) {
            pArch->TranslateInstr (Pc, Emitter);   // normal effect (CALL push / RET pop included)
        }
        Count++;

        if (InlinedRet) {
            Emitter->Branch (Target (RetPt));         // RET -> the call's continuation, directly
        } else if (InlinedCall) {
            Emitter->Branch (Target (NewPc));         // CALL -> fall into the callee, no push
        } else if (Tag & TagReturn) {
            // A non-inlined indirect transfer (RET): TranslateInstr stashed the runtime
            // target via SetDispatchTarget; route through the in-artifact dispatcher.
            Emitter->Branch (pDispatch);
        } else if (Tag & TagConditional) {
            ComPtr<ICpuValue> Cond;
            if (SUCCEEDED (pArch->TranslateCond (Pc, Emitter, &Cond)) && Cond != nullptr) {
                Emitter->CondBranch (Cond, Target (NewPc), Target (NextPc));
            } else {
                Emitter->Branch (Target (NextPc));   // no condition available: fall through
            }
        } else if (Tag & (TagBranch | TagCall)) {
            Emitter->Branch (Target (NewPc));         // CALL: branch to the callee
        } else {
            Emitter->Branch (Target (NextPc));        // TagContinue / end
        }
    }

    //
    // 4. Fill the dispatcher chain: disp[k] compares the runtime target (DispPc)
    //    against the k-th block's address and routes to it, else to disp[k+1]; the
    //    final block falls back to a host re-translate for an unknown target.
    //
    if (HasIndirect && pSmc != nullptr) {
        UINT32 K = 0;
        for (auto CONST &Pair : Blocks) {
            Emitter->SetInsertBlock (Disp[K]);
            ComPtr<ICpuValue> Pc;   pSmc->GetDispatchTarget (&Pc);
            ComPtr<ICpuValue> Addr; Emitter->ConstInt (64, (UINT64) Pair.first, &Addr);
            ComPtr<ICpuValue> Cond; Emitter->Compare (CmpEq, Pc, Addr, &Cond);
            Emitter->CondBranch (Cond, Pair.second, Disp[K + 1]);
            K++;
        }
        Emitter->SetInsertBlock (Disp[K]);
        ComPtr<ICpuValue> Pc; pSmc->GetDispatchTarget (&Pc);
        pSmc->IndirectBranch (Pc);   // unknown target: write TrapPc, return -> host resumes
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
    for (ICpuBlock *pB : Disp) {
        if (pB != nullptr) {
            pB->Release ();
        }
    }
    if (pExit != nullptr) {
        pExit->Release ();
    }
    if (pSmc != nullptr) {
        pSmc->Release ();
    }
    return hr;
}

HRESULT
GenerateAotCfg (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                CPU_ADDR Entry, CPU_ADDR End,
                OUT ICpuCode **ppCode, OUT UINT32 *pInstrCount)
{
    return GenerateAotCfgInlined (pArch, pBackend, Entry, End, nullptr, 0, ppCode, pInstrCount);
}

UINT32
CollectCallEdges (ICpuArchitecture *pArch, CPU_ADDR Entry, CPU_ADDR End,
                  OUT CPU_CALL_EDGE *pEdges, UINT32 MaxEdges)
{
    if (pArch == nullptr) {
        return 0;
    }
    std::set<CPU_ADDR>    Seen;
    std::vector<CPU_ADDR> Work;
    UINT32                Found = 0;
    Work.push_back (Entry);
    while (!Work.empty ()) {
        CPU_ADDR Pc = Work.back ();
        Work.pop_back ();
        if (Pc >= End || Seen.count (Pc) != 0) {
            continue;
        }
        Seen.insert (Pc);

        UINT32   Tag;
        CPU_ADDR NewPc;
        CPU_ADDR NextPc;
        if (FAILED (pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc))) {
            continue;
        }
        if (Tag & TagCall) {
            if (pEdges != nullptr && Found < MaxEdges) {
                pEdges[Found].Site        = Pc;
                pEdges[Found].Callee      = NewPc;
                pEdges[Found].ReturnPoint = NextPc;
            }
            Found++;
        }
        if (Tag & (TagContinue | TagConditional | TagCall)) {
            Work.push_back (NextPc);
        }
        if (Tag & (TagBranch | TagConditional | TagCall)) {
            Work.push_back (NewPc);
        }
    }
    return Found;
}

} // namespace LibCPU
