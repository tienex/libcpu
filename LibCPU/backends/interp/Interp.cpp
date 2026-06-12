/** @file
  Interpreter backend implementation. See Interp.h.
**/
#include "Interp.h"
#include <vector>
#include <cstring>

namespace LibCPU {
namespace {

//
// Linear IR opcodes recorded by the emitter and run by the code object.
//
typedef enum _INTERP_OP {
    OpConstInt, OpGetReg, OpPutReg, OpLoad, OpStore,
    OpBinary, OpUnary, OpCompare, OpCast, OpSelect,
    OpGetFlag, OpSetFlag, OpSetPC,
    OpBranch,       // Imm = target block id
    OpCondBranch,   // A = cond temp, Imm = true block id, B = false block id
    OpCodeGuard,    // Imm = block PC: trap if this page's dirty bit is set
    OpIndirect,     // A = target temp: set TrapPc = target and return (resume there)
    OpSetDisp,      // A = target temp: DispPc = target (in-artifact dispatch scratch)
    OpGetDisp,      // Dest = DispPc
    OpEdgeCount     // Imm = slot: ++EdgeCount[slot] (profiling instrumentation)
} INTERP_OP;

//
// One recorded operation. Temp indices reference an SSA-style value array.
//
typedef struct _INTERP_INSN {
    UINT8  Op;      // INTERP_OP
    UINT8  Aux;     // sub-opcode (CPU_BINOP/CPU_CMP/CPU_CAST/CPU_UNOP) or Sext
    UINT32 Bits;    // result/access width
    UINT32 Dest;    // destination temp (for value-producing ops)
    UINT32 A, B, C; // operand temps / source width
    UINT64 Imm;     // immediate / register index / flag / pc
} INTERP_INSN;

//
// Bit helpers.
//
static UINT64
MaskBits (UINT64 Value, UINT32 Bits)
{
    if (Bits >= 64) {
        return Value;
    }
    return Value & ((UINT64) 1 << Bits) - 1;
}

static UINT64
SignExtend (UINT64 Value, UINT32 Bits)
{
    if (Bits == 0 || Bits >= 64) {
        return Value;
    }
    UINT64 Mask = (UINT64) 1 << (Bits - 1);
    Value &= ((UINT64) 1 << Bits) - 1;
    return (Value ^ Mask) - Mask;
}

static UINT64
RamRead (UINT8 CONST *pRam, UINT64 Addr, UINT32 Bits)
{
    UINT64 Value = 0;
    for (UINT32 Index = 0; Index < Bits / 8; Index++) {
        Value |= (UINT64) pRam[Addr + Index] << (8 * Index);
    }
    return Value;
}

static VOID
RamWrite (UINT8 *pRam, UINT64 Addr, UINT64 Value, UINT32 Bits)
{
    for (UINT32 Index = 0; Index < Bits / 8; Index++) {
        pRam[Addr + Index] = (UINT8)(Value >> (8 * Index));
    }
}

//
// ICpuValue / ICpuBlock implementations: opaque handles carrying a temp index.
//
class InterpValue final : public LcComObject<ICpuValue> {
public:
    InterpValue (UINT32 TempId, UINT32 Bits) : m_TempId (TempId), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_TempId;
    UINT32 m_Bits;
};

class InterpBlock final : public LcComObject<ICpuBlock> {
public:
    explicit InterpBlock (UINT32 Id) : m_Id (Id) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
    UINT32 m_Id;
};

static UINT32 TempOf (ICpuValue *pValue) { return static_cast<InterpValue *> (pValue)->m_TempId; }
static UINT32 BitsOf (ICpuValue *pValue) { return static_cast<InterpValue *> (pValue)->m_Bits; }
static UINT32 BlkId  (ICpuBlock *pBlock) { return static_cast<InterpBlock *> (pBlock)->m_Id; }

//
// The runnable code object: interprets the recorded IR.
//
class InterpCode final : public LcComObject<ICpuCode> {
public:
    InterpCode (std::vector<INTERP_INSN> Insns, UINT32 TempCount, std::vector<UINT32> BlockStart)
        : m_Insns (std::move (Insns)), m_TempCount (TempCount), m_BlockStart (std::move (BlockStart)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }

    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        UINT8        *pRam   = (UINT8 *) pRAM;
        INTERP_STATE *pState = (INTERP_STATE *) pGRF;
        std::vector<UINT64> Temp (m_TempCount, 0);

        for (UINT32 Ip = 0; Ip < m_Insns.size (); ) {
            INTERP_INSN CONST &In = m_Insns[Ip];
            switch ((INTERP_OP) In.Op) {
            case OpConstInt:
                Temp[In.Dest] = MaskBits (In.Imm, In.Bits);
                break;
            case OpGetReg:
                Temp[In.Dest] = MaskBits (pState->Reg[In.Imm], In.Bits);
                break;
            case OpPutReg: {
                UINT64 Value = In.Aux ? SignExtend (Temp[In.A], In.C) : Temp[In.A];
                pState->Reg[In.Imm] = MaskBits (Value, In.Bits);
                break;
            }
            case OpLoad:
                Temp[In.Dest] = RamRead (pRam, Temp[In.A], In.Bits);
                break;
            case OpStore:
                RamWrite (pRam, Temp[In.B], Temp[In.A], In.Bits);
                if (Temp[In.B] >= pState->CodeStart && Temp[In.B] < pState->CodeEnd) {   // SMC write-barrier
                    pState->CodeDirty[(Temp[In.B] >> 11) & 31] |= (UINT8) (1u << ((Temp[In.B] >> 8) & 7));
                }
                break;
            case OpBinary:
                Temp[In.Dest] = ApplyBinary ((CPU_BINOP) In.Aux, Temp[In.A], Temp[In.B], In.Bits);
                break;
            case OpUnary:
                Temp[In.Dest] = ApplyUnary ((CPU_UNOP) In.Aux, Temp[In.A], In.Bits);
                break;
            case OpCompare:
                Temp[In.Dest] = ApplyCompare ((CPU_CMP) In.Aux, Temp[In.A], Temp[In.B], In.Bits) ? 1 : 0;
                break;
            case OpCast:
                Temp[In.Dest] = ApplyCast ((CPU_CAST) In.Aux, Temp[In.A], In.C, In.Bits);
                break;
            case OpSelect:
                Temp[In.Dest] = (Temp[In.A] & 1) ? Temp[In.B] : Temp[In.C];
                break;
            case OpGetFlag:
                Temp[In.Dest] = pState->Flag[In.Imm] & 1;
                break;
            case OpSetFlag:
                pState->Flag[In.Imm] = (UINT8)(Temp[In.A] & 1);
                break;
            case OpSetPC:
                pState->Pc = In.Imm;
                break;
            case OpBranch:
                Ip = m_BlockStart[In.Imm];
                continue;
            case OpCondBranch:
                Ip = m_BlockStart[(Temp[In.A] & 1) ? (UINT32) In.Imm : In.B];
                continue;
            case OpCodeGuard: {
                UINT32 Page = (UINT32) ((In.Imm >> 8) & 255);
                if (pState->CodeDirty[Page >> 3] & (1u << (Page & 7))) {   // this page modified?
                    pState->TrapPc = In.Imm;
                    return ExecSmc;
                }
                break;
            }
            case OpIndirect:                          // indirect branch: resume at the target
                pState->TrapPc = Temp[In.A];
                return ExecSmc;
            case OpSetDisp:                           // stash the dispatch target
                pState->DispPc = Temp[In.A];
                break;
            case OpGetDisp:                           // read it back for the dispatcher's compares
                Temp[In.Dest] = pState->DispPc;
                break;
            case OpEdgeCount:                         // bump this call site's edge counter
                if (In.Imm < CPU_PROFILE_SLOTS) {
                    pState->EdgeCount[In.Imm]++;
                }
                break;
            }
            Ip++;
        }
        return ExecOk;
    }

private:
    static UINT64 ApplyBinary (CPU_BINOP Op, UINT64 A, UINT64 B, UINT32 Bits) {
        UINT64 R = 0;
        switch (Op) {
        case BinAdd:  R = A + B; break;
        case BinSub:  R = A - B; break;
        case BinMul:  R = A * B; break;
        case BinUDiv: R = B ? A / B : 0; break;
        case BinSDiv: R = B ? (UINT64)(SignExtend (A, Bits) / SignExtend (B, Bits)) : 0; break;
        case BinURem: R = B ? A % B : 0; break;
        case BinSRem: R = B ? (UINT64)(SignExtend (A, Bits) % SignExtend (B, Bits)) : 0; break;
        case BinAnd:  R = A & B; break;
        case BinOr:   R = A | B; break;
        case BinXor:  R = A ^ B; break;
        case BinShl:  R = A << (B & 63); break;
        case BinLShr: R = MaskBits (A, Bits) >> (B & 63); break;
        case BinAShr: R = (UINT64)(SignExtend (A, Bits) >> (B & 63)); break;
        case BinRol:  { UINT32 S = (UINT32)(B % Bits); R = (A << S) | (MaskBits (A, Bits) >> (Bits - S)); break; }
        case BinRor:  { UINT32 S = (UINT32)(B % Bits); R = (MaskBits (A, Bits) >> S) | (A << (Bits - S)); break; }
        }
        return MaskBits (R, Bits);
    }
    static UINT64 ApplyUnary (CPU_UNOP Op, UINT64 A, UINT32 Bits) {
        switch (Op) {
        case UnNeg: return MaskBits ((UINT64) 0 - A, Bits);
        case UnCom: return MaskBits (~A, Bits);
        case UnNot: return (MaskBits (A, Bits) == 0) ? 1 : 0;
        }
        return 0;
    }
    static bool ApplyCompare (CPU_CMP Pred, UINT64 A, UINT64 B, UINT32 Bits) {
        UINT64 Ua = MaskBits (A, Bits), Ub = MaskBits (B, Bits);
        INT64  Sa = (INT64) SignExtend (A, Bits), Sb = (INT64) SignExtend (B, Bits);
        switch (Pred) {
        case CmpEq:  return Ua == Ub;
        case CmpNe:  return Ua != Ub;
        case CmpULt: return Ua < Ub;
        case CmpULe: return Ua <= Ub;
        case CmpUGt: return Ua > Ub;
        case CmpUGe: return Ua >= Ub;
        case CmpSLt: return Sa < Sb;
        case CmpSLe: return Sa <= Sb;
        case CmpSGt: return Sa > Sb;
        case CmpSGe: return Sa >= Sb;
        }
        return false;
    }
    static UINT64 ApplyCast (CPU_CAST Op, UINT64 A, UINT32 SrcBits, UINT32 DstBits) {
        switch (Op) {
        case CastTrunc: return MaskBits (A, DstBits);
        case CastZExt:  return MaskBits (A, SrcBits);
        case CastSExt:  return MaskBits (SignExtend (A, SrcBits), DstBits);
        }
        return A;
    }

    std::vector<INTERP_INSN> m_Insns;
    UINT32                   m_TempCount;
    std::vector<UINT32>      m_BlockStart;   // block id -> index into m_Insns
};

//
// The builder: records ops, hands back opaque value handles.
//
class InterpEmitter final : public LcComObject<ICpuEmitter>, public ICpuSmcEmitter, public ICpuProfileEmitter {
public:
    // Three interfaces (ICpuEmitter + ICpuSmcEmitter + ICpuProfileEmitter): resolve QI
    // here, forward refcounting to the LcComObject base.
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        if (ppvObject != nullptr && LcIsEqualGUID (&riid, &IID_ICpuSmcEmitter)) {
            *ppvObject = static_cast<ICpuSmcEmitter *> (this);
            AddRef ();
            return S_OK;
        }
        if (ppvObject != nullptr && LcIsEqualGUID (&riid, &IID_ICpuProfileEmitter)) {
            *ppvObject = static_cast<ICpuProfileEmitter *> (this);
            AddRef ();
            return S_OK;
        }
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }
    UINT32 STDMETHODCALLTYPE AddRef () override { return LcComObject<ICpuEmitter>::AddRef (); }
    UINT32 STDMETHODCALLTYPE Release () override { return LcComObject<ICpuEmitter>::Release (); }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        return Produce (Bits, OpConstInt, 0, 0, 0, 0, Bits, Value, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        return Produce (Bits, OpGetReg, 0, 0, 0, 0, Bits, Index, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN Sext) override {
        Record (OpPutReg, Sext, Bits, 0, TempOf (pValue), 0, BitsOf (pValue), Index);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        return Produce (Bits, OpLoad, 0, TempOf (pAddr), 0, 0, Bits, 0, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Record (OpStore, 0, Bits, 0, TempOf (pValue), TempOf (pAddr), 0, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        return Produce (BitsOf (pA), OpBinary, (UINT8) Op, TempOf (pA), TempOf (pB), 0, BitsOf (pA), 0, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = (Op == UnNot) ? 1 : BitsOf (pA);
        return Produce (Bits, OpUnary, (UINT8) Op, TempOf (pA), 0, 0, BitsOf (pA), 0, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        return Produce (1, OpCompare, (UINT8) Pred, TempOf (pA), TempOf (pB), 0, BitsOf (pA), 0, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        // C carries the source width; Bits is the destination width.
        return Produce (Bits, OpCast, (UINT8) Op, TempOf (pA), 0, BitsOf (pA), Bits, 0, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *pCond, ICpuValue *pTrue, ICpuValue *pFalse, ICpuValue **ppValue) override {
        return Produce (BitsOf (pTrue), OpSelect, 0, TempOf (pCond), TempOf (pTrue), TempOf (pFalse), BitsOf (pTrue), 0, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        return Produce (1, OpGetFlag, 0, 0, 0, 0, 1, (UINT64) Flag, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        Record (OpSetFlag, 0, 1, 0, TempOf (pValue), 0, 0, (UINT64) Flag);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        Record (OpSetPC, 0, 0, 0, 0, 0, 0, Pc);
        return S_OK;
    }

    // ---- control flow: blocks are index ranges, edges set the instruction ptr
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override {
        *ppBlock = new InterpBlock ((UINT32) m_BlockStart.size ());
        m_BlockStart.push_back (0xFFFFFFFFu);   // unset until SetInsertBlock (exit block stays unset)
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *pBlock) override {
        m_BlockStart[BlkId (pBlock)] = (UINT32) m_Insns.size ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *pTarget) override {
        Record (OpBranch, 0, 0, 0, 0, 0, 0, BlkId (pTarget));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *pCond, ICpuBlock *pTrue, ICpuBlock *pFalse) override {
        Record (OpCondBranch, 0, 0, 0, TempOf (pCond), BlkId (pFalse), 0, BlkId (pTrue));
        return S_OK;
    }

    // ---- self-modifying-code guard (ICpuSmcEmitter); the barrier lives in Execute
    HRESULT STDMETHODCALLTYPE EmitCodeGuard (CPU_ADDR Pc) override {
        Record (OpCodeGuard, 0, 0, 0, 0, 0, 0, Pc);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IndirectBranch (ICpuValue *pTargetPc) override {
        Record (OpIndirect, 0, 0, 0, TempOf (pTargetPc), 0, 0, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetDispatchTarget (ICpuValue *pTargetPc) override {
        Record (OpSetDisp, 0, 0, 0, TempOf (pTargetPc), 0, 0, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDispatchTarget (ICpuValue **ppValue) override {
        return Produce (64, OpGetDisp, 0, 0, 0, 0, 64, 0, ppValue);
    }
    HRESULT STDMETHODCALLTYPE EmitEdgeCounter (UINT32 Index) override {
        Record (OpEdgeCount, 0, 0, 0, 0, 0, 0, (UINT64) Index);
        return S_OK;
    }

    ICpuCode *Build () {
        // Resolve any block never given a body (the AOT exit block) to "past the
        // end", so branching to it ends execution.
        for (UINT32 &Start : m_BlockStart) {
            if (Start == 0xFFFFFFFFu) {
                Start = (UINT32) m_Insns.size ();
            }
        }
        return new InterpCode (m_Insns, m_NextTemp, m_BlockStart);
    }

private:
    VOID Record (INTERP_OP Op, UINT8 Aux, UINT32 Bits, UINT32 Dest, UINT32 A, UINT32 B, UINT32 C, UINT64 Imm) {
        INTERP_INSN In{};
        In.Op = (UINT8) Op; In.Aux = Aux; In.Bits = Bits;
        In.Dest = Dest; In.A = A; In.B = B; In.C = C; In.Imm = Imm;
        m_Insns.push_back (In);
    }
    HRESULT Produce (UINT32 ResultBits, INTERP_OP Op, UINT8 Aux, UINT32 A, UINT32 B, UINT32 C, UINT32 Bits, UINT64 Imm, ICpuValue **ppValue) {
        UINT32 Dest = m_NextTemp++;
        Record (Op, Aux, Bits, Dest, A, B, C, Imm);
        *ppValue = new InterpValue (Dest, ResultBits);   // refcount 1, adopted by caller
        return S_OK;
    }

    std::vector<INTERP_INSN> m_Insns;
    UINT32                   m_NextTemp = 0;
    std::vector<UINT32>      m_BlockStart;   // block id -> index into m_Insns
};

//
// The backend object.
//
class InterpBackend final : public LcComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "interpreter"; }

    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture * /*pArch*/, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new InterpEmitter ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        *ppCode = static_cast<InterpEmitter *> (pEmitter)->Build ();
        return S_OK;
    }
};

} // anonymous namespace

ICpuBackend *
CreateInterpreterBackend (VOID)
{
    return new InterpBackend ();
}

} // namespace LibCPU
