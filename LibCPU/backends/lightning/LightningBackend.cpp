/** @file
  GNU Lightning textual-less JIT backend. See LightningBackend.h.

  Values are 8-byte stack slots (jit_allocai), masked to width. JIT_V0 holds the
  RAM pointer and JIT_V1 the register-file pointer for the whole function; JIT_R0
  /R1/R2 are per-op scratch.
**/
#include "LightningBackend.h"
#include "LibCPU/CpuState.h"

#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <vector>

extern "C" {
#include <lightning.h>
}

namespace LibCPU {
namespace {

static UINT64 Mask (UINT64 V, UINT32 Bits) { return (Bits >= 64) ? V : (V & (((UINT64) 1 << Bits) - 1)); }
// Temp slots are reset at every block entry, so this need only cover the widest single instruction.
static CONST UINT32 NUM_SLOTS = 256;

class LnValue final : public ComObject<ICpuValue> {
public:
    LnValue (INT32 Slot, UINT32 Bits) : m_Slot (Slot), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    INT32  m_Slot;
    UINT32 m_Bits;
};

class LnBlock final : public ComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
    jit_node_t              *m_Label = nullptr;   // emitted when the block is first inserted into
    std::vector<jit_node_t *> m_Pending;          // jumps awaiting this block's label
};

static INT32  SlotOf (ICpuValue *pV) { return static_cast<LnValue *> (pV)->m_Slot; }
static UINT32 BitsOf (ICpuValue *pV) { return static_cast<LnValue *> (pV)->m_Bits; }

typedef int (*JittedFn) (void *pRAM, void *pGRF, void *pFRF);

class LnCode final : public ComObject<ICpuCode> {
public:
    LnCode (jit_state_t *Jit, JittedFn Fn) : _jit (Jit), m_Fn (Fn) {}
    ~LnCode () override { if (_jit) jit_destroy_state (); }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }
    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID *pFRF) override {
        return (CPU_EXEC_STATUS) m_Fn (pRAM, pGRF, pFRF);
    }
private:
    jit_state_t *_jit;   // kept alive: holds the executable code
    JittedFn     m_Fn;
};

class LnEmitter final : public ComObject<ICpuEmitter>, public ICpuSmcEmitter,
                        public ICpuSystemEmitter, public ICpuSyscallEmitter, public ICpuClockEmitter {
public:
    LnEmitter () {
        _jit = jit_new_state ();
        jit_prolog ();
        jit_node_t *a0 = jit_arg ();
        jit_node_t *a1 = jit_arg ();
        jit_node_t *a2 = jit_arg ();
        jit_getarg_l (JIT_V0, a0);   // RAM
        jit_getarg_l (JIT_V1, a1);   // GRF
        // One fixed temp frame, reused per block (a value never outlives its instruction). Allocating
        // a slot per value instead would grow the frame without bound on a large window and overflow.
        m_FrameBase = jit_allocai (NUM_SLOTS * 8);
        (void) a2;                   // FRF unused
    }
    ~LnEmitter () override {
        for (LnBlock *pB : m_Blocks) { pB->Release (); }   // release m_Blocks' retained references
        if (_jit) { jit_destroy_state (); }
    }

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

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        INT32 S = Slot (); jit_movi (JIT_R0, (jit_word_t) Mask (Value, Bits)); Store (S); return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        INT32 S = Slot ();
        jit_ldxi_l (JIT_R0, JIT_V1, CPU_STATE_REG_OFFSET + Index * 8);
        MaskReg (Bits);
        Store (S); return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 /*Bits*/, BOOLEAN /*Sext*/) override {
        jit_ldxi_l (JIT_R0, JIT_FP, SlotOf (pValue));
        jit_stxi_l (CPU_STATE_REG_OFFSET + Index * 8, JIT_V1, JIT_R0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        INT32 S = Slot ();
        jit_ldxi_l (JIT_R1, JIT_FP, SlotOf (pAddr));
        jit_addr (JIT_R2, JIT_V0, JIT_R1);
        switch (Bits) {
        case 8:  jit_ldxi_uc (JIT_R0, JIT_R2, 0); break;
        case 16: jit_ldxi_us (JIT_R0, JIT_R2, 0); break;
        case 32: jit_ldxi_ui (JIT_R0, JIT_R2, 0); break;
        default: jit_ldxi_l  (JIT_R0, JIT_R2, 0); break;
        }
        Store (S); return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        jit_ldxi_l (JIT_R0, JIT_FP, SlotOf (pValue));
        jit_ldxi_l (JIT_R1, JIT_FP, SlotOf (pAddr));
        jit_addr (JIT_R2, JIT_V0, JIT_R1);
        switch (Bits) {
        case 8:  jit_stxi_c (0, JIT_R2, JIT_R0); break;
        case 16: jit_stxi_s (0, JIT_R2, JIT_R0); break;
        case 32: jit_stxi_i (0, JIT_R2, JIT_R0); break;
        default: jit_stxi_l (0, JIT_R2, JIT_R0); break;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA); INT32 S = Slot ();
        jit_ldxi_l (JIT_R0, JIT_FP, SlotOf (pA));
        jit_ldxi_l (JIT_R1, JIT_FP, SlotOf (pB));
        switch (Op) {
        case BinAdd: jit_addr (JIT_R0, JIT_R0, JIT_R1); break;
        case BinSub: jit_subr (JIT_R0, JIT_R0, JIT_R1); break;
        case BinMul: jit_mulr (JIT_R0, JIT_R0, JIT_R1); break;
        case BinAnd: jit_andr (JIT_R0, JIT_R0, JIT_R1); break;
        case BinOr:  jit_orr  (JIT_R0, JIT_R0, JIT_R1); break;
        case BinXor: jit_xorr (JIT_R0, JIT_R0, JIT_R1); break;
        case BinShl: jit_lshr (JIT_R0, JIT_R0, JIT_R1); break;
        case BinLShr:jit_rshr_u (JIT_R0, JIT_R0, JIT_R1); break;
        case BinAShr:jit_rshr (JIT_R0, JIT_R0, JIT_R1); break;
        case BinUDiv:jit_divr_u (JIT_R0, JIT_R0, JIT_R1); break;
        case BinSDiv:jit_divr (JIT_R0, JIT_R0, JIT_R1); break;
        case BinURem:jit_remr_u (JIT_R0, JIT_R0, JIT_R1); break;
        case BinSRem:jit_remr (JIT_R0, JIT_R0, JIT_R1); break;
        default: break;
        }
        MaskReg (Bits);
        Store (S); return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA); INT32 S = Slot ();
        jit_ldxi_l (JIT_R0, JIT_FP, SlotOf (pA));
        switch (Op) {
        case UnNeg: jit_negr (JIT_R0, JIT_R0); MaskReg (Bits); Store (S); return Make (S, Bits, ppValue);
        case UnCom: jit_comr (JIT_R0, JIT_R0); MaskReg (Bits); Store (S); return Make (S, Bits, ppValue);
        case UnNot: jit_eqi (JIT_R0, JIT_R0, 0); Store (S); return Make (S, 1, ppValue);
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        INT32 S = Slot ();
        jit_ldxi_l (JIT_R0, JIT_FP, SlotOf (pA));
        jit_ldxi_l (JIT_R1, JIT_FP, SlotOf (pB));
        switch (Pred) {
        case CmpEq:  jit_eqr (JIT_R0, JIT_R0, JIT_R1); break;
        case CmpNe:  jit_ner (JIT_R0, JIT_R0, JIT_R1); break;
        case CmpULt: jit_ltr_u (JIT_R0, JIT_R0, JIT_R1); break;
        case CmpULe: jit_ler_u (JIT_R0, JIT_R0, JIT_R1); break;
        case CmpUGt: jit_gtr_u (JIT_R0, JIT_R0, JIT_R1); break;
        case CmpUGe: jit_ger_u (JIT_R0, JIT_R0, JIT_R1); break;
        case CmpSLt: jit_ltr (JIT_R0, JIT_R0, JIT_R1); break;
        case CmpSLe: jit_ler (JIT_R0, JIT_R0, JIT_R1); break;
        case CmpSGt: jit_gtr (JIT_R0, JIT_R0, JIT_R1); break;
        case CmpSGe: jit_ger (JIT_R0, JIT_R0, JIT_R1); break;
        }
        Store (S); return Make (S, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA); INT32 S = Slot ();
        jit_ldxi_l (JIT_R0, JIT_FP, SlotOf (pA));
        switch (Op) {
        case CastTrunc: MaskReg (Bits); break;
        case CastZExt:  break;
        case CastSExt:
            if (SrcBits == 8)       jit_extr_c (JIT_R0, JIT_R0);
            else if (SrcBits == 16) jit_extr_s (JIT_R0, JIT_R0);
            else if (SrcBits == 32) jit_extr_i (JIT_R0, JIT_R0);
            MaskReg (Bits);
            break;
        }
        Store (S); return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *pCond, ICpuValue *pTrue, ICpuValue *pFalse, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pTrue);
        INT32  S    = Slot ();
        jit_ldxi_l (JIT_R0, JIT_FP, SlotOf (pFalse));        // default = false value
        jit_ldxi_l (JIT_R1, JIT_FP, SlotOf (pCond));
        jit_node_t *pSkip = jit_beqi (JIT_R1, 0);            // cond == 0 -> keep false
        jit_ldxi_l (JIT_R0, JIT_FP, SlotOf (pTrue));
        jit_patch_at (pSkip, jit_label ());
        Store (S); return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        INT32 S = Slot ();
        jit_ldxi_uc (JIT_R0, JIT_V1, CPU_STATE_FLAG_OFFSET + (UINT32) Flag);
        jit_andi (JIT_R0, JIT_R0, 1);
        Store (S); return Make (S, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        jit_ldxi_l (JIT_R0, JIT_FP, SlotOf (pValue));
        jit_andi (JIT_R0, JIT_R0, 1);
        jit_stxi_c (CPU_STATE_FLAG_OFFSET + (UINT32) Flag, JIT_V1, JIT_R0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        jit_movi (JIT_R0, (jit_word_t) Pc);
        jit_stxi_l (CPU_STATE_PC_OFFSET, JIT_V1, JIT_R0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override {
        LnBlock *pB = new LnBlock ();                    // refcount 1: the reference returned to the caller
        pB->AddRef ();                                   // refcount 2: m_Blocks retains its own reference for
        m_Blocks.push_back (pB);                         // the exit pass in Build -- the caller (frontend or
        *ppBlock = pB;                                   // generator) owns blocks via ComPtr and may release
        return S_OK;                                     // them (e.g. a REP prefix's helper blocks) pre-Build
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *pBlock) override {
        LnBlock *pB = static_cast<LnBlock *> (pBlock);
        pB->m_Label = jit_label ();
        for (jit_node_t *pJ : pB->m_Pending) { jit_patch_at (pJ, pB->m_Label); }
        pB->m_Pending.clear ();
        m_Cur      = pB;
        m_NextSlot = 0;            // an instruction's temps don't outlive its block: reuse the frame
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override {
        if (m_Cur != nullptr) { m_Cur->AddRef (); }
        *ppBlock = m_Cur;
        return m_Cur != nullptr ? S_OK : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *pTarget) override {
        WireJump (jit_jmpi (), static_cast<LnBlock *> (pTarget));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *pCond, ICpuBlock *pTrue, ICpuBlock *pFalse) override {
        jit_ldxi_l (JIT_R0, JIT_FP, SlotOf (pCond));
        WireJump (jit_bnei (JIT_R0, 0), static_cast<LnBlock *> (pTrue));    // cond != 0 -> true
        WireJump (jit_jmpi (), static_cast<LnBlock *> (pFalse));
        return S_OK;
    }

    // ---- self-modifying-code / dispatch (ICpuSmcEmitter) ------------------
    HRESULT STDMETHODCALLTYPE EmitCodeGuard (CPU_ADDR Pc) override {
        UINT32     Page = (UINT32) ((Pc >> CPU_SMC_PAGE_SHIFT) & (CPU_SMC_PAGE_COUNT - 1));
        jit_word_t Mask = (jit_word_t) (1u << (Page & 7));
        jit_ldxi_uc (JIT_R0, JIT_V1, CPU_STATE_CODEDIRTY_OFFSET + (Page >> 3));
        jit_andi (JIT_R0, JIT_R0, Mask);
        jit_node_t *pSkip = jit_beqi (JIT_R0, 0);            // clean -> skip the trap
        jit_movi (JIT_R0, (jit_word_t) Pc);
        jit_stxi_l (CPU_STATE_TRAPPC_OFFSET, JIT_V1, JIT_R0);
        jit_movi (JIT_R0, ExecSmc);
        jit_retr (JIT_R0);
        jit_patch_at (pSkip, jit_label ());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IndirectBranch (ICpuValue *pTargetPc) override {
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pTargetPc));
        return TrapReturn ();
    }
    HRESULT STDMETHODCALLTYPE SetDispatchTarget (ICpuValue *pTargetPc) override {
        StoreField (CPU_STATE_DISPPC_OFFSET, SlotOf (pTargetPc));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDispatchTarget (ICpuValue **ppValue) override {
        INT32 S = Slot ();
        jit_ldxi_l (JIT_R0, JIT_V1, CPU_STATE_DISPPC_OFFSET);
        Store (S);
        return Make (S, 64, ppValue);
    }

    // ---- port I/O + system traps (ICpuSystemEmitter / ICpuSyscallEmitter) -
    HRESULT STDMETHODCALLTYPE EmitPortOut (ICpuValue *pPort, ICpuValue *pData, UINT32 Width, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (jit_word_t) CPU_IO_MAKE (CPU_IO_OUT, Width));
        StoreField (CPU_STATE_IOPORT_OFFSET, SlotOf (pPort));
        StoreField (CPU_STATE_IODATA_OFFSET, SlotOf (pData));
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        return TrapReturn ();
    }
    HRESULT STDMETHODCALLTYPE EmitPortIn (ICpuValue *pPort, UINT32 Width, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (jit_word_t) CPU_IO_MAKE (CPU_IO_IN, Width));
        StoreField (CPU_STATE_IOPORT_OFFSET, SlotOf (pPort));
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        return TrapReturn ();
    }
    HRESULT STDMETHODCALLTYPE EmitSystemTrap (UINT32 Reason, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (jit_word_t) CPU_IO_MAKE (Reason, 0));
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        return TrapReturn ();
    }
    HRESULT STDMETHODCALLTYPE EmitSystemTrapValue (UINT32 Reason, ICpuValue *pValue, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (jit_word_t) CPU_IO_MAKE (Reason, 0));
        StoreField (CPU_STATE_IODATA_OFFSET, SlotOf (pValue));
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        return TrapReturn ();
    }
    HRESULT STDMETHODCALLTYPE EmitSyscall (UINT32 Vector, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_SYSCALL_OFFSET, (jit_word_t) Vector);
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        return TrapReturn ();
    }

    // ---- time base (ICpuClockEmitter): Cycles += Count, once per guest instruction --------
    HRESULT STDMETHODCALLTYPE EmitTick (UINT32 Count) override {
        jit_ldxi_l (JIT_R0, JIT_V1, CPU_STATE_CYCLES_OFFSET);
        jit_addi (JIT_R0, JIT_R0, (jit_word_t) Count);
        jit_stxi_l (CPU_STATE_CYCLES_OFFSET, JIT_V1, JIT_R0);
        return S_OK;
    }

    ICpuCode *Build () {
        jit_node_t *pExit = jit_label ();                    // shared exit for never-inserted blocks
        for (LnBlock *pB : m_Blocks) {
            if (pB->m_Label == nullptr) {
                for (jit_node_t *pJ : pB->m_Pending) { jit_patch_at (pJ, pExit); }
                pB->m_Pending.clear ();
                pB->m_Label = pExit;
            }
        }
        jit_movi (JIT_R0, ExecOk);
        jit_retr (JIT_R0);
        JittedFn Fn = (JittedFn) jit_emit ();
        jit_clear_state ();
        jit_state_t *Owned = _jit;
        _jit = nullptr;                // ownership transferred to LnCode
        return new LnCode (Owned, Fn);
    }

private:
    INT32 Slot () { return m_FrameBase + (INT32) (m_NextSlot++ * 8); }
    void  Store (INT32 S) { jit_stxi_l (S, JIT_FP, JIT_R0); }
    void  MaskReg (UINT32 Bits) { if (Bits < 64) jit_andi (JIT_R0, JIT_R0, (jit_word_t) Mask (~0ull, Bits)); }
    HRESULT Make (INT32 S, UINT32 Bits, ICpuValue **ppValue) { *ppValue = new LnValue (S, Bits); return S_OK; }
    void  WireJump (jit_node_t *pJump, LnBlock *pTarget) {
        if (pTarget->m_Label != nullptr) { jit_patch_at (pJump, pTarget->m_Label); }
        else                             { pTarget->m_Pending.push_back (pJump); }
    }
    void  StoreField (UINT32 Off, INT32 Slot) {              // CPU_STATE[Off] = slot value (full word)
        jit_ldxi_l (JIT_R0, JIT_FP, Slot);
        jit_stxi_l (Off, JIT_V1, JIT_R0);
    }
    void  StoreFieldImm (UINT32 Off, jit_word_t Imm) {
        jit_movi (JIT_R0, Imm);
        jit_stxi_l (Off, JIT_V1, JIT_R0);
    }
    HRESULT TrapReturn () {                                  // hand control back to the host machine loop
        jit_movi (JIT_R0, ExecSmc);
        jit_retr (JIT_R0);
        return S_OK;
    }

    jit_state_t              *_jit;
    LnBlock                  *m_Cur = nullptr;
    std::vector<LnBlock *>    m_Blocks;
    INT32                     m_FrameBase = 0;     // base offset of the fixed temp frame
    UINT32                    m_NextSlot  = 0;     // next free slot within the current block
};

class LightningBackend final : public ComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "lightning"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new LnEmitter (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<LnEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateLightningBackend (VOID)
{
    static std::atomic<bool> Inited{false};
    bool Expected = false;
    if (Inited.compare_exchange_strong (Expected, true)) {
        init_jit ("libcpu");
    }
    return new LightningBackend ();
}

} // namespace LibCPU
