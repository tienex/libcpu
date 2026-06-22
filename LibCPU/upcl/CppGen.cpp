/** @file  UPCL -> C++ frontend generator. See CppGen.h. */

#include "CppGen.h"
#include "RegisterLayout.h"
#include "Semantics.h"
#include "LibCPU/ICpu.h"
#include "LibCPU/PCom.h"
#include "LibCPU/CpuState.h"
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace LibCPU {
namespace Upcl {
namespace {

// ---- emitter-enum spellings ----------------------------------------------
//
// The generated C++ names the same unscoped enumerators the interpreter uses, so the
// printed emitter calls compile against ICpu.h unchanged.

static CHAR8 CONST *
BinopName (CPU_BINOP Op)
{
    switch (Op) {
    case BinAdd: return "BinAdd"; case BinSub: return "BinSub"; case BinMul: return "BinMul";
    case BinUDiv: return "BinUDiv"; case BinSDiv: return "BinSDiv";
    case BinURem: return "BinURem"; case BinSRem: return "BinSRem";
    case BinAnd: return "BinAnd"; case BinOr: return "BinOr"; case BinXor: return "BinXor";
    case BinShl: return "BinShl"; case BinLShr: return "BinLShr"; case BinAShr: return "BinAShr";
    case BinRol: return "BinRol"; case BinRor: return "BinRor";
    case BinFAdd: return "BinFAdd"; case BinFSub: return "BinFSub";
    case BinFMul: return "BinFMul"; case BinFDiv: return "BinFDiv"; case BinFAtan2: return "BinFAtan2";
    }
    return "BinAdd";
}

static CHAR8 CONST *
UnopName (CPU_UNOP Op)
{
    switch (Op) {
    case UnNeg: return "UnNeg"; case UnCom: return "UnCom"; case UnNot: return "UnNot";
    case UnFNeg: return "UnFNeg"; case UnFAbs: return "UnFAbs"; case UnFSqrt: return "UnFSqrt";
    case UnF2xm1: return "UnF2xm1"; case UnFLog2: return "UnFLog2"; case UnFTan: return "UnFTan";
    }
    return "UnNeg";
}

static CHAR8 CONST *
CmpName (CPU_CMP Pred)
{
    switch (Pred) {
    case CmpEq: return "CmpEq"; case CmpNe: return "CmpNe";
    case CmpULt: return "CmpULt"; case CmpULe: return "CmpULe"; case CmpUGt: return "CmpUGt"; case CmpUGe: return "CmpUGe";
    case CmpSLt: return "CmpSLt"; case CmpSLe: return "CmpSLe"; case CmpSGt: return "CmpSGt"; case CmpSGe: return "CmpSGe";
    case CmpFOEq: return "CmpFOEq"; case CmpFOLt: return "CmpFOLt"; case CmpFOGt: return "CmpFOGt"; case CmpFUno: return "CmpFUno";
    }
    return "CmpEq";
}

static CHAR8 CONST *
CastName (CPU_CAST Op)
{
    switch (Op) {
    case CastTrunc: return "CastTrunc"; case CastZExt: return "CastZExt"; case CastSExt: return "CastSExt";
    case CastSIToF: return "CastSIToF"; case CastFToSI: return "CastFToSI";
    case CastFExt: return "CastFExt"; case CastFTrunc: return "CastFTrunc";
    case CastIToFBits: return "CastIToFBits"; case CastFToIBits: return "CastFToIBits";
    }
    return "CastTrunc";
}

static CHAR8 CONST *
FlagName (CPU_FLAG Flag)
{
    switch (Flag) {
    case FlagNegative: return "FlagNegative"; case FlagOverflow: return "FlagOverflow";
    case FlagZero: return "FlagZero"; case FlagCarry: return "FlagCarry";
    case FlagParity: return "FlagParity"; case FlagDirection: return "FlagDirection";
    case FlagAux: return "FlagAux";
    }
    return "FlagZero";
}

// The C++ spelling of a binary operator -- for rendering a generated loop's control
// expressions (init / condition / step) as plain C++ integer arithmetic.
static CHAR8 CONST *
CppBinOp (TOKEN_KIND Op)
{
    switch (Op) {
    case TokPlus: return "+"; case TokMinus: return "-"; case TokStar: return "*";
    case TokSlash: return "/"; case TokPercent: return "%";
    case TokAmp: return "&"; case TokPipe: return "|"; case TokCaret: return "^";
    case TokShl: return "<<"; case TokShr: return ">>";
    case TokLt: return "<"; case TokLtEq: return "<="; case TokGt: return ">"; case TokGtEq: return ">=";
    case TokEqEq: return "=="; case TokNotEq: return "!=";
    case TokAndAnd: return "&&"; case TokOrOr: return "||";
    default: return "+";
    }
}

// ---- printing emitter -----------------------------------------------------
//
// A value handle carries the name of the C++ ComPtr the generated code declared for it;
// a block handle the same for a basic block. Every ICpuValue * the printing emitter ever
// hands out is a SourceValue, so a received operand's variable name is just a downcast.

class SourceValue final : public ComObject<ICpuValue> {
public:
    explicit SourceValue (std::string Name) : m_Name (std::move (Name)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    std::string CONST &Name () CONST { return m_Name; }

    // If non-empty, this value is a compile-time-known C++ integer with this expression (a
    // ConstInt, or arithmetic over such). It lets a register-array index that is known at
    // translate time (an operand or a loop counter) become a direct register access.
    std::string IntExpr;
    bool        IsBankFlag = false;   // a ConstInt equal to CPU_REGBANK_FLAG
    std::string BankSlot;             // a register-bank address whose slot index is this C++ int
private:
    std::string m_Name;
};

class SourceBlock final : public ComObject<ICpuBlock> {
public:
    explicit SourceBlock (std::string Name) : m_Name (std::move (Name)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
    std::string CONST &Name () CONST { return m_Name; }
private:
    std::string m_Name;
};

static std::string
ValName (ICpuValue *p)
{
    return (p != nullptr) ? static_cast<SourceValue *> (p)->Name () : std::string ("nullptr");
}

static std::string
BlkName (ICpuBlock *p)
{
    return (p != nullptr) ? static_cast<SourceBlock *> (p)->Name () : std::string ("nullptr");
}

// Drive the proven Semantics::Translator against this, and every emitter call becomes a
// line of generated C++ that re-issues the identical call at frontend run time. An
// immediate operand (or `pc`) is bound to a SENTINEL value; ConstInt maps the sentinel
// back to the runtime extraction expression (m_pCode[Pc + 1], (CPU_ADDR)(Pc + Len), ...).
class SourceEmitter final : public ComObject<ICpuEmitter> {
public:
    explicit SourceEmitter (std::string Indent) : m_Indent (std::move (Indent)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

    std::string CONST &Body () CONST { return m_Body; }

    // Append already-formatted generated text (e.g. an SMC dispatch tail, or a C++ for-loop
    // wrapper the generator builds directly rather than through an emitter call).
    void AppendRaw (std::string CONST &Text) { m_Body += Text; }

    // Indentation control, so emitter calls nested inside a generated C++ loop are indented.
    std::string CONST &IndentStr () CONST { return m_Indent; }
    void PushIndent () { m_Indent += "    "; }
    void PopIndent () { if (m_Indent.size () >= 4) { m_Indent.resize (m_Indent.size () - 4); } }

    // Register an operand/PC sentinel value -> the C++ expression that recovers it at run time.
    void MapSentinel (UINT64 Value, std::string CONST &Expr) { m_Sentinels[Value] = Expr; }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        std::string Expr = ConstExpr (Value);
        SourceValue *pV = Produce ("ConstInt", std::to_string (Bits) + ", " + Expr, ppValue);
        pV->IntExpr = Expr;                                  // a constant is a C++ integer expression
        if (Value == CPU_REGBANK_FLAG) { pV->IsBankFlag = true; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        Produce ("GetRegister", std::to_string (Index) + ", " + std::to_string (Bits), ppValue);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN Sext) override {
        Put ("pE->PutRegister (" + std::to_string (Index) + ", " + ValName (pValue) + ", "
             + std::to_string (Bits) + ", " + (Sext ? "TRUE" : "FALSE") + ");");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        SourceValue *pA = static_cast<SourceValue *> (pAddr);
        if (!pA->BankSlot.empty ()) {                        // a translate-time slot -> a direct read
            Produce ("GetRegister", pA->BankSlot + ", " + std::to_string (Bits), ppValue);
            return S_OK;
        }
        Produce ("Load", ValName (pAddr) + ", " + std::to_string (Bits), ppValue);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        SourceValue *pA = static_cast<SourceValue *> (pAddr);
        if (!pA->BankSlot.empty ()) {                        // a translate-time slot -> a direct write
            Put ("pE->PutRegister (" + pA->BankSlot + ", " + ValName (pValue) + ", " + std::to_string (Bits) + ", FALSE);");
            return S_OK;
        }
        Put ("pE->Store (" + ValName (pValue) + ", " + ValName (pAddr) + ", " + std::to_string (Bits) + ");");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        SourceValue *pa = static_cast<SourceValue *> (pA), *pb = static_cast<SourceValue *> (pB);
        SourceValue *pV = Produce ("BinaryOp", std::string (BinopName (Op)) + ", " + ValName (pA) + ", " + ValName (pB), ppValue);
        // base + index of a translate-time-known slot stays a C++ integer expression.
        if (Op == BinAdd && !pa->IntExpr.empty () && !pb->IntExpr.empty ()) {
            pV->IntExpr = "(" + pa->IntExpr + " + " + pb->IntExpr + ")";
        }
        // REGBANK_FLAG | slot, with the slot a C++ integer, marks a register-bank address whose
        // physical slot is known at translate time (a direct register access, not a bank load).
        if (Op == BinOr) {
            if (pa->IsBankFlag && !pb->IntExpr.empty ()) { pV->BankSlot = pb->IntExpr; }
            else if (pb->IsBankFlag && !pa->IntExpr.empty ()) { pV->BankSlot = pa->IntExpr; }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        Produce ("UnaryOp", std::string (UnopName (Op)) + ", " + ValName (pA), ppValue);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        Produce ("Compare", std::string (CmpName (Pred)) + ", " + ValName (pA) + ", " + ValName (pB), ppValue);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        SourceValue *pa = static_cast<SourceValue *> (pA);
        SourceValue *pV = Produce ("Cast", std::string (CastName (Op)) + ", " + ValName (pA) + ", " + std::to_string (Bits), ppValue);
        // A width cast of a known C++ integer (the index widened to the bank slot type) keeps it
        // a C++ integer -- register indices are small, so the value is preserved.
        if (!pa->IntExpr.empty ()) { pV->IntExpr = pa->IntExpr; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *pCond, ICpuValue *pTrue, ICpuValue *pFalse, ICpuValue **ppValue) override {
        Produce ("Select", ValName (pCond) + ", " + ValName (pTrue) + ", " + ValName (pFalse), ppValue);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        Produce ("GetFlag", FlagName (Flag), ppValue);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        Put ("pE->SetFlag (" + std::string (FlagName (Flag)) + ", " + ValName (pValue) + ");");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *pName, ICpuBlock **ppBlock) override {
        std::string b = "b" + std::to_string (m_BlockId++);
        Put ("ComPtr<ICpuBlock> " + b + "; pE->CreateBlock (\"" + (pName ? pName : "") + "\", &" + b + ");");
        *ppBlock = new SourceBlock (b);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *pBlock) override {
        Put ("pE->SetInsertBlock (" + BlkName (pBlock) + ");");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override {
        std::string b = "b" + std::to_string (m_BlockId++);
        Put ("ComPtr<ICpuBlock> " + b + "; pE->GetInsertBlock (&" + b + ");");
        *ppBlock = new SourceBlock (b);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *pTarget) override {
        Put ("pE->Branch (" + BlkName (pTarget) + ");");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *pCond, ICpuBlock *pTrue, ICpuBlock *pFalse) override {
        Put ("pE->CondBranch (" + ValName (pCond) + ", " + BlkName (pTrue) + ", " + BlkName (pFalse) + ");");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        Put ("pE->SetPC ((CPU_ADDR) " + std::to_string ((unsigned long long) Pc) + ");");
        return S_OK;
    }

private:
    std::string NewVar () { return "t" + std::to_string (m_VarId++); }
    void Put (std::string CONST &Line) { m_Body += m_Indent + Line + "\n"; }

    // A value-producing call: declare a fresh ComPtr and pass it as the trailing out-param.
    // Returns the new handle so the caller can attach the value's C++-int metadata.
    SourceValue *Produce (std::string CONST &Method, std::string CONST &Args, ICpuValue **ppValue) {
        std::string v = NewVar ();
        Put ("ComPtr<ICpuValue> " + v + "; pE->" + Method + " (" + Args + ", &" + v + ");");
        SourceValue *pV = new SourceValue (v);
        *ppValue = pV;
        return pV;
    }

    // The C++ expression for a ConstInt value: a sentinel maps to its runtime recovery
    // expression; a genuine literal prints as itself (UINT64_C for the wide ones).
    std::string ConstExpr (UINT64 Value) CONST {
        auto It = m_Sentinels.find (Value);
        if (It != m_Sentinels.end ()) { return It->second; }
        char B[40];
        if (Value <= 0xFFFFFFFFu) { std::snprintf (B, sizeof (B), "0x%llx", (unsigned long long) Value); }
        else                      { std::snprintf (B, sizeof (B), "UINT64_C (0x%llx)", (unsigned long long) Value); }
        return B;
    }

    std::string                   m_Indent;
    std::string                   m_Body;
    std::map<UINT64, std::string> m_Sentinels;
    UINT32                        m_VarId = 0;
    UINT32                        m_BlockId = 0;
};

// ---- encoding-field layout ------------------------------------------------
//
// Fields are laid MSB-first (the same order ExtractFields uses): a field's bit position
// is the sum of the widths before it. A C++ expression recovers the field from the bytes
// at run time -- a byte-swap for a byte-aligned multi-byte field on a little-endian arch,
// a shift+mask for a sub-byte field.

static UINT32
FieldBitPos (EncAlt *pAlt, size_t Index)
{
    UINT32 Pos = 0;
    for (size_t I = 0; I < Index; I++) { Pos += pAlt->Fields[I].Width; }
    return Pos;
}

static std::string
ExtractExpr (UINT32 BitPos, UINT32 Width, bool Little)
{
    UINT32 ByteOff = BitPos / 8;
    char   B[64];
    if ((BitPos % 8) == 0 && (Width % 8) == 0) {
        UINT32 Bytes = Width / 8;
        if (Bytes == 1) {
            std::snprintf (B, sizeof (B), "m_pCode[Pc + %u]", ByteOff);
            return B;
        }
        std::string Out = "(";
        for (UINT32 K = 0; K < Bytes; K++) {
            UINT32 Shift = Little ? (8 * K) : (8 * (Bytes - 1 - K));
            std::snprintf (B, sizeof (B), "%s(m_pCode[Pc + %u]%s%s)",
                           K ? " | " : "", ByteOff + K,
                           Shift ? " << " : "", Shift ? std::to_string (Shift).c_str () : "");
            Out += B;
        }
        return Out + ")";
    }
    // A sub-byte field wholly within one byte: shift it down and mask.
    if ((BitPos % 8) + Width <= 8) {
        UINT32 Shift = 8 - (BitPos % 8) - Width;
        std::snprintf (B, sizeof (B), "((m_pCode[Pc + %u] >> %u) & 0x%x)", ByteOff, Shift, (1u << Width) - 1u);
        return B;
    }
    // A field that crosses byte boundaries but is not byte-aligned (e.g. CHIP-8's 12-bit nnn at
    // bit offset 4): assemble the spanned bytes MSB-first, then shift down and mask -- the same
    // big-endian bit walk the standard Decoder uses.
    UINT32 FirstByte = BitPos / 8;
    UINT32 LastByte  = (BitPos + Width - 1) / 8;
    UINT32 NumBytes  = LastByte - FirstByte + 1;
    std::string Acc = "(";
    for (UINT32 K = 0; K < NumBytes; K++) {
        UINT32 Sh = 8 * (NumBytes - 1 - K);
        std::snprintf (B, sizeof (B), "%s(m_pCode[Pc + %u]%s%s)",
                       K ? " | " : "", FirstByte + K,
                       Sh ? " << " : "", Sh ? std::to_string (Sh).c_str () : "");
        Acc += B;
    }
    Acc += ")";
    UINT32 RightShift = NumBytes * 8 - (BitPos - FirstByte * 8) - Width;
    UINT32 Mask = (Width >= 32) ? 0xFFFFFFFFu : ((1u << Width) - 1u);
    std::string Out = Acc;
    if (RightShift != 0) { Out = "(" + Acc + " >> " + std::to_string (RightShift) + ")"; }
    std::snprintf (B, sizeof (B), " & 0x%x)", Mask);
    return "(" + Out + B;
}

// ---- feature resolution ---------------------------------------------------

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
    for (Feature CONST &F : pArch->Features) { Out.insert (F.Name); }
    return Out;
}

// ---- one decodable entry --------------------------------------------------

class Entry {
public:
    std::string Name;
    bool        IsJump = false;
    std::string JumpType;
    UINT32      Len = 1;
    std::string Match;                 // the decode condition (const-field compares)
    std::string TranslateBody;         // generated TranslateInstr lines
    std::string CondBody;              // generated TranslateCond lines (a conditional branch)
    std::string CondVar;               // the C++ var holding the condition value
    bool        HasTarget = false;     // a jump with a foldable target
    bool        TargetRel = false;
    UINT32      TargetWidth = 0;
    std::string TargetExpr;            // the target operand's extracted value
    std::string DisasmFmt;             // a printf format for Disassemble
    std::vector<std::string> DisasmArgs;
};

// The program-counter write the standard branch detector recognises (pc / %PC).
static bool
IsPcWrite (Stmt *pStmt, std::string CONST &PcName)
{
    if (pStmt->Kind != StmtAssign || pStmt->Lhs == nullptr) { return false; }
    Expr *L = pStmt->Lhs;
    if (L->Kind == ExprName && !PcName.empty () && L->Name == PcName) { return true; }
    if (L->Kind == ExprMeta && L->Name == "PC") { return true; }
    return false;
}

} // anonymous namespace

bool
GenerateCpp (Module *pModule, UINT32 ArchIndex, CHAR8 CONST *pCpu,
             std::string CONST &CreateName, std::string *pOut)
{
    if (pModule == nullptr || ArchIndex >= pModule->Archs.size ()) {
        if (pOut != nullptr) { *pOut = "GenerateCpp: bad module/arch index"; }
        return false;
    }
    Arch *pArch = pModule->Archs[ArchIndex];
    if (pArch->RegFile == nullptr) {
        if (pOut != nullptr) { *pOut = "GenerateCpp: only the standard (register_file) path is supported"; }
        return false;
    }

    std::set<std::string> Enabled = ResolveFeatures (pArch, pCpu);
    RegisterLayout        Layout  = BuildRegisterLayout (pArch);
    UINT32 WordBits = pArch->WordSize ? pArch->WordSize : 16;
    UINT32 AddrBits = pArch->AddressSize ? pArch->AddressSize : 16;
    UINT32 PcIndex  = Layout.PcIndex ();
    std::string PcName = (PcIndex != ~(UINT32) 0) ? Layout.Phys[PcIndex].Name : std::string ();

    // The PC sentinel: `pc` reads as the NEXT instruction's address (so a call pushes the
    // right return). Per entry the Len differs, so the expression is re-bound each time.
    UINT64 CONST SentBase = UINT64_C (0xF1F2F30000000000);
    UINT64 CONST SentPc   = SentBase;            // reserved for `pc`

    std::vector<Entry> Entries;

    // Build one entry per (instruction/jump, encoding alternative), driving the real
    // Translator with a printing emitter to capture the per-instruction emitter calls.
    auto Build = [&] (std::vector<Stmt *> CONST &Semantics, std::vector<EncAlt *> CONST &Encs,
                      std::string CONST &Feature, bool IsJump, JumpInsn *pJump, DisasmSpec *pDisasm,
                      std::string CONST &Name) {
        if (!Feature.empty () && Enabled.count (Feature) == 0) { return; }
        for (EncAlt *pAlt : Encs) {
            Entry E;
            E.Name = Name;
            E.IsJump = IsJump;
            E.JumpType = (pJump != nullptr) ? pJump->JumpType : std::string ();
            E.Len = (pAlt->TotalBits () + 7) / 8;

            // The decode condition: every constant field equals the bits at its position.
            std::string Match;
            std::map<std::string, std::string> OperandExpr;   // operand name -> extraction
            std::map<std::string, UINT32>       OperandWidth;
            std::map<std::string, bool>         OperandRel;
            for (size_t I = 0; I < pAlt->Fields.size (); I++) {
                EncField CONST &F = pAlt->Fields[I];
                UINT32 BitPos = FieldBitPos (pAlt, I);
                std::string Expr = ExtractExpr (BitPos, F.Width, pArch->Little);
                if (F.HasConst) {
                    char B[40];
                    std::snprintf (B, sizeof (B), "0x%llx", (unsigned long long) F.Const);
                    if (!Match.empty ()) { Match += " && "; }
                    Match += "(" + Expr + " == " + B + ")";
                }
                if (!F.Operand.empty ()) {
                    OperandExpr[F.Operand]  = Expr;
                    OperandWidth[F.Operand] = F.Width;
                    OperandRel[F.Operand]   = F.Relative;
                }
            }
            E.Match = Match.empty () ? std::string ("true") : Match;

            // Drive the Translator with the printing emitter to capture TranslateInstr.
            SourceEmitter Em ("            ");
            Translator Tr (Layout, pArch, &Em, WordBits);

            // Bind each operand to a sentinel; ConstInt recovers the runtime expression. A
            // relative field resolves to its absolute target (NextPc + sign-extended disp).
            UINT64 Sent = SentBase + 1;
            for (auto CONST &Kv : OperandExpr) {
                Operand Op;
                Op.Kind = Operand::Imm;
                Op.Bits = OperandWidth[Kv.first] ? OperandWidth[Kv.first] : WordBits;
                Op.ImmValue = Sent;
                std::string Recover;
                if (OperandRel[Kv.first]) {
                    char B[96];
                    std::snprintf (B, sizeof (B), "(CPU_ADDR)((INT64)(Pc + %u) + (INT8)(%s))",
                                   E.Len, Kv.second.c_str ());
                    Recover = B;
                    E.TargetRel = true;
                } else {
                    Recover = Kv.second;
                }
                Em.MapSentinel (Sent, Recover);
                Tr.BindOperand (const_cast<std::string &> (Kv.first), Op);

                // A jump's (single) operand field is its branch target.
                if (IsJump) {
                    E.HasTarget = true;
                    E.TargetWidth = Op.Bits;
                    E.TargetExpr = Recover;
                }
                Sent++;
            }

            // Bind `pc` to its sentinel: the next instruction's address.
            {
                Operand PcOp;
                PcOp.Kind = Operand::Imm;
                PcOp.Bits = AddrBits;
                PcOp.ImmValue = SentPc;
                char B[48];
                std::snprintf (B, sizeof (B), "(CPU_ADDR)(Pc + %u)", E.Len);
                std::string PcExpr = B;
                Em.MapSentinel (SentPc, PcExpr);
                if (!PcName.empty ()) { Tr.BindOperand (PcName, PcOp); }
            }

            // A loop's control expression rendered as plain C++ integer arithmetic: the counter
            // is a C++ variable, an operand is its runtime extraction, a literal is itself.
            std::set<std::string> LoopVars;
            UINT64 LoopSent = SentBase + UINT64_C (0x10000);
            std::function<std::string (Expr *)> CppIntExpr = [&] (Expr *Ex) -> std::string {
                if (Ex == nullptr) { return "0"; }
                switch (Ex->Kind) {
                case ExprInt:  return std::to_string ((unsigned long long) Ex->Int);
                case ExprName: {
                    if (LoopVars.count (Ex->Name)) { return Ex->Name; }     // a C++ loop counter
                    auto It = OperandExpr.find (Ex->Name);
                    if (It != OperandExpr.end ()) { return "(" + It->second + ")"; }
                    return "0";
                }
                case ExprCast: return CppIntExpr (Ex->Args[0]);             // width is irrelevant here
                case ExprUnary: return std::string (Ex->Op == TokMinus ? "-" : Ex->Op == TokTilde ? "~"
                                                  : Ex->Op == TokNot ? "!" : "") + CppIntExpr (Ex->Args[0]);
                case ExprBinary: return "(" + CppIntExpr (Ex->Args[0]) + " " + CppBinOp (Ex->Op) + " "
                                            + CppIntExpr (Ex->Args[1]) + ")";
                default: return "0";
                }
            };

            // Emit a statement list, turning a `for` whose bound is a runtime operand into a C++
            // loop (a translate-time unroll, as the hand-written frontends do): the counter is a
            // C++ variable bound to a sentinel, and the body's emitter calls run per iteration.
            std::function<void (std::vector<Stmt *> CONST &)> GenStmts = [&] (std::vector<Stmt *> CONST &Stmts) {
                for (Stmt *S : Stmts) {
                    if (S->Kind == StmtFor && !S->Init.empty () && S->Init[0]->Lhs != nullptr
                        && S->Init[0]->Lhs->Kind == ExprName) {
                        std::string Lv = S->Init[0]->Lhs->Name;
                        Operand LvOp;
                        LvOp.Kind = Operand::Imm;
                        LvOp.Bits = (S->Init[0]->LhsType != nullptr) ? S->Init[0]->LhsType->Width : AddrBits;
                        LvOp.ImmValue = LoopSent;
                        Em.MapSentinel (LoopSent, Lv);
                        Tr.BindOperand (Lv, LvOp);
                        LoopVars.insert (Lv);
                        LoopSent++;
                        std::string Init = CppIntExpr (S->Init[0]->Rhs);
                        std::string Cond = (S->Cond != nullptr) ? CppIntExpr (S->Cond) : std::string ("1");
                        std::string Step = (!S->Step.empty () && S->Step[0]->Rhs != nullptr)
                                         ? (Lv + " = " + CppIntExpr (S->Step[0]->Rhs)) : Lv;
                        Em.AppendRaw (Em.IndentStr () + "for (UINT32 " + Lv + " = " + Init + "; " + Cond + "; " + Step + ") {\n");
                        Em.PushIndent ();
                        GenStmts (S->Body);
                        Em.PopIndent ();
                        Em.AppendRaw (Em.IndentStr () + "}\n");
                        LoopVars.erase (Lv);
                    } else {
                        Tr.EmitOne (S);
                    }
                }
            };

            // Select the statements to emit, mirroring UpclArch: a jump's transfer (a pc
            // write) is the block edge -- wired from the tag -- so it is omitted; a computed
            // transfer (a return, or an unfoldable target) is emitted in indirect-PC mode
            // and an IndirectBranch follows.
            std::vector<Stmt *> Body;
            bool Computed = false;
            if (IsJump) {
                Computed = (E.JumpType == "return");
                if (Computed) {
                    Tr.SetIndirectPc (true);
                    for (Stmt *S : pJump->Pre) { Tr.EmitOne (S); }
                    Tr.Emit (pJump->Action);
                    ICpuValue *Target = Tr.IndirectTarget ();
                    if (Target != nullptr) {
                        // Hand the computed return target to the in-artifact dispatcher, which
                        // routes to the matching block with no host round-trip -- the path the
                        // tiering / PGO machinery counts edges and inlines along.
                        std::string Tgt = ValName (Target);
                        std::string Smc;
                        Smc += "            {\n";
                        Smc += "                ICpuSmcEmitter *pFlow = nullptr;\n";
                        Smc += "                if (SUCCEEDED (pE->QueryInterface (IID_ICpuSmcEmitter, (VOID **) &pFlow)) && pFlow != nullptr) {\n";
                        Smc += "                    pFlow->SetDispatchTarget (" + Tgt + ");\n";
                        Smc += "                    pFlow->Release ();\n";
                        Smc += "                }\n";
                        Smc += "            }\n";
                        Em.AppendRaw (Smc);
                    }
                } else {
                    for (Stmt *S : pJump->Pre) { Body.push_back (S); }
                    for (Stmt *S : pJump->Action) {
                        if (!IsPcWrite (S, PcName)) { Body.push_back (S); }
                    }
                    GenStmts (Body);
                }
            } else {
                for (Stmt *S : Semantics) {
                    if (!IsPcWrite (S, PcName)) { Body.push_back (S); }
                }
                GenStmts (Body);
            }
            E.TranslateBody = Em.Body ();

            // A conditional jump: capture its condition through a second emitter.
            if (IsJump && pJump->Condition != nullptr) {
                SourceEmitter CEm ("            ");
                Translator CTr (Layout, pArch, &CEm, WordBits);
                ICpuValue *pCond = nullptr;
                if (SUCCEEDED (CTr.EmitCondition (pJump->Condition, &pCond)) && pCond != nullptr) {
                    E.CondBody = CEm.Body ();
                    E.CondVar  = ValName (pCond);
                    pCond->Release ();
                }
            }

            // A simple disassembly: the mnemonic followed by each declared operand's value.
            std::string Mnem = (pDisasm != nullptr) ? pDisasm->Mnemonic : Name;
            E.DisasmFmt = Mnem;
            if (pDisasm != nullptr) {
                for (std::string CONST &Op : pDisasm->Operands) {
                    auto It = OperandExpr.find (Op);
                    if (It != OperandExpr.end ()) {
                        E.DisasmFmt += " $%x";
                        E.DisasmArgs.push_back ("(unsigned)(" + It->second + ")");
                    }
                }
            }
            Entries.push_back (std::move (E));
        }
    };

    for (Insn *I : pArch->Insns) {
        Build (I->Semantics, I->Encodings, I->Feature, false, nullptr, I->DisasmDecl, I->Name);
    }
    for (JumpInsn *J : pArch->Jumps) {
        std::vector<Stmt *> Empty;
        Build (Empty, J->Encodings, J->Feature, true, J, J->DisasmDecl, J->Name);
    }

    // ---- assemble the source ---------------------------------------------
    std::string &S = *pOut;
    S.clear ();
    S += "/** @file\n";
    S += "  GENERATED from a UPCL description by Upcl::GenerateCpp -- DO NOT EDIT.\n\n";
    S += "  A specialised ICpuArchitecture: the decode is a generated dispatch and each\n";
    S += "  instruction's translation is the exact emitter-call sequence the UPCL semantics\n";
    S += "  interpreter would issue. Compiled into a frontend, it replaces the hand-written one.\n";
    S += "**/\n\n";
    S += "#include \"LibCPU/ICpu.h\"\n";
    S += "#include \"LibCPU/PCom.h\"\n";
    S += "#include \"LibCPU/CpuState.h\"\n";
    S += "#include <cstdio>\n\n";
    S += "namespace LibCPU {\n";
    S += "namespace {\n\n";
    S += "class GenFrontend final : public ComObject<ICpuArchitecture> {\n";
    S += "public:\n";
    S += "    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {\n";
    S += "        return DefaultQuery (riid, IID_ICpuArchitecture, ppvObject);\n";
    S += "    }\n\n";

    // GetInfo
    S += "    HRESULT STDMETHODCALLTYPE GetInfo (CPU_ARCH_INFO *pInfo) override {\n";
    S += "        pInfo->pName       = \"" + pArch->Name + "\";\n";
    S += "        pInfo->pFullName   = \"" + pArch->FullName + "\";\n";
    S += "        pInfo->ByteSize    = 8;\n";
    S += "        pInfo->WordSize    = " + std::to_string (WordBits) + ";\n";
    S += "        pInfo->AddressSize = " + std::to_string (AddrBits) + ";\n";
    S += "        pInfo->PsrSize     = " + std::to_string (WordBits) + ";\n";
    S += std::string ("        pInfo->IsBigEndian = ") + (pArch->Little ? "FALSE" : "TRUE") + ";\n";
    S += "        pInfo->GprCount    = " + std::to_string (Layout.Phys.size ()) + ";\n";
    S += "        pInfo->GprBits     = " + std::to_string (WordBits) + ";\n";
    S += "        pInfo->AddrSegShift = " + std::to_string (pArch->AddrSegShift) + ";\n";
    S += "        pInfo->AddrOffBits  = " + std::to_string (pArch->AddrOffBits) + ";\n";
    S += "        return S_OK;\n";
    S += "    }\n\n";

    S += "    HRESULT STDMETHODCALLTYPE SetCodeMemory (UINT8 CONST *pBase, UINT64 Size) override {\n";
    S += "        m_pCode = pBase; m_CodeSize = Size; return S_OK;\n";
    S += "    }\n\n";

    // Decode: the shared dispatch every entry point routes through.
    S += "    int Decode (CPU_ADDR Pc, UINT32 *pLen) CONST {\n";
    for (size_t I = 0; I < Entries.size (); I++) {
        Entry CONST &E = Entries[I];
        S += "        if (Pc + " + std::to_string (E.Len) + " <= m_CodeSize && (" + E.Match + ")) { *pLen = "
             + std::to_string (E.Len) + "; return " + std::to_string (I) + "; }\n";
    }
    S += "        *pLen = 1; return -1;\n";
    S += "    }\n\n";

    // TagInstr
    S += "    HRESULT STDMETHODCALLTYPE TagInstr (CPU_ADDR Pc, UINT32 *pTag, CPU_ADDR *pNewPc, CPU_ADDR *pNextPc) override {\n";
    S += "        UINT32 Len = 1;\n";
    S += "        int Id = Decode (Pc, &Len);\n";
    S += "        *pNextPc = Pc + Len;\n";
    S += "        *pNewPc  = (CPU_ADDR) -1;\n";
    S += "        *pTag    = TagContinue;\n";
    S += "        switch (Id) {\n";
    for (size_t I = 0; I < Entries.size (); I++) {
        Entry CONST &E = Entries[I];
        if (!E.IsJump) { continue; }
        S += "        case " + std::to_string (I) + ":   // " + E.Name + "\n";
        if (E.JumpType == "return") {
            S += "            *pTag = TagReturn;\n";
        } else if (E.JumpType == "call") {
            S += "            *pTag = TagCall;\n";
            if (E.HasTarget) { S += "            *pNewPc = (CPU_ADDR)(" + E.TargetExpr + ");\n"; }
        } else {   // branch (conditional if it carries a condition)
            S += std::string ("            *pTag = ") + (!E.CondVar.empty () ? "TagConditional | TagBranch" : "TagBranch") + ";\n";
            if (E.HasTarget) { S += "            *pNewPc = (CPU_ADDR)(" + E.TargetExpr + ");\n"; }
        }
        S += "            break;\n";
    }
    S += "        default: break;\n";
    S += "        }\n";
    S += "        return S_OK;\n";
    S += "    }\n\n";

    // Disassemble
    S += "    HRESULT STDMETHODCALLTYPE Disassemble (CPU_ADDR Pc, CHAR8 *pLine, UINT32 MaxLine) override {\n";
    S += "        UINT32 Len = 1;\n";
    S += "        int Id = Decode (Pc, &Len);\n";
    S += "        switch (Id) {\n";
    for (size_t I = 0; I < Entries.size (); I++) {
        Entry CONST &E = Entries[I];
        S += "        case " + std::to_string (I) + ": std::snprintf (pLine, MaxLine, \"" + E.DisasmFmt + "\"";
        for (std::string CONST &A : E.DisasmArgs) { S += ", " + A; }
        S += "); return S_OK;\n";
    }
    S += "        default: std::snprintf (pLine, MaxLine, \"db 0x%02x\", m_pCode[Pc]); return S_OK;\n";
    S += "        }\n";
    S += "    }\n\n";

    // TranslateInstr
    S += "    HRESULT STDMETHODCALLTYPE TranslateInstr (CPU_ADDR Pc, ICpuEmitter *pE) override {\n";
    S += "        UINT32 Len = 1;\n";
    S += "        int Id = Decode (Pc, &Len);\n";
    S += "        (void) Len;\n";
    S += "        switch (Id) {\n";
    for (size_t I = 0; I < Entries.size (); I++) {
        Entry CONST &E = Entries[I];
        S += "        case " + std::to_string (I) + ": {   // " + E.Name + "\n";
        S += E.TranslateBody;
        S += "            break;\n";
        S += "        }\n";
    }
    S += "        default: break;\n";
    S += "        }\n";
    S += "        return S_OK;\n";
    S += "    }\n\n";

    // TranslateCond
    S += "    HRESULT STDMETHODCALLTYPE TranslateCond (CPU_ADDR Pc, ICpuEmitter *pE, ICpuValue **ppCond) override {\n";
    S += "        *ppCond = nullptr;\n";
    S += "        UINT32 Len = 1;\n";
    S += "        int Id = Decode (Pc, &Len);\n";
    S += "        (void) Len;\n";
    S += "        switch (Id) {\n";
    for (size_t I = 0; I < Entries.size (); I++) {
        Entry CONST &E = Entries[I];
        if (E.CondVar.empty ()) { continue; }
        S += "        case " + std::to_string (I) + ": {   // " + E.Name + "\n";
        S += E.CondBody;
        S += "            " + E.CondVar + "->AddRef ();\n";
        S += "            *ppCond = " + E.CondVar + ";\n";
        S += "            return S_OK;\n";
        S += "        }\n";
    }
    S += "        default: break;\n";
    S += "        }\n";
    S += "        return E_NOTIMPL;\n";
    S += "    }\n\n";

    S += "private:\n";
    S += "    UINT8 CONST *m_pCode    = nullptr;\n";
    S += "    UINT64       m_CodeSize = 0;\n";
    S += "};\n\n";
    S += "} // anonymous namespace\n\n";
    S += "ICpuArchitecture *\n" + CreateName + " (VOID)\n{\n    return new GenFrontend ();\n}\n\n";
    S += "} // namespace LibCPU\n";

    return true;
}

} // namespace Upcl
} // namespace LibCPU
