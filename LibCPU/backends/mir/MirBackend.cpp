/** @file
  MIR JIT backend implementation. See MirBackend.h.

  Lowering model: each ICpuValue is a MIR virtual (function) register holding the
  value masked to its width. MIR is an IR builder (virtual registers + MIR_gen's
  own register allocator), like the LLVM backend rather than the slot-based
  register-assemblers -- function registers are live across blocks, so the control
  flow graph needs no per-block slot reset.

  Blocks are MIR labels (MIR_new_label, placed by MIR_append_insn); branches are
  MIR_JMP / MIR_BT, with forward references resolved by MIR at link time. Every
  trap/exit path moves a status code into a shared return register and jumps to a
  single epilog that performs the lone MIR_RET -- mirroring the AsmJit backend, and
  avoiding any reliance on multiple-return support in MIR_gen.
**/
extern "C" {
#include "mir.h"
#include "mir-gen.h"
}

#include "MirBackend.h"
#include "LibCPU/CpuState.h"

#include <cstdio>
#include <pthread.h>
#include <vector>

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
    MIR_label_t m_Label = nullptr;   // created at CreateBlock; placed by SetInsertBlock / Build
    bool        m_Placed = false;
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

// MIR_gen's CFG analysis (generate_func_code -> DFS) recurses to a depth that scales with the
// function's instruction count, overflowing the default thread stack on the machine's large
// deeply-inlined windows. Run the link/codegen step on a thread with a large (mostly-virtual) stack
// so the recursion fits. opt level 0 keeps the codegen conservative (the higher levels produced an
// unaligned-access bug in MIR's AArch64 generator on this workload).
struct MirGenArg {
    MIR_context_t Ctx;
    MIR_module_t  Mod;
    MIR_item_t    Item;
    void         *Addr;
};
static void *
MirGenOnBigStack (void *pArg)
{
    MirGenArg *p = static_cast<MirGenArg *> (pArg);
    MIR_load_module (p->Ctx, p->Mod);
    MIR_gen_init (p->Ctx);
    MIR_gen_set_optimize_level (p->Ctx, 0);
    MIR_link (p->Ctx, MIR_set_gen_interface, nullptr);
    p->Addr = p->Item->addr;
    return nullptr;
}

class MirEmitter final : public ComObject<ICpuEmitter>, public ICpuSmcEmitter,
                         public ICpuSystemEmitter, public ICpuSyscallEmitter, public ICpuClockEmitter {
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
        m_RetReg = MIR_new_func_reg (m_Ctx, m_Func, MIR_T_I64, "rv");   // shared return value
        m_Epilog = MIR_new_label (m_Ctx);                              // the function's single ret point
    }
    ~MirEmitter () override {
        for (MirBlock *pB : m_Blocks) { pB->Release (); }   // m_Blocks holds a retained reference per block
        if (m_Ctx) { MIR_finish (m_Ctx); }                  // ownership transfers to MirCode on the Build path
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
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 RegBits, BOOLEAN Sext) override {
        MIR_reg_t R = NewTemp ();
        Emit (MIR_MOV, RegOp (R), RegOp (RegOf (pValue)));
        if (Sext) { SignExtend (R, BitsOf (pValue)); }
        MaskReg (R, RegBits);
        Emit (MIR_MOV, MemOp (MIR_T_I64, CPU_STATE_REG_OFFSET + Index * 8, m_Grf, 0), RegOp (R));
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
        MIR_reg_t R = NewTemp ();
        switch (Op) {
        case BinUDiv: case BinSDiv: case BinURem: case BinSRem:
            EmitDivide (Op, Bits, RegOf (pA), RegOf (pB), R);
            break;
        default: {
            MIR_insn_code_t Code;
            switch (Op) {
            case BinAdd:  Code = MIR_ADD;  break;
            case BinSub:  Code = MIR_SUB;  break;
            case BinMul:  Code = MIR_MUL;  break;
            case BinAnd:  Code = MIR_AND;  break;
            case BinOr:   Code = MIR_OR;   break;
            case BinXor:  Code = MIR_XOR;  break;
            case BinShl:  Code = MIR_LSH;  break;
            case BinLShr: Code = MIR_URSH; break;
            case BinAShr: { MIR_reg_t T = NewTemp (); Emit (MIR_MOV, RegOp (T), RegOp (RegOf (pA))); SignExtend (T, Bits);
                            Emit3 (MIR_RSH, RegOp (R), RegOp (T), RegOp (RegOf (pB)));
                            MaskReg (R, Bits); return Make (R, Bits, ppValue); }
            default:      Code = MIR_ADD;  break;   // Rol/Ror: not emitted by the V20 frontend
            }
            Emit3 (Code, RegOp (R), RegOp (RegOf (pA)), RegOp (RegOf (pB)));
            break;
        }
        }
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
        UINT32 Bits = BitsOf (pA);
        MIR_insn_code_t Code;
        bool Signed = false;
        switch (Pred) {
        case CmpEq:  Code = MIR_EQ;   break;
        case CmpNe:  Code = MIR_NE;   break;
        case CmpULt: Code = MIR_ULT;  break;
        case CmpULe: Code = MIR_ULE;  break;
        case CmpUGt: Code = MIR_UGT;  break;
        case CmpUGe: Code = MIR_UGE;  break;
        case CmpSLt: Code = MIR_LT;   Signed = true; break;
        case CmpSLe: Code = MIR_LE;   Signed = true; break;
        case CmpSGt: Code = MIR_GT;   Signed = true; break;
        case CmpSGe: Code = MIR_GE;   Signed = true; break;
        default:     Code = MIR_EQ;   break;
        }
        MIR_reg_t A = RegOf (pA), B = RegOf (pB);
        if (Signed) {                                   // values are zero-extended; sign-extend for signed compares
            MIR_reg_t Ta = NewTemp (), Tb = NewTemp ();
            Emit (MIR_MOV, RegOp (Ta), RegOp (A)); SignExtend (Ta, Bits);
            Emit (MIR_MOV, RegOp (Tb), RegOp (B)); SignExtend (Tb, Bits);
            A = Ta; B = Tb;
        }
        MIR_reg_t R = NewTemp ();
        Emit3 (Code, RegOp (R), RegOp (A), RegOp (B));
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
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *pCond, ICpuValue *pTrue, ICpuValue *pFalse, ICpuValue **ppValue) override {
        MIR_reg_t R = NewTemp ();
        Emit (MIR_MOV, RegOp (R), RegOp (RegOf (pFalse)));            // default = false value
        MIR_label_t Skip = MIR_new_label (m_Ctx);
        EmitBranch (MIR_BF, Skip, RegOf (pCond));                    // cond == 0 -> keep false
        Emit (MIR_MOV, RegOp (R), RegOp (RegOf (pTrue)));
        Place (Skip);
        return Make (R, BitsOf (pTrue), ppValue);
    }

    // ---- flags ------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        MIR_reg_t R = NewTemp ();
        Emit (MIR_MOV, RegOp (R), MemOp (MIR_T_U8, CPU_STATE_FLAG_OFFSET + (MIR_disp_t) Flag, m_Grf, 0));
        MaskReg (R, 1);
        return Make (R, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        MIR_reg_t R = NewTemp ();
        Emit3 (MIR_AND, RegOp (R), RegOp (RegOf (pValue)), MIR_new_int_op (m_Ctx, 1));
        Emit (MIR_MOV, MemOp (MIR_T_U8, CPU_STATE_FLAG_OFFSET + (MIR_disp_t) Flag, m_Grf, 0), RegOp (R));
        return S_OK;
    }

    // ---- control flow -----------------------------------------------------
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        Emit (MIR_MOV, MemOp (MIR_T_I64, CPU_STATE_PC_OFFSET, m_Grf, 0), MIR_new_int_op (m_Ctx, (int64_t) Pc));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override {
        MirBlock *pB = new MirBlock ();          // refcount 1: the reference returned to the caller
        pB->m_Label = MIR_new_label (m_Ctx);     // MIR resolves forward references at link time
        pB->AddRef ();                           // refcount 2: m_Blocks retains its own reference for the
        m_Blocks.push_back (pB);                 // exit pass in Build -- the caller owns blocks via ComPtr
        *ppBlock = pB;                           // and may release them (e.g. REP helper blocks) before Build
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *pBlock) override {
        MirBlock *pB = static_cast<MirBlock *> (pBlock);
        Place (pB->m_Label);
        pB->m_Placed = true;
        m_Cur = pB;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override {
        if (m_Cur != nullptr) { m_Cur->AddRef (); }
        *ppBlock = m_Cur;
        return m_Cur != nullptr ? S_OK : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *pTarget) override {
        EmitJmp (static_cast<MirBlock *> (pTarget)->m_Label);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *pCond, ICpuBlock *pTrue, ICpuBlock *pFalse) override {
        EmitBranch (MIR_BT, static_cast<MirBlock *> (pTrue)->m_Label, RegOf (pCond));   // cond != 0 -> true
        EmitJmp (static_cast<MirBlock *> (pFalse)->m_Label);
        return S_OK;
    }

    // ---- self-modifying-code / dispatch (ICpuSmcEmitter) ------------------
    HRESULT STDMETHODCALLTYPE EmitCodeGuard (CPU_ADDR Pc) override {
        UINT32 Page = (UINT32) ((Pc >> CPU_SMC_PAGE_SHIFT) & (CPU_SMC_PAGE_COUNT - 1));
        MIR_reg_t G = NewTemp ();
        Emit (MIR_MOV, RegOp (G), MemOp (MIR_T_U8, CPU_STATE_CODEDIRTY_OFFSET + (Page >> 3), m_Grf, 0));
        Emit3 (MIR_AND, RegOp (G), RegOp (G), MIR_new_int_op (m_Ctx, (int64_t) (1u << (Page & 7))));
        MIR_label_t Skip = MIR_new_label (m_Ctx);
        EmitBranch (MIR_BF, Skip, G);            // clean (bit == 0) -> skip the trap
        Emit (MIR_MOV, MemOp (MIR_T_I64, CPU_STATE_TRAPPC_OFFSET, m_Grf, 0), MIR_new_int_op (m_Ctx, (int64_t) Pc));
        TrapReturn ();
        Place (Skip);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IndirectBranch (ICpuValue *pTargetPc) override {
        StoreField (CPU_STATE_TRAPPC_OFFSET, RegOf (pTargetPc));
        TrapReturn (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetDispatchTarget (ICpuValue *pTargetPc) override {
        StoreField (CPU_STATE_DISPPC_OFFSET, RegOf (pTargetPc));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDispatchTarget (ICpuValue **ppValue) override {
        MIR_reg_t R = NewTemp ();
        Emit (MIR_MOV, RegOp (R), MemOp (MIR_T_I64, CPU_STATE_DISPPC_OFFSET, m_Grf, 0));
        return Make (R, 64, ppValue);
    }

    // ---- port I/O + system traps (ICpuSystemEmitter / ICpuSyscallEmitter) -
    HRESULT STDMETHODCALLTYPE EmitPortOut (ICpuValue *pPort, ICpuValue *pData, UINT32 Width, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (int64_t) CPU_IO_MAKE (CPU_IO_OUT, Width));
        StoreField (CPU_STATE_IOPORT_OFFSET, RegOf (pPort));
        StoreField (CPU_STATE_IODATA_OFFSET, RegOf (pData));
        StoreField (CPU_STATE_TRAPPC_OFFSET, RegOf (pReturnPc));
        TrapReturn (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitPortIn (ICpuValue *pPort, UINT32 Width, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (int64_t) CPU_IO_MAKE (CPU_IO_IN, Width));
        StoreField (CPU_STATE_IOPORT_OFFSET, RegOf (pPort));
        StoreField (CPU_STATE_TRAPPC_OFFSET, RegOf (pReturnPc));
        TrapReturn (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitSystemTrap (UINT32 Reason, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (int64_t) CPU_IO_MAKE (Reason, 0));
        StoreField (CPU_STATE_TRAPPC_OFFSET, RegOf (pReturnPc));
        TrapReturn (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitSystemTrapValue (UINT32 Reason, ICpuValue *pValue, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (int64_t) CPU_IO_MAKE (Reason, 0));
        StoreField (CPU_STATE_IODATA_OFFSET, RegOf (pValue));
        StoreField (CPU_STATE_TRAPPC_OFFSET, RegOf (pReturnPc));
        TrapReturn (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitSyscall (UINT32 Vector, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_SYSCALL_OFFSET, (int64_t) Vector);
        StoreField (CPU_STATE_TRAPPC_OFFSET, RegOf (pReturnPc));
        TrapReturn (); return S_OK;
    }

    // ---- time base (ICpuClockEmitter): Cycles += Count, once per guest instruction --------
    HRESULT STDMETHODCALLTYPE EmitTick (UINT32 Count) override {
        MIR_reg_t R = NewTemp ();
        Emit (MIR_MOV, RegOp (R), MemOp (MIR_T_I64, CPU_STATE_CYCLES_OFFSET, m_Grf, 0));
        Emit3 (MIR_ADD, RegOp (R), RegOp (R), MIR_new_int_op (m_Ctx, (int64_t) Count));
        Emit (MIR_MOV, MemOp (MIR_T_I64, CPU_STATE_CYCLES_OFFSET, m_Grf, 0), RegOp (R));
        return S_OK;
    }

    ICpuCode *Build () {
        // Place any block created but never inserted (the CFG exit/fallback target); control falls into
        // the shared "return ExecOk" tail and the single epilog. Real blocks always end with a branch.
        for (MirBlock *pB : m_Blocks) {
            if (!pB->m_Placed) { Place (pB->m_Label); pB->m_Placed = true; }
        }
        Emit (MIR_MOV, RegOp (m_RetReg), MIR_new_int_op (m_Ctx, ExecOk));
        Place (m_Epilog);
        MIR_append_insn (m_Ctx, m_Item, MIR_new_ret_insn (m_Ctx, 1, RegOp (m_RetReg)));
        MIR_finish_func (m_Ctx);
        MIR_finish_module (m_Ctx);
        // MIR_link runs the simplify pass (lowering immediates for the target), then generate_func_code;
        // run it on a large stack (see MirGenOnBigStack). The machine code is then at item->addr.
        MirGenArg GA = { m_Ctx, m_Mod, m_Item, nullptr };
        pthread_attr_t Attr;
        pthread_attr_init (&Attr);
        pthread_attr_setstacksize (&Attr, (size_t) 64 * 1024 * 1024);
        pthread_t Th;
        if (pthread_create (&Th, &Attr, MirGenOnBigStack, &GA) != 0) {
            pthread_attr_destroy (&Attr);
            return nullptr;
        }
        pthread_join (Th, nullptr);
        pthread_attr_destroy (&Attr);
        JittedFn Fn = (JittedFn) GA.Addr;
        if (Fn == nullptr) { return nullptr; }
        MIR_context_t Ctx = m_Ctx;
        m_Ctx = nullptr;   // ownership transfers to MirCode
        return new MirCode (Ctx, Fn);
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
    void Place (MIR_label_t L) { MIR_append_insn (m_Ctx, m_Item, L); }   // a label IS an insn; appending places it
    void EmitJmp (MIR_label_t L) {
        MIR_append_insn (m_Ctx, m_Item, MIR_new_insn (m_Ctx, MIR_JMP, MIR_new_label_op (m_Ctx, L)));
    }
    void EmitBranch (MIR_insn_code_t Code, MIR_label_t L, MIR_reg_t Cond) {   // MIR_BT/MIR_BF: label first
        MIR_append_insn (m_Ctx, m_Item, MIR_new_insn (m_Ctx, Code, MIR_new_label_op (m_Ctx, L), RegOp (Cond)));
    }
    void TrapReturn () {                                 // hand control back to the host machine loop
        Emit (MIR_MOV, RegOp (m_RetReg), MIR_new_int_op (m_Ctx, ExecSmc));
        EmitJmp (m_Epilog);
    }
    void StoreField (UINT32 Off, MIR_reg_t R) {          // CPU_STATE[Off] = reg (full word)
        Emit (MIR_MOV, MemOp (MIR_T_I64, (MIR_disp_t) Off, m_Grf, 0), RegOp (R));
    }
    void StoreFieldImm (UINT32 Off, int64_t Imm) {
        Emit (MIR_MOV, MemOp (MIR_T_I64, (MIR_disp_t) Off, m_Grf, 0), MIR_new_int_op (m_Ctx, Imm));
    }
    // R = A (op) B for divide/remainder, guarding a zero divisor to yield 0 (matching the interpreter;
    // a real CPU raises #DE). Operands are copied to temps; signed ops sign-extend them to 64 bits first.
    void EmitDivide (CPU_BINOP Op, UINT32 Bits, MIR_reg_t A, MIR_reg_t B, MIR_reg_t R) {
        bool Signed = (Op == BinSDiv || Op == BinSRem);
        bool Rem    = (Op == BinURem || Op == BinSRem);
        MIR_reg_t Ta = NewTemp (), Tb = NewTemp ();
        Emit (MIR_MOV, RegOp (Ta), RegOp (A));
        Emit (MIR_MOV, RegOp (Tb), RegOp (B));
        if (Signed) { SignExtend (Ta, Bits); SignExtend (Tb, Bits); }
        MIR_label_t NonZero = MIR_new_label (m_Ctx);
        MIR_label_t Done    = MIR_new_label (m_Ctx);
        EmitBranch (MIR_BT, NonZero, Tb);                // divisor != 0 -> do the division
        Emit (MIR_MOV, RegOp (R), MIR_new_int_op (m_Ctx, 0));
        EmitJmp (Done);
        Place (NonZero);
        MIR_insn_code_t Code = Signed ? (Rem ? MIR_MOD : MIR_DIV) : (Rem ? MIR_UMOD : MIR_UDIV);
        Emit3 (Code, RegOp (R), RegOp (Ta), RegOp (Tb));
        Place (Done);
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
    MIR_reg_t     m_Ram, m_Grf, m_RetReg;
    MIR_label_t   m_Epilog;
    MirBlock     *m_Cur = nullptr;
    UINT32        m_NextTemp = 0;
    std::vector<MirBlock *> m_Blocks;
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
