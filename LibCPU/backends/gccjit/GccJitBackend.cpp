/** @file
  libgccjit JIT backend implementation. See GccJitBackend.h.

  Values are libgccjit rvalues; registers/flags/memory are lvalues built from
  the function's pointer parameters (RAM/GRF/FRF). One function "insn" per
  translation unit is compiled and run.
**/
extern "C" {
#include "libgccjit.h"
}

#include "GccJitBackend.h"
#include "LibCPU/CpuState.h"

namespace LibCPU {
namespace {

//
// Opaque handles.
//
class GccValue final : public LcComObject<ICpuValue> {
public:
    GccValue (gcc_jit_rvalue *pV, UINT32 Bits) : m_pV (pV), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    gcc_jit_rvalue *m_pV;
    UINT32          m_Bits;
};

class GccBlock final : public LcComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
};

static gcc_jit_rvalue *RvalOf (ICpuValue *pV) { return static_cast<GccValue *> (pV)->m_pV; }
static UINT32          BitsOf (ICpuValue *pV) { return static_cast<GccValue *> (pV)->m_Bits; }

//
// The compiled code object: owns the gcc_jit_result (which owns the code).
//
typedef int (*JittedFn) (void *pRAM, void *pGRF, void *pFRF);

class GccCode final : public LcComObject<ICpuCode> {
public:
    GccCode (gcc_jit_result *pResult, JittedFn Fn) : m_pResult (pResult), m_Fn (Fn) {}
    ~GccCode () override { if (m_pResult) gcc_jit_result_release (m_pResult); }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }
    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID *pFRF) override {
        return (CPU_EXEC_STATUS) m_Fn (pRAM, pGRF, pFRF);
    }
private:
    gcc_jit_result *m_pResult;
    JittedFn        m_Fn;
};

//
// The builder.
//
class GccEmitter final : public LcComObject<ICpuEmitter> {
public:
    GccEmitter () {
        m_C = gcc_jit_context_acquire ();

        m_Bool = T (GCC_JIT_TYPE_BOOL);
        m_U8   = T (GCC_JIT_TYPE_UINT8_T);
        m_U16  = T (GCC_JIT_TYPE_UINT16_T);
        m_U32  = T (GCC_JIT_TYPE_UINT32_T);
        m_U64  = T (GCC_JIT_TYPE_UINT64_T);
        m_Int  = T (GCC_JIT_TYPE_INT);
        gcc_jit_type *pVoidPtr = T (GCC_JIT_TYPE_VOID_PTR);

        gcc_jit_param *Params[3] = {
            gcc_jit_context_new_param (m_C, nullptr, pVoidPtr, "RAM"),
            gcc_jit_context_new_param (m_C, nullptr, pVoidPtr, "GRF"),
            gcc_jit_context_new_param (m_C, nullptr, pVoidPtr, "FRF")
        };
        m_Fn = gcc_jit_context_new_function (m_C, nullptr, GCC_JIT_FUNCTION_EXPORTED,
                                             m_Int, "insn", 3, Params, 0);
        m_Block = gcc_jit_function_new_block (m_Fn, "entry");

        // Pre-cast the pointer params for register/flag/memory addressing.
        m_RamU8  = gcc_jit_context_new_cast (m_C, nullptr, gcc_jit_param_as_rvalue (Params[0]), Ptr (m_U8));
        m_GrfU8  = gcc_jit_context_new_cast (m_C, nullptr, gcc_jit_param_as_rvalue (Params[1]), Ptr (m_U8));
        m_GrfU64 = gcc_jit_context_new_cast (m_C, nullptr, gcc_jit_param_as_rvalue (Params[1]), Ptr (m_U64));
        m_Zero   = gcc_jit_context_new_rvalue_from_int (m_C, m_Int, 0);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

    // ---- values -----------------------------------------------------------
    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        return Make (gcc_jit_context_new_rvalue_from_long (m_C, IntType (Bits), (long)(Mask (Value, Bits))), Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        gcc_jit_lvalue *pSlot = gcc_jit_context_new_array_access (m_C, nullptr, m_GrfU64, Idx (Index));
        gcc_jit_rvalue *pVal  = gcc_jit_context_new_cast (m_C, nullptr, gcc_jit_lvalue_as_rvalue (pSlot), IntType (Bits));
        return Make (pVal, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 /*RegBits*/, BOOLEAN /*Sext*/) override {
        gcc_jit_lvalue *pSlot = gcc_jit_context_new_array_access (m_C, nullptr, m_GrfU64, Idx (Index));
        gcc_jit_rvalue *pVal  = gcc_jit_context_new_cast (m_C, nullptr, RvalOf (pValue), m_U64);
        gcc_jit_block_add_assignment (m_Block, nullptr, pSlot, pVal);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        return Make (gcc_jit_lvalue_as_rvalue (MemLValue (RvalOf (pAddr), Bits)), Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        gcc_jit_lvalue *pMem = MemLValue (RvalOf (pAddr), Bits);
        gcc_jit_block_add_assignment (m_Block, nullptr, pMem,
                                      gcc_jit_context_new_cast (m_C, nullptr, RvalOf (pValue), IntType (Bits)));
        return S_OK;
    }

    // ---- arithmetic / compare / cast --------------------------------------
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        enum gcc_jit_binary_op GccOp;
        switch (Op) {
        case BinAdd: GccOp = GCC_JIT_BINARY_OP_PLUS;        break;
        case BinSub: GccOp = GCC_JIT_BINARY_OP_MINUS;       break;
        case BinMul: GccOp = GCC_JIT_BINARY_OP_MULT;        break;
        case BinUDiv:GccOp = GCC_JIT_BINARY_OP_DIVIDE;      break;
        case BinURem:GccOp = GCC_JIT_BINARY_OP_MODULO;      break;
        case BinAnd: GccOp = GCC_JIT_BINARY_OP_BITWISE_AND; break;
        case BinOr:  GccOp = GCC_JIT_BINARY_OP_BITWISE_OR;  break;
        case BinXor: GccOp = GCC_JIT_BINARY_OP_BITWISE_XOR; break;
        case BinShl: GccOp = GCC_JIT_BINARY_OP_LSHIFT;      break;
        case BinLShr:GccOp = GCC_JIT_BINARY_OP_RSHIFT;      break;  // unsigned type -> logical
        default:     GccOp = GCC_JIT_BINARY_OP_PLUS;        break;  // SDiv/SRem/AShr/Rol/Ror: TODO
        }
        gcc_jit_rvalue *pR = gcc_jit_context_new_binary_op (m_C, nullptr, GccOp, IntType (Bits), RvalOf (pA), RvalOf (pB));
        return Make (pR, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        switch (Op) {
        case UnNeg: return Make (gcc_jit_context_new_unary_op (m_C, nullptr, GCC_JIT_UNARY_OP_MINUS, IntType (Bits), RvalOf (pA)), Bits, ppValue);
        case UnCom: return Make (gcc_jit_context_new_unary_op (m_C, nullptr, GCC_JIT_UNARY_OP_BITWISE_NEGATE, IntType (Bits), RvalOf (pA)), Bits, ppValue);
        case UnNot: {
            gcc_jit_rvalue *pZero = gcc_jit_context_new_rvalue_from_long (m_C, IntType (Bits), 0);
            gcc_jit_rvalue *pCmp  = gcc_jit_context_new_comparison (m_C, nullptr, GCC_JIT_COMPARISON_EQ, RvalOf (pA), pZero);
            return Make (gcc_jit_context_new_cast (m_C, nullptr, pCmp, m_U8), 1, ppValue);
        }
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        enum gcc_jit_comparison GccCmp;
        switch (Pred) {
        case CmpEq:  GccCmp = GCC_JIT_COMPARISON_EQ; break;
        case CmpNe:  GccCmp = GCC_JIT_COMPARISON_NE; break;
        case CmpULt: case CmpSLt: GccCmp = GCC_JIT_COMPARISON_LT; break;
        case CmpULe: case CmpSLe: GccCmp = GCC_JIT_COMPARISON_LE; break;
        case CmpUGt: case CmpSGt: GccCmp = GCC_JIT_COMPARISON_GT; break;
        case CmpUGe: case CmpSGe: GccCmp = GCC_JIT_COMPARISON_GE; break;
        default:     GccCmp = GCC_JIT_COMPARISON_EQ; break;
        }
        gcc_jit_rvalue *pCmp = gcc_jit_context_new_comparison (m_C, nullptr, GccCmp, RvalOf (pA), RvalOf (pB));
        return Make (gcc_jit_context_new_cast (m_C, nullptr, pCmp, m_U8), 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        gcc_jit_rvalue *pR = gcc_jit_context_new_cast (m_C, nullptr, RvalOf (pA), IntType (Bits));
        if (Op == CastTrunc && Bits < 8) {
            gcc_jit_rvalue *pMask = gcc_jit_context_new_rvalue_from_long (m_C, IntType (Bits), (long)(((UINT64) 1 << Bits) - 1));
            pR = gcc_jit_context_new_binary_op (m_C, nullptr, GCC_JIT_BINARY_OP_BITWISE_AND, IntType (Bits), pR, pMask);
        }
        return Make (pR, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override {
        *ppValue = nullptr; return E_NOTIMPL;   // unused by the slice
    }

    // ---- flags ------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        gcc_jit_lvalue *pByte = gcc_jit_context_new_array_access (m_C, nullptr, m_GrfU8, Idx (CPU_STATE_FLAG_OFFSET + (UINT32) Flag));
        gcc_jit_rvalue *pOne  = gcc_jit_context_new_rvalue_from_long (m_C, m_U8, 1);
        gcc_jit_rvalue *pV    = gcc_jit_context_new_binary_op (m_C, nullptr, GCC_JIT_BINARY_OP_BITWISE_AND, m_U8, gcc_jit_lvalue_as_rvalue (pByte), pOne);
        return Make (pV, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        gcc_jit_lvalue *pByte = gcc_jit_context_new_array_access (m_C, nullptr, m_GrfU8, Idx (CPU_STATE_FLAG_OFFSET + (UINT32) Flag));
        gcc_jit_rvalue *pOne  = gcc_jit_context_new_rvalue_from_long (m_C, m_U8, 1);
        gcc_jit_rvalue *pV    = gcc_jit_context_new_binary_op (m_C, nullptr, GCC_JIT_BINARY_OP_BITWISE_AND, m_U8,
                                    gcc_jit_context_new_cast (m_C, nullptr, RvalOf (pValue), m_U8), pOne);
        gcc_jit_block_add_assignment (m_Block, nullptr, pByte, pV);
        return S_OK;
    }

    // ---- control flow (unused by the straight-line slice) -----------------
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        gcc_jit_lvalue *pSlot = gcc_jit_context_new_array_access (m_C, nullptr, m_GrfU64, Idx (CPU_STATE_PC_OFFSET / 8));
        gcc_jit_block_add_assignment (m_Block, nullptr, pSlot, gcc_jit_context_new_rvalue_from_long (m_C, m_U64, (long) Pc));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = new GccBlock (); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        gcc_jit_block_end_with_return (m_Block, nullptr, m_Zero);
        gcc_jit_result *pResult = gcc_jit_context_compile (m_C);
        gcc_jit_context_release (m_C);
        m_C = nullptr;
        if (!pResult) {
            return nullptr;
        }
        JittedFn Fn = (JittedFn) gcc_jit_result_get_code (pResult, "insn");
        return Fn ? new GccCode (pResult, Fn) : nullptr;
    }

private:
    gcc_jit_type *T (enum gcc_jit_types K) { return gcc_jit_context_get_type (m_C, K); }
    gcc_jit_type *Ptr (gcc_jit_type *pT) { return gcc_jit_type_get_pointer (pT); }
    gcc_jit_type *IntType (UINT32 Bits) {
        if (Bits <= 8)  return m_U8;
        if (Bits <= 16) return m_U16;
        if (Bits <= 32) return m_U32;
        return m_U64;
    }
    static UINT64 Mask (UINT64 V, UINT32 Bits) { return (Bits >= 64) ? V : (V & (((UINT64) 1 << Bits) - 1)); }
    gcc_jit_rvalue *Idx (UINT32 N) { return gcc_jit_context_new_rvalue_from_long (m_C, m_U64, (long) N); }
    gcc_jit_lvalue *MemLValue (gcc_jit_rvalue *pAddr, UINT32 Bits) {
        gcc_jit_lvalue *pByte = gcc_jit_context_new_array_access (m_C, nullptr, m_RamU8, pAddr);
        if (Bits == 8) {
            return pByte;
        }
        gcc_jit_rvalue *pByteAddr = gcc_jit_lvalue_get_address (pByte, nullptr);
        gcc_jit_rvalue *pTyped    = gcc_jit_context_new_cast (m_C, nullptr, pByteAddr, Ptr (IntType (Bits)));
        return gcc_jit_context_new_array_access (m_C, nullptr, pTyped, m_Zero);
    }
    HRESULT Make (gcc_jit_rvalue *pV, UINT32 Bits, ICpuValue **ppValue) {
        *ppValue = new GccValue (pV, Bits);
        return S_OK;
    }

    gcc_jit_context  *m_C = nullptr;
    gcc_jit_function *m_Fn;
    gcc_jit_block    *m_Block;
    gcc_jit_type     *m_Bool, *m_U8, *m_U16, *m_U32, *m_U64, *m_Int;
    gcc_jit_rvalue   *m_RamU8, *m_GrfU8, *m_GrfU64, *m_Zero;
};

class GccJitBackend final : public LcComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "gccjit"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new GccEmitter ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<GccEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateGccJitBackend (VOID)
{
    return new GccJitBackend ();
}

} // namespace LibCPU
