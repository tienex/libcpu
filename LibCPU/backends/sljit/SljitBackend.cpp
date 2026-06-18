/** @file
  SLJIT JIT backend implementation. See SljitBackend.h.

  Lowering model: every ICpuValue is a slot in a stack-local temp array
  (SLJIT_MEM1(SLJIT_SP), slot*WordSize). Each op loads its operand slots into
  scratch registers R0/R1, computes, masks to the value's bit width, and stores
  the result into a fresh slot. Values are always kept masked to their width.
**/
extern "C" {
#include "sljitLir.h"
}

#include "SljitBackend.h"
#include "LibCPU/CpuState.h"
#include <vector>

namespace LibCPU {
namespace {

static CONST sljit_sw WS = (sljit_sw) sizeof (sljit_sw);   // temp-slot word size
// Slots are reset at every block entry (an instruction's temps never outlive its block -- the
// frontend materialises all state to CPU_STATE at instruction boundaries), so this need only cover
// the widest single instruction, not a whole window.
static CONST UINT32   NUM_SLOTS = 256;

static sljit_sw
MaskValue (UINT64 Value, UINT32 Bits)
{
    if (Bits >= 64) {
        return (sljit_sw) Value;
    }
    return (sljit_sw)(Value & (((UINT64) 1 << Bits) - 1));
}

//
// Opaque handles.
//
class SljitValue final : public ComObject<ICpuValue> {
public:
    SljitValue (UINT32 Slot, UINT32 Bits) : m_Slot (Slot), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Slot;
    UINT32 m_Bits;
};

class SljitBlock final : public ComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
    struct sljit_label             *m_Label = nullptr;   // set when the block is first inserted into
    std::vector<struct sljit_jump *> m_Pending;          // jumps to this block awaiting its label
};

static UINT32 SlotOf (ICpuValue *pV) { return static_cast<SljitValue *> (pV)->m_Slot; }
static UINT32 BitsOf (ICpuValue *pV) { return static_cast<SljitValue *> (pV)->m_Bits; }

//
// The native code object: owns the generated code and frees it on release.
//
typedef sljit_sw (SLJIT_FUNC *JittedFn) (void *pRAM, void *pGRF, void *pFRF);

class SljitCode final : public ComObject<ICpuCode> {
public:
    explicit SljitCode (void *pCode) : m_pCode (pCode) {}
    ~SljitCode () override { if (m_pCode) sljit_free_code (m_pCode, nullptr); }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }
    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID *pFRF) override {
        JittedFn Fn = (JittedFn) m_pCode;
        return (CPU_EXEC_STATUS) Fn (pRAM, pGRF, pFRF);
    }
private:
    void *m_pCode;
};

//
// The builder.
//
class SljitEmitter final : public ComObject<ICpuEmitter>, public ICpuSmcEmitter,
                           public ICpuSystemEmitter, public ICpuSyscallEmitter, public ICpuClockEmitter {
public:
    SljitEmitter () {
        m_C = sljit_create_compiler (nullptr);
        sljit_emit_enter (m_C, 0, SLJIT_ARGS3 (W, P, P, P),
                          4 /*scratches R0..R3*/, 3 /*saveds S0..S2*/,
                          (sljit_s32)(NUM_SLOTS * WS));
        // Args map to saved registers: S0=RAM, S1=GRF, S2=FRF.
    }
    ~SljitEmitter () override {
        for (SljitBlock *pB : m_Blocks) { pB->Release (); }   // release m_Blocks' retained references
        if (m_C != nullptr) { sljit_free_compiler (m_C); }    // freed in Build on success; here for error paths
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
        UINT32 S = NewSlot ();
        StoreImm (S, MaskValue (Value, Bits));
        return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        sljit_emit_op1 (m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1 (SLJIT_S1), CPU_STATE_REG_OFFSET + Index * 8);
        Mask (SLJIT_R0, Bits);
        UINT32 S = NewSlot ();
        StoreReg (S, SLJIT_R0);
        return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 RegBits, BOOLEAN Sext) override {
        LoadReg (SLJIT_R0, SlotOf (pValue));
        if (Sext) SignExtend (SLJIT_R0, BitsOf (pValue));
        Mask (SLJIT_R0, RegBits);
        sljit_emit_op1 (m_C, SLJIT_MOV, SLJIT_MEM1 (SLJIT_S1), CPU_STATE_REG_OFFSET + Index * 8, SLJIT_R0, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        LoadReg (SLJIT_R1, SlotOf (pAddr));
        sljit_emit_op2 (m_C, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_R1, 0);  // R1 = RAM + addr
        sljit_emit_op1 (m_C, MovLoad (Bits), SLJIT_R0, 0, SLJIT_MEM1 (SLJIT_R1), 0);
        UINT32 S = NewSlot ();
        StoreReg (S, SLJIT_R0);
        return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        LoadReg (SLJIT_R0, SlotOf (pValue));
        LoadReg (SLJIT_R1, SlotOf (pAddr));
        sljit_emit_op2 (m_C, SLJIT_ADD, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_R1, 0);  // R1 = RAM + addr
        sljit_emit_op1 (m_C, MovLoad (Bits), SLJIT_MEM1 (SLJIT_R1), 0, SLJIT_R0, 0);
        return S_OK;
    }

    // ---- arithmetic / compare / cast --------------------------------------
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        LoadReg (SLJIT_R0, SlotOf (pA));
        LoadReg (SLJIT_R1, SlotOf (pB));
        if (Op == BinUDiv || Op == BinSDiv || Op == BinURem || Op == BinSRem) {
            EmitDivide (Op, Bits);
        } else {
            sljit_s32 SlOp;
            switch (Op) {
            case BinAdd: SlOp = SLJIT_ADD; break;
            case BinSub: SlOp = SLJIT_SUB; break;
            case BinMul: SlOp = SLJIT_MUL; break;
            case BinAnd: SlOp = SLJIT_AND; break;
            case BinOr:  SlOp = SLJIT_OR;  break;
            case BinXor: SlOp = SLJIT_XOR; break;
            case BinShl: SlOp = SLJIT_SHL; break;
            case BinLShr: SlOp = SLJIT_LSHR; break;
            case BinAShr: SignExtend (SLJIT_R0, Bits); SlOp = SLJIT_ASHR; break;
            case BinRol: SlOp = SLJIT_ROTL; break;
            case BinRor: SlOp = SLJIT_ROTR; break;
            default: SlOp = SLJIT_ADD; break;
            }
            sljit_emit_op2 (m_C, SlOp, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0);
        }
        Mask (SLJIT_R0, Bits);
        UINT32 S = NewSlot ();
        StoreReg (S, SLJIT_R0);
        return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        LoadReg (SLJIT_R0, SlotOf (pA));
        UINT32 OutBits = Bits;
        switch (Op) {
        case UnNeg: sljit_emit_op2 (m_C, SLJIT_SUB, SLJIT_R0, 0, SLJIT_IMM, 0, SLJIT_R0, 0); break;
        case UnCom: sljit_emit_op2 (m_C, SLJIT_XOR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, -1); break;
        case UnNot:
            sljit_emit_op2u (m_C, SLJIT_SUB | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 0);
            sljit_emit_op_flags (m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_EQUAL);
            OutBits = 1; break;
        }
        Mask (SLJIT_R0, OutBits);
        UINT32 S = NewSlot ();
        StoreReg (S, SLJIT_R0);
        return Make (S, OutBits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        LoadReg (SLJIT_R0, SlotOf (pA));
        LoadReg (SLJIT_R1, SlotOf (pB));
        sljit_s32 Cond;
        bool Signed = false;
        switch (Pred) {
        case CmpEq:  Cond = SLJIT_EQUAL;             break;
        case CmpNe:  Cond = SLJIT_NOT_EQUAL;         break;
        case CmpULt: Cond = SLJIT_LESS;              break;
        case CmpULe: Cond = SLJIT_LESS_EQUAL;        break;
        case CmpUGt: Cond = SLJIT_GREATER;           break;
        case CmpUGe: Cond = SLJIT_GREATER_EQUAL;     break;
        case CmpSLt: Cond = SLJIT_SIG_LESS;          Signed = true; break;
        case CmpSLe: Cond = SLJIT_SIG_LESS_EQUAL;    Signed = true; break;
        case CmpSGt: Cond = SLJIT_SIG_GREATER;       Signed = true; break;
        case CmpSGe: Cond = SLJIT_SIG_GREATER_EQUAL; Signed = true; break;
        default:     Cond = SLJIT_EQUAL;             break;
        }
        if (Signed) { SignExtend (SLJIT_R0, Bits); SignExtend (SLJIT_R1, Bits); }
        // EQUAL/NOT_EQUAL are condition 0/1 -> backed by the zero flag (SLJIT_SET_Z).
        // SLJIT condition codes come in pairs (LESS/GREATER_EQUAL, ...) that share
        // one flag; SLJIT_SET must be given the EVEN (canonical) member of the pair
        // (the low bit is the pair discriminator, outside VARIABLE_FLAG_MASK), while
        // sljit_emit_op_flags reads the specific (possibly odd) condition.
        sljit_s32 SetFlags = (Cond == SLJIT_EQUAL || Cond == SLJIT_NOT_EQUAL)
                                 ? SLJIT_SET_Z : SLJIT_SET (Cond & ~1);
        sljit_emit_op2u (m_C, SLJIT_SUB | SetFlags, SLJIT_R0, 0, SLJIT_R1, 0);
        sljit_emit_op_flags (m_C, SLJIT_MOV, SLJIT_R0, 0, Cond);
        UINT32 S = NewSlot ();
        StoreReg (S, SLJIT_R0);
        return Make (S, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA);
        LoadReg (SLJIT_R0, SlotOf (pA));
        switch (Op) {
        case CastTrunc: Mask (SLJIT_R0, Bits); break;
        case CastZExt:  Mask (SLJIT_R0, SrcBits); break;   // already masked; no-op
        case CastSExt:  SignExtend (SLJIT_R0, SrcBits); Mask (SLJIT_R0, Bits); break;
        }
        UINT32 S = NewSlot ();
        StoreReg (S, SLJIT_R0);
        return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *pCond, ICpuValue *pTrue, ICpuValue *pFalse, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pTrue);
        LoadReg (SLJIT_R0, SlotOf (pFalse));   // default
        LoadReg (SLJIT_R1, SlotOf (pTrue));
        LoadReg (SLJIT_R2, SlotOf (pCond));
        sljit_emit_op2u (m_C, SLJIT_SUB | SLJIT_SET_Z, SLJIT_R2, 0, SLJIT_IMM, 0);
        sljit_emit_select (m_C, SLJIT_NOT_EQUAL, SLJIT_R0, SLJIT_R1, 0, SLJIT_R0);
        UINT32 S = NewSlot ();
        StoreReg (S, SLJIT_R0);
        return Make (S, Bits, ppValue);
    }

    // ---- flags ------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        sljit_emit_op1 (m_C, SLJIT_MOV_U8, SLJIT_R0, 0, SLJIT_MEM1 (SLJIT_S1), CPU_STATE_FLAG_OFFSET + (sljit_sw) Flag);
        sljit_emit_op2 (m_C, SLJIT_AND, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
        UINT32 S = NewSlot ();
        StoreReg (S, SLJIT_R0);
        return Make (S, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        LoadReg (SLJIT_R0, SlotOf (pValue));
        sljit_emit_op2 (m_C, SLJIT_AND, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
        sljit_emit_op1 (m_C, SLJIT_MOV_U8, SLJIT_MEM1 (SLJIT_S1), CPU_STATE_FLAG_OFFSET + (sljit_sw) Flag, SLJIT_R0, 0);
        return S_OK;
    }

    // ---- control flow -----------------------------------------------------
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        sljit_emit_op1 (m_C, SLJIT_MOV, SLJIT_MEM1 (SLJIT_S1), CPU_STATE_PC_OFFSET, SLJIT_IMM, (sljit_sw) Pc);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override {
        SljitBlock *pB = new SljitBlock ();              // refcount 1: the reference returned to the caller
        pB->AddRef ();                                   // refcount 2: m_Blocks retains its own reference for
        m_Blocks.push_back (pB);                         // the exit pass in Build -- the caller (frontend or
        *ppBlock = pB;                                   // generator) owns blocks via ComPtr and may release
        return S_OK;                                     // them (e.g. a REP prefix's helper blocks) pre-Build
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *pBlock) override {
        SljitBlock *pB = static_cast<SljitBlock *> (pBlock);
        pB->m_Label = sljit_emit_label (m_C);            // this block begins at the current position
        for (struct sljit_jump *pJ : pB->m_Pending) { sljit_set_label (pJ, pB->m_Label); }
        pB->m_Pending.clear ();
        m_Cur      = pB;
        m_NextSlot = 0;                                  // an instruction's temps don't outlive its block
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override {
        if (m_Cur != nullptr) { m_Cur->AddRef (); }
        *ppBlock = m_Cur;
        return m_Cur != nullptr ? S_OK : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *pTarget) override {
        WireJump (sljit_emit_jump (m_C, SLJIT_JUMP), static_cast<SljitBlock *> (pTarget));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *pCond, ICpuBlock *pTrue, ICpuBlock *pFalse) override {
        LoadReg (SLJIT_R0, SlotOf (pCond));
        WireJump (sljit_emit_cmp (m_C, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0), static_cast<SljitBlock *> (pTrue));
        WireJump (sljit_emit_jump (m_C, SLJIT_JUMP), static_cast<SljitBlock *> (pFalse));
        return S_OK;
    }

    // ---- self-modifying-code / dispatch (ICpuSmcEmitter) ------------------
    HRESULT STDMETHODCALLTYPE EmitCodeGuard (CPU_ADDR Pc) override {
        UINT32 Page = (UINT32) ((Pc >> CPU_SMC_PAGE_SHIFT) & (CPU_SMC_PAGE_COUNT - 1));
        sljit_sw Mask = (sljit_sw) (1u << (Page & 7));   // this page's dirty bit (a constant)
        sljit_emit_op1 (m_C, SLJIT_MOV_U8, SLJIT_R0, 0, SLJIT_MEM1 (SLJIT_S1), CPU_STATE_CODEDIRTY_OFFSET + (Page >> 3));
        sljit_emit_op2 (m_C, SLJIT_AND, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, Mask);
        struct sljit_jump *pSkip = sljit_emit_cmp (m_C, SLJIT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);  // clean -> skip
        sljit_emit_op1 (m_C, SLJIT_MOV, SLJIT_MEM1 (SLJIT_S1), CPU_STATE_TRAPPC_OFFSET, SLJIT_IMM, (sljit_sw) Pc);
        sljit_emit_return (m_C, SLJIT_MOV, SLJIT_IMM, ExecSmc);                                   // dirty -> re-translate
        sljit_set_label (pSkip, sljit_emit_label (m_C));
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
        sljit_emit_op1 (m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1 (SLJIT_S1), CPU_STATE_DISPPC_OFFSET);
        UINT32 S = NewSlot ();
        StoreReg (S, SLJIT_R0);
        return Make (S, 64, ppValue);
    }

    // ---- port I/O + system traps (ICpuSystemEmitter / ICpuSyscallEmitter) -
    HRESULT STDMETHODCALLTYPE EmitPortOut (ICpuValue *pPort, ICpuValue *pData, UINT32 Width, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (sljit_sw) CPU_IO_MAKE (CPU_IO_OUT, Width));
        StoreField (CPU_STATE_IOPORT_OFFSET, SlotOf (pPort));
        StoreField (CPU_STATE_IODATA_OFFSET, SlotOf (pData));
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        return TrapReturn ();
    }
    HRESULT STDMETHODCALLTYPE EmitPortIn (ICpuValue *pPort, UINT32 Width, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (sljit_sw) CPU_IO_MAKE (CPU_IO_IN, Width));
        StoreField (CPU_STATE_IOPORT_OFFSET, SlotOf (pPort));
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        return TrapReturn ();
    }
    HRESULT STDMETHODCALLTYPE EmitSystemTrap (UINT32 Reason, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (sljit_sw) CPU_IO_MAKE (Reason, 0));
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        return TrapReturn ();
    }
    HRESULT STDMETHODCALLTYPE EmitSystemTrapValue (UINT32 Reason, ICpuValue *pValue, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (sljit_sw) CPU_IO_MAKE (Reason, 0));
        StoreField (CPU_STATE_IODATA_OFFSET, SlotOf (pValue));
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        return TrapReturn ();
    }
    HRESULT STDMETHODCALLTYPE EmitSyscall (UINT32 Vector, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_SYSCALL_OFFSET, (sljit_sw) Vector);
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        return TrapReturn ();
    }

    // ---- time base (ICpuClockEmitter): Cycles += Count, once per guest instruction --------
    HRESULT STDMETHODCALLTYPE EmitTick (UINT32 Count) override {
        sljit_emit_op1 (m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1 (SLJIT_S1), CPU_STATE_CYCLES_OFFSET);
        sljit_emit_op2 (m_C, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw) Count);
        sljit_emit_op1 (m_C, SLJIT_MOV, SLJIT_MEM1 (SLJIT_S1), CPU_STATE_CYCLES_OFFSET, SLJIT_R0, 0);
        return S_OK;
    }

    ICpuCode *Build () {
        // Any block created but never inserted (the CFG's exit/fallback target) gets a label here,
        // its pending jumps patched to it, and a clean "return ExecOk".
        struct sljit_label *pExit = sljit_emit_label (m_C);
        for (SljitBlock *pB : m_Blocks) {
            if (pB->m_Label == nullptr) {
                for (struct sljit_jump *pJ : pB->m_Pending) { sljit_set_label (pJ, pExit); }
                pB->m_Pending.clear ();
                pB->m_Label = pExit;
            }
        }
        sljit_emit_return (m_C, SLJIT_MOV, SLJIT_IMM, ExecOk);
        void *pCode = sljit_generate_code (m_C, 0, nullptr);
        sljit_free_compiler (m_C);
        m_C = nullptr;
        return pCode ? new SljitCode (pCode) : nullptr;
    }

private:
    sljit_s32 MovLoad (UINT32 Bits) {
        switch (Bits) {
        case 8:  return SLJIT_MOV_U8;
        case 16: return SLJIT_MOV_U16;
        case 32: return SLJIT_MOV_U32;
        default: return SLJIT_MOV;
        }
    }
    UINT32 NewSlot () { return m_NextSlot++; }
    void StoreImm (UINT32 Slot, sljit_sw Imm) {
        sljit_emit_op1 (m_C, SLJIT_MOV, SLJIT_MEM1 (SLJIT_SP), Slot * WS, SLJIT_IMM, Imm);
    }
    void StoreReg (UINT32 Slot, sljit_s32 Reg) {
        sljit_emit_op1 (m_C, SLJIT_MOV, SLJIT_MEM1 (SLJIT_SP), Slot * WS, Reg, 0);
    }
    void LoadReg (sljit_s32 Reg, UINT32 Slot) {
        sljit_emit_op1 (m_C, SLJIT_MOV, Reg, 0, SLJIT_MEM1 (SLJIT_SP), Slot * WS);
    }
    void Mask (sljit_s32 Reg, UINT32 Bits) {
        if (Bits < 64) {
            sljit_emit_op2 (m_C, SLJIT_AND, Reg, 0, Reg, 0, SLJIT_IMM, (sljit_sw)(((UINT64) 1 << Bits) - 1));
        }
    }
    void SignExtend (sljit_s32 Reg, UINT32 Bits) {
        if (Bits < 64) {
            sljit_sw Sh = (sljit_sw)(64 - Bits);
            sljit_emit_op2 (m_C, SLJIT_SHL,  Reg, 0, Reg, 0, SLJIT_IMM, Sh);
            sljit_emit_op2 (m_C, SLJIT_ASHR, Reg, 0, Reg, 0, SLJIT_IMM, Sh);
        }
    }
    // R0 = R0 (op) R1, where op is divide or remainder. sljit's DIVMOD leaves the quotient in R0 and
    // the remainder in R1. A zero divisor would trap on the host, so guard it to yield 0 (matching the
    // interpreter; a real CPU raises #DE, which the frontend models separately if at all).
    void EmitDivide (CPU_BINOP Op, UINT32 Bits) {
        bool Signed = (Op == BinSDiv || Op == BinSRem);
        bool Rem    = (Op == BinURem || Op == BinSRem);
        if (Signed) { SignExtend (SLJIT_R0, Bits); SignExtend (SLJIT_R1, Bits); }
        struct sljit_jump *pNZ   = sljit_emit_cmp (m_C, SLJIT_NOT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);
        sljit_emit_op1 (m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 0);   // divisor 0 -> 0
        struct sljit_jump *pDone = sljit_emit_jump (m_C, SLJIT_JUMP);
        sljit_set_label (pNZ, sljit_emit_label (m_C));
        sljit_emit_op0 (m_C, Signed ? SLJIT_DIVMOD_SW : SLJIT_DIVMOD_UW);
        if (Rem) { sljit_emit_op1 (m_C, SLJIT_MOV, SLJIT_R0, 0, SLJIT_R1, 0); }
        sljit_set_label (pDone, sljit_emit_label (m_C));
    }
    HRESULT Make (UINT32 Slot, UINT32 Bits, ICpuValue **ppValue) {
        *ppValue = new SljitValue (Slot, Bits);
        return S_OK;
    }
    // Patch a freshly-emitted jump to its target block, or queue it if the block has no label yet.
    void WireJump (struct sljit_jump *pJump, SljitBlock *pTarget) {
        if (pTarget->m_Label != nullptr) { sljit_set_label (pJump, pTarget->m_Label); }
        else                             { pTarget->m_Pending.push_back (pJump); }
    }
    void StoreField (UINT32 Off, UINT32 Slot) {          // CPU_STATE[Off] = slot value (full word)
        LoadReg (SLJIT_R0, Slot);
        sljit_emit_op1 (m_C, SLJIT_MOV, SLJIT_MEM1 (SLJIT_S1), Off, SLJIT_R0, 0);
    }
    void StoreFieldImm (UINT32 Off, sljit_sw Imm) {
        sljit_emit_op1 (m_C, SLJIT_MOV, SLJIT_MEM1 (SLJIT_S1), Off, SLJIT_IMM, Imm);
    }
    HRESULT TrapReturn () {                              // hand control back to the host machine loop
        sljit_emit_return (m_C, SLJIT_MOV, SLJIT_IMM, ExecSmc);
        return S_OK;
    }

    struct sljit_compiler   *m_C       = nullptr;
    UINT32                   m_NextSlot = 0;
    SljitBlock              *m_Cur      = nullptr;
    std::vector<SljitBlock *> m_Blocks;                  // every created block (borrowed)
};

class SljitBackend final : public ComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "sljit"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new SljitEmitter ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<SljitEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateSljitBackend (VOID)
{
    return new SljitBackend ();
}

} // namespace LibCPU
