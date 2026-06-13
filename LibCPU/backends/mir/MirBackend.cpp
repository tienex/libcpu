/** @file
  MIR JIT backend implementation. See MirBackend.h.

  Each ICpuValue is a MIR virtual register (I64) holding the value masked to its
  width. Ops append MIR 3-address instructions; memory/registers/flags are MIR
  memory operands over the RAM/GRF pointer args. One function "insn" per unit is
  compiled with MIR_gen.
**/
extern "C" {
#include "mir.h"
#include "mir-gen.h"
}

#include "MirBackend.h"
#include "LibCPU/CpuState.h"

#include <cstdio>

namespace LibCPU {
namespace {

class MirValue final : public ComObject<ICpuValue> {
public:
    MirValue (MIR_reg_t Reg, UINT32 Bits) : m_Reg (Reg), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    MIR_reg_t m_Reg;
    UINT32    m_Bits;
};

class MirBlock final : public ComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
};

static MIR_reg_t RegOf  (ICpuValue *pV) { return static_cast<MirValue *> (pV)->m_Reg; }
static UINT32    BitsOf (ICpuValue *pV) { return static_cast<MirValue *> (pV)->m_Bits; }

typedef int64_t (*JittedFn) (void *pRAM, void *pGRF, void *pFRF);

//
// The compiled code object owns the MIR context (which owns the generated code).
//
class MirCode final : public ComObject<ICpuCode> {
public:
    MirCode (MIR_context_t Ctx, JittedFn Fn) : m_Ctx (Ctx), m_Fn (Fn) {}
    ~MirCode () override {
        if (m_Ctx) { MIR_gen_finish (m_Ctx); MIR_finish (m_Ctx); }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }
    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID *pFRF) override {
        return (CPU_EXEC_STATUS) m_Fn (pRAM, pGRF, pFRF);
    }
private:
    MIR_context_t m_Ctx;
    JittedFn      m_Fn;
};

class MirEmitter final : public ComObject<ICpuEmitter> {
public:
    MirEmitter () {
        m_Ctx  = MIR_init ();
        m_Mod  = MIR_new_module (m_Ctx, "m");
        MIR_type_t ResType = MIR_T_I64;
        m_Item = MIR_new_func (m_Ctx, "insn", 1, &ResType, 3,
                               MIR_T_I64, "RAM", MIR_T_I64, "GRF", MIR_T_I64, "FRF");
        m_Func = m_Item->u.func;
        m_Ram  = MIR_reg (m_Ctx, "RAM", m_Func);
        m_Grf  = MIR_reg (m_Ctx, "GRF", m_Func);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

    // ---- values -----------------------------------------------------------
    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        MIR_reg_t R = NewTemp ();
        Emit (MIR_MOV, RegOp (R), MIR_new_int_op (m_Ctx, (int64_t) Mask (Value, Bits)));
        return Make (R, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        MIR_reg_t R = NewTemp ();
        Emit (MIR_MOV, RegOp (R), MemOp (MIR_T_I64, CPU_STATE_REG_OFFSET + Index * 8, m_Grf, 0));
        MaskReg (R, Bits);
        return Make (R, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 /*RegBits*/, BOOLEAN /*Sext*/) override {
        Emit (MIR_MOV, MemOp (MIR_T_I64, CPU_STATE_REG_OFFSET + Index * 8, m_Grf, 0), RegOp (RegOf (pValue)));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        MIR_reg_t R = NewTemp ();
        Emit (MIR_MOV, RegOp (R), MemOp (MemType (Bits), 0, m_Ram, RegOf (pAddr)));
        return Make (R, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Emit (MIR_MOV, MemOp (MemType (Bits), 0, m_Ram, RegOf (pAddr)), RegOp (RegOf (pValue)));
        return S_OK;
    }

    // ---- arithmetic / compare / cast --------------------------------------
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        MIR_insn_code_t Code;
        switch (Op) {
        case BinAdd: Code = MIR_ADD;  break;
        case BinSub: Code = MIR_SUB;  break;
        case BinMul: Code = MIR_MUL;  break;
        case BinAnd: Code = MIR_AND;  break;
        case BinOr:  Code = MIR_OR;   break;
        case BinXor: Code = MIR_XOR;  break;
        case BinShl: Code = MIR_LSH;  break;
        case BinLShr:Code = MIR_URSH; break;
        case BinAShr:Code = MIR_RSH;  break;
        default:     Code = MIR_ADD;  break;  // UDiv/SDiv/URem/SRem/Rol/Ror: TODO
        }
        MIR_reg_t R = NewTemp ();
        Emit3 (Code, RegOp (R), RegOp (RegOf (pA)), RegOp (RegOf (pB)));
        MaskReg (R, Bits);
        return Make (R, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        MIR_reg_t R = NewTemp ();
        switch (Op) {
        case UnNeg: Emit (MIR_NEG, RegOp (R), RegOp (RegOf (pA))); MaskReg (R, Bits); return Make (R, Bits, ppValue);
        case UnCom: Emit3 (MIR_XOR, RegOp (R), RegOp (RegOf (pA)), MIR_new_int_op (m_Ctx, -1)); MaskReg (R, Bits); return Make (R, Bits, ppValue);
        case UnNot: Emit3 (MIR_EQ, RegOp (R), RegOp (RegOf (pA)), MIR_new_int_op (m_Ctx, 0)); return Make (R, 1, ppValue);
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        MIR_insn_code_t Code;
        switch (Pred) {
        case CmpEq:  Code = MIR_EQ;   break;
        case CmpNe:  Code = MIR_NE;   break;
        case CmpULt: Code = MIR_ULT;  break;
        case CmpULe: Code = MIR_ULE;  break;
        case CmpUGt: Code = MIR_UGT;  break;
        case CmpUGe: Code = MIR_UGE;  break;
        case CmpSLt: Code = MIR_LT;   break;
        case CmpSLe: Code = MIR_LE;   break;
        case CmpSGt: Code = MIR_GT;   break;
        case CmpSGe: Code = MIR_GE;   break;
        default:     Code = MIR_EQ;   break;
        }
        MIR_reg_t R = NewTemp ();
        Emit3 (Code, RegOp (R), RegOp (RegOf (pA)), RegOp (RegOf (pB)));
        return Make (R, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA);
        MIR_reg_t R = NewTemp ();
        Emit (MIR_MOV, RegOp (R), RegOp (RegOf (pA)));
        switch (Op) {
        case CastTrunc: MaskReg (R, Bits); break;
        case CastZExt:  MaskReg (R, SrcBits); break;  // already masked
        case CastSExt:  SignExtend (R, SrcBits); MaskReg (R, Bits); break;
        }
        return Make (R, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override {
        *ppValue = nullptr; return E_NOTIMPL;
    }

    // ---- flags ------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        MIR_reg_t R = NewTemp ();
        Emit (MIR_MOV, RegOp (R), MemOp (MIR_T_U8, CPU_STATE_FLAG_OFFSET + (MIR_disp_t) Flag, m_Grf, 0));
        MaskReg (R, 1);
        return Make (R, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        Emit (MIR_MOV, MemOp (MIR_T_U8, CPU_STATE_FLAG_OFFSET + (MIR_disp_t) Flag, m_Grf, 0), RegOp (RegOf (pValue)));
        return S_OK;
    }

    // ---- control flow (unused by the straight-line slice) -----------------
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        Emit (MIR_MOV, MemOp (MIR_T_I64, CPU_STATE_PC_OFFSET, m_Grf, 0), MIR_new_int_op (m_Ctx, (int64_t) Pc));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = new MirBlock (); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        // The return value must be a register operand (MIR_OP_VAR), not an immediate.
        MIR_reg_t Ret = NewTemp ();
        Emit (MIR_MOV, RegOp (Ret), MIR_new_int_op (m_Ctx, 0));
        MIR_append_insn (m_Ctx, m_Item, MIR_new_ret_insn (m_Ctx, 1, RegOp (Ret)));
        MIR_finish_func (m_Ctx);
        MIR_finish_module (m_Ctx);
        MIR_load_module (m_Ctx, m_Mod);
        MIR_gen_init (m_Ctx);
        // MIR_link runs the simplify pass (lowering immediates for the target) and
        // sets the gen interface; the function's machine code is then at item->addr.
        MIR_link (m_Ctx, MIR_set_gen_interface, nullptr);
        JittedFn Fn = (JittedFn) m_Item->addr;
        MIR_context_t Ctx = m_Ctx;
        m_Ctx = nullptr;   // ownership transfers to MirCode
        return Fn ? new MirCode (Ctx, Fn) : nullptr;
    }

private:
    MIR_op_t RegOp (MIR_reg_t R) { return MIR_new_reg_op (m_Ctx, R); }
    MIR_op_t MemOp (MIR_type_t Ty, MIR_disp_t Disp, MIR_reg_t Base, MIR_reg_t Index) {
        return MIR_new_mem_op (m_Ctx, Ty, Disp, Base, Index, 1);
    }
    void Emit (MIR_insn_code_t Code, MIR_op_t A, MIR_op_t B) {
        MIR_append_insn (m_Ctx, m_Item, MIR_new_insn (m_Ctx, Code, A, B));
    }
    void Emit3 (MIR_insn_code_t Code, MIR_op_t A, MIR_op_t B, MIR_op_t C) {
        MIR_append_insn (m_Ctx, m_Item, MIR_new_insn (m_Ctx, Code, A, B, C));
    }
    MIR_reg_t NewTemp () {
        char Name[24];
        std::snprintf (Name, sizeof (Name), "t%u", m_NextTemp++);
        return MIR_new_func_reg (m_Ctx, m_Func, MIR_T_I64, Name);
    }
    void MaskReg (MIR_reg_t R, UINT32 Bits) {
        if (Bits < 64) {
            Emit3 (MIR_AND, RegOp (R), RegOp (R), MIR_new_int_op (m_Ctx, (int64_t)(((UINT64) 1 << Bits) - 1)));
        }
    }
    void SignExtend (MIR_reg_t R, UINT32 Bits) {
        if (Bits < 64) {
            int64_t Sh = (int64_t)(64 - Bits);
            Emit3 (MIR_LSH, RegOp (R), RegOp (R), MIR_new_int_op (m_Ctx, Sh));
            Emit3 (MIR_RSH, RegOp (R), RegOp (R), MIR_new_int_op (m_Ctx, Sh));
        }
    }
    static MIR_type_t MemType (UINT32 Bits) {
        switch (Bits) {
        case 8:  return MIR_T_U8;
        case 16: return MIR_T_U16;
        case 32: return MIR_T_U32;
        default: return MIR_T_I64;
        }
    }
    static UINT64 Mask (UINT64 V, UINT32 Bits) { return (Bits >= 64) ? V : (V & (((UINT64) 1 << Bits) - 1)); }
    HRESULT Make (MIR_reg_t R, UINT32 Bits, ICpuValue **ppValue) {
        *ppValue = new MirValue (R, Bits);
        return S_OK;
    }

    MIR_context_t m_Ctx = nullptr;
    MIR_module_t  m_Mod;
    MIR_item_t    m_Item;
    MIR_func_t    m_Func;
    MIR_reg_t     m_Ram, m_Grf;
    UINT32        m_NextTemp = 0;
};

class MirBackend final : public ComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "mir"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new MirEmitter ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<MirEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateMirBackend (VOID)
{
    return new MirBackend ();
}

} // namespace LibCPU
