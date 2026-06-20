/** @file  Shadow CFG -- universal machine capabilities for any backend. See ShadowCfg.h. */

#include "ShadowCfg.h"
#include "LibCPU/PCom.h"
#include <cstdio>
#include <map>

namespace LibCPU {

namespace {

// Width of the shadow scratch registers + the next-PC values. 32 bits faithfully holds every
// shadow quantity (a 16-bit IP, the status+width word, a CS/port/data operand) and is stored
// correctly by every backend -- unlike 64 bits, which the <=32-bit-guest backends (js, cmd,
// pwsh) do not round-trip.
static UINT32 CONST SHADOW_W = 32;

//
// Synthesize Select(cond, t, f) for a backend whose emitter does not implement it natively.
// cond is an i1 from Compare; widen it, build an all-ones/all-zero mask, and blend:
//   r = f ^ (mask & (t ^ f))     mask = 0 - zext(cond)   (all-ones when cond, else 0)
// Uses only Cast/ConstInt/BinaryOp -- the core ops every backend implements.
//
static HRESULT
SynthSelect (ICpuEmitter *pE, ICpuValue *pCond, ICpuValue *pTrue, ICpuValue *pFalse,
             UINT32 Bits, OUT ICpuValue **ppOut)
{
    // Prefer the native op when present.
    if (SUCCEEDED (pE->Select (pCond, pTrue, pFalse, ppOut)) && *ppOut != nullptr) { return S_OK; }

    ComPtr<ICpuValue> CondW, Zero, Mask, Xtf, Masked;
    if (FAILED (pE->Cast (CastZExt, pCond, Bits, &CondW)) || CondW == nullptr) { return E_FAIL; }
    if (FAILED (pE->ConstInt (Bits, 0, &Zero)) || Zero == nullptr) { return E_FAIL; }
    if (FAILED (pE->BinaryOp (BinSub, Zero.Get (), CondW.Get (), &Mask)) || Mask == nullptr) { return E_FAIL; }
    if (FAILED (pE->BinaryOp (BinXor, pTrue, pFalse, &Xtf)) || Xtf == nullptr) { return E_FAIL; }
    if (FAILED (pE->BinaryOp (BinAnd, Mask.Get (), Xtf.Get (), &Masked)) || Masked == nullptr) { return E_FAIL; }
    return pE->BinaryOp (BinXor, pFalse, Masked.Get (), ppOut);
}

//
// ShadowEmitter wraps an inner emitter. Data ops forward verbatim (returning the inner
// backend's values); control flow and the machine trap capabilities are lowered to writes of
// the reserved scratch registers (SHADOW_REG_*) using the inner's core PutRegister, so the
// inner backend never needs native branches or capability interfaces. Intra-instruction
// control flow (the V20 REP string loop) is flagged via m_UsedCfg for the driver to reject
// (handled by host-redispatch in a later step).
//
class ShadowEmitter final : public ComObject<ICpuEmitter>, public ICpuSmcEmitter,
                            public ICpuSystemEmitter, public ICpuSyscallEmitter, public ICpuClockEmitter {
public:
    explicit ShadowEmitter (ICpuEmitter *pInner) : m_pInner (pInner) { if (m_pInner) { m_pInner->AddRef (); } }
    ~ShadowEmitter () { if (m_pInner) { m_pInner->Release (); } }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_ICpuSmcEmitter))     { *ppvObject = static_cast<ICpuSmcEmitter *> (this);     AddRef (); return S_OK; }
        if (CompareGuid (&riid, &IID_ICpuSystemEmitter))  { *ppvObject = static_cast<ICpuSystemEmitter *> (this);  AddRef (); return S_OK; }
        if (CompareGuid (&riid, &IID_ICpuSyscallEmitter)) { *ppvObject = static_cast<ICpuSyscallEmitter *> (this); AddRef (); return S_OK; }
        if (CompareGuid (&riid, &IID_ICpuClockEmitter))   { *ppvObject = static_cast<ICpuClockEmitter *> (this);   AddRef (); return S_OK; }
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }
    UINT32 STDMETHODCALLTYPE AddRef () override { return ComObject<ICpuEmitter>::AddRef (); }
    UINT32 STDMETHODCALLTYPE Release () override { return ComObject<ICpuEmitter>::Release (); }

    // --- data ops: forward to the inner emitter (its values flow straight back), recording each
    //     result's bit width so a synthesized Select (below) knows the operand width. ICpuValue is
    //     an opaque handle with no width accessor, so the shadow layer tracks it here. -----------
    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 B, UINT64 V, ICpuValue **pp) override { return RW (m_pInner->ConstInt (B, V, pp), pp, B); }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 I, UINT32 B, ICpuValue **pp) override { return RW (m_pInner->GetRegister (I, B, pp), pp, B); }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 I, ICpuValue *pV, UINT32 B, BOOLEAN S) override { return m_pInner->PutRegister (I, pV, B, S); }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pA, UINT32 B, ICpuValue **pp) override { return RW (m_pInner->Load (pA, B, pp), pp, B); }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pV, ICpuValue *pA, UINT32 B) override { return m_pInner->Store (pV, pA, B); }
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP O, ICpuValue *pA, ICpuValue *pB, ICpuValue **pp) override { return RW (m_pInner->BinaryOp (O, pA, pB, pp), pp, GetW (pA)); }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP O, ICpuValue *pA, ICpuValue **pp) override { return RW (m_pInner->UnaryOp (O, pA, pp), pp, GetW (pA)); }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP P, ICpuValue *pA, ICpuValue *pB, ICpuValue **pp) override { return RW (m_pInner->Compare (P, pA, pB, pp), pp, 1); }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST O, ICpuValue *pA, UINT32 B, ICpuValue **pp) override { return RW (m_pInner->Cast (O, pA, B, pp), pp, B); }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *pC, ICpuValue *pT, ICpuValue *pF, ICpuValue **pp) override {
        // Synthesize from the core ops when the inner backend has no native Select (it returns
        // null/E_NOTIMPL); this makes the FRONTEND's Select calls valid on any backend, instead of
        // handing a null value to the next op. The operand width comes from the tracked map.
        UINT32 W = GetW (pT);
        if (W == 0) { W = GetW (pF); }
        if (W == 0) { W = 16; }
        return RW (SynthSelect (m_pInner, pC, pT, pF, W, pp), pp, W);
    }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG F, ICpuValue **pp) override { return RW (m_pInner->GetFlag (F, pp), pp, 1); }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG F, ICpuValue *pV) override { return m_pInner->SetFlag (F, pV); }

    // --- control flow: intra-instruction CFG (REP) is unsupported in this step ------------
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **pp) override { m_UsedCfg = true; if (pp) { *pp = nullptr; } return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { m_UsedCfg = true; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **pp) override { m_UsedCfg = true; if (pp) { *pp = nullptr; } return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { m_UsedCfg = true; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { m_UsedCfg = true; return E_NOTIMPL; }

    // A static PC set the frontend may emit: record it as the unit's plain next-PC.
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        ComPtr<ICpuValue> P;
        if (FAILED (m_pInner->ConstInt (SHADOW_W, (UINT64) Pc, &P)) || P == nullptr) { return E_FAIL; }
        return TerminateNext (P.Get ());
    }

    // --- ICpuSmcEmitter: indirect/RET/far-jump targets, SMC guard, dispatch scratch -------
    HRESULT STDMETHODCALLTYPE EmitCodeGuard (CPU_ADDR) override { return S_OK; }   // SMC re-check is host-side between blocks
    HRESULT STDMETHODCALLTYPE IndirectBranch (ICpuValue *pTargetPc) override { return TerminateNext (pTargetPc); }
    HRESULT STDMETHODCALLTYPE SetDispatchTarget (ICpuValue *pTargetPc) override { m_pDispatch = pTargetPc; if (pTargetPc) { pTargetPc->AddRef (); } return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDispatchTarget (ICpuValue **pp) override {
        if (pp == nullptr) { return E_POINTER; }
        *pp = m_pDispatch; if (m_pDispatch) { m_pDispatch->AddRef (); }
        return m_pDispatch ? S_OK : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE GetCodeBase (ICpuValue **pp) override {
        // No PIC relativization in the shadow path: the unit runs at its guest address.
        return m_pInner->ConstInt (64, 0, pp);
    }

    // --- ICpuSystemEmitter: port I/O and privileged control -> trap to the host -----------
    HRESULT STDMETHODCALLTYPE EmitPortOut (ICpuValue *pPort, ICpuValue *pData, UINT32 Width, ICpuValue *pRet) override {
        return TerminateTrap (SHADOW_ST_PORTOUT | ((UINT64) Width << 8), pPort, pData, pRet);
    }
    HRESULT STDMETHODCALLTYPE EmitPortIn (ICpuValue *pPort, UINT32 Width, ICpuValue *pRet) override {
        return TerminateTrap (SHADOW_ST_PORTIN | ((UINT64) Width << 8), pPort, nullptr, pRet);
    }
    HRESULT STDMETHODCALLTYPE EmitSystemTrap (UINT32 Reason, ICpuValue *pRet) override {
        return TerminateTrapImm (SHADOW_ST_SYSTRAP, (UINT64) Reason, nullptr, pRet);   // A=reason
    }
    HRESULT STDMETHODCALLTYPE EmitSystemTrapValue (UINT32 Reason, ICpuValue *pValue, ICpuValue *pRet) override {
        if (FAILED (PutScratchImm (SHADOW_REG_A, (UINT64) Reason))) { return E_FAIL; }   // A=reason
        return TerminateTrap (SHADOW_ST_SYSTRAP, nullptr, pValue, pRet);                 // B=value
    }

    // --- ICpuSyscallEmitter: INT n -> vector to the host ---------------------------------
    HRESULT STDMETHODCALLTYPE EmitSyscall (UINT32 Vector, ICpuValue *pRet) override {
        return TerminateTrapImm (SHADOW_ST_SYSCALL, (UINT64) Vector, nullptr, pRet);
    }

    // --- ICpuClockEmitter: a plain counter bump (forwarded if the inner supports it) ------
    HRESULT STDMETHODCALLTYPE EmitTick (UINT32 Count) override {
        ICpuClockEmitter *pClk = nullptr;
        if (SUCCEEDED (m_pInner->QueryInterface (IID_ICpuClockEmitter, (VOID **) &pClk)) && pClk != nullptr) {
            HRESULT hr = pClk->EmitTick (Count);
            pClk->Release ();
            return hr;
        }
        return S_OK;   // System credits a representative cost per burst when Cycles is untouched
    }

    // Helpers used by GenerateShadowUnit for the unit terminator.
    HRESULT TerminateNext (ICpuValue *pNextPc) {
        if (FAILED (PutScratch (SHADOW_REG_NEXTPC, pNextPc))) { return E_FAIL; }
        m_Terminated = true;
        return PutScratchImm (SHADOW_REG_STATUS, SHADOW_ST_NEXT);
    }
    bool UsedCfg () CONST { return m_UsedCfg; }
    bool Terminated () CONST { return m_Terminated; }
    ICpuEmitter *Inner () CONST { return m_pInner; }

private:
    // Record a freshly-produced value's bit width (keyed by the inner backend's value pointer).
    HRESULT RW (HRESULT hr, ICpuValue **pp, UINT32 Bits) {
        if (SUCCEEDED (hr) && pp != nullptr && *pp != nullptr && Bits != 0) { m_Bits[*pp] = Bits; }
        return hr;
    }
    UINT32 GetW (ICpuValue *pV) CONST {
        std::map<ICpuValue *, UINT32>::const_iterator It = m_Bits.find (pV);
        return It != m_Bits.end () ? It->second : 0;
    }
    HRESULT PutScratch (UINT32 Reg, ICpuValue *pV) { return m_pInner->PutRegister (Reg, pV, SHADOW_W, FALSE); }
    HRESULT PutScratchImm (UINT32 Reg, UINT64 V) {
        ComPtr<ICpuValue> C;
        if (FAILED (m_pInner->ConstInt (SHADOW_W, V, &C)) || C == nullptr) { return E_FAIL; }
        return m_pInner->PutRegister (Reg, C.Get (), SHADOW_W, FALSE);
    }
    HRESULT TerminateTrap (UINT64 Status, ICpuValue *pA, ICpuValue *pB, ICpuValue *pRet) {
        if (pA != nullptr && FAILED (PutScratch (SHADOW_REG_A, pA))) { return E_FAIL; }
        if (pB != nullptr && FAILED (PutScratch (SHADOW_REG_B, pB))) { return E_FAIL; }
        if (pRet != nullptr && FAILED (PutScratch (SHADOW_REG_NEXTPC, pRet))) { return E_FAIL; }
        m_Terminated = true;
        return PutScratchImm (SHADOW_REG_STATUS, Status);
    }
    HRESULT TerminateTrapImm (UINT64 Status, UINT64 A, ICpuValue *pB, ICpuValue *pRet) {
        if (FAILED (PutScratchImm (SHADOW_REG_A, A))) { return E_FAIL; }
        return TerminateTrap (Status, nullptr, pB, pRet);
    }

    ICpuEmitter                  *m_pInner;
    ICpuValue                    *m_pDispatch = nullptr;
    bool                          m_UsedCfg   = false;
    bool                          m_Terminated = false;
    std::map<ICpuValue *, UINT32> m_Bits;          // value -> bit width, for synthesizing Select
};

} // anonymous namespace

bool
BackendHasNativeCfg (ICpuArchitecture *pArch, ICpuBackend *pBackend)
{
    ComPtr<ICpuEmitter> E;
    if (FAILED (pBackend->CreateEmitter (pArch, &E)) || E == nullptr) { return false; }
    ComPtr<ICpuBlock> A, B;
    if (FAILED (E->CreateBlock ("probe_a", &A)) || A == nullptr) { return false; }
    if (FAILED (E->CreateBlock ("probe_b", &B)) || B == nullptr) { return false; }
    E->SetInsertBlock (A.Get ());
    return SUCCEEDED (E->Branch (B.Get ()));   // E_NOTIMPL from a leaf backend -> needs the shadow path
}

bool
BackendHasSystemEmitter (ICpuArchitecture *pArch, ICpuBackend *pBackend)
{
    // The machine path emits port I/O, software interrupts and the per-instruction cycle clock; a
    // backend that does not implement ICpuSystemEmitter cannot translate those, so even if it has
    // native CFG it must drive the machine through the shadow path (which synthesises them from the
    // basic emitter). The three machine emitters travel together in every backend, so the System
    // one is a sufficient sentinel.
    ComPtr<ICpuEmitter> E;
    if (FAILED (pBackend->CreateEmitter (pArch, &E)) || E == nullptr) { return false; }
    ComPtr<ICpuSystemEmitter> Sys;
    E->QueryInterface (IID_ICpuSystemEmitter, (VOID **) &Sys);
    return Sys != nullptr;
}

HRESULT
GenerateShadowUnit (ICpuArchitecture *pArch, ICpuBackend *pInner, CPU_ADDR Entry, CPU_ADDR End,
                    OUT ICpuCode **ppCode)
{
    if (ppCode == nullptr) { return E_POINTER; }
    *ppCode = nullptr;

    ComPtr<ICpuEmitter> InnerE;
    if (FAILED (pInner->CreateEmitter (pArch, &InnerE)) || InnerE == nullptr) { return E_FAIL; }
    ComPtr<ShadowEmitter> Shadow (new ShadowEmitter (InnerE.Get ()));

    // Uniform time base: one tick per retired guest instruction, exactly as GenerateAotCfg does,
    // so CPU_STATE.Cycles advances identically whether a block ran via the shadow path or natively.
    // Without it the cycle clock stalls (the host credits a flat per-burst cost) and time-driven
    // devices (the PIT) drift, which diverges the machine. The tick forwards to the inner backend.
    ComPtr<ICpuClockEmitter> Clk;
    Shadow->QueryInterface (IID_ICpuClockEmitter, (VOID **) &Clk);

    // Reject an out-of-window entry up front: a guest that branches to a bad PC (e.g. executing
    // non-code, like an unbootable boot sector) must FAULT, not read past the code memory and
    // crash the host. The run loop turns this E_FAIL into a clean machine fault.
    if (Entry >= End) { return E_FAIL; }

    // Decode ONE guest basic block: emit each instruction's body through the shadow emitter
    // (straight-line into the inner backend) and stop at the first terminator or trap. Every
    // exit leaves the next guest PC / trap outcome in the scratch registers for the host.
    CPU_ADDR Pc = Entry;
    for (UINT32 Guard = 0; Guard < 4096; Guard++) {
        if (Pc >= End) {                                       // ran to the window edge: resume there
            ComPtr<ICpuValue> T;
            if (FAILED (Shadow->Inner ()->ConstInt (SHADOW_W, (UINT64) Pc, &T)) || T == nullptr) { return E_FAIL; }
            if (FAILED (Shadow->TerminateNext (T.Get ()))) { return E_FAIL; }
            break;
        }
        UINT32   Tag;
        CPU_ADDR NewPc;
        CPU_ADDR NextPc;
        if (FAILED (pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc))) { return E_FAIL; }

        if (FAILED (pArch->TranslateInstr (Pc, Shadow.Get ()))) { return E_FAIL; }
        if (Shadow->UsedCfg ()) { return E_FAIL; }              // intra-instruction loop (REP): not yet handled
        if (Clk != nullptr) { Clk->EmitTick (1); }              // one tick per retired guest instruction
        if (Shadow->Terminated ()) { break; }                  // a trap/indirect instruction ended the unit

        if (Tag & TagReturn) {
            // RET emits IndirectBranch internally; if it did not terminate, fail rather than guess.
            if (!Shadow->Terminated ()) { return E_FAIL; }
            break;
        }
        if (Tag & TagConditional) {
            ComPtr<ICpuValue> Cond, T, F, Next;
            if (FAILED (pArch->TranslateCond (Pc, Shadow.Get (), &Cond)) || Cond == nullptr) { return E_FAIL; }
            if (FAILED (Shadow->Inner ()->ConstInt (SHADOW_W, (UINT64) NewPc, &T)) || T == nullptr) { return E_FAIL; }
            if (FAILED (Shadow->Inner ()->ConstInt (SHADOW_W, (UINT64) NextPc, &F)) || F == nullptr) { return E_FAIL; }
            if (FAILED (SynthSelect (Shadow->Inner (), Cond.Get (), T.Get (), F.Get (), SHADOW_W, &Next)) || Next == nullptr) { return E_FAIL; }
            if (FAILED (Shadow->TerminateNext (Next.Get ()))) { return E_FAIL; }
            break;
        }
        if (Tag & (TagBranch | TagCall)) {
            ComPtr<ICpuValue> T;
            if (FAILED (Shadow->Inner ()->ConstInt (SHADOW_W, (UINT64) NewPc, &T)) || T == nullptr) { return E_FAIL; }
            if (FAILED (Shadow->TerminateNext (T.Get ()))) { return E_FAIL; }
            break;
        }

        // TagContinue: straight-line. Advance to the next instruction; the window-edge case is
        // handled at the top of the loop (resume there for the host to re-dispatch).
        Pc = NextPc;
    }

    return pInner->Compile (Shadow->Inner (), ppCode);
}

} // namespace LibCPU
