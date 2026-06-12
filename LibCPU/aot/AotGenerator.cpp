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

    // Is the CALL at Site inlined? If so, return its callee + this site's return point.
    auto SiteInfo = [&] (CPU_ADDR Site, CPU_ADDR *pCallee, CPU_ADDR *pRet) -> bool {
        for (UINT32 I = 0; I < InlineCount; I++) {
            if (pInline[I].Site == Site) {
                *pCallee = pInline[I].Callee;
                *pRet    = pInline[I].ReturnPoint;
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

    // Optional self-modifying-code support (inert unless the host arms CPU_STATE.Code*).
    ICpuSmcEmitter *pSmc = nullptr;
    if (FAILED (Emitter->QueryInterface (IID_ICpuSmcEmitter, (VOID **) &pSmc))) {
        pSmc = nullptr;
    }

    //
    // 1. Discover the CALLER's blocks. An inlined CALL site does NOT pull its callee
    //    into the shared CFG (it is duplicated per site, below); it still reaches its
    //    return point. A caller-level RET still needs the dispatcher.
    //
    std::set<CPU_ADDR>    Pcs;
    std::vector<CPU_ADDR> Work;
    bool                  HasIndirect = false;
    Work.push_back (Entry);
    while (!Work.empty ()) {
        CPU_ADDR Pc = Work.back ();
        Work.pop_back ();
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
        CPU_ADDR Cal = 0, Ret = 0;
        bool InlinedCall = (Tag & TagCall) != 0 && SiteInfo (Pc, &Cal, &Ret);

        if (Tag & TagReturn) {
            HasIndirect = true;
        }
        if (Tag & (TagContinue | TagConditional | TagCall)) {
            Work.push_back (NextPc);   // CALL returns to NextPc, so it is reachable too
        }
        if (!InlinedCall && (Tag & (TagBranch | TagConditional | TagCall))) {
            Work.push_back (NewPc);    // an inlined callee is NOT a shared block
        }
    }
    if (Pcs.empty ()) {
        return E_FAIL;
    }

    //
    // 2. For each inlined site, discover its callee's blocks -- a PRIVATE copy. The
    //    callee is a leaf (no nested CALL); discovery follows internal branches but
    //    stops at each RET.
    //
    struct Instance {
        CPU_ADDR                        Callee;
        CPU_ADDR                        ReturnPoint;
        std::vector<CPU_ADDR>           CalleePcs;
        std::map<CPU_ADDR, ICpuBlock *> Blocks;
    };
    std::vector<Instance>      Instances;
    std::map<CPU_ADDR, UINT32> SiteToInst;   // call site -> instance index
    for (UINT32 I = 0; I < InlineCount; I++) {
        Instance Inst;
        Inst.Callee      = pInline[I].Callee;
        Inst.ReturnPoint = pInline[I].ReturnPoint;

        std::set<CPU_ADDR>    Seen;
        std::vector<CPU_ADDR> W;
        W.push_back (Inst.Callee);
        while (!W.empty ()) {
            CPU_ADDR Cp = W.back ();
            W.pop_back ();
            if (Cp >= End || Seen.count (Cp) != 0) {
                continue;
            }
            Seen.insert (Cp);
            Inst.CalleePcs.push_back (Cp);

            UINT32   T;
            CPU_ADDR Nw;
            CPU_ADDR Nx;
            if (FAILED (pArch->TagInstr (Cp, &T, &Nw, &Nx))) {
                continue;
            }
            if (T & TagReturn) {
                continue;   // the callee exits here; do not follow past the RET
            }
            if (T & (TagContinue | TagConditional | TagCall)) { W.push_back (Nx); }
            if (T & (TagBranch | TagConditional | TagCall))   { W.push_back (Nw); }
        }
        SiteToInst[pInline[I].Site] = (UINT32) Instances.size ();
        Instances.push_back (std::move (Inst));
    }

    //
    // 3. Create blocks: caller blocks + shared exit + (dispatcher) + per-instance copies.
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

    for (Instance &Inst : Instances) {
        for (CPU_ADDR Cp : Inst.CalleePcs) {
            ICpuBlock *pB = nullptr;
            Emitter->CreateBlock ("inl", &pB);
            Inst.Blocks[Cp] = pB;
        }
    }
    auto InstTarget = [&] (Instance CONST &Inst, CPU_ADDR Pc) -> ICpuBlock * {
        auto It = Inst.Blocks.find (Pc);
        return (It != Inst.Blocks.end ()) ? It->second : Target (Pc);
    };

    //
    // 4. Entry edge (and the clean bail-out for backends without block ops).
    //
    HRESULT BrHr = Emitter->Branch (Target (Entry));
    if (FAILED (BrHr)) {
        for (auto CONST &Pair : Blocks) { if (Pair.second != nullptr) { Pair.second->Release (); } }
        for (Instance CONST &Inst : Instances) {
            for (auto CONST &P : Inst.Blocks) { if (P.second != nullptr) { P.second->Release (); } }
        }
        for (ICpuBlock *pB : Disp) { if (pB != nullptr) { pB->Release (); } }
        if (pExit != nullptr) { pExit->Release (); }
        if (pSmc != nullptr)  { pSmc->Release (); }
        return BrHr;
    }

    //
    // 5. Fill caller blocks. An inlined CALL site elides its push and falls into this
    //    site's private callee copy.
    //
    UINT32 Count = 0;
    for (CPU_ADDR Pc : Pcs) {
        Emitter->SetInsertBlock (Blocks[Pc]);
        if (pSmc != nullptr) {
            pSmc->EmitCodeGuard (Pc);
        }

        UINT32   Tag;
        CPU_ADDR NewPc;
        CPU_ADDR NextPc;
        pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc);

        CPU_ADDR Cal = 0, Ret = 0;
        if ((Tag & TagCall) && SiteInfo (Pc, &Cal, &Ret)) {
            Instance CONST &Inst = Instances[SiteToInst[Pc]];
            Emitter->Branch (Inst.Blocks.at (Inst.Callee));   // no push; enter the copy
            Count++;
            continue;
        }

        pArch->TranslateInstr (Pc, Emitter);
        Count++;

        if (Tag & TagReturn) {
            Emitter->Branch (pDispatch);
        } else if (Tag & TagConditional) {
            ComPtr<ICpuValue> Cond;
            if (SUCCEEDED (pArch->TranslateCond (Pc, Emitter, &Cond)) && Cond != nullptr) {
                Emitter->CondBranch (Cond, Target (NewPc), Target (NextPc));
            } else {
                Emitter->Branch (Target (NextPc));
            }
        } else if (Tag & (TagBranch | TagCall)) {
            Emitter->Branch (Target (NewPc));
        } else {
            Emitter->Branch (Target (NextPc));
        }
    }

    //
    // 6. Fill each inline instance (the duplicated callee body). Its RET is
    //    specialized: it branches straight to this site's continuation -- no pop, no
    //    dispatcher. Internal branches stay within the instance's private blocks.
    //
    for (Instance CONST &Inst : Instances) {
        for (CPU_ADDR Cp : Inst.CalleePcs) {
            Emitter->SetInsertBlock (Inst.Blocks.at (Cp));
            if (pSmc != nullptr) {
                pSmc->EmitCodeGuard (Cp);
            }

            UINT32   T;
            CPU_ADDR Nw;
            CPU_ADDR Nx;
            pArch->TagInstr (Cp, &T, &Nw, &Nx);

            bool IsRet = (T & TagReturn) != 0;
            if (!IsRet) {
                pArch->TranslateInstr (Cp, Emitter);   // callee effect (no pop for the RET)
            }
            Count++;

            if (IsRet) {
                Emitter->Branch (Target (Inst.ReturnPoint));   // specialized return
            } else if (T & TagConditional) {
                ComPtr<ICpuValue> Cond;
                if (SUCCEEDED (pArch->TranslateCond (Cp, Emitter, &Cond)) && Cond != nullptr) {
                    Emitter->CondBranch (Cond, InstTarget (Inst, Nw), InstTarget (Inst, Nx));
                } else {
                    Emitter->Branch (InstTarget (Inst, Nx));
                }
            } else if (T & TagBranch) {
                Emitter->Branch (InstTarget (Inst, Nw));
            } else {
                Emitter->Branch (InstTarget (Inst, Nx));   // TagContinue
            }
        }
    }

    //
    // 7. Fill the dispatcher chain (caller RETs only).
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
        pSmc->IndirectBranch (Pc);
    }

    if (pInstrCount != nullptr) {
        *pInstrCount = Count;
    }
    hr = pBackend->Compile (Emitter, ppCode);

    for (auto CONST &Pair : Blocks) { if (Pair.second != nullptr) { Pair.second->Release (); } }
    for (Instance CONST &Inst : Instances) {
        for (auto CONST &P : Inst.Blocks) { if (P.second != nullptr) { P.second->Release (); } }
    }
    for (ICpuBlock *pB : Disp) { if (pB != nullptr) { pB->Release (); } }
    if (pExit != nullptr) { pExit->Release (); }
    if (pSmc != nullptr)  { pSmc->Release (); }
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
