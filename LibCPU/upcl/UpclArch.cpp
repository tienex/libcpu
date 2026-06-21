/** @file  A live ICpuArchitecture that interprets a UPCL description. See UpclArch.h. */

#include "UpclArch.h"
#include "RegisterLayout.h"
#include "Decoder.h"
#include "Semantics.h"
#include "LibCPU/PCom.h"
#include "LibCPU/CpuState.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
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
        // Standard UPCL (a register_file with `encode` clauses) drives the main path: a
        // flattened register layout + the generic decoder + the semantics translator. (The
        // formats-based path below is the experimental variant.)
        m_Standard = (m_pArch->RegFile != nullptr);
        if (m_Standard) {
            m_Layout   = BuildRegisterLayout (m_pArch);
            m_pDecoder.reset (new Decoder (m_pArch, &m_Layout));
            UINT32 Pc = m_Layout.PcIndex ();
            if (Pc != ~(UINT32) 0) { m_PcName = m_Layout.Phys[Pc].Name; }
        }
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
        // The disassembly syntax style: the first declared style by default, overridable via
        // the LCX_SYNTAX environment variable (e.g. att / intel).
        if (m_pArch->Disasm != nullptr && !m_pArch->Disasm->Styles.empty ()) {
            m_DisasmStyle = m_pArch->Disasm->Styles[0];
        }
        if (CHAR8 CONST *pEnv = std::getenv ("LCX_SYNTAX")) { m_DisasmStyle = pEnv; }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuArchitecture, ppvObject);
    }

    // The register names in index order -- from the flattened register file (standard path)
    // or the flat register list. Lets the host label registers without re-deriving them.
    void RegisterNames (std::vector<std::string> *pOut) CONST {
        if (m_Standard) {
            for (RegPhys CONST &P : m_Layout.Phys) { pOut->push_back (P.Name); }
        } else {
            for (Reg CONST &R : m_pArch->Registers) { pOut->push_back (R.Name); }
        }
    }

    HRESULT STDMETHODCALLTYPE GetInfo (CPU_ARCH_INFO *pInfo) override {
        pInfo->pName       = m_pArch->Name.c_str ();
        pInfo->pFullName   = m_pArch->FullName.c_str ();
        pInfo->ByteSize    = 8;
        pInfo->WordSize    = (UINT8) m_WordBits;
        pInfo->AddressSize = m_AddrBits;
        pInfo->PsrSize     = (UINT8) m_WordBits;
        pInfo->IsBigEndian = m_pArch->Little ? FALSE : TRUE;
        pInfo->GprCount    = m_Standard ? (UINT32) m_Layout.Phys.size ()
                                         : (UINT32) m_pArch->Registers.size ();
        pInfo->GprBits     = m_WordBits;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetCodeMemory (UINT8 CONST *pBase, UINT64 Size) override {
        m_pCode = pBase;
        m_CodeSize = Size;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TagInstr (CPU_ADDR Pc, UINT32 *pTag, CPU_ADDR *pNewPc, CPU_ADDR *pNextPc) override {
        if (m_Standard) {
            DecodedInsn D;
            if (!m_pDecoder->Decode (m_pCode, m_CodeSize, Pc, &D)) {
                *pTag = TagContinue; *pNewPc = (CPU_ADDR) -1; *pNextPc = Pc + 1;   // unknown: skip a byte
                return S_OK;
            }
            *pNextPc = Pc + D.Length;
            // A jump insn: tag by its declared type (a conditional branch carries a
            // condition). The transfer target is the resolved branch-target operand.
            if (D.pJump != nullptr) {
                UINT64 Target = 0;
                std::string CONST &Ty = D.pJump->JumpType;
                if (Ty == "return") {
                    *pTag = TagTrap; *pNewPc = (CPU_ADDR) -1;       // computed: pops + indirect-branches
                } else if ((Ty == "branch" || Ty == "call") && JumpTarget (D, Pc, &Target)) {
                    *pNewPc = (CPU_ADDR) Target;
                    *pTag = (D.pJump->Condition != nullptr) ? TagConditional
                          : (Ty == "call")                  ? TagCall
                          :                                   TagBranch;
                } else {
                    *pTag = TagContinue; *pNewPc = (CPU_ADDR) -1;   // computed target: a later step
                }
                return S_OK;
            }
            // A pc-write with a constant-foldable target is a branch; an `if (c) pc=...` is a
            // conditional branch (taken target in NewPc, fall-through in NextPc). The edge is
            // wired from the tag (the pc-write itself is not emitted).
            UINT64 Target = 0;
            Expr  *Cond = nullptr;
            switch (StdBranchInfo (D, Pc, &Target, &Cond)) {
            case StdBrUncond: *pTag = TagBranch;      *pNewPc = (CPU_ADDR) Target; break;
            case StdBrCond:   *pTag = TagConditional; *pNewPc = (CPU_ADDR) Target; break;
            default:          *pTag = TagContinue;    *pNewPc = (CPU_ADDR) -1;     break;
            }
            return S_OK;
        }
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
        if (m_Standard) {
            DecodedInsn D;
            if (!m_pDecoder->Decode (m_pCode, m_CodeSize, Pc, &D)) {
                std::snprintf (pLine, MaxLine, "db 0x%02x", m_pCode[Pc]);
                return S_OK;
            }
            bool        HasFmt = (D.pJump != nullptr) ? D.pJump->HasDisasm : D.pInsn->HasDisasm;
            std::string Fmt    = (D.pJump != nullptr) ? D.pJump->Disasm    : D.pInsn->Disasm;
            std::string Name   = (D.pJump != nullptr) ? D.pJump->Name      : D.pInsn->Name;
            DisasmSpec *Decl   = (D.pJump != nullptr) ? D.pJump->DisasmDecl : D.pInsn->DisasmDecl;
            std::string Out;
            if (Decl != nullptr && !HasFmt) {
                // Declarative disassembly rendered through the style features.
                std::snprintf (pLine, MaxLine, "%s", RenderDisasm (Decl, D).c_str ());
                return S_OK;
            }
            if (HasFmt) {
                // Render the format: a %<operand> placeholder becomes the operand's register
                // name (a register operand) or its hex value (an immediate).
                for (size_t I = 0; I < Fmt.size (); ) {
                    if (Fmt[I] == '%' && I + 1 < Fmt.size () && IsWord (Fmt[I + 1])) {
                        size_t J = I + 1;
                        while (J < Fmt.size () && IsWord (Fmt[J])) { J++; }
                        std::string Op = Fmt.substr (I + 1, J - (I + 1));
                        auto It = D.Operands.find (Op);
                        if (It != D.Operands.end ()) {
                            char B[24];
                            if (It->second.Kind == Operand::Reg) {
                                Out += m_Layout.Phys[It->second.RegIndex].Name;
                            } else {
                                std::snprintf (B, sizeof (B), "%llx", (unsigned long long) It->second.ImmValue);
                                Out += B;
                            }
                        }
                        I = J;
                    } else {
                        Out.push_back (Fmt[I++]);
                    }
                }
            } else {
                // No format given: the mnemonic followed by the decoded operands.
                Out = Name;
                for (auto CONST &Kv : D.Operands) {
                    Operand CONST &Op = Kv.second;
                    char B[32];
                    if (Op.Kind == Operand::Reg) {
                        std::snprintf (B, sizeof (B), " %s", m_Layout.Phys[Op.RegIndex].Name.c_str ());
                    } else {
                        std::snprintf (B, sizeof (B), " 0x%llx", (unsigned long long) Op.ImmValue);
                    }
                    Out += B;
                }
            }
            std::snprintf (pLine, MaxLine, "%s", Out.c_str ());
            return S_OK;
        }
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
        if (m_Standard) {
            DecodedInsn D;
            if (!m_pDecoder->Decode (m_pCode, m_CodeSize, Pc, &D)) { return S_OK; }
            Translator Tr (m_Layout, m_pArch, pE, m_WordBits);
            for (auto CONST &Kv : D.Operands) { Operand Op = Kv.second; Tr.BindOperand (Kv.first, Op); }
            // The program counter reads as the NEXT instruction's address (so a call pushes
            // the right return address); pc-writes are branch edges, handled below. The PC's
            // offset register (ip, from the seg:off composition) reads the same, so a near
            // call that pushes `ip` pushes the return offset.
            Operand PcOp; PcOp.Kind = Operand::Imm;
            PcOp.Bits = m_AddrBits; PcOp.ImmValue = (UINT64) (Pc + D.Length);
            if (!m_PcName.empty ()) { Tr.BindOperand (m_PcName, PcOp); }
            auto OffIt = m_Layout.PcFields.find ("off");
            if (OffIt != m_Layout.PcFields.end ()) { Tr.BindOperand (OffIt->second, PcOp); }

            std::vector<Stmt *> Body;
            if (D.pJump != nullptr) {
                if (D.pJump->JumpType == "return") {
                    // Computed return: translate the action in indirect-PC mode -- the pop
                    // runs (stack adjust included), the pc/pc.off write is captured, and the
                    // IndirectBranch is emitted last (resuming at the popped address).
                    Tr.SetIndirectPc (true);
                    for (Stmt *S : D.pJump->Pre) { Tr.EmitOne (S); }
                    Tr.Emit (D.pJump->Action);
                    ICpuValue *Target = Tr.IndirectTarget ();
                    ICpuSmcEmitter *pFlow = nullptr;
                    if (Target != nullptr
                        && SUCCEEDED (pE->QueryInterface (IID_ICpuSmcEmitter, (VOID **) &pFlow)) && pFlow != nullptr) {
                        pFlow->IndirectBranch (Target);
                        pFlow->Release ();
                    }
                    return S_OK;
                }
                // A branch/call: pre-actions run; the transfer (a static pc-write or the
                // transfer macro) is the edge (wired from the tag), so it is omitted.
                std::string TgtOp = JumpTargetOperand (D.pJump);
                for (Stmt *S : D.pJump->Pre) { Body.push_back (S); }
                for (Stmt *S : D.pJump->Action) {
                    if (!IsStdPcWrite (S) && !IsJumpMacroStmt (S, TgtOp)) { Body.push_back (S); }
                }
            } else {
                // The branch statement (an unconditional pc-write or `if (c) pc=...`) is the
                // edge, wired from the tag -- emit the rest of the body straight-line.
                for (Stmt *S : D.pInsn->Semantics) {
                    if (!IsStdPcWrite (S) && !IsCondBranchStmt (S)) { Body.push_back (S); }
                }
            }
            Tr.Emit (Body);
            return S_OK;
        }
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

    HRESULT STDMETHODCALLTYPE TranslateCond (CPU_ADDR Pc, ICpuEmitter *pE, ICpuValue **ppCond) override {
        *ppCond = nullptr;
        if (m_Standard) {
            DecodedInsn D;
            if (!m_pDecoder->Decode (m_pCode, m_CodeSize, Pc, &D)) { return E_NOTIMPL; }
            Expr *Cond = nullptr;
            if (D.pJump != nullptr) {
                Cond = D.pJump->Condition;          // a conditional jump (jcc)
            } else {
                UINT64 Target = 0;
                if (StdBranchInfo (D, Pc, &Target, &Cond) != StdBrCond) { Cond = nullptr; }
            }
            if (Cond == nullptr) { return E_NOTIMPL; }
            Translator Tr (m_Layout, m_pArch, pE, m_WordBits);
            for (auto CONST &Kv : D.Operands) { Operand Op = Kv.second; Tr.BindOperand (Kv.first, Op); }
            return Tr.EmitCondition (Cond, ppCond);
        }
        return E_NOTIMPL;                            // experimental path: conditional flow later
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

    // ---- standard-path branch detection -----------------------------------
    //
    // A statement that assigns the program counter (by its register name or the %PC meta).
    bool IsStdPcWrite (Stmt *S) CONST {
        if (S->Kind != StmtAssign || S->Lhs == nullptr) { return false; }
        Expr *L = S->Lhs;
        if (L->Kind == ExprName && !m_PcName.empty () && L->Name == m_PcName) { return true; }
        if (L->Kind == ExprMeta && L->Name == "PC") { return true; }
        return false;
    }

    // Constant-fold a branch target from a decoded instruction: the program counter reads
    // as the NEXT instruction's address (the usual relative-branch convention), immediate
    // operands as their values. Returns false if the target is not a compile-time constant
    // (a computed/register branch -- handled at run time, a later increment).
    bool FoldStd (Expr *E, DecodedInsn CONST &D, INT64 NextPc, INT64 *pOut) CONST {
        switch (E->Kind) {
        case ExprInt:
            *pOut = (INT64) E->Int;
            return true;
        case ExprName: {
            if (!m_PcName.empty () && E->Name == m_PcName) { *pOut = NextPc; return true; }
            auto It = D.Operands.find (E->Name);
            if (It != D.Operands.end () && It->second.Kind == Operand::Imm) {
                *pOut = (INT64) It->second.ImmValue;
                return true;
            }
            return false;
        }
        case ExprBinary: {
            INT64 A = 0, B = 0;
            if (!FoldStd (E->Args[0], D, NextPc, &A) || !FoldStd (E->Args[1], D, NextPc, &B)) { return false; }
            switch (E->Op) {
            case TokPlus:  *pOut = A + B; return true;
            case TokMinus: *pOut = A - B; return true;
            default:       return false;
            }
        }
        case ExprCast:
            return FoldStd (E->Args[0], D, NextPc, pOut);          // width is irrelevant to folding
        case ExprAugment: {
            // %S ( [ #iN x ] ) sign-extends the folded value from N bits (a backward branch's
            // negative displacement); %U and other augments pass the value through.
            if (E->Args.empty () || !FoldStd (E->Args[0], D, NextPc, pOut)) { return false; }
            Expr *Inner = E->Args[0];
            if (E->Name == "S" && Inner->Kind == ExprCast && Inner->VType != nullptr) {
                UINT32 W = Inner->VType->Width;
                if (W > 0 && W < 64) {
                    INT64 Sign = (INT64) 1 << (W - 1);
                    *pOut = (*pOut ^ Sign) - Sign;
                }
            }
            return true;
        }
        default:
            return false;
        }
    }

    // ---- jump-insn support ------------------------------------------------
    //
    // Is Name a declared decoder operand?
    bool IsOperandName (std::string CONST &Name) CONST {
        for (DecoderOperand *D : m_pArch->DecoderOps) { if (D->Name == Name) { return true; } }
        return false;
    }

    // The branch-target operand of a jump: the decoder-operand argument to the action's
    // jump macro (the address it transfers to, e.g. `src` in `@i8086_jump(src)`).
    std::string JumpTargetOperand (JumpInsn *J) CONST {
        for (Stmt *S : J->Action) {
            Expr *E = (S->Kind == StmtExpr) ? S->Rhs : nullptr;
            if (E != nullptr && E->Kind == ExprCall) {
                for (Expr *A : E->Args) {
                    if (A->Kind == ExprName && IsOperandName (A->Name)) { return A->Name; }
                }
            }
        }
        return std::string ();
    }

    // The action statement that performs the transfer (the macro called with the target
    // operand) -- the branch edge, omitted from translation.
    bool IsJumpMacroStmt (Stmt *S, std::string CONST &TgtOp) CONST {
        if (S->Kind != StmtExpr || S->Rhs == nullptr || S->Rhs->Kind != ExprCall) { return false; }
        for (Expr *A : S->Rhs->Args) {
            if (A->Kind == ExprName && A->Name == TgtOp) { return true; }
        }
        return false;
    }

    // A decoded jump's static target: either a direct pc-write in the action (pc = dst) or
    // the branch-target operand passed to the action's transfer macro (@i8086_jump(src)).
    bool JumpTarget (DecodedInsn CONST &D, CPU_ADDR Pc, UINT64 *pTarget) CONST {
        INT64 NextPc = (INT64) (Pc + D.Length);
        for (Stmt *S : D.pJump->Action) {
            if (IsStdPcWrite (S) && S->Rhs != nullptr) {
                INT64 T = 0;
                if (FoldStd (S->Rhs, D, NextPc, &T)) { *pTarget = (UINT64) T; return true; }
            }
        }
        std::string Op = JumpTargetOperand (D.pJump);
        if (Op.empty ()) { return false; }
        auto It = D.Operands.find (Op);
        if (It == D.Operands.end () || It->second.Kind != Operand::Imm) { return false; }
        *pTarget = It->second.ImmValue;
        return true;
    }

    // A conditional-branch statement: `if (cond) <pc-write>` with a single then-statement
    // and no else -- the shape a jcc/jnz instruction takes.
    bool IsCondBranchStmt (Stmt *S) CONST {
        return S->Kind == StmtIf && S->Else.empty () && S->Then.size () == 1
            && IsStdPcWrite (S->Then[0]);
    }

    // Classify a decoded instruction's control flow and, for a branch, fold its target (and
    // surface the condition expression for a conditional branch).
    enum STD_BR { StdBrNone, StdBrUncond, StdBrCond };
    STD_BR StdBranchInfo (DecodedInsn CONST &D, CPU_ADDR Pc, UINT64 *pTarget, Expr **ppCond) CONST {
        INT64 NextPc = (INT64) (Pc + D.Length);
        *ppCond = nullptr;
        for (Stmt *S : D.pInsn->Semantics) {
            if (IsStdPcWrite (S) && S->Rhs != nullptr) {
                INT64 T = 0;
                if (FoldStd (S->Rhs, D, NextPc, &T)) { *pTarget = (UINT64) T; return StdBrUncond; }
            }
            if (IsCondBranchStmt (S)) {
                Stmt *W = S->Then[0];
                INT64 T = 0;
                if (W->Rhs != nullptr && FoldStd (W->Rhs, D, NextPc, &T)) {
                    *pTarget = (UINT64) T; *ppCond = S->Cond; return StdBrCond;
                }
            }
        }
        return StdBrNone;
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
    bool                             m_Standard = false; // register_file + encode (standard .upcl)
    RegisterLayout                   m_Layout;            // standard-path register layout
    std::unique_ptr<Decoder>         m_pDecoder;          // standard-path generic decoder
    std::string                      m_PcName;            // the program-counter register's name
    std::string                      m_DisasmStyle;       // active disassembly syntax style

    // Render a declarative disassembly (mnemonic + operands) for the active style, applying
    // its mnemonic casing/size-suffix, operand ordering, and register/immediate formatting.
    std::string RenderDisasm (DisasmSpec *pDecl, DecodedInsn CONST &D) CONST {
        DisasmFeatures *pF = m_pArch->Disasm;
        DisasmStyle    *pS = (pF != nullptr) ? pF->Find (m_DisasmStyle) : nullptr;
        if (pS == nullptr) {                            // no features/style: bare mnemonic + operands
            std::string Out = pDecl->Mnemonic;
            for (std::string CONST &Op : pDecl->Operands) { Out += " " + Op; }
            return Out;
        }
        std::string Mnem = pDecl->Mnemonic;
        if (pS->MnemSizeSuffix && !pDecl->Size.empty ()) {
            auto It = pF->Sizes.find (pDecl->Size);
            if (It != pF->Sizes.end ()) { Mnem += It->second; }
        }
        Mnem = Cased (Mnem, pS->MnemCasing);

        std::vector<std::string> Ops = pDecl->Operands;
        if (pS->ReverseOperands) { std::reverse (Ops.begin (), Ops.end ()); }

        std::string Out = Mnem;
        for (size_t I = 0; I < Ops.size (); I++) {
            Out += (I == 0) ? " " : ", ";
            auto It = D.Operands.find (Ops[I]);
            if (It == D.Operands.end ()) { continue; }
            Operand CONST &Op = It->second;
            if (Op.Kind == Operand::Reg) {
                Out += pS->RegPrefix + Cased (m_Layout.Phys[Op.RegIndex].Name, pS->RegCasing);
            } else {
                char B[24];
                std::snprintf (B, sizeof (B), "%llx", (unsigned long long) Op.ImmValue);
                Out += pS->IntPrefix + std::string (B) + pS->IntSuffix;
            }
        }
        return Out;
    }

    static std::string Cased (std::string CONST &S, std::string CONST &Casing) {
        std::string Out = S;
        if (Casing == "upper") { for (char &C : Out) { C = (char) std::toupper ((unsigned char) C); } }
        else if (Casing == "lower") { for (char &C : Out) { C = (char) std::tolower ((unsigned char) C); } }
        return Out;
    }
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
CreateUpclArch (Module *pModule, UINT32 ArchIndex, CHAR8 CONST *pCpu,
                std::vector<std::string> *pRegNamesOut)
{
    if (pModule == nullptr || ArchIndex >= pModule->Archs.size ()) {
        return nullptr;
    }
    Arch *pArch = pModule->Archs[ArchIndex];
    UpclArch *pUpcl = new UpclArch (pModule, pArch, ResolveFeatures (pArch, pCpu));
    if (pRegNamesOut != nullptr) { pUpcl->RegisterNames (pRegNamesOut); }
    return pUpcl;
}

} // namespace Upcl
} // namespace LibCPU
