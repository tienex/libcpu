/** @file  The UPCL semantics translator. See Semantics.h. */

#include "Semantics.h"
#include <cstring>

namespace LibCPU {
namespace Upcl {

Translator::Translator (RegisterLayout CONST &Layout, Arch *pArch, ICpuEmitter *pEmitter,
                        UINT32 WordBits)
    : m_Layout (Layout), m_pArch (pArch), m_pE (pEmitter), m_WordBits (WordBits)
{
    for (Macro *M : pArch->Macros) { m_Macros[M->Name].push_back (M); }
}

// Resolve a macro call to its overload with the matching parameter count (i8086_jump has a
// near (address) form and a far (seg, off) form); fall back to the first if none matches.
Macro *
Translator::FindMacro (std::string CONST &Name, size_t ArgCount) CONST
{
    auto It = m_Macros.find (Name);
    if (It == m_Macros.end () || It->second.empty ()) { return nullptr; }
    for (Macro *M : It->second) { if (M->Params.size () == ArgCount) { return M; } }
    return It->second.front ();
}

void
Translator::Bind (std::string CONST &Name, Value CONST &Val)
{
    m_Env[Name] = Val;
}

void
Translator::BindOperand (std::string CONST &Name, Operand CONST &Op)
{
    m_Operands[Name] = Op;
}

// ---- emitter helpers ------------------------------------------------------

Value
Translator::Pool (ComPtr<ICpuValue> V, UINT32 Bits)
{
    Value Out;
    Out.V    = V.Get ();
    Out.Bits = Bits;
    m_Pool.push_back (std::move (V));
    return Out;
}

Value
Translator::Const (UINT32 Bits, UINT64 N)
{
    ComPtr<ICpuValue> V;
    m_pE->ConstInt (Bits ? Bits : m_WordBits, N, &V);
    return Pool (std::move (V), Bits ? Bits : m_WordBits);
}

Value
Translator::Bin (CPU_BINOP Op, Value CONST &A, Value CONST &B)
{
    ComPtr<ICpuValue> V;
    m_pE->BinaryOp (Op, A.V, B.V, &V);
    return Pool (std::move (V), A.Bits);
}

Value
Translator::Un (CPU_UNOP Op, Value CONST &A)
{
    ComPtr<ICpuValue> V;
    m_pE->UnaryOp (Op, A.V, &V);
    return Pool (std::move (V), Op == UnNot ? 1 : A.Bits);
}

Value
Translator::Cmp (CPU_CMP Pred, Value CONST &A, Value CONST &B)
{
    ComPtr<ICpuValue> V;
    m_pE->Compare (Pred, A.V, B.V, &V);
    return Pool (std::move (V), 1);
}

Value
Translator::CastTo (CPU_CAST Op, Value CONST &A, UINT32 Bits)
{
    ComPtr<ICpuValue> V;
    m_pE->Cast (Op, A.V, Bits, &V);
    return Pool (std::move (V), Bits);
}

// Narrow / widen A to exactly Bits, sign- or zero-extending when widening.
Value
Translator::Coerce (Value CONST &A, UINT32 Bits, bool Signed)
{
    if (A.Bits == Bits || Bits == 0) { return A; }
    if (A.Bits > Bits) { return CastTo (CastTrunc, A, Bits); }
    return CastTo (Signed ? CastSExt : CastZExt, A, Bits);
}

// The Width-bit field at bit Lo of Parent: shift it down, then truncate.
Value
Translator::Extract (Value CONST &Parent, UINT32 Lo, UINT32 Width)
{
    Value Shifted = Parent;
    if (Lo != 0) { Shifted = Bin (BinLShr, Parent, Const (Parent.Bits, Lo)); }
    if (Width != Parent.Bits) { return CastTo (CastTrunc, Shifted, Width); }
    return Shifted;
}

// Parent with its Width-bit field at bit Lo replaced by Field: clear the window, OR in
// the (zero-extended, shifted) field.
Value
Translator::Insert (Value CONST &Parent, Value CONST &Field, UINT32 Lo, UINT32 Width)
{
    UINT64 Ones  = (Width >= 64) ? ~UINT64_C (0) : ((UINT64_C (1) << Width) - 1);
    UINT64 Mask  = ~(Ones << Lo);
    Value  Clear = Bin (BinAnd, Parent, Const (Parent.Bits, Mask));
    Value  Wide  = Coerce (Field, Parent.Bits, false);
    if (Lo != 0) { Wide = Bin (BinShl, Wide, Const (Parent.Bits, Lo)); }
    return Bin (BinOr, Clear, Wide);
}

// ---- name / register / flag resolution ------------------------------------

bool
Translator::FindSub (std::string CONST &Name, RegSub CONST **ppSub) CONST
{
    for (RegSub CONST &S : m_Layout.Subs) {
        if (S.Name == Name) { *ppSub = &S; return true; }
    }
    return false;
}

bool
Translator::FindFlag (std::string CONST &Name, RegFlag CONST **ppFlag) CONST
{
    for (RegFlag CONST &F : m_Layout.Flags) {
        if (F.Name == Name) { *ppFlag = &F; return true; }
    }
    return false;
}

// A flag's architectural role: its %meta mapping (V/N/Z/C/P) or, for the unmapped
// direction / aux flags, its name. Flags with neither (interrupt, trap) live only as a
// bit of the PSR word -- the caller falls back to a bit insert/extract.
bool
Translator::MapFlag (RegFlag CONST &Flag, CPU_FLAG *pFlag) CONST
{
    std::string CONST &M = Flag.Meta;
    if (M == "V") { *pFlag = FlagOverflow; return true; }
    if (M == "N") { *pFlag = FlagNegative; return true; }
    if (M == "Z") { *pFlag = FlagZero;     return true; }
    if (M == "C") { *pFlag = FlagCarry;    return true; }
    if (M == "P") { *pFlag = FlagParity;   return true; }
    if (Flag.Name == "D") { *pFlag = FlagDirection; return true; }
    if (Flag.Name == "A") { *pFlag = FlagAux;       return true; }
    return false;
}

Value
Translator::GetFlagBit (RegFlag CONST &Flag)
{
    CPU_FLAG Which;
    if (MapFlag (Flag, &Which)) {
        ComPtr<ICpuValue> V;
        m_pE->GetFlag (Which, &V);
        return Pool (std::move (V), 1);
    }
    // Unmapped: read the bit straight out of the PSR word.
    RegPhys CONST &Psr = m_Layout.Phys[Flag.Parent];
    ComPtr<ICpuValue> Reg;
    m_pE->GetRegister (Psr.Index, Psr.Width, &Reg);
    return Extract (Pool (std::move (Reg), Psr.Width), Flag.Bit, 1);
}

void
Translator::SetFlagBit (RegFlag CONST &Flag, Value CONST &Bit)
{
    Value One = Coerce (Bit, 1, false);
    CPU_FLAG Which;
    if (MapFlag (Flag, &Which)) {
        m_pE->SetFlag (Which, One.V);
        return;
    }
    RegPhys CONST &Psr = m_Layout.Phys[Flag.Parent];
    ComPtr<ICpuValue> Reg;
    m_pE->GetRegister (Psr.Index, Psr.Width, &Reg);
    Value Merged = Insert (Pool (std::move (Reg), Psr.Width), One, Flag.Bit, 1);
    m_pE->PutRegister (Psr.Index, Merged.V, Psr.Width, FALSE);
}

// The static bit width of an expression, without emitting anything -- used to lay out a
// bit-combine assignment target ( (dx : ax) = ... ) where each field's width sets where
// the next field begins.
UINT32
Translator::WidthOf (Expr *pExpr) CONST
{
    switch (pExpr->Kind) {
    case ExprName:
    case ExprMember: {
        std::string CONST &Name = pExpr->Name;
        auto O = m_Operands.find (Name);
        if (O != m_Operands.end ()) {
            Operand CONST &Op = O->second;
            if (Op.Kind == Operand::Reg && Op.SubWidth == 0) { return m_Layout.Phys[Op.RegIndex].Width; }
            return Op.Bits;
        }
        auto E = m_Env.find (Name);
        if (E != m_Env.end ()) { return E->second.Bits; }
        auto P = m_Layout.PhysIndex.find (Name);
        if (P != m_Layout.PhysIndex.end ()) { return m_Layout.Phys[P->second].Width; }
        RegSub CONST *pSub;
        if (FindSub (Name, &pSub)) { return pSub->Width; }
        RegFlag CONST *pFlag;
        if (FindFlag (Name, &pFlag)) { return 1; }
        return m_WordBits;
    }
    case ExprCast:
    case ExprMem:
        return (pExpr->VType != nullptr) ? pExpr->VType->Width : m_WordBits;
    default:
        return m_WordBits;
    }
}

// Read a decoded operand: an immediate is a constant; a register operand loads from its
// physical register (extracting the sub-window for a sub-register operand).
Value
Translator::ReadOperand (Operand CONST &Op)
{
    if (Op.Kind == Operand::Imm) { return Const (Op.Bits ? Op.Bits : m_WordBits, Op.ImmValue); }
    RegPhys CONST &Phys = m_Layout.Phys[Op.RegIndex];
    ComPtr<ICpuValue> V;
    m_pE->GetRegister (Phys.Index, Phys.Width, &V);
    Value Whole = Pool (std::move (V), Phys.Width);
    if (Op.SubWidth != 0 && Op.SubWidth != Phys.Width) { return Extract (Whole, Op.SubLo, Op.SubWidth); }
    return Whole;
}

Value
Translator::EvalName (std::string CONST &Name)
{
    auto O = m_Operands.find (Name);
    if (O != m_Operands.end ()) { return ReadOperand (O->second); }

    auto E = m_Env.find (Name);
    if (E != m_Env.end ()) { return E->second; }

    auto P = m_Layout.PhysIndex.find (Name);
    if (P != m_Layout.PhysIndex.end ()) {
        RegPhys CONST &Phys = m_Layout.Phys[P->second];
        ComPtr<ICpuValue> V;
        m_pE->GetRegister (Phys.Index, Phys.Width, &V);
        return Pool (std::move (V), Phys.Width);
    }

    RegSub CONST *pSub;
    if (FindSub (Name, &pSub)) {
        RegPhys CONST &Phys = m_Layout.Phys[pSub->Parent];
        ComPtr<ICpuValue> V;
        m_pE->GetRegister (Phys.Index, Phys.Width, &V);
        return Extract (Pool (std::move (V), Phys.Width), pSub->Lo, pSub->Width);
    }

    RegFlag CONST *pFlag;
    if (FindFlag (Name, &pFlag)) { return GetFlagBit (*pFlag); }

    return Const (m_WordBits, 0);          // unbound: a zero of word width
}

// pc.<field> where <field> is a composition field (off/seg) -> the source register it maps
// to (ip/cs). Sets *pReg and returns true for that shape.
bool
Translator::PcField (Expr *pExpr, std::string *pReg) CONST
{
    if (pExpr->Kind != ExprMember || pExpr->Args.empty () || pExpr->Args[0]->Kind != ExprName) { return false; }
    if (pExpr->Args[0]->Name != m_Layout.PcName ()) { return false; }
    auto It = m_Layout.PcFields.find (pExpr->Name);
    if (It == m_Layout.PcFields.end ()) { return false; }
    *pReg = It->second;
    return true;
}

// Is this LHS a write to the program counter: pc, the %PC meta, or pc.<composition-field>?
bool
Translator::IsPcTarget (Expr *pLhs) CONST
{
    if (pLhs->Kind == ExprName && pLhs->Name == m_Layout.PcName () && !m_Layout.PcName ().empty ()) { return true; }
    if (pLhs->Kind == ExprMeta && pLhs->Name == "PC") { return true; }
    std::string Reg;
    return PcField (pLhs, &Reg);
}

Value
Translator::EvalMember (Expr *pExpr)
{
    // pc.off / pc.seg -- resolve through the PC composition to the source register (ip/cs).
    std::string Reg;
    if (PcField (pExpr, &Reg)) { return EvalName (Reg); }
    // a.b -- b names a sub-field of register a. Field names are unique across the file,
    // so resolving the member directly gives the same storage.
    if (!pExpr->Name.empty ()) { return EvalName (pExpr->Name); }
    return Const (m_WordBits, 0);          // a.[m,n] multi-field: not in straight-line bodies
}

// ---- expressions ----------------------------------------------------------

static CPU_BINOP
MapBinop (TOKEN_KIND Op, bool Signed)
{
    switch (Op) {
    case TokPlus:    return BinAdd;
    case TokMinus:   return BinSub;
    case TokStar:    return BinMul;
    case TokSlash:   return Signed ? BinSDiv : BinUDiv;
    case TokPercent: return Signed ? BinSRem : BinURem;
    case TokAmp:     return BinAnd;
    case TokPipe:    return BinOr;
    case TokCaret:   return BinXor;
    case TokShl:     return BinShl;
    case TokShr:     return Signed ? BinAShr : BinLShr;
    case TokRol:     return BinRol;
    case TokRor:     return BinRor;
    case TokAndCom:  return BinAnd;        // rhs complemented by the caller
    case TokOrCom:   return BinOr;
    case TokXorCom:  return BinXor;
    case TokAndAnd:  return BinAnd;        // operands are i1
    case TokOrOr:    return BinOr;
    default:         return BinAdd;
    }
}

static bool
MapCompare (TOKEN_KIND Op, bool Signed, CPU_CMP *pPred)
{
    switch (Op) {
    case TokEqEq:  *pPred = CmpEq;  return true;
    case TokNotEq: *pPred = CmpNe;  return true;
    case TokLt:    *pPred = Signed ? CmpSLt : CmpULt; return true;
    case TokLtEq:  *pPred = Signed ? CmpSLe : CmpULe; return true;
    case TokGt:    *pPred = Signed ? CmpSGt : CmpUGt; return true;
    case TokGtEq:  *pPred = Signed ? CmpSGe : CmpUGe; return true;
    default:       return false;
    }
}

Value
Translator::EvalExpr (Expr *pExpr)
{
    switch (pExpr->Kind) {
    case ExprInt:
        return Const (m_WordBits, pExpr->Int);

    case ExprName:
        return EvalName (pExpr->Name);

    case ExprMeta:
        // a bare meta-register reference (%PC etc.); %result is an inlined-macro local.
        if (pExpr->Name == "result") { return EvalName ("result"); }
        return EvalName (pExpr->Name);

    case ExprMember:
        return EvalMember (pExpr);

    case ExprUnary: {
        Value A = EvalExpr (pExpr->Args[0]);
        if (pExpr->Op == TokNot)   { return Un (UnNot, A); }
        if (pExpr->Op == TokTilde) { return Un (UnCom, A); }
        return Un (UnNeg, A);                            // unary minus
    }

    case ExprBinary: {
        bool Signed = false;
        CPU_CMP Pred;
        Value A = EvalExpr (pExpr->Args[0]);
        Value B = EvalExpr (pExpr->Args[1]);
        // Compares and AndCom/OrCom/XorCom need shaping before the op.
        if (MapCompare (pExpr->Op, Signed, &Pred)) {
            B = Coerce (B, A.Bits, false);
            return Cmp (Pred, A, B);
        }
        if (pExpr->Op == TokAndCom || pExpr->Op == TokOrCom || pExpr->Op == TokXorCom) {
            B = Un (UnCom, B);
        }
        B = Coerce (B, A.Bits, false);
        return Bin (MapBinop (pExpr->Op, Signed), A, B);
    }

    case ExprAugment: {
        // %S / %U mark the wrapped expression's signedness; %ORD/%UNO/%OFTRAP are FP
        // predicates not used by the integer core -- pass the value through.
        bool Signed = (pExpr->Name == "S");
        if (pExpr->Args.empty ()) { return Const (m_WordBits, 0); }
        Expr *Inner = pExpr->Args[0];
        if (Inner->Kind == ExprBinary) {
            CPU_CMP Pred;
            Value A = EvalExpr (Inner->Args[0]);
            Value B = EvalExpr (Inner->Args[1]);
            if (MapCompare (Inner->Op, Signed, &Pred)) {
                B = Coerce (B, A.Bits, Signed);
                return Cmp (Pred, A, B);
            }
            B = Coerce (B, A.Bits, Signed);
            return Bin (MapBinop (Inner->Op, Signed), A, B);
        }
        if (Inner->Kind == ExprCast) {
            // %S ( [ #iN expr ] ) -- the widening cast sign-extends (cbw/cwd).
            UINT32 Bits = (Inner->VType != nullptr) ? Inner->VType->Width : m_WordBits;
            Value A = EvalExpr (Inner->Args[0]);
            return Coerce (A, Bits, Signed);
        }
        return EvalExpr (Inner);
    }

    case ExprCast: {
        UINT32 Bits = (pExpr->VType != nullptr) ? pExpr->VType->Width : m_WordBits;
        Value A = EvalExpr (pExpr->Args[0]);
        return Coerce (A, Bits, false);
    }

    case ExprMem: {
        UINT32 Bits = (pExpr->VType != nullptr) ? pExpr->VType->Width : m_WordBits;
        Value Addr = EvalExpr (pExpr->Args[0]);
        ComPtr<ICpuValue> V;
        m_pE->Load (Addr.V, Bits, &V);
        return Pool (std::move (V), Bits);
    }

    case ExprSelect: {
        Value C = EvalExpr (pExpr->Args[0]);
        Value T = EvalExpr (pExpr->Args[1]);
        Value F = EvalExpr (pExpr->Args[2]);
        F = Coerce (F, T.Bits, false);
        ComPtr<ICpuValue> V;
        m_pE->Select (C.V, T.V, F.V, &V);
        return Pool (std::move (V), T.Bits);
    }

    case ExprBitSlice: {
        Value Op = EvalExpr (pExpr->Args[0]);
        UINT64 Hi = (pExpr->Args[1]->Kind == ExprInt) ? pExpr->Args[1]->Int : 0;
        UINT64 Lo = (pExpr->Args[2]->Kind == ExprInt) ? pExpr->Args[2]->Int : 0;
        if (Lo > Hi) { UINT64 T = Lo; Lo = Hi; Hi = T; }
        return Extract (Op, (UINT32) Lo, (UINT32) (Hi - Lo + 1));
    }

    case ExprBitCombine: {
        // ( a : b : c ) MSB-first: total width is the sum, a occupies the high bits.
        UINT32 Total = 0;
        std::vector<Value> Parts;
        for (Expr *A : pExpr->Args) {
            Value P = EvalExpr (A);
            Parts.push_back (P);
            Total += P.Bits;
        }
        Value Acc = Const (Total, 0);
        UINT32 Pos = Total;
        for (Value CONST &P : Parts) {
            Pos -= P.Bits;
            Value Wide = Coerce (P, Total, false);
            if (Pos != 0) { Wide = Bin (BinShl, Wide, Const (Total, Pos)); }
            Acc = Bin (BinOr, Acc, Wide);
        }
        return Acc;
    }

    case ExprCC:
        return EvalCC (pExpr);

    case ExprCall:
        return EvalMacroCall (pExpr);

    case ExprIs:
        return Const (1, 1);              // dynamic type test: always an int here

    case ExprIndex: {
        Value Addr = EvalExpr (pExpr->Args[1]);
        ComPtr<ICpuValue> V;
        m_pE->Load (Addr.V, m_WordBits, &V);
        return Pool (std::move (V), m_WordBits);
    }

    default:
        return Const (m_WordBits, 0);
    }
}

// ---- %CC: the value, plus the flags the operation sets -------------------

// Low-byte parity (x86 PF): fold the byte down to one bit, then invert -- PF is set when
// the byte has an EVEN number of ones.
static UINT32 ParityFoldShifts[] = { 4, 2, 1 };

Value
Translator::EvalCC (Expr *pExpr)
{
    Expr *Inner = pExpr->Args[0];

    // Peek the root: an add/sub gives us both operands, so we can derive carry/overflow/
    // aux exactly. Anything else yields only the result-derived flags (Z/N/P).
    bool   HaveOperands = false;
    Value  A {}, B {};
    Value  Result;
    if (Inner->Kind == ExprBinary && (Inner->Op == TokPlus || Inner->Op == TokMinus)) {
        A = EvalExpr (Inner->Args[0]);
        B = EvalExpr (Inner->Args[1]);
        B = Coerce (B, A.Bits, false);
        Result = Bin (Inner->Op == TokPlus ? BinAdd : BinSub, A, B);
        HaveOperands = true;
    } else {
        Result = EvalExpr (Inner);
    }

    DeriveFlags (Inner, Result, A, B, HaveOperands, pExpr->CcFlags, pExpr->CcNeg);
    return Result;
}

void
Translator::DeriveFlags (Expr *pInner, Value CONST &Result, Value CONST &A, Value CONST &B,
                         bool HaveOperands, std::vector<std::string> CONST &CcFlags,
                         std::vector<bool> CONST &CcNeg)
{
    UINT32 W = Result.Bits ? Result.Bits : m_WordBits;

    // The flag list is either an exclusion set (`!C` -> all but carry) or, if any entry is
    // positive, an inclusion set (only those). Bare %CC(...) affects the default set.
    std::vector<std::string> Exclude, Include;
    for (size_t I = 0; I < CcFlags.size (); ++I) {
        if (I < CcNeg.size () && CcNeg[I]) { Exclude.push_back (CcFlags[I]); }
        else                               { Include.push_back (CcFlags[I]); }
    }
    auto Wanted = [&] (CHAR8 CONST *FlagName) -> bool {
        std::string N = FlagName;
        if (!Include.empty ()) {
            for (std::string CONST &S : Include) { if (S == N) { return true; } }
            return false;
        }
        for (std::string CONST &S : Exclude) { if (S == N) { return false; } }
        return true;
    };

    // Apply one derived bit to whichever flag carries the given %meta role (or, for aux,
    // the flag literally named "A"), honouring the include/exclude filter by flag name.
    auto Apply = [&] (CHAR8 CONST *Meta, CHAR8 CONST *AuxName, Value CONST &Bit) {
        for (RegFlag CONST &F : m_Layout.Flags) {
            bool RoleMatch = (Meta[0] != '\0' && F.Meta == Meta)
                          || (AuxName[0] != '\0' && F.Name == AuxName);
            if (RoleMatch && Wanted (F.Name.c_str ())) { SetFlagBit (F, Bit); return; }
        }
    };

    // Z and N follow from the result alone.
    Apply ("Z", "", Cmp (CmpEq, Result, Const (W, 0)));
    Apply ("N", "", Extract (Result, W - 1, 1));

    // P: fold the low byte to a single parity bit, then invert (PF = even parity). The
    // shifts stay 8-bit (a truncating extract would mismatch the XOR operand widths).
    Value Byte = Coerce (Result, 8, false);
    for (UINT32 Sh : ParityFoldShifts) {
        Value Shifted = Bin (BinLShr, Byte, Const (8, Sh));
        Byte = Bin (BinXor, Byte, Shifted);
    }
    Value Odd = Coerce (Bin (BinAnd, Byte, Const (8, 1)), 1, false);
    Apply ("P", "", Un (UnNot, Odd));

    if (!HaveOperands) { return; }

    bool IsAdd = (pInner->Op == TokPlus);

    // Carry: add -> result wrapped below an operand; sub -> first operand below second.
    Apply ("C", "", IsAdd ? Cmp (CmpULt, Result, A) : Cmp (CmpULt, A, B));

    // Overflow (signed): the sign-bit of the "operands disagree with result" term.
    Value Xor1 = Bin (BinXor, IsAdd ? A : A, IsAdd ? Result : B);
    Value Xor2 = Bin (BinXor, IsAdd ? B : A, Result);
    Apply ("V", "", Extract (Bin (BinAnd, Xor1, Xor2), W - 1, 1));

    // Aux (BCD): carry/borrow out of bit 3 = bit 4 of (A ^ B ^ Result).
    Value Aux = Bin (BinXor, Bin (BinXor, A, B), Result);
    Apply ("", "A", Extract (Aux, 4, 1));
}

// ---- statements -----------------------------------------------------------

bool
Translator::Emit (std::vector<Stmt *> CONST &Body)
{
    bool Ok = true;
    for (Stmt *S : Body) {
        if (!EmitStmt (S)) { Ok = false; }
    }
    return Ok;
}

HRESULT
Translator::EmitCondition (Expr *pExpr, ICpuValue **ppOut)
{
    Value V = EvalExpr (pExpr);
    // A one-bit value (a flag, a compare) is the condition; anything wider is true when
    // non-zero (`if (reg)` == reg != 0).
    Value Bit = (V.Bits == 1) ? V : Cmp (CmpNe, V, Const (V.Bits ? V.Bits : m_WordBits, 0));
    if (Bit.V == nullptr) { *ppOut = nullptr; return E_FAIL; }
    Bit.V->AddRef ();                                // ownership transferred to the caller
    *ppOut = Bit.V;
    return S_OK;
}

HRESULT
Translator::EmitExpr (Expr *pExpr, ICpuValue **ppOut)
{
    Value V = EvalExpr (pExpr);
    if (V.V == nullptr) { *ppOut = nullptr; return E_FAIL; }
    V.V->AddRef ();                                  // ownership transferred to the caller
    *ppOut = V.V;
    return S_OK;
}

bool
Translator::EmitStmt (Stmt *pStmt)
{
    switch (pStmt->Kind) {
    case StmtAssign:
        return EmitAssign (pStmt);

    case StmtExpr: {
        Expr *E = pStmt->Rhs;
        if (E == nullptr) { return true; }
        if (E->Kind == ExprCall) { return EmitMacroStmt (E); }
        EvalExpr (E);                        // %CC(...) and the like: emitted for effect
        return true;
    }

    case StmtBlock:
        return Emit (pStmt->Body);           // a brace group: same block, nested scope

    case StmtIf:
        return EmitIf (pStmt);

    case StmtWhile:
        return EmitWhile (pStmt);

    case StmtFor:
        return EmitFor (pStmt);

    default:
        return false;
    }
}

ComPtr<ICpuBlock>
Translator::NewBlock (CHAR8 CONST *pName)
{
    ICpuBlock *pBlock = nullptr;
    m_pE->CreateBlock (pName, &pBlock);
    return ComPtr<ICpuBlock> (pBlock);
}

// A condition known at translation time -- a literal, or a type test `e is #t` whose width
// is fixed (the `address is #i16` in i8086_jump). Lets EmitIf skip the CFG diamond.
bool
Translator::TryConstCond (Expr *pCond, bool *pResult) CONST
{
    if (pCond->Kind == ExprInt) { *pResult = (pCond->Int != 0); return true; }
    if (pCond->Kind == ExprIs && pCond->VType != nullptr && !pCond->Args.empty ()) {
        *pResult = (WidthOf (pCond->Args[0]) == pCond->VType->Width);
        return true;
    }
    return false;
}

// if (Cond) Then [else Else].  Register state lives in the register file (memory), not in
// SSA values, so the branches need no PHI nodes: each block reads and writes registers
// afresh -- the merge block simply continues.
bool
Translator::EmitIf (Stmt *pStmt)
{
    // A compile-time-known condition collapses to its taken branch -- no diamond, so a
    // captured value (a ret's pc.off write) stays in the current block.
    bool Known = false;
    if (TryConstCond (pStmt->Cond, &Known)) {
        return Known ? Emit (pStmt->Then) : Emit (pStmt->Else);
    }

    Value Cond = Coerce (EvalExpr (pStmt->Cond), 1, false);

    ComPtr<ICpuBlock> Then = NewBlock ("if.then");
    ComPtr<ICpuBlock> Else = pStmt->Else.empty () ? ComPtr<ICpuBlock> () : NewBlock ("if.else");
    ComPtr<ICpuBlock> End  = NewBlock ("if.end");

    m_pE->CondBranch (Cond.V, Then.Get (), Else.Get () ? Else.Get () : End.Get ());

    m_pE->SetInsertBlock (Then.Get ());
    bool Ok = Emit (pStmt->Then);
    m_pE->Branch (End.Get ());

    if (Else.Get () != nullptr) {
        m_pE->SetInsertBlock (Else.Get ());
        Ok = Emit (pStmt->Else) && Ok;
        m_pE->Branch (End.Get ());
    }

    m_pE->SetInsertBlock (End.Get ());
    return Ok;
}

// while (Cond) Body.  head tests, body runs and loops back, end continues.
bool
Translator::EmitWhile (Stmt *pStmt)
{
    ComPtr<ICpuBlock> Head = NewBlock ("while.head");
    ComPtr<ICpuBlock> Body = NewBlock ("while.body");
    ComPtr<ICpuBlock> End  = NewBlock ("while.end");

    m_pE->Branch (Head.Get ());
    m_pE->SetInsertBlock (Head.Get ());
    Value Cond = Coerce (EvalExpr (pStmt->Cond), 1, false);
    m_pE->CondBranch (Cond.V, Body.Get (), End.Get ());

    m_pE->SetInsertBlock (Body.Get ());
    bool Ok = Emit (pStmt->Body);
    m_pE->Branch (Head.Get ());

    m_pE->SetInsertBlock (End.Get ());
    return Ok;
}

// for (Init; Cond; Step) Body.  Init runs once, then head/body/step/end as usual.
bool
Translator::EmitFor (Stmt *pStmt)
{
    bool Ok = Emit (pStmt->Init);

    ComPtr<ICpuBlock> Head = NewBlock ("for.head");
    ComPtr<ICpuBlock> Body = NewBlock ("for.body");
    ComPtr<ICpuBlock> Step = NewBlock ("for.step");
    ComPtr<ICpuBlock> End  = NewBlock ("for.end");

    m_pE->Branch (Head.Get ());
    m_pE->SetInsertBlock (Head.Get ());
    if (pStmt->Cond != nullptr) {
        Value Cond = Coerce (EvalExpr (pStmt->Cond), 1, false);
        m_pE->CondBranch (Cond.V, Body.Get (), End.Get ());
    } else {
        m_pE->Branch (Body.Get ());          // for (;;) with no test
    }

    m_pE->SetInsertBlock (Body.Get ());
    Ok = Emit (pStmt->Body) && Ok;
    m_pE->Branch (Step.Get ());

    m_pE->SetInsertBlock (Step.Get ());
    Ok = Emit (pStmt->Step) && Ok;
    m_pE->Branch (Head.Get ());

    m_pE->SetInsertBlock (End.Get ());
    return Ok;
}

bool
Translator::EmitAssign (Stmt *pStmt)
{
    if (pStmt->Lhs == nullptr || pStmt->Rhs == nullptr) { return true; }

    // The parser desugars `lhs <op>= rhs` to AssignOp = the base op (TokPlus for +=, ...);
    // a plain store is TokAssign. So anything but TokAssign is a read-modify-write.
    Value Rhs;
    if (pStmt->AssignOp != TokAssign) {
        TOKEN_KIND BinTok = pStmt->AssignOp;
        Value Cur = EvalExpr (pStmt->Lhs);
        Value Add = EvalExpr (pStmt->Rhs);
        if (BinTok == TokAndCom || BinTok == TokOrCom || BinTok == TokXorCom) {
            Add = Un (UnCom, Add);
        }
        Add = Coerce (Add, Cur.Bits, false);
        Rhs = Bin (MapBinop (BinTok, false), Cur, Add);
    } else {
        Rhs = EvalExpr (pStmt->Rhs);
    }

    // `<type> name = ...` with name not naming storage declares an inlined local.
    if (pStmt->LhsType != nullptr && pStmt->Lhs->Kind == ExprName
        && m_Layout.PhysIndex.find (pStmt->Lhs->Name) == m_Layout.PhysIndex.end ()) {
        RegSub CONST *pSub; RegFlag CONST *pFlag;
        if (!FindSub (pStmt->Lhs->Name, &pSub) && !FindFlag (pStmt->Lhs->Name, &pFlag)) {
            m_Env[pStmt->Lhs->Name] = Coerce (Rhs, pStmt->LhsType->Width, false);
            return true;
        }
    }

    StoreTo (pStmt->Lhs, Rhs);
    return true;
}

void
Translator::StoreTo (Expr *pLhs, Value CONST &Rhs)
{
    // A computed write to the program counter (a ret popping its target) is captured as the
    // branch target; the caller emits the IndirectBranch after the body runs. The value is
    // pooled, so it stays alive until the translation ends.
    if (m_IndirectPc && IsPcTarget (pLhs)) {
        m_IndirectTarget = Rhs.V;
        return;
    }

    switch (pLhs->Kind) {
    case ExprName:
        WriteName (pLhs->Name, Rhs);
        return;

    case ExprMeta:
        m_Env[pLhs->Name] = Rhs;             // %result and other inlined-macro locals
        return;

    case ExprMember: {
        std::string Reg;
        if (PcField (pLhs, &Reg)) { WriteName (Reg, Rhs); return; }  // pc.off = ... -> ip
        WriteName (pLhs->Name, Rhs);         // flags.C = ...  ->  the named field
        return;
    }

    case ExprMem: {
        UINT32 Bits = (pLhs->VType != nullptr) ? pLhs->VType->Width : Rhs.Bits;
        Value Addr = EvalExpr (pLhs->Args[0]);
        Value V = Coerce (Rhs, Bits, false);
        m_pE->Store (V.V, Addr.V, Bits);
        return;
    }

    case ExprBitCombine: {
        // ( a : b ) = rhs -- peel the fields off rhs MSB-first and store each.
        UINT32 Total = Rhs.Bits;
        UINT32 Pos = Total;
        for (Expr *Field : pLhs->Args) {
            UINT32 Fw = WidthOf (Field);
            if (Fw > Pos) { Fw = Pos; }
            Pos -= Fw;
            Value Part = Extract (Rhs, Pos, Fw);
            StoreTo (Field, Part);
        }
        return;
    }

    default:
        return;                              // pc.[seg,off] and other composites: later
    }
}

void
Translator::WriteName (std::string CONST &Name, Value CONST &Rhs)
{
    auto O = m_Operands.find (Name);
    if (O != m_Operands.end ()) {
        Operand CONST &Op = O->second;
        if (Op.Kind == Operand::Imm) { return; }     // an immediate has no write-back
        RegPhys CONST &Phys = m_Layout.Phys[Op.RegIndex];
        if (Op.SubWidth != 0 && Op.SubWidth != Phys.Width) {
            ComPtr<ICpuValue> Reg;
            m_pE->GetRegister (Phys.Index, Phys.Width, &Reg);
            Value Merged = Insert (Pool (std::move (Reg), Phys.Width), Rhs, Op.SubLo, Op.SubWidth);
            m_pE->PutRegister (Phys.Index, Merged.V, Phys.Width, FALSE);
        } else {
            Value V = Coerce (Rhs, Phys.Width, false);
            m_pE->PutRegister (Phys.Index, V.V, Phys.Width, FALSE);
        }
        return;
    }

    if (m_Env.find (Name) != m_Env.end ()) { m_Env[Name] = Rhs; return; }

    auto P = m_Layout.PhysIndex.find (Name);
    if (P != m_Layout.PhysIndex.end ()) {
        RegPhys CONST &Phys = m_Layout.Phys[P->second];
        Value V = Coerce (Rhs, Phys.Width, false);
        m_pE->PutRegister (Phys.Index, V.V, Phys.Width, FALSE);
        return;
    }

    RegSub CONST *pSub;
    if (FindSub (Name, &pSub)) {
        RegPhys CONST &Phys = m_Layout.Phys[pSub->Parent];
        ComPtr<ICpuValue> Reg;
        m_pE->GetRegister (Phys.Index, Phys.Width, &Reg);
        Value Merged = Insert (Pool (std::move (Reg), Phys.Width), Rhs, pSub->Lo, pSub->Width);
        m_pE->PutRegister (Phys.Index, Merged.V, Phys.Width, FALSE);
        return;
    }

    RegFlag CONST *pFlag;
    if (FindFlag (Name, &pFlag)) { SetFlagBit (*pFlag, Rhs); return; }

    m_Env[Name] = Rhs;                       // an otherwise-unknown name: a fresh local
}

// ---- macros ---------------------------------------------------------------

bool
Translator::EmitMacroStmt (Expr *pCall)
{
    // @trap ( vector ) -- a builtin: record the vector and trap to the host (a software
    // interrupt, or HLT's wait-on-interrupt). Emitted via the syscall capability.
    if (pCall->Name == "trap") {
        ICpuSyscallEmitter *pSys = nullptr;
        if (SUCCEEDED (m_pE->QueryInterface (IID_ICpuSyscallEmitter, (VOID **) &pSys)) && pSys != nullptr) {
            UINT64 Vec = 0;
            if (!pCall->Args.empty ()) {
                Expr *A = pCall->Args[0];
                if (A->Kind == ExprInt) { Vec = A->Int; }
                else if (A->Kind == ExprName) {
                    auto It = m_Operands.find (A->Name);
                    if (It != m_Operands.end () && It->second.Kind == Operand::Imm) { Vec = It->second.ImmValue; }
                }
            }
            Value Ret = Const (m_WordBits, m_TrapReturnPc);
            pSys->EmitSyscall ((UINT32) Vec, Ret.V);
            pSys->Release ();
        }
        return true;
    }

    Macro *M = FindMacro (pCall->Name, pCall->Args.size ());
    if (M == nullptr) { return false; }

    std::map<std::string, Value> Saved;
    std::vector<std::string> Names = M->Params;
    Names.push_back ("result");
    for (std::string CONST &N : Names) {
        auto E = m_Env.find (N);
        if (E != m_Env.end ()) { Saved[N] = E->second; }
    }
    for (size_t I = 0; I < M->Params.size () && I < pCall->Args.size (); ++I) {
        m_Env[M->Params[I]] = EvalExpr (pCall->Args[I]);
    }

    bool Ok = Emit (M->Body);

    for (std::string CONST &N : Names) {
        if (Saved.count (N)) { m_Env[N] = Saved[N]; } else { m_Env.erase (N); }
    }
    return Ok;
}

Value
Translator::EvalMacroCall (Expr *pCall)
{
    Macro *M = FindMacro (pCall->Name, pCall->Args.size ());
    if (M == nullptr) { return Const (m_WordBits, 0); }

    std::map<std::string, Value> Saved;
    std::vector<std::string> Names = M->Params;
    Names.push_back ("result");
    for (std::string CONST &N : Names) {
        auto E = m_Env.find (N);
        if (E != m_Env.end ()) { Saved[N] = E->second; }
    }
    for (size_t I = 0; I < M->Params.size () && I < pCall->Args.size (); ++I) {
        m_Env[M->Params[I]] = EvalExpr (pCall->Args[I]);
    }
    m_Env.erase ("result");

    Emit (M->Body);

    Value Result;
    auto R = m_Env.find ("result");
    Result = (R != m_Env.end ()) ? R->second : Const (m_WordBits, 0);

    for (std::string CONST &N : Names) {
        if (Saved.count (N)) { m_Env[N] = Saved[N]; } else { m_Env.erase (N); }
    }
    return Result;
}

} // namespace Upcl
} // namespace LibCPU
