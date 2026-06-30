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
                       OUT ICpuCode **ppCode, OUT UINT32 *pInstrCount, BOOLEAN Pic)
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
    // Optional uniform time base: one tick per translated guest instruction (see ICpuClockEmitter),
    // so CPU_STATE.Cycles counts instructions retired identically across interpreter and JITs.
    ICpuClockEmitter *pClk = nullptr;
    if (FAILED (Emitter->QueryInterface (IID_ICpuClockEmitter, (VOID **) &pClk))) {
        pClk = nullptr;
    }

    // Position-independent translation: ask the (segmented) frontend to materialize the code-address
    // values it bakes relative to Entry, so this unit is valid at any load address. A no-op for flat
    // archs (no ICpuSegmentedCode). Reset on every exit path so it never leaks into the next unit.
    ComPtr<ICpuSegmentedCode> Seg;
    if (Pic) {
        pArch->QueryInterface (IID_ICpuSegmentedCode, (VOID **) &Seg);
        if (Seg != nullptr) { Seg->SetPicTranslation (Entry, TRUE); }
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
        if (Seg != nullptr) { Seg->SetPicTranslation (0, FALSE); }
        return E_FAIL;
    }

    //
    // 1b. Basic-block coalescing. By default every guest instruction is its own CFG block (a
    //     branch may target any of them, and the RET dispatcher routes to any of them). But with NO
    //     inlining and NO indirect branch in the unit, the only block leaders are the entry, direct
    //     branch targets, and the instructions right after a control transfer -- everything else is
    //     reached solely by fall-through, so a maximal straight-line run can share ONE block. That
    //     elides the per-instruction branch/tick/SMC-guard the leaf blocks each carried (a big cut
    //     in emitted code for the per-block-compiling backends, and fewer ops at run time). Runs are
    //     also split at an SMC page boundary so a single EmitCodeGuard(leader) covers the block.
    bool Coalesce = (InlineCount == 0 && !HasIndirect);
    std::set<CPU_ADDR>                        Leaders;
    std::map<CPU_ADDR, std::vector<CPU_ADDR>> BlockBody;   // leader -> its instruction Pcs, in order
    if (Coalesce) {
        Leaders.insert (Entry);
        for (CPU_ADDR Pc : Pcs) {
            UINT32 Tag; CPU_ADDR NewPc; CPU_ADDR NextPc;
            if (FAILED (pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc))) { Coalesce = false; break; }
            if ((Tag & (TagBranch | TagConditional | TagCall)) && Pcs.count (NewPc) != 0) {
                Leaders.insert (NewPc);                                  // a direct branch target
            }
            if ((Tag & (TagBranch | TagConditional | TagCall | TagReturn | TagTrap)) && Pcs.count (NextPc) != 0) {
                Leaders.insert (NextPc);                                 // the instruction after a transfer
            }
        }
    }
    if (Coalesce) {
        // A fall-through that crosses an SMC page (256 bytes) starts a new block, so each block
        // stays on one page and a single guard recording the leader as the trap PC is correct.
        for (CPU_ADDR Pc : Pcs) {
            UINT32 Tag; CPU_ADDR NewPc; CPU_ADDR NextPc;
            pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc);
            if (!(Tag & (TagBranch | TagConditional | TagCall | TagReturn | TagTrap))
                && Pcs.count (NextPc) != 0 && ((NextPc >> 8) & 255) != ((Pc >> 8) & 255)) {
                Leaders.insert (NextPc);
            }
        }
        // Group each leader's maximal straight-line run (follow fall-through until a transfer, a
        // leader, or the window edge).
        for (CPU_ADDR L : Leaders) {
            std::vector<CPU_ADDR> Body;
            CPU_ADDR Pc = L;
            for (;;) {
                Body.push_back (Pc);
                UINT32 Tag; CPU_ADDR NewPc; CPU_ADDR NextPc;
                pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc);
                if (Tag & (TagBranch | TagConditional | TagCall | TagReturn | TagTrap)) { break; }
                if (Pcs.count (NextPc) == 0 || Leaders.count (NextPc) != 0) { break; }
                Pc = NextPc;
            }
            BlockBody[L] = Body;
        }
    }

    //
    // 2. Create caller blocks + shared exit. (Inlined callees are NOT shared blocks;
    //    each inlined site gets a private copy, built recursively below.)
    //    With coalescing on, only block leaders get a block; the rest fall through inside one.
    //
    std::map<CPU_ADDR, ICpuBlock *> Blocks;
    std::set<CPU_ADDR> CONST        &BlockLeaders = Coalesce ? Leaders : Pcs;
    for (CPU_ADDR Pc : BlockLeaders) {
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
        if (pClk != nullptr)  { pClk->Release (); }
        if (Seg != nullptr)   { Seg->SetPicTranslation (0, FALSE); }
        return BrHr;
    }

    //
    // 5. Fill caller blocks. An inlined CALL site elides its push and falls into this
    //    site's private callee copy.
    //
    UINT32 Count = 0;
    if (Coalesce) {
        // One block per straight-line run: guard + tick once, the bodies in sequence, then the same
        // terminator branch the per-instruction path would emit (only the intra-run fall-through
        // branches are elided -- they were pure no-ops between adjacent blocks).
        for (auto CONST &Pair : BlockBody) {
            Emitter->SetInsertBlock (Blocks[Pair.first]);
            if (pSmc != nullptr) { pSmc->EmitCodeGuard (Pair.first); }            // run is one SMC page
            if (pClk != nullptr) { pClk->EmitTick ((UINT32) Pair.second.size ()); } // one tick for the run

            std::vector<CPU_ADDR> CONST &Body = Pair.second;
            for (size_t I = 0; I < Body.size (); I++) {
                CPU_ADDR Pc = Body[I];
                UINT32   Tag;
                CPU_ADDR NewPc;
                CPU_ADDR NextPc;
                pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc);
                pArch->TranslateInstr (Pc, Emitter);
                Count++;
                if (I + 1 != Body.size ()) {
                    continue;                          // interior instruction: fall through to the next body
                }
                if (Tag & TagTrap) {
                    // IndirectBranch already emitted by TranslateInstr (far transfer; traps to host).
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
                    Emitter->Branch (Target (NextPc));   // run ended at a leader/window edge
                }
            }
        }
    } else {
        for (CPU_ADDR Pc : Pcs) {
            Emitter->SetInsertBlock (Blocks[Pc]);
            if (pSmc != nullptr) {
                pSmc->EmitCodeGuard (Pc);
            }
            if (pClk != nullptr) {
                pClk->EmitTick (1);                     // one tick per guest instruction executed
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
            if (pClk != nullptr) {
                pClk->EmitTick (1);
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
            ComPtr<ICpuValue> Rel;  // the CodeBase-relative target (PIC only); keeps the value alive
            ComPtr<ICpuValue> Addr;
            ICpuValue        *Lhs = Pc.Get ();
            if (Pic) {
                // Compare in CodeBase-relative space so the dispatcher routes correctly at any load
                // address: (target - CodeBase) vs (entry - Entry). Modular equality handles entry < Entry.
                ComPtr<ICpuValue> Base; pSmc->GetCodeBase (&Base);
                Emitter->BinaryOp (BinSub, Pc, Base, &Rel);
                Lhs = Rel.Get ();
                Emitter->ConstInt (64, (UINT64) (Pair.first - Entry), &Addr);
            } else {
                Emitter->ConstInt (64, (UINT64) Pair.first, &Addr);
            }
            ComPtr<ICpuValue> Cond; Emitter->Compare (CmpEq, Lhs, Addr, &Cond);
            Emitter->CondBranch (Cond, Pair.second, Disp[K + 1]);
            K++;
        }
        Emitter->SetInsertBlock (Disp[K]);
        ComPtr<ICpuValue> Pc; pSmc->GetDispatchTarget (&Pc);
        pSmc->IndirectBranch (Pc);   // no match: the absolute target traps to the host, which re-derives the unit
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
    if (pClk != nullptr)  { pClk->Release (); }
    if (Seg != nullptr)   { Seg->SetPicTranslation (0, FALSE); }
    return hr;
}

HRESULT
GenerateAotCfg (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                CPU_ADDR Entry, CPU_ADDR End,
                OUT ICpuCode **ppCode, OUT UINT32 *pInstrCount, BOOLEAN Pic)
{
    return GenerateAotCfgInlined (pArch, pBackend, Entry, End, nullptr, 0, ppCode, pInstrCount, Pic);
}

HRESULT
GenerateAotExecOne (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                    CPU_ADDR Pc, OUT ICpuCode **ppCode)
{
    //
    // Translate the single instruction at Pc for use by the $exec (XCT) run-loop handler.
    // On return, CPU_STATE.DispPc holds the resolved successor address so the handler can
    // route correctly for ALL exit kinds:
    //
    //   TagConditional (skip): DispPc = NewPc (skip target) if condition TRUE,
    //                                   NextPc (fall-through) if condition FALSE.
    //   TagBranch      (JMP):  DispPc = NewPc (static jump target).
    //   TagContinue    (fall): DispPc = NextPc.
    //   TagTrap (IOT/HLT/indirect): TrapPc and SyscallVector are set by the Syscall /
    //                               IndirectBranch path; DispPc is never reached.
    //
    // Requires the backend to expose ICpuSmcEmitter (SetDispatchTarget). Returns E_NOTIMPL
    // if the backend does not support it; the caller falls back to GenerateAotCfg.
    //
    if (pArch == nullptr || pBackend == nullptr || ppCode == nullptr) {
        return E_INVALIDARG;
    }
    *ppCode = nullptr;

    UINT32   Tag    = 0;
    CPU_ADDR NewPc  = 0;
    CPU_ADDR NextPc = 0;
    if (FAILED (pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc))) {
        return E_FAIL;
    }

    ComPtr<ICpuEmitter> Emitter;
    HRESULT hr = pBackend->CreateEmitter (pArch, &Emitter);
    if (FAILED (hr) || Emitter == nullptr) {
        return FAILED (hr) ? hr : E_FAIL;
    }

    ICpuSmcEmitter *pSmc = nullptr;
    if (FAILED (Emitter->QueryInterface (IID_ICpuSmcEmitter, (VOID **) &pSmc)) || pSmc == nullptr) {
        return E_NOTIMPL;
    }

    //
    // Four blocks: the instruction body, a taken-exit (writes NewPc to DispPc), a fall-exit
    // (writes NextPc to DispPc), and the empty pExit (falls off, ends execution with ExecOk).
    //
    ICpuBlock *pBody      = nullptr;
    ICpuBlock *pTakenExit = nullptr;
    ICpuBlock *pFallExit  = nullptr;
    ICpuBlock *pExit      = nullptr;
    Emitter->CreateBlock ("body",  &pBody);
    Emitter->CreateBlock ("taken", &pTakenExit);
    Emitter->CreateBlock ("fall",  &pFallExit);
    Emitter->CreateBlock ("exit",  &pExit);

    // Body: translate the instruction; add the outgoing edge based on its tag.
    Emitter->SetInsertBlock (pBody);
    pArch->TranslateInstr (Pc, Emitter);
    if (Tag & (TagTrap | TagReturn)) {
        // TranslateInstr already emitted IndirectBranch / Syscall which terminates the
        // block (the interpreter returns ExecSmc before reaching the next opcode). Emit a
        // fallback branch so the block has a valid terminator in IR-based backends.
        Emitter->Branch (pFallExit);
    } else if (Tag & TagConditional) {
        ComPtr<ICpuValue> Cond;
        if (SUCCEEDED (pArch->TranslateCond (Pc, Emitter, &Cond)) && Cond != nullptr) {
            Emitter->CondBranch (Cond, pTakenExit, pFallExit);
        } else {
            Emitter->Branch (pFallExit);   // condition unavailable; treat as not-taken
        }
    } else if (Tag & TagBranch) {
        Emitter->Branch (pTakenExit);   // static direct jump
    } else {
        Emitter->Branch (pFallExit);    // TagContinue / fall-through
    }

    // Taken-exit: record NewPc as the resolved successor, then end execution.
    Emitter->SetInsertBlock (pTakenExit);
    {
        ComPtr<ICpuValue> Val;
        Emitter->ConstInt (64, (UINT64) NewPc, &Val);
        pSmc->SetDispatchTarget (Val);
    }
    Emitter->Branch (pExit);

    // Fall-exit: record NextPc as the resolved successor, then end execution.
    Emitter->SetInsertBlock (pFallExit);
    {
        ComPtr<ICpuValue> Val;
        Emitter->ConstInt (64, (UINT64) NextPc, &Val);
        pSmc->SetDispatchTarget (Val);
    }
    Emitter->Branch (pExit);

    // pExit has no body -- its BlockStart stays 0xFFFFFFFF, which the interpreter
    // resolves to "past the end" so branching to it ends execution cleanly.

    pSmc->Release ();
    if (pBody      != nullptr) { pBody->Release ();      }
    if (pTakenExit != nullptr) { pTakenExit->Release (); }
    if (pFallExit  != nullptr) { pFallExit->Release ();  }
    if (pExit      != nullptr) { pExit->Release ();      }

    return pBackend->Compile (Emitter, ppCode);
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
