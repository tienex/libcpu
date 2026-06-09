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

extern "C" {
#include <lightning.h>
}

namespace LibCPU {
namespace {

static UINT64 Mask (UINT64 V, UINT32 Bits) { return (Bits >= 64) ? V : (V & (((UINT64) 1 << Bits) - 1)); }

class LnValue final : public LcComObject<ICpuValue> {
public:
    LnValue (INT32 Slot, UINT32 Bits) : m_Slot (Slot), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    INT32  m_Slot;
    UINT32 m_Bits;
};

class LnBlock final : public LcComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
};

static INT32  SlotOf (ICpuValue *pV) { return static_cast<LnValue *> (pV)->m_Slot; }
static UINT32 BitsOf (ICpuValue *pV) { return static_cast<LnValue *> (pV)->m_Bits; }

typedef int (*JittedFn) (void *pRAM, void *pGRF, void *pFRF);

class LnCode final : public LcComObject<ICpuCode> {
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

class LnEmitter final : public LcComObject<ICpuEmitter> {
public:
    LnEmitter () {
        _jit = jit_new_state ();
        jit_prolog ();
        jit_node_t *a0 = jit_arg ();
        jit_node_t *a1 = jit_arg ();
        jit_node_t *a2 = jit_arg ();
        jit_getarg_l (JIT_V0, a0);   // RAM
        jit_getarg_l (JIT_V1, a1);   // GRF
        (void) a2;                   // FRF unused
    }
    ~LnEmitter () override { if (_jit) jit_destroy_state (); }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

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
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override { *ppValue = nullptr; return E_NOTIMPL; }
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
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = new LnBlock (); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        jit_movi (JIT_R0, 0);          // return ExecOk
        jit_retr (JIT_R0);
        JittedFn Fn = (JittedFn) jit_emit ();
        jit_clear_state ();
        jit_state_t *Owned = _jit;
        _jit = nullptr;                // ownership transferred to LnCode
        return new LnCode (Owned, Fn);
    }

private:
    INT32 Slot () { return jit_allocai (8); }
    void  Store (INT32 S) { jit_stxi_l (S, JIT_FP, JIT_R0); }
    void  MaskReg (UINT32 Bits) { if (Bits < 64) jit_andi (JIT_R0, JIT_R0, (jit_word_t) Mask (~0ull, Bits)); }
    HRESULT Make (INT32 S, UINT32 Bits, ICpuValue **ppValue) { *ppValue = new LnValue (S, Bits); return S_OK; }

    jit_state_t *_jit;
};

class LightningBackend final : public LcComObject<ICpuBackend> {
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
