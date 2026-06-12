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
#include "LibCPU/CpuState.h"

#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <utility>
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
    // 2. Create caller blocks + shared exit. (Inlined callees are NOT shared blocks;
    //    each inlined site gets a private copy, built recursively below.)
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

    //
    // 3. Build the inline-instance TREE. Each inlined CALL site -- at ANY depth -- gets
    //    a private copy of its callee (duplicate-and-specialize). A CALL inside a copy
    //    recurses into a child instance whose RET returns to THIS copy's continuation
    //    block (ReturnBlock). Post-order: children are pushed before their parent.
    //
    struct Instance {
        CPU_ADDR                        Callee;
        ICpuBlock                      *ReturnBlock;
        std::vector<CPU_ADDR>           CalleePcs;
        std::map<CPU_ADDR, ICpuBlock *> Blocks;
        std::map<CPU_ADDR, UINT32>      NestedAt;   // nested-call site Pc -> child instance index
    };
    std::vector<Instance>      Instances;
    std::map<CPU_ADDR, UINT32> SiteToTop;   // top-level call site -> instance index
    bool                       InlineFailed = false;

    std::function<UINT32 (CPU_ADDR, ICpuBlock *, UINT32)> BuildInstance =
        [&] (CPU_ADDR Callee, ICpuBlock *ReturnBlock, UINT32 Depth) -> UINT32 {
            if (Depth > 16) {                       // defensive (ProfiledAot vets finite trees)
                InlineFailed = true;
                return 0;
            }
            Instance Inst;
            Inst.Callee      = Callee;
            Inst.ReturnBlock = ReturnBlock;

            std::set<CPU_ADDR>                          Seen;
            std::vector<CPU_ADDR>                        W;
            std::vector<std::pair<CPU_ADDR, CPU_ADDR> >  Nested;   // (siteCp, nestedCallee)
            W.push_back (Callee);
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
                    continue;                       // the callee exits here
                }
                if (T & TagCall) {
                    Nested.push_back (std::make_pair (Cp, Nw));
                    W.push_back (Nx);               // the continuation is a block in THIS copy
                } else {
                    if (T & (TagContinue | TagConditional)) { W.push_back (Nx); }
                    if (T & (TagBranch | TagConditional))   { W.push_back (Nw); }
                }
            }

            for (CPU_ADDR Cp : Inst.CalleePcs) {
                ICpuBlock *pB = nullptr;
                Emitter->CreateBlock ("inl", &pB);
                Inst.Blocks[Cp] = pB;
            }

            for (std::pair<CPU_ADDR, CPU_ADDR> CONST &N : Nested) {
                UINT32   T2;
                CPU_ADDR Nw2;
                CPU_ADDR ContPc;
                pArch->TagInstr (N.first, &T2, &Nw2, &ContPc);
                auto It = Inst.Blocks.find (ContPc);
                ICpuBlock *pCont = (It != Inst.Blocks.end ()) ? It->second : Target (ContPc);
                Inst.NestedAt[N.first] = BuildInstance (N.second, pCont, Depth + 1);
            }

            UINT32 Idx = (UINT32) Instances.size ();
            Instances.push_back (std::move (Inst));
            return Idx;
        };

    for (UINT32 I = 0; I < InlineCount; I++) {
        SiteToTop[pInline[I].Site] = BuildInstance (pInline[I].Callee, Target (pInline[I].ReturnPoint), 0);
    }

    auto InstTarget = [&] (Instance CONST &Inst, CPU_ADDR Pc) -> ICpuBlock * {
        auto It = Inst.Blocks.find (Pc);
        return (It != Inst.Blocks.end ()) ? It->second : Target (Pc);
    };

    //
    // 4. Entry edge (and the clean bail-out for backends without block ops, or a tree
    //    that exceeded the inline-depth guard).
    //
    HRESULT BrHr = InlineFailed ? E_FAIL : Emitter->Branch (Target (Entry));
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
            Instance CONST &Inst = Instances[SiteToTop[Pc]];
            Emitter->Branch (Inst.Blocks.at (Inst.Callee));   // no push; enter the copy
            Count++;
            continue;
        }

        pArch->TranslateInstr (Pc, Emitter);
        Count++;

        if (Tag & TagTrap) {
            // A far transfer (CS:IP reload): TranslateInstr emitted IndirectBranch,
            // which terminates the block and traps to the host. Nothing to add.
        } else if (Tag & TagReturn) {
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
    // 6. Fill each inline instance copy. A nested CALL branches into ITS child copy;
    //    the RET branches straight to this copy's return block (no pop, no dispatcher);
    //    internal branches stay within this copy's private blocks.
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

            if (T & TagReturn) {
                Emitter->Branch (Inst.ReturnBlock);            // specialized return
                Count++;
                continue;
            }
            if (T & TagCall) {                                 // nested inlined call
                UINT32 Child = Inst.NestedAt.at (Cp);
                Emitter->Branch (Instances[Child].Blocks.at (Instances[Child].Callee));
                Count++;
                continue;
            }

            pArch->TranslateInstr (Cp, Emitter);   // callee effect
            Count++;

            if (T & TagConditional) {
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

HRESULT
GenerateAotCfgProfiling (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                         CPU_ADDR Entry, CPU_ADDR End,
                         OUT ICpuCode **ppCode,
                         OUT CPU_CALL_EDGE *pSites, UINT32 MaxSites, OUT UINT32 *pSiteCount)
{
    if (pArch == nullptr || pBackend == nullptr || ppCode == nullptr) {
        return E_INVALIDARG;
    }
    *ppCode = nullptr;
    if (pSiteCount != nullptr) {
        *pSiteCount = 0;
    }

    ComPtr<ICpuEmitter> Emitter;
    HRESULT hr = pBackend->CreateEmitter (pArch, &Emitter);
    if (FAILED (hr) || Emitter == nullptr) {
        return FAILED (hr) ? hr : E_FAIL;
    }
    ICpuSmcEmitter *pSmc = nullptr;
    if (FAILED (Emitter->QueryInterface (IID_ICpuSmcEmitter, (VOID **) &pSmc))) {
        pSmc = nullptr;
    }
    ICpuProfileEmitter *pProf = nullptr;
    if (FAILED (Emitter->QueryInterface (IID_ICpuProfileEmitter, (VOID **) &pProf))) {
        pProf = nullptr;
    }
    if (pProf == nullptr) {
        if (pSmc != nullptr) { pSmc->Release (); }
        return E_NOTIMPL;   // backend cannot instrument edge counters
    }

    // Discover every reachable block (no inlining: callees are shared blocks).
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
        if (Tag & TagReturn) {
            HasIndirect = true;
        }
        if (Tag & (TagContinue | TagConditional | TagCall)) { Work.push_back (NextPc); }
        if (Tag & (TagBranch | TagConditional | TagCall))   { Work.push_back (NewPc); }
    }
    if (Pcs.empty ()) {
        if (pSmc != nullptr) { pSmc->Release (); }
        pProf->Release ();
        return E_FAIL;
    }

    std::map<CPU_ADDR, ICpuBlock *> Blocks;
    for (CPU_ADDR Pc : Pcs) {
        ICpuBlock *pBlock = nullptr;
        Emitter->CreateBlock ("pc", &pBlock);
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

    HRESULT BrHr = Emitter->Branch (Target (Entry));
    if (FAILED (BrHr)) {
        for (auto CONST &Pair : Blocks) { if (Pair.second != nullptr) { Pair.second->Release (); } }
        for (ICpuBlock *pB : Disp) { if (pB != nullptr) { pB->Release (); } }
        if (pExit != nullptr) { pExit->Release (); }
        if (pSmc != nullptr)  { pSmc->Release (); }
        pProf->Release ();
        return BrHr;
    }

    UINT32 SiteCount = 0;
    for (CPU_ADDR Pc : Pcs) {
        Emitter->SetInsertBlock (Blocks[Pc]);
        if (pSmc != nullptr) {
            pSmc->EmitCodeGuard (Pc);
        }
        UINT32   Tag;
        CPU_ADDR NewPc;
        CPU_ADDR NextPc;
        pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc);

        if (Tag & TagCall) {
            // Instrument: assign this call site a slot, record it, bump on every run.
            if (SiteCount < CPU_PROFILE_SLOTS) {
                if (pSites != nullptr && SiteCount < MaxSites) {
                    pSites[SiteCount].Site        = Pc;
                    pSites[SiteCount].Callee      = NewPc;
                    pSites[SiteCount].ReturnPoint = NextPc;
                }
                pProf->EmitEdgeCounter (SiteCount);
                SiteCount++;
            }
        }

        pArch->TranslateInstr (Pc, Emitter);

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

    if (pSiteCount != nullptr) {
        *pSiteCount = SiteCount;
    }
    hr = pBackend->Compile (Emitter, ppCode);

    for (auto CONST &Pair : Blocks) { if (Pair.second != nullptr) { Pair.second->Release (); } }
    for (ICpuBlock *pB : Disp) { if (pB != nullptr) { pB->Release (); } }
    if (pExit != nullptr) { pExit->Release (); }
    if (pSmc != nullptr)  { pSmc->Release (); }
    pProf->Release ();
    return hr;
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

UINT32
CollectDirectCallEdges (ICpuArchitecture *pArch, CPU_ADDR Entry, CPU_ADDR End,
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
            Work.push_back (NextPc);   // CALL returns to NextPc -- but do NOT descend
        }
        if (!(Tag & TagCall) && (Tag & (TagBranch | TagConditional))) {
            Work.push_back (NewPc);    // follow the caller's own branches only
        }
    }
    return Found;
}

} // namespace LibCPU
