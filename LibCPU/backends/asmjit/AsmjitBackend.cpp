/** @file
  AsmJit a64-Compiler backend. See AsmjitBackend.h.

  Each ICpuValue is an AsmJit virtual Gp (64-bit); AsmJit allocates physical
  registers. RAM and the register file arrive as pointer args; ops emit AArch64
  instructions. The compiled function is added to a per-code JitRuntime that keeps
  the executable memory alive.
**/
#include "AsmjitBackend.h"
#include "LibCPU/CpuState.h"

#include <cstdint>

extern "C++" {
#include <asmjit/a64.h>
}

namespace LibCPU {
namespace {

using namespace asmjit;

static UINT64 Mask (UINT64 V, UINT32 Bits) { return (Bits >= 64) ? V : (V & (((UINT64) 1 << Bits) - 1)); }

class AjValue final : public ComObject<ICpuValue> {
public:
    AjValue (a64::Gp Reg, UINT32 Bits) : m_Reg (Reg), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuValue, ppvObject); }
    a64::Gp m_Reg;
    UINT32  m_Bits;
};
class AjBlock final : public ComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuBlock, ppvObject); }
};
static a64::Gp RegOf  (ICpuValue *pV) { return static_cast<AjValue *> (pV)->m_Reg; }
static UINT32  BitsOf (ICpuValue *pV) { return static_cast<AjValue *> (pV)->m_Bits; }

typedef int (*JittedFn) (void *pRAM, void *pGRF, void *pFRF);

class AjCode final : public ComObject<ICpuCode> {
public:
    AjCode (JitRuntime *Rt, JittedFn Fn) : m_Rt (Rt), m_Fn (Fn) {}
    ~AjCode () override { if (m_Rt) { m_Rt->release (m_Fn); delete m_Rt; } }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuCode, ppvObject); }
    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID *pFRF) override {
        return (CPU_EXEC_STATUS) m_Fn (pRAM, pGRF, pFRF);
    }
private:
    JitRuntime *m_Rt;
    JittedFn    m_Fn;
};

class AjEmitter final : public ComObject<ICpuEmitter> {
public:
    AjEmitter () {
        m_Rt = new JitRuntime ();
        m_Code.init (m_Rt->environment (), m_Rt->cpu_features ());
        m_cc = new a64::Compiler (&m_Code);
        FuncNode *Fn = m_cc->add_func (FuncSignature::build<int, void *, void *, void *> ());
        m_Ram = m_cc->new_gp_ptr ();
        m_Grf = m_cc->new_gp_ptr ();
        a64::Gp Frf = m_cc->new_gp_ptr ();
        Fn->set_arg (0, m_Ram);
        Fn->set_arg (1, m_Grf);
        Fn->set_arg (2, Frf);
    }
    ~AjEmitter () override { delete m_cc; if (m_Rt) delete m_Rt; }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuEmitter, ppvObject); }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        a64::Gp g = m_cc->new_gp64 (); m_cc->mov (g, Mask (Value, Bits)); return Make (g, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        a64::Gp g = m_cc->new_gp64 ();
        m_cc->ldr (g, a64::ptr (m_Grf, CPU_STATE_REG_OFFSET + Index * 8));
        MaskReg (g, Bits);
        return Make (g, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 /*Bits*/, BOOLEAN /*Sext*/) override {
        m_cc->str (RegOf (pValue), a64::ptr (m_Grf, CPU_STATE_REG_OFFSET + Index * 8));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        a64::Gp g = m_cc->new_gp64 ();
        a64::Mem m = a64::ptr (m_Ram, RegOf (pAddr));
        switch (Bits) {
        case 8:  m_cc->ldrb (g.w (), m); break;
        case 16: m_cc->ldrh (g.w (), m); break;
        case 32: m_cc->ldr  (g.w (), m); break;
        default: m_cc->ldr  (g, m);      break;
        }
        return Make (g, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        a64::Gp v = RegOf (pValue);
        a64::Mem m = a64::ptr (m_Ram, RegOf (pAddr));
        switch (Bits) {
        case 8:  m_cc->strb (v.w (), m); break;
        case 16: m_cc->strh (v.w (), m); break;
        case 32: m_cc->str  (v.w (), m); break;
        default: m_cc->str  (v, m);      break;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        a64::Gp g = m_cc->new_gp64 (), a = RegOf (pA), b = RegOf (pB);
        switch (Op) {
        case BinAdd: m_cc->add (g, a, b); break;
        case BinSub: m_cc->sub (g, a, b); break;
        case BinMul: m_cc->mul (g, a, b); break;
        case BinAnd: m_cc->and_ (g, a, b); break;
        case BinOr:  m_cc->orr (g, a, b); break;
        case BinXor: m_cc->eor (g, a, b); break;
        case BinShl: m_cc->lslv (g, a, b); break;
        case BinLShr:m_cc->lsrv (g, a, b); break;
        case BinAShr:m_cc->asrv (g, a, b); break;
        case BinUDiv:m_cc->udiv (g, a, b); break;
        case BinSDiv:m_cc->sdiv (g, a, b); break;
        case BinURem:{ a64::Gp q = m_cc->new_gp64 (); m_cc->udiv (q, a, b); m_cc->msub (g, q, b, a); break; }
        case BinSRem:{ a64::Gp q = m_cc->new_gp64 (); m_cc->sdiv (q, a, b); m_cc->msub (g, q, b, a); break; }
        default: m_cc->add (g, a, b); break;
        }
        MaskReg (g, Bits);
        return Make (g, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        a64::Gp g = m_cc->new_gp64 (), a = RegOf (pA);
        switch (Op) {
        case UnNeg: m_cc->neg (g, a); MaskReg (g, Bits); return Make (g, Bits, ppValue);
        case UnCom: m_cc->mvn (g, a); MaskReg (g, Bits); return Make (g, Bits, ppValue);
        case UnNot: m_cc->cmp (a, 0); m_cc->cset (g, arm::CondCode::kEQ); return Make (g, 1, ppValue);
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        a64::Gp g = m_cc->new_gp64 ();
        m_cc->cmp (RegOf (pA), RegOf (pB));
        arm::CondCode cc;
        switch (Pred) {
        case CmpEq: cc = arm::CondCode::kEQ; break; case CmpNe: cc = arm::CondCode::kNE; break;
        case CmpULt:cc = arm::CondCode::kLO; break; case CmpULe:cc = arm::CondCode::kLS; break;
        case CmpUGt:cc = arm::CondCode::kHI; break; case CmpUGe:cc = arm::CondCode::kHS; break;
        case CmpSLt:cc = arm::CondCode::kLT; break; case CmpSLe:cc = arm::CondCode::kLE; break;
        case CmpSGt:cc = arm::CondCode::kGT; break; case CmpSGe:cc = arm::CondCode::kGE; break;
        default:    cc = arm::CondCode::kEQ; break;
        }
        m_cc->cset (g, cc);
        return Make (g, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA);
        a64::Gp g = m_cc->new_gp64 (), a = RegOf (pA);
        switch (Op) {
        case CastTrunc: m_cc->and_ (g, a, Mask (~0ull, Bits)); break;
        case CastZExt:  m_cc->mov (g, a); break;
        case CastSExt:
            if (SrcBits == 8)       m_cc->sxtb (g, a.w ());
            else if (SrcBits == 16) m_cc->sxth (g, a.w ());
            else if (SrcBits == 32) m_cc->sxtw (g, a.w ());
            else                    m_cc->mov (g, a);
            MaskReg (g, Bits);
            break;
        }
        return Make (g, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override { *ppValue = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        a64::Gp g = m_cc->new_gp64 ();
        m_cc->ldrb (g.w (), a64::ptr (m_Grf, CPU_STATE_FLAG_OFFSET + (UINT32) Flag));
        m_cc->and_ (g, g, 1);
        return Make (g, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        a64::Gp t = m_cc->new_gp64 ();
        m_cc->and_ (t, RegOf (pValue), 1);
        m_cc->strb (t.w (), a64::ptr (m_Grf, CPU_STATE_FLAG_OFFSET + (UINT32) Flag));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        a64::Gp g = m_cc->new_gp64 ();
        m_cc->mov (g, (UINT64) Pc);
        m_cc->str (g, a64::ptr (m_Grf, CPU_STATE_PC_OFFSET));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = new AjBlock (); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        a64::Gp z = m_cc->new_gp32 ();
        m_cc->mov (z, 0);
        m_cc->ret (z);
        m_cc->end_func ();
        if (m_cc->finalize () != Error::kOk) return nullptr;
        JittedFn Fn = nullptr;
        if (m_Rt->add (&Fn, &m_Code) != Error::kOk) return nullptr;
        JitRuntime *Rt = m_Rt;
        m_Rt = nullptr;   // ownership transferred to AjCode
        return new AjCode (Rt, Fn);
    }

private:
    void MaskReg (a64::Gp &g, UINT32 Bits) { if (Bits < 64) m_cc->and_ (g, g, Mask (~0ull, Bits)); }
    HRESULT Make (a64::Gp Reg, UINT32 Bits, ICpuValue **ppValue) { *ppValue = new AjValue (Reg, Bits); return S_OK; }

    JitRuntime    *m_Rt;
    CodeHolder     m_Code;
    a64::Compiler *m_cc;
    a64::Gp        m_Ram, m_Grf;
};

class AsmjitBackend final : public ComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuBackend, ppvObject); }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "asmjit"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override { *ppEmitter = new AjEmitter (); return S_OK; }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<AjEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateAsmjitBackend (VOID)
{
    return new AsmjitBackend ();
}

} // namespace LibCPU
