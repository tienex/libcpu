/** @file  A live ICpuArchitecture that interprets a UPCL description. See UpclArch.h. */

#include "UpclArch.h"
#include "LibCPU/PCom.h"
#include "LibCPU/CpuState.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace LibCPU {
namespace Upcl {
namespace {

typedef std::map<std::string, UINT64> FIELD_MAP;

// A decoded instruction's per-field values, plus the matched Insn.
typedef struct _DECODED {
    Insn      *pInsn;
    Format    *pFormat;
    FIELD_MAP  Fields;
    UINT32     Length;
} DECODED;

class UpclArch final : public ComObject<ICpuArchitecture> {
public:
    UpclArch (Module *pMod, Arch *pArch, std::set<std::string> Enabled)
        : m_pMod (pMod), m_pArch (pArch), m_Enabled (std::move (Enabled))
    {
        m_WordBits = m_pArch->WordSize ? m_pArch->WordSize : 16;
        m_AddrBits = m_pArch->AddressSize ? m_pArch->AddressSize : 16;
        // Index formats by name and register names by spelling.
        for (Format *F : m_pArch->Formats) { m_Formats[F->Name] = F; }
        for (UINT32 I = 0; I < m_pArch->Registers.size (); I++) {
            m_RegIndex[m_pArch->Registers[I].Name] = I;
        }
        // The decodable set: base-ISA instructions (no feature gate) plus those whose
        // gating feature is enabled by the selected CPU model. Decode/disasm/translate
        // all run over this filtered list, so a model that lacks a feature never sees its
        // instructions.
        for (Insn *I : m_pArch->Insns) {
            if (I->Feature.empty () || m_Enabled.count (I->Feature) != 0) {
                m_EnabledInsns.push_back (I);
            }
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuArchitecture, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE GetInfo (CPU_ARCH_INFO *pInfo) override {
        pInfo->pName       = m_pArch->Name.c_str ();
        pInfo->pFullName   = m_pArch->FullName.c_str ();
        pInfo->ByteSize    = 8;
        pInfo->WordSize    = (UINT8) m_WordBits;
        pInfo->AddressSize = m_AddrBits;
        pInfo->PsrSize     = (UINT8) m_WordBits;
        pInfo->IsBigEndian = m_pArch->Little ? FALSE : TRUE;
        pInfo->GprCount    = (UINT32) m_pArch->Registers.size ();
        pInfo->GprBits     = m_WordBits;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetCodeMemory (UINT8 CONST *pBase, UINT64 Size) override {
        m_pCode = pBase;
        m_CodeSize = Size;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TagInstr (CPU_ADDR Pc, UINT32 *pTag, CPU_ADDR *pNewPc, CPU_ADDR *pNextPc) override {
        DECODED D;
        if (!Decode (Pc, &D)) {
            *pTag = TagContinue; *pNewPc = (CPU_ADDR) -1; *pNextPc = Pc + 1;   // unknown: skip a byte
            return S_OK;
        }
        *pNextPc = Pc + D.Length;
        // A constant-target write to pc is a branch; otherwise straight-line.
        UINT64 Target = 0;
        if (PcTarget (D, Pc, &Target)) {
            *pTag = TagBranch;
            *pNewPc = (CPU_ADDR) Target;
        } else {
            *pTag = TagContinue;
            *pNewPc = (CPU_ADDR) -1;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Disassemble (CPU_ADDR Pc, CHAR8 *pLine, UINT32 MaxLine) override {
        DECODED D;
        if (!Decode (Pc, &D)) {
            std::snprintf (pLine, MaxLine, "db 0x%02x", m_pCode[Pc]);
            return S_OK;
        }
        std::string Out;
        std::string CONST &Fmt = D.pInsn->Disasm;
        for (size_t I = 0; I < Fmt.size (); ) {
            if (Fmt[I] == '%' && I + 1 < Fmt.size () && IsWord (Fmt[I + 1])) {
                size_t J = I + 1;
                while (J < Fmt.size () && IsWord (Fmt[J])) { J++; }
                std::string Name = Fmt.substr (I + 1, J - (I + 1));
                auto It = D.Fields.find (Name);
                UINT64 V = (It != D.Fields.end ()) ? It->second : 0;
                if (IsRegField (D.pInsn, Name) && V < m_pArch->Registers.size ()) {
                    Out += m_pArch->Registers[(size_t) V].Name;
                } else {
                    char B[20];
                    std::snprintf (B, sizeof (B), "%llx", (unsigned long long) V);
                    Out += B;
                }
                I = J;
            } else {
                Out.push_back (Fmt[I++]);
            }
        }
        std::snprintf (pLine, MaxLine, "%s", Out.c_str ());
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TranslateInstr (CPU_ADDR Pc, ICpuEmitter *pE) override {
        DECODED D;
        if (!Decode (Pc, &D)) {
            return S_OK;
        }
        std::vector<ComPtr<ICpuValue>> Pool;        // holds emitted temporaries
        for (Stmt *S : D.pInsn->Semantics) {
            if (IsPcWrite (S)) {
                continue;                            // the branch edge is wired from the tag
            }
            ExecAssign (S, pE, D, Pc, Pool);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TranslateCond (CPU_ADDR /*Pc*/, ICpuEmitter * /*pE*/, ICpuValue **ppCond) override {
        *ppCond = nullptr;
        return E_NOTIMPL;                            // conditional control flow: next increment
    }

private:
    static bool IsWord (CHAR8 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

    UINT32 RegIndexOf (std::string CONST &Name) CONST {
        auto It = m_RegIndex.find (Name);
        return (It != m_RegIndex.end ()) ? It->second : ~(UINT32) 0;
    }

    // Does field Name get used as a register index (reg[Name]) anywhere in pInsn?
    bool IsRegField (Insn *pInsn, std::string CONST &Name) CONST {
        for (Stmt *S : pInsn->Semantics) {
            if (ExprUsesAsRegIndex (S->Lhs, Name) || ExprUsesAsRegIndex (S->Rhs, Name)) { return true; }
        }
        return false;
    }
    static bool ExprUsesAsRegIndex (Expr *E, std::string CONST &Name) {
        if (E == nullptr) { return false; }
        if (E->Kind == ExprIndex && E->Args.size () == 2 &&
            E->Args[0]->Kind == ExprName && E->Args[0]->Name == "reg" &&
            E->Args[1]->Kind == ExprName && E->Args[1]->Name == Name) {
            return true;
        }
        for (Expr *A : E->Args) { if (ExprUsesAsRegIndex (A, Name)) { return true; } }
        return false;
    }

    static bool IsPcWrite (Stmt *S) {
        return S->Kind == StmtAssign && S->Lhs != nullptr && S->Lhs->Kind == ExprName && S->Lhs->Name == "pc";
    }

    // Decode the instruction at Pc: find the first instruction whose format-extracted
    // fields satisfy its opcode bindings.
    bool Decode (CPU_ADDR Pc, DECODED *pOut) CONST {
        for (Insn *I : m_EnabledInsns) {
            auto Fi = m_Formats.find (I->Format);
            if (Fi == m_Formats.end ()) { continue; }
            Format *F = Fi->second;
            UINT32 Bits = F->TotalBits ();
            UINT32 Len = (Bits + 7) / 8;
            if (Pc + Len > m_CodeSize) { continue; }
            FIELD_MAP Fields;
            ExtractFields (F, Pc, &Fields);
            bool Match = true;
            for (Field *B : I->Bindings) {
                UINT64 Want = EvalConst (B->Value, Fields, Pc);
                auto It = Fields.find (B->Name);
                if (It == Fields.end () || It->second != Want) { Match = false; break; }
            }
            if (Match) {
                pOut->pInsn = I; pOut->pFormat = F; pOut->Fields = std::move (Fields); pOut->Length = Len;
                return true;
            }
        }
        return false;
    }

    // Lay the format's fields out MSB-first. A byte-aligned, byte-sized field is read
    // as an integer with the architecture endianness; a sub-byte field is the bits at
    // its position within the current byte.
    void ExtractFields (Format *F, CPU_ADDR Pc, FIELD_MAP *pFields) CONST {
        UINT32 BitPos = 0;
        for (FormatField CONST &FF : F->Fields) {
            UINT32 ByteOff = BitPos / 8;
            UINT64 Value = 0;
            if ((BitPos % 8) == 0 && (FF.Width % 8) == 0) {
                UINT32 Bytes = FF.Width / 8;
                for (UINT32 B = 0; B < Bytes; B++) {
                    UINT8 Byte = m_pCode[Pc + ByteOff + B];
                    if (m_pArch->Little) { Value |= (UINT64) Byte << (8 * B); }
                    else                 { Value = (Value << 8) | Byte; }
                }
            } else {
                UINT32 Shift = 8 - (BitPos % 8) - FF.Width;
                Value = (m_pCode[Pc + ByteOff] >> Shift) & ((1u << FF.Width) - 1);
            }
            (*pFields)[FF.Name] = Value;
            BitPos += FF.Width;
        }
    }

    // Compile-time evaluation (field values + literals + arithmetic) -- for register
    // indices, opcode-binding constants, and constant branch targets.
    UINT64 EvalConst (Expr *E, FIELD_MAP CONST &F, CPU_ADDR Pc) CONST {
        if (E == nullptr) { return 0; }
        switch (E->Kind) {
        case ExprInt:  return E->Int;
        case ExprName: {
            if (E->Name == "pc") { return (UINT64) Pc; }
            auto It = F.find (E->Name);
            return (It != F.end ()) ? It->second : 0;
        }
        case ExprUnary: {
            UINT64 A = EvalConst (E->Args[0], F, Pc);
            return (E->Op == TokMinus) ? (UINT64) (-(INT64) A) : (E->Op == TokTilde) ? ~A : A;
        }
        case ExprBinary: {
            UINT64 A = EvalConst (E->Args[0], F, Pc), B = EvalConst (E->Args[1], F, Pc);
            return ApplyConst (E->Op, A, B);
        }
        default: return 0;
        }
    }

    static UINT64 ApplyConst (TOKEN_KIND Op, UINT64 A, UINT64 B) {
        switch (Op) {
        case TokPlus: return A + B;   case TokMinus: return A - B;  case TokStar: return A * B;
        case TokAmp:  return A & B;   case TokPipe:  return A | B;  case TokCaret: return A ^ B;
        case TokShl:  return A << B;  case TokShr:   return A >> B;
        case TokSlash: return B ? A / B : 0;  case TokPercent: return B ? A % B : 0;
        default: return 0;
        }
    }

    // A pc-write whose target is a compile-time constant -> a branch.
    bool PcTarget (DECODED CONST &D, CPU_ADDR Pc, UINT64 *pTarget) CONST {
        for (Stmt *S : D.pInsn->Semantics) {
            if (IsPcWrite (S) && S->Rhs != nullptr) {
                *pTarget = EvalConst (S->Rhs, D.Fields, Pc);
                return true;
            }
        }
        return false;
    }

    static CPU_BINOP MapBinop (TOKEN_KIND Op) {
        switch (Op) {
        case TokPlus: return BinAdd;  case TokMinus: return BinSub;  case TokStar: return BinMul;
        case TokAmp:  return BinAnd;  case TokPipe:  return BinOr;   case TokCaret: return BinXor;
        case TokShl:  return BinShl;  case TokShr:   return BinLShr;
        case TokSlash: return BinUDiv; case TokPercent: return BinURem;
        default: return BinAdd;
        }
    }

    // Evaluate an expression into an emitted ICpuValue. The value is kept alive in Pool
    // (released when the translation finishes); the raw pointer is returned for use as
    // an operand.
    ICpuValue *EvalValue (Expr *E, ICpuEmitter *pE, DECODED CONST &D, CPU_ADDR Pc,
                          std::vector<ComPtr<ICpuValue>> &Pool) {
        ComPtr<ICpuValue> V;
        switch (E->Kind) {
        case ExprInt:
            pE->ConstInt (m_WordBits, E->Int, &V);
            break;
        case ExprName: {
            if (E->Name == "pc") {
                pE->ConstInt (m_AddrBits, (UINT64) Pc, &V);
            } else if (D.Fields.count (E->Name)) {
                pE->ConstInt (m_WordBits, D.Fields.at (E->Name), &V);
            } else if (RegIndexOf (E->Name) != ~(UINT32) 0) {
                pE->GetRegister (RegIndexOf (E->Name), m_WordBits, &V);
            } else {
                pE->ConstInt (m_WordBits, 0, &V);
            }
            break;
        }
        case ExprUnary: {
            ICpuValue *A = EvalValue (E->Args[0], pE, D, Pc, Pool);
            pE->UnaryOp (E->Op == TokTilde ? UnCom : UnNeg, A, &V);
            break;
        }
        case ExprBinary: {
            ICpuValue *A = EvalValue (E->Args[0], pE, D, Pc, Pool);
            ICpuValue *B = EvalValue (E->Args[1], pE, D, Pc, Pool);
            pE->BinaryOp (MapBinop (E->Op), A, B, &V);
            break;
        }
        case ExprIndex: {
            Expr *Base = E->Args[0];
            if (Base->Kind == ExprName && Base->Name == "reg") {
                UINT32 Idx = (UINT32) EvalConst (E->Args[1], D.Fields, Pc);
                pE->GetRegister (Idx, m_WordBits, &V);
            } else {                                 // mem[addr]
                ICpuValue *Addr = EvalValue (E->Args[1], pE, D, Pc, Pool);
                pE->Load (Addr, m_WordBits, &V);
            }
            break;
        }
        default:
            pE->ConstInt (m_WordBits, 0, &V);
            break;
        }
        ICpuValue *Raw = V.Get ();
        Pool.push_back (std::move (V));
        return Raw;
    }

    void ExecAssign (Stmt *S, ICpuEmitter *pE, DECODED CONST &D, CPU_ADDR Pc,
                     std::vector<ComPtr<ICpuValue>> &Pool) {
        if (S->Rhs == nullptr || S->Lhs == nullptr) { return; }
        ICpuValue *Rhs = EvalValue (S->Rhs, pE, D, Pc, Pool);
        Expr *L = S->Lhs;
        if (L->Kind == ExprIndex && L->Args.size () == 2 && L->Args[0]->Kind == ExprName) {
            if (L->Args[0]->Name == "reg") {
                UINT32 Idx = (UINT32) EvalConst (L->Args[1], D.Fields, Pc);
                pE->PutRegister (Idx, Rhs, m_WordBits, FALSE);
            } else {                                 // mem[addr] = ...
                ICpuValue *Addr = EvalValue (L->Args[1], pE, D, Pc, Pool);
                pE->Store (Rhs, Addr, m_WordBits);
            }
        } else if (L->Kind == ExprName && RegIndexOf (L->Name) != ~(UINT32) 0) {
            pE->PutRegister (RegIndexOf (L->Name), Rhs, m_WordBits, FALSE);
        }
    }

    Module                          *m_pMod;
    Arch                            *m_pArch;
    std::set<std::string>            m_Enabled;     // enabled feature names (from the CPU model)
    std::vector<Insn *>              m_EnabledInsns; // base + feature-enabled instructions
    UINT32                           m_WordBits = 16;
    UINT32                           m_AddrBits = 16;
    UINT8 CONST                     *m_pCode = nullptr;
    UINT64                           m_CodeSize = 0;
    std::map<std::string, Format *>  m_Formats;
    std::map<std::string, UINT32>    m_RegIndex;
};

// Resolve a CPU-model name to the set of features it enables. A null/unknown model
// enables EVERY declared feature (the permissive default); a named model enables exactly
// its feature list.
static std::set<std::string>
ResolveFeatures (Arch *pArch, CHAR8 CONST *pCpu)
{
    std::set<std::string> Out;
    if (pCpu != nullptr) {
        for (Cpu *C : pArch->Cpus) {
            if (C->Name == pCpu) {
                for (std::string CONST &F : C->Features) { Out.insert (F); }
                return Out;
            }
        }
    }
    // No model named, or it was not found: enable everything so all instructions decode.
    for (Feature CONST &F : pArch->Features) { Out.insert (F.Name); }
    return Out;
}

} // anonymous namespace

ICpuArchitecture *
CreateUpclArch (Module *pModule, UINT32 ArchIndex, CHAR8 CONST *pCpu)
{
    if (pModule == nullptr || ArchIndex >= pModule->Archs.size ()) {
        return nullptr;
    }
    Arch *pArch = pModule->Archs[ArchIndex];
    return new UpclArch (pModule, pArch, ResolveFeatures (pArch, pCpu));
}

} // namespace Upcl
} // namespace LibCPU
