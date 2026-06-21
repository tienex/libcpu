/** @file
  Interpreter backend implementation. See Interp.h.
**/
#include "Interp.h"
#include <vector>
#include <cstring>
#include <cstdio>
#include <string>
#include <cmath>

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
    OpGetCodeBase,  // Dest = CodeBase (PIC: artifact reconstructs addresses as CodeBase + relative)
    OpEdgeCount,    // Imm = slot: ++EdgeCount[slot] (profiling instrumentation)
    OpSyscall,      // Imm = vector, A = return-pc temp: set SyscallVector + TrapPc, return
    OpPortOut,      // Bits = width, A = port, B = data, C = return-pc: set IoCtrl=OUT, trap
    OpPortIn,       // Bits = width, A = port, B = return-pc: set IoCtrl=IN, trap
    OpSysTrap,      // Imm = reason (IRET/HLT/STI/CLI), A = return-pc: set IoCtrl=reason, trap
    OpSysTrapData,  // Imm = reason, A = return-pc, B = value: set IoCtrl=reason, IoData=value, trap
    OpTick          // Imm = count: Cycles += count (one per guest instruction; the time base)
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
class InterpValue final : public ComObject<ICpuValue> {
public:
    InterpValue (UINT32 TempId, UINT32 Bits) : m_TempId (TempId), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_TempId;
    UINT32 m_Bits;
};

class InterpBlock final : public ComObject<ICpuBlock> {
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
// Serialised artifact header: magic+version, then the three counts. The op array
// and block table follow as raw little-endian POD (the cache is host/build-local;
// the magic invalidates blobs if the layout ever changes).
static UINT32 CONST INTERP_BLOB_MAGIC = 0x31494C43;   // 'CLI1'

class InterpCode final : public ComObject<ICpuCode>, public ICpuCodeListing, public ICpuCodeSerialize {
public:
    InterpCode (std::vector<INTERP_INSN> Insns, UINT32 TempCount, std::vector<UINT32> BlockStart)
        : m_Insns (std::move (Insns)), m_TempCount (TempCount), m_BlockStart (std::move (BlockStart)) {}

    // ICpuCode + the optional ICpuCodeListing (disasm) and ICpuCodeSerialize (cache).
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        if (ppvObject != nullptr && CompareGuid (&riid, &IID_ICpuCodeListing)) {
            *ppvObject = static_cast<ICpuCodeListing *> (this);
            AddRef ();
            return S_OK;
        }
        if (ppvObject != nullptr && CompareGuid (&riid, &IID_ICpuCodeSerialize)) {
            *ppvObject = static_cast<ICpuCodeSerialize *> (this);
            AddRef ();
            return S_OK;
        }
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }
    UINT32 STDMETHODCALLTYPE AddRef () override { return ComObject<ICpuCode>::AddRef (); }
    UINT32 STDMETHODCALLTYPE Release () override { return ComObject<ICpuCode>::Release (); }
    HRESULT STDMETHODCALLTYPE GetListing (CHAR8 *pBuf, UINT32 BufSize, UINT32 *pNeeded) override;
    HRESULT STDMETHODCALLTYPE Serialize (UINT8 *pBuf, UINT32 BufSize, UINT32 *pNeeded) override;

    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        UINT8        *pRam   = (UINT8 *) pRAM;
        INTERP_STATE *pState = (INTERP_STATE *) pGRF;
        std::vector<UINT64>      Temp  (m_TempCount, 0);
        std::vector<long double> FTemp (m_TempCount, 0.0L);   // parallel float bank (80-bit values)

        for (UINT32 Ip = 0; Ip < m_Insns.size (); ) {
            INTERP_INSN CONST &In = m_Insns[Ip];
            switch ((INTERP_OP) In.Op) {
            case OpConstInt:
                Temp[In.Dest] = MaskBits (In.Imm, In.Bits);
                break;
            case OpGetReg:
                if (In.Bits > 64) { FTemp[In.Dest] = pState->Fpu[In.Imm & 31]; }   // an 80-bit FP register
                else { Temp[In.Dest] = MaskBits (pState->Reg[In.Imm], In.Bits); }
                break;
            case OpPutReg:
                if (In.Bits > 64) { pState->Fpu[In.Imm & 31] = FTemp[In.A]; }       // an 80-bit FP register
                else {
                    UINT64 Value = In.Aux ? SignExtend (Temp[In.A], In.C) : Temp[In.A];
                    pState->Reg[In.Imm] = MaskBits (Value, In.Bits);
                }
                break;
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
                if (IsFloatBinop ((CPU_BINOP) In.Aux)) {
                    FTemp[In.Dest] = ApplyBinaryF ((CPU_BINOP) In.Aux, FTemp[In.A], FTemp[In.B]);
                } else {
                    Temp[In.Dest] = ApplyBinary ((CPU_BINOP) In.Aux, Temp[In.A], Temp[In.B], In.Bits);
                }
                break;
            case OpUnary:
                if (IsFloatUnop ((CPU_UNOP) In.Aux)) {
                    FTemp[In.Dest] = ApplyUnaryF ((CPU_UNOP) In.Aux, FTemp[In.A]);
                } else {
                    Temp[In.Dest] = ApplyUnary ((CPU_UNOP) In.Aux, Temp[In.A], In.Bits);
                }
                break;
            case OpCompare:
                if (IsFloatCmp ((CPU_CMP) In.Aux)) {
                    Temp[In.Dest] = ApplyCompareF ((CPU_CMP) In.Aux, FTemp[In.A], FTemp[In.B]) ? 1 : 0;
                } else {
                    Temp[In.Dest] = ApplyCompare ((CPU_CMP) In.Aux, Temp[In.A], Temp[In.B], In.Bits) ? 1 : 0;
                }
                break;
            case OpCast:
                // The float casts move between the integer and float banks; the others stay integer.
                switch ((CPU_CAST) In.Aux) {
                case CastSIToF:  FTemp[In.Dest] = (long double) (INT64) SignExtend (Temp[In.A], In.C); break;
                case CastFToSI:  Temp[In.Dest]  = MaskBits ((UINT64) (INT64) FTemp[In.A], In.Bits); break;
                case CastFExt:   FTemp[In.Dest] = FTemp[In.A]; break;   // already the widest internally
                case CastFTrunc: FTemp[In.Dest] = (In.Bits == 32) ? (long double) (float) FTemp[In.A]
                                                : (In.Bits == 64) ? (long double) (double) FTemp[In.A]
                                                :                   FTemp[In.A]; break;
                default:         Temp[In.Dest]  = ApplyCast ((CPU_CAST) In.Aux, Temp[In.A], In.C, In.Bits); break;
                }
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
            case OpGetCodeBase:                       // PIC: the unit's load base, for address reconstruction
                Temp[In.Dest] = pState->CodeBase;
                break;
            case OpEdgeCount:                         // bump this call site's edge counter
                if (In.Imm < CPU_PROFILE_SLOTS) {
                    pState->EdgeCount[In.Imm]++;
                }
                break;
            case OpTick:                              // time base: one tick per guest instruction
                pState->Cycles += In.Imm;
                break;
            case OpSyscall:                           // guest system call: record + trap to host
                pState->SyscallVector = In.Imm;
                pState->TrapPc        = Temp[In.A];
                return ExecSmc;
            case OpPortOut:                           // device-bus write: record + trap
                pState->IoCtrl = CPU_IO_MAKE (CPU_IO_OUT, In.Bits);
                pState->IoPort = Temp[In.A];
                pState->IoData = Temp[In.B];
                pState->TrapPc = Temp[In.C];
                return ExecSmc;
            case OpPortIn:                            // device-bus read: record + trap
                pState->IoCtrl = CPU_IO_MAKE (CPU_IO_IN, In.Bits);
                pState->IoPort = Temp[In.A];
                pState->TrapPc = Temp[In.B];
                return ExecSmc;
            case OpSysTrap:                           // privileged control (IRET/HLT/STI/CLI)
                pState->IoCtrl = CPU_IO_MAKE (In.Imm, 0);
                pState->TrapPc = Temp[In.A];
                return ExecSmc;
            case OpSysTrapData:                       // privileged control carrying a runtime value
                pState->IoCtrl = CPU_IO_MAKE (In.Imm, 0);
                pState->IoData = Temp[In.B];
                pState->TrapPc = Temp[In.A];
                return ExecSmc;
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
        // Signed divide/remainder: the operands are sign-extended bit patterns, but the operator
        // itself must be signed too -- a UINT64 `/` would divide their full 64-bit magnitudes
        // unsigned (2^64-n / m), so reinterpret as INT64 before dividing.
        case BinSDiv: R = B ? (UINT64)((INT64) SignExtend (A, Bits) / (INT64) SignExtend (B, Bits)) : 0; break;
        case BinURem: R = B ? A % B : 0; break;
        case BinSRem: R = B ? (UINT64)((INT64) SignExtend (A, Bits) % (INT64) SignExtend (B, Bits)) : 0; break;
        case BinAnd:  R = A & B; break;
        case BinOr:   R = A | B; break;
        case BinXor:  R = A ^ B; break;
        case BinShl:  R = A << (B & 63); break;
        case BinLShr: R = MaskBits (A, Bits) >> (B & 63); break;
        case BinAShr: R = (UINT64)(SignExtend (A, Bits) >> (B & 63)); break;
        case BinRol:  { UINT32 S = (UINT32)(B % Bits); R = (A << S) | (MaskBits (A, Bits) >> (Bits - S)); break; }
        case BinRor:  { UINT32 S = (UINT32)(B % Bits); R = (MaskBits (A, Bits) >> S) | (A << (Bits - S)); break; }
        case BinFAdd: case BinFSub: case BinFMul: case BinFDiv: break;   // float ops: the FTemp path
        }
        return MaskBits (R, Bits);
    }
    static UINT64 ApplyUnary (CPU_UNOP Op, UINT64 A, UINT32 Bits) {
        switch (Op) {
        case UnNeg: return MaskBits ((UINT64) 0 - A, Bits);
        case UnCom: return MaskBits (~A, Bits);
        case UnNot: return (MaskBits (A, Bits) == 0) ? 1 : 0;
        case UnFNeg: case UnFAbs: case UnFSqrt: break;   // float ops: the FTemp path
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
        case CmpFOEq: case CmpFOLt: case CmpFOGt: case CmpFUno: break;   // float compares: the FTemp path
        }
        return false;
    }
    static UINT64 ApplyCast (CPU_CAST Op, UINT64 A, UINT32 SrcBits, UINT32 DstBits) {
        switch (Op) {
        case CastTrunc: return MaskBits (A, DstBits);
        case CastZExt:  return MaskBits (A, SrcBits);
        case CastSExt:  return MaskBits (SignExtend (A, SrcBits), DstBits);
        default:        return A;          // the float casts are handled on the long double path
        }
    }

    // Floating-point execution on the host long double (80-bit extended on x86). FP values live in
    // a parallel temp/register bank, so these mirror the integer apply helpers for the FP opcodes.
    static bool IsFloatBinop (CPU_BINOP Op) { return Op >= BinFAdd; }
    static bool IsFloatUnop  (CPU_UNOP Op)  { return Op >= UnFNeg; }
    static bool IsFloatCmp   (CPU_CMP Op)   { return Op >= CmpFOEq; }

    static long double ApplyBinaryF (CPU_BINOP Op, long double A, long double B) {
        switch (Op) {
        case BinFAdd: return A + B;
        case BinFSub: return A - B;
        case BinFMul: return A * B;
        case BinFDiv: return A / B;        // IEEE: division by zero yields +/-inf or NaN, no trap
        default:      return 0;
        }
    }
    static long double ApplyUnaryF (CPU_UNOP Op, long double A) {
        switch (Op) {
        case UnFNeg:  return -A;
        case UnFAbs:  return std::fabsl (A);
        case UnFSqrt: return std::sqrtl (A);
        default:      return 0;
        }
    }
    static bool ApplyCompareF (CPU_CMP Pred, long double A, long double B) {
        switch (Pred) {
        case CmpFOEq: return A == B;       // ordered: false if either is NaN (== is already so)
        case CmpFOLt: return A < B;
        case CmpFOGt: return A > B;
        case CmpFUno: return std::isnan ((double) A) || std::isnan ((double) B);
        default:      return false;
        }
    }

    std::vector<INTERP_INSN> m_Insns;
    UINT32                   m_TempCount;
    std::vector<UINT32>      m_BlockStart;   // block id -> index into m_Insns
};

//
// Render one op as a readable line (the "translated code" a debugger shows).
//
static std::string
FormatInterpInsn (INTERP_INSN CONST &In)
{
    static CHAR8 CONST *const Bin[] = { "add", "sub", "mul", "udiv", "sdiv", "urem", "srem",
                                        "and", "or", "xor", "shl", "lshr", "ashr", "rol", "ror" };
    static CHAR8 CONST *const Un[]  = { "neg", "com", "not" };
    static CHAR8 CONST *const Cmp[] = { "eq", "ne", "ult", "ule", "ugt", "uge", "slt", "sle", "sgt", "sge" };
    static CHAR8 CONST *const Cst[] = { "trunc", "zext", "sext" };
    char Buf[160];
    switch ((INTERP_OP) In.Op) {
    case OpConstInt:   std::snprintf (Buf, sizeof (Buf), "t%u = const 0x%llx        ; %u-bit", In.Dest, (unsigned long long) In.Imm, In.Bits); break;
    case OpGetReg:     std::snprintf (Buf, sizeof (Buf), "t%u = reg[%llu]            ; %u-bit", In.Dest, (unsigned long long) In.Imm, In.Bits); break;
    case OpPutReg:     std::snprintf (Buf, sizeof (Buf), "reg[%llu] = t%u", (unsigned long long) In.Imm, In.A); break;
    case OpLoad:       std::snprintf (Buf, sizeof (Buf), "t%u = load.%u [t%u]", In.Dest, In.Bits, In.A); break;
    case OpStore:      std::snprintf (Buf, sizeof (Buf), "store.%u [t%u] = t%u", In.Bits, In.B, In.A); break;
    case OpBinary:     std::snprintf (Buf, sizeof (Buf), "t%u = %s t%u, t%u", In.Dest, In.Aux < 15 ? Bin[In.Aux] : "?", In.A, In.B); break;
    case OpUnary:      std::snprintf (Buf, sizeof (Buf), "t%u = %s t%u", In.Dest, In.Aux < 3 ? Un[In.Aux] : "?", In.A); break;
    case OpCompare:    std::snprintf (Buf, sizeof (Buf), "t%u = cmp.%s t%u, t%u", In.Dest, In.Aux < 10 ? Cmp[In.Aux] : "?", In.A, In.B); break;
    case OpCast:       std::snprintf (Buf, sizeof (Buf), "t%u = %s t%u           ; %u->%u", In.Dest, In.Aux < 3 ? Cst[In.Aux] : "?", In.A, In.C, In.Bits); break;
    case OpSelect:     std::snprintf (Buf, sizeof (Buf), "t%u = sel t%u ? t%u : t%u", In.Dest, In.A, In.B, In.C); break;
    case OpGetFlag:    std::snprintf (Buf, sizeof (Buf), "t%u = flag[%llu]", In.Dest, (unsigned long long) In.Imm); break;
    case OpSetFlag:    std::snprintf (Buf, sizeof (Buf), "flag[%llu] = t%u", (unsigned long long) In.Imm, In.A); break;
    case OpSetPC:      std::snprintf (Buf, sizeof (Buf), "pc = 0x%llx", (unsigned long long) In.Imm); break;
    case OpBranch:     std::snprintf (Buf, sizeof (Buf), "branch L%llu", (unsigned long long) In.Imm); break;
    case OpCondBranch: std::snprintf (Buf, sizeof (Buf), "condbranch t%u ? L%llu : L%u", In.A, (unsigned long long) In.Imm, In.B); break;
    case OpCodeGuard:  std::snprintf (Buf, sizeof (Buf), "codeguard 0x%llx", (unsigned long long) In.Imm); break;
    case OpIndirect:   std::snprintf (Buf, sizeof (Buf), "indirect t%u            ; trap -> resume at target", In.A); break;
    case OpSetDisp:    std::snprintf (Buf, sizeof (Buf), "setdisp t%u", In.A); break;
    case OpGetDisp:    std::snprintf (Buf, sizeof (Buf), "t%u = getdisp", In.Dest); break;
    case OpGetCodeBase: std::snprintf (Buf, sizeof (Buf), "t%u = getcodebase", In.Dest); break;
    case OpEdgeCount:  std::snprintf (Buf, sizeof (Buf), "edgecount[%llu]++", (unsigned long long) In.Imm); break;
    case OpSyscall:    std::snprintf (Buf, sizeof (Buf), "syscall 0x%llx -> t%u    ; trap -> host dispatch", (unsigned long long) In.Imm, In.A); break;
    case OpPortOut:    std::snprintf (Buf, sizeof (Buf), "out.%u port t%u, t%u     ; trap -> device bus", In.Bits, In.A, In.B); break;
    case OpPortIn:     std::snprintf (Buf, sizeof (Buf), "in.%u  port t%u          ; trap -> device bus", In.Bits, In.A); break;
    case OpSysTrap:    std::snprintf (Buf, sizeof (Buf), "systrap %llu -> t%u       ; trap -> machine", (unsigned long long) In.Imm, In.A); break;
    case OpSysTrapData: std::snprintf (Buf, sizeof (Buf), "systrap %llu t%u -> t%u   ; trap -> machine", (unsigned long long) In.Imm, In.B, In.A); break;
    case OpTick:       std::snprintf (Buf, sizeof (Buf), "tick %llu                 ; cycles += n", (unsigned long long) In.Imm); break;
    default:           std::snprintf (Buf, sizeof (Buf), "op%u", In.Op); break;
    }
    return std::string (Buf);
}

HRESULT STDMETHODCALLTYPE
InterpCode::GetListing (CHAR8 *pBuf, UINT32 BufSize, UINT32 *pNeeded)
{
    // Reverse the block-start map so we can label where each block begins.
    std::string Out;
    for (UINT32 Ip = 0; Ip < m_Insns.size (); Ip++) {
        for (UINT32 B = 0; B < m_BlockStart.size (); B++) {
            if (m_BlockStart[B] == Ip) {
                Out += "L" + std::to_string (B) + ":\n";
            }
        }
        Out += "    " + FormatInterpInsn (m_Insns[Ip]) + "\n";
    }
    if (pNeeded != nullptr) {
        *pNeeded = (UINT32) Out.size ();
    }
    if (pBuf != nullptr && BufSize > 0) {
        UINT32 Copy = (UINT32) Out.size () < (BufSize - 1) ? (UINT32) Out.size () : (BufSize - 1);
        std::memcpy (pBuf, Out.data (), Copy);
        pBuf[Copy] = '\0';
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE
InterpCode::Serialize (UINT8 *pBuf, UINT32 BufSize, UINT32 *pNeeded)
{
    UINT32 InsnBytes = (UINT32) (m_Insns.size () * sizeof (INTERP_INSN));
    UINT32 BlkBytes  = (UINT32) (m_BlockStart.size () * sizeof (UINT32));
    UINT32 Total     = 4 * (UINT32) sizeof (UINT32) + InsnBytes + BlkBytes;
    if (pNeeded != nullptr) {
        *pNeeded = Total;
    }
    if (pBuf == nullptr || BufSize < Total) {
        return S_OK;                               // caller sizes the buffer and retries
    }
    UINT8 *p = pBuf;
    auto PutU32 = [&p] (UINT32 v) { std::memcpy (p, &v, 4); p += 4; };
    PutU32 (INTERP_BLOB_MAGIC);
    PutU32 ((UINT32) m_Insns.size ());
    PutU32 (m_TempCount);
    PutU32 ((UINT32) m_BlockStart.size ());
    if (InsnBytes) { std::memcpy (p, m_Insns.data (), InsnBytes); p += InsnBytes; }
    if (BlkBytes)  { std::memcpy (p, m_BlockStart.data (), BlkBytes); }
    return S_OK;
}

//
// The builder: records ops, hands back opaque value handles.
//
class InterpEmitter final : public ComObject<ICpuEmitter>, public ICpuSmcEmitter, public ICpuProfileEmitter, public ICpuSyscallEmitter, public ICpuSystemEmitter, public ICpuClockEmitter {
public:
    // Five interfaces (ICpuEmitter + ICpuSmcEmitter + ICpuProfileEmitter +
    // ICpuSyscallEmitter): resolve QI here, forward refcounting to the ComObject base.
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        if (ppvObject != nullptr && CompareGuid (&riid, &IID_ICpuSmcEmitter)) {
            *ppvObject = static_cast<ICpuSmcEmitter *> (this);
            AddRef ();
            return S_OK;
        }
        if (ppvObject != nullptr && CompareGuid (&riid, &IID_ICpuProfileEmitter)) {
            *ppvObject = static_cast<ICpuProfileEmitter *> (this);
            AddRef ();
            return S_OK;
        }
        if (ppvObject != nullptr && CompareGuid (&riid, &IID_ICpuSyscallEmitter)) {
            *ppvObject = static_cast<ICpuSyscallEmitter *> (this);
            AddRef ();
            return S_OK;
        }
        if (ppvObject != nullptr && CompareGuid (&riid, &IID_ICpuSystemEmitter)) {
            *ppvObject = static_cast<ICpuSystemEmitter *> (this);
            AddRef ();
            return S_OK;
        }
        if (ppvObject != nullptr && CompareGuid (&riid, &IID_ICpuClockEmitter)) {
            *ppvObject = static_cast<ICpuClockEmitter *> (this);
            AddRef ();
            return S_OK;
        }
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }
    UINT32 STDMETHODCALLTYPE AddRef () override { return ComObject<ICpuEmitter>::AddRef (); }
    UINT32 STDMETHODCALLTYPE Release () override { return ComObject<ICpuEmitter>::Release (); }

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
    HRESULT STDMETHODCALLTYPE GetCodeBase (ICpuValue **ppValue) override {
        return Produce (64, OpGetCodeBase, 0, 0, 0, 0, 64, 0, ppValue);
    }
    HRESULT STDMETHODCALLTYPE EmitEdgeCounter (UINT32 Index) override {
        Record (OpEdgeCount, 0, 0, 0, 0, 0, 0, (UINT64) Index);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitTick (UINT32 Count) override {
        Record (OpTick, 0, 0, 0, 0, 0, 0, (UINT64) Count);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitSyscall (UINT32 Vector, ICpuValue *pReturnPc) override {
        Record (OpSyscall, 0, 0, 0, TempOf (pReturnPc), 0, 0, (UINT64) Vector);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitPortOut (ICpuValue *pPort, ICpuValue *pData, UINT32 Width, ICpuValue *pReturnPc) override {
        Record (OpPortOut, 0, Width, 0, TempOf (pPort), TempOf (pData), TempOf (pReturnPc), 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitPortIn (ICpuValue *pPort, UINT32 Width, ICpuValue *pReturnPc) override {
        Record (OpPortIn, 0, Width, 0, TempOf (pPort), TempOf (pReturnPc), 0, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitSystemTrap (UINT32 Reason, ICpuValue *pReturnPc) override {
        Record (OpSysTrap, 0, 0, 0, TempOf (pReturnPc), 0, 0, (UINT64) Reason);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitSystemTrapValue (UINT32 Reason, ICpuValue *pValue, ICpuValue *pReturnPc) override {
        Record (OpSysTrapData, 0, 0, 0, TempOf (pReturnPc), TempOf (pValue), 0, (UINT64) Reason);
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
class InterpBackend final : public ComObject<ICpuBackend>, public ICpuBackendCache {
public:
    // ICpuBackend + the optional ICpuBackendCache (reload a serialised artifact).
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        if (ppvObject != nullptr && CompareGuid (&riid, &IID_ICpuBackendCache)) {
            *ppvObject = static_cast<ICpuBackendCache *> (this);
            AddRef ();
            return S_OK;
        }
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    UINT32 STDMETHODCALLTYPE AddRef () override { return ComObject<ICpuBackend>::AddRef (); }
    UINT32 STDMETHODCALLTYPE Release () override { return ComObject<ICpuBackend>::Release (); }

    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "interpreter"; }

    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture * /*pArch*/, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new InterpEmitter ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        *ppCode = static_cast<InterpEmitter *> (pEmitter)->Build ();
        return S_OK;
    }

    // Rebuild an InterpCode from a blob produced by InterpCode::Serialize.
    HRESULT STDMETHODCALLTYPE LoadCode (UINT8 CONST *pBytes, UINT32 Len, ICpuCode **ppCode) override {
        *ppCode = nullptr;
        if (Len < 4 * sizeof (UINT32)) {
            return E_FAIL;
        }
        UINT8 CONST *p = pBytes;
        auto GetU32 = [&p] () -> UINT32 { UINT32 v; std::memcpy (&v, p, 4); p += 4; return v; };
        if (GetU32 () != INTERP_BLOB_MAGIC) {
            return E_FAIL;
        }
        UINT32 InsnCount = GetU32 ();
        UINT32 TempCount = GetU32 ();
        UINT32 BlkCount  = GetU32 ();
        UINT32 Want = 4 * (UINT32) sizeof (UINT32) + InsnCount * (UINT32) sizeof (INTERP_INSN) + BlkCount * (UINT32) sizeof (UINT32);
        if (Len < Want) {
            return E_FAIL;
        }
        std::vector<INTERP_INSN> Insns (InsnCount);
        if (InsnCount) { std::memcpy (Insns.data (), p, InsnCount * sizeof (INTERP_INSN)); p += InsnCount * sizeof (INTERP_INSN); }
        std::vector<UINT32> Blocks (BlkCount);
        if (BlkCount) { std::memcpy (Blocks.data (), p, BlkCount * sizeof (UINT32)); }
        *ppCode = new InterpCode (std::move (Insns), TempCount, std::move (Blocks));
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
