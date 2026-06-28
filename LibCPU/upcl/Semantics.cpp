/** @file  The UPCL semantics translator. See Semantics.h. */

#include "Semantics.h"
#include "LibCPU/CpuState.h"     // CPU_REGBANK_FLAG: the runtime-indexed register-bank sentinel
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

// The register array a `name[i]` indexes -- its base is a plain name matching a layout array.
RegArray CONST *
Translator::FindArray (Expr *pBase) CONST
{
    if (pBase->Kind != ExprName && pBase->Kind != ExprMember) { return nullptr; }
    for (RegArray CONST &A : m_Layout.Arrays) {
        if (A.Name == pBase->Name) { return &A; }
    }
    return nullptr;
}

// The Load/Store address that selects array element [Idx]: the bank sentinel OR the physical slot
// (BaseIndex + Idx). Built at 64-bit width so the sentinel's high bits are not truncated.
Value
Translator::RegBankAddr (RegArray CONST &Arr, Value CONST &Idx)
{
    // The physical slot is BaseIndex + (architectural index - Start): a `r?:1` array holds r1..rN
    // at BaseIndex.., so r[k] maps to BaseIndex + k - 1 and r[0] resolves below the array (the
    // separate hardwired r0). For the common Start==0 array this is just BaseIndex + Idx.
    Value Slot = Bin (BinAdd, Const (64, Arr.BaseIndex - Arr.Start), Coerce (Idx, 64, false));
    return Bin (BinOr, Const (64, CPU_REGBANK_FLAG), Slot);
}

// A register index that is a compile-time constant: a literal, or a decoder operand bound to an
// immediate (the m88k rd/rs1/rs2 fields). Lets the translator fold a hardwired-zero access.
bool
Translator::TryConstIndex (Expr *pIdx, UINT64 *pVal) CONST
{
    if (pIdx == nullptr) { return false; }
    if (pIdx->Kind == ExprInt) { *pVal = pIdx->Int; return true; }
    if (pIdx->Kind == ExprName) {
        auto O = m_Operands.find (pIdx->Name);
        if (O != m_Operands.end () && O->second.Kind == Operand::Imm) { *pVal = O->second.ImmValue; return true; }
    }
    return false;
}

// Does `array[idx]` resolve, at translation time, to a hardwired-zero physical register (r0)?
// The physical slot is BaseIndex + (idx - Start); only a compile-time-constant index can be folded.
bool
Translator::ZeroWiredSlot (RegArray CONST &Arr, Expr *pIdx) CONST
{
    UINT64 K = 0;
    if (!TryConstIndex (pIdx, &K)) { return false; }
    UINT64 Slot = (UINT64) Arr.BaseIndex + K - Arr.Start;
    return Slot < m_Layout.Phys.size () && m_Layout.Phys[(size_t) Slot].ZeroWired;
}

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
            return Op.Bits ? Op.Bits : m_WordBits;
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
// The effective address of a memory operand: its displacement plus its base register(s).
Value
Translator::MemAddress (Operand CONST &Op)
{
    Value Addr = Const (m_WordBits, (UINT64) Op.Disp);
    if (Op.Base1 != ~(UINT32) 0) {
        RegPhys CONST &B = m_Layout.Phys[Op.Base1];
        ComPtr<ICpuValue> V; m_pE->GetRegister (B.Index, B.Width, &V);
        Addr = Bin (BinAdd, Addr, Coerce (Pool (std::move (V), B.Width), m_WordBits, false));
    }
    if (Op.Base2 != ~(UINT32) 0) {
        RegPhys CONST &B = m_Layout.Phys[Op.Base2];
        ComPtr<ICpuValue> V; m_pE->GetRegister (B.Index, B.Width, &V);
        Addr = Bin (BinAdd, Addr, Coerce (Pool (std::move (V), B.Width), m_WordBits, false));
    }
    return Addr;
}

Value
Translator::ReadOperand (Operand CONST &Op)
{
    if (Op.Kind == Operand::Imm) { return Const (Op.Bits ? Op.Bits : m_WordBits, Op.ImmValue); }
    if (Op.Kind == Operand::Mem) {
        UINT32 Bits = Op.Bits ? Op.Bits : m_WordBits;
        Value Addr = MemAddress (Op);
        ComPtr<ICpuValue> V; m_pE->Load (Addr.V, Bits, &V);
        return Pool (std::move (V), Bits);
    }
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
        Value Out = Pool (std::move (V), Phys.Width);
        Out.Float = Phys.Float;                          // a floating-point register (8087 st)
        return Out;
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

// The floating-point binary op for an arithmetic operator (operands are IEEE floats).
static CPU_BINOP
MapBinopF (TOKEN_KIND Op)
{
    switch (Op) {
    case TokPlus:  return BinFAdd;
    case TokMinus: return BinFSub;
    case TokStar:  return BinFMul;
    case TokSlash: return BinFDiv;
    default:       return BinFAdd;
    }
}

// The floating-point compare predicate for a relational operator, if it is one.
static bool
MapCompareF (TOKEN_KIND Op, CPU_CMP *pPred)
{
    switch (Op) {
    case TokEqEq: *pPred = CmpFOEq; return true;
    case TokLt:   *pPred = CmpFOLt; return true;
    case TokGt:   *pPred = CmpFOGt; return true;
    default:      return false;
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

    case ExprFloat: {
        // A floating-point literal: materialise its 64-bit (double) IEEE pattern as an integer
        // constant, then reinterpret those bits as a float. A `[ #f80 <literal> ]` cast then widens
        // it to the 80-bit stack (an 8087 FLDPI loads pi this way). Constants are double-precision.
        double D = pExpr->Real;
        UINT64 Bits = 0;
        std::memcpy (&Bits, &D, sizeof (Bits));
        Value R = CastTo (CastIToFBits, Const (64, Bits), 64);
        R.Float = true;
        return R;
    }

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
        if (A.Float) { Value R = Un (UnFNeg, A); R.Float = true; return R; }   // float negate (FCHS)
        return Un (UnNeg, A);                            // unary minus
    }

    case ExprBinary: {
        CPU_CMP Pred;
        Value A = EvalExpr (pExpr->Args[0]);
        Value B = EvalExpr (pExpr->Args[1]);
        // Packed SIMD: if either operand is a vector (#vN:W), do the op LANE-BY-LANE (SWAR) -- each
        // lane is computed in its own width so carries never cross lanes. A compare yields an all-ones
        // mask per lane (the usual SIMD result). This is how UPCL represents GPR SIMD on a scalar
        // emitter: the lanes are extracted, operated, and recombined with shifts/ors.
        if (A.Lanes > 0 || B.Lanes > 0) {
            UINT32 N  = (A.Lanes > 0) ? A.Lanes : B.Lanes;
            UINT32 W  = (A.Bits ? A.Bits : B.Bits);
            UINT32 LW = W / N;
            bool   VSigned = A.Signed || B.Signed;
            bool   IsCmp = MapCompare (pExpr->Op, VSigned, &Pred);
            Value  Acc = Const (W, 0);
            for (UINT32 i = 0; i < N; ++i) {
                Value la = Extract (A, i * LW, LW);
                Value lb = Extract (B, i * LW, LW);
                Value lr;
                if (IsCmp) {
                    Value c = Cmp (Pred, la, lb);                  // 1-bit lane predicate
                    Value ones = Const (LW, 0); ones = Bin (BinSub, ones, Const (LW, 1)); // all-ones
                    Value zero = Const (LW, 0);
                    ComPtr<ICpuValue> sel;
                    m_pE->Select (c.V, ones.V, zero.V, &sel);
                    lr = Pool (std::move (sel), LW);
                } else {
                    lr = Bin (MapBinop (pExpr->Op, VSigned), la, lb);  // lane-width arithmetic wraps in-lane
                }
                Value wide = Coerce (lr, W, false);
                if (i != 0) { wide = Bin (BinShl, wide, Const (W, i * LW)); }
                Acc = Bin (BinOr, Acc, wide);
            }
            Acc.Lanes = N;
            return Acc;
        }
        // A floating-point operand makes this a float operation (the 8087's arithmetic).
        if (A.Float || B.Float) {
            if (MapCompareF (pExpr->Op, &Pred)) { return Cmp (Pred, A, B); }
            Value R = Bin (MapBinopF (pExpr->Op), A, B);
            R.Float = true;
            return R;
        }
        // The operation is signed if either operand was marked signed via %S (so a narrower operand
        // sign-extends to width, and < / <= / > / >= and / and % select their signed forms).
        bool Signed = A.Signed || B.Signed;
        // Compares and AndCom/OrCom/XorCom need shaping before the op.
        if (MapCompare (pExpr->Op, Signed, &Pred)) {
            B = Coerce (B, A.Bits, Signed);
            return Cmp (Pred, A, B);
        }
        if (pExpr->Op == TokAndCom || pExpr->Op == TokOrCom || pExpr->Op == TokXorCom) {
            B = Un (UnCom, B);
        }
        B = Coerce (B, A.Bits, Signed);
        Value R = Bin (MapBinop (pExpr->Op, Signed), A, B);
        R.Signed = Signed;            // a signed result stays signed for any further widening
        return R;
    }

    case ExprAugment: {
        // %FLT / %INT: a BITCAST (reinterpret the bit pattern), not a value conversion. %FLT reads an
        // integer's bits as an IEEE float of the same width (an f32 held in a 32-bit GPR); %INT reads
        // a float's bits back as an integer. Used by FP instructions whose operands live in GPRs.
        if (pExpr->Name == "FLT") {
            if (pExpr->Args.empty ()) { return Const (m_WordBits, 0); }
            Value A = EvalExpr (pExpr->Args[0]);
            Value R = CastTo (CastIToFBits, A, A.Bits);
            R.Float = true;
            return R;
        }
        if (pExpr->Name == "INT") {
            if (pExpr->Args.empty ()) { return Const (m_WordBits, 0); }
            Value A = EvalExpr (pExpr->Args[0]);
            Value R = CastTo (CastFToIBits, A, A.Bits);
            R.Float = false;
            return R;
        }
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
            Value R = Coerce (A, Bits, Signed);
            R.Signed = Signed;
            return R;
        }
        // A bare operand (`%S(imm)` / `%U(rs)`): widen to the word with the marked signedness, and
        // tag it so a surrounding binary op extends and compares/divides with the same signedness.
        Value R = Coerce (EvalExpr (Inner), m_WordBits, Signed);
        R.Signed = Signed;
        return R;
    }

    case ExprCast: {
        // A vector cast (#vN:W) REINTERPRETS the operand's bits as N lanes of W bits -- no value
        // change, just a tag so a subsequent binary op runs lane-by-lane (see ExprBinary).
        if (pExpr->VType != nullptr && pExpr->VType->Kind == TypeVector) {
            UINT32 VBits = pExpr->VType->Lanes * pExpr->VType->Width;
            Value  A = Coerce (EvalExpr (pExpr->Args[0]), VBits, false);
            A.Lanes = pExpr->VType->Lanes;
            A.Float = false;
            return A;
        }
        UINT32 Bits = (pExpr->VType != nullptr) ? pExpr->VType->Width : m_WordBits;
        bool   ToFloat = (pExpr->VType != nullptr && pExpr->VType->Kind == TypeFloat);
        // A cast of an integer literal builds the constant directly at the target width, so a
        // value wider than the machine word (e.g. [ #i16 0x100 ] on an 8-bit-word arch) is not
        // truncated to the word width first and then zero-extended back.
        if (!ToFloat && pExpr->Args[0]->Kind == ExprInt) {
            return Const (Bits, pExpr->Args[0]->Int);
        }
        Value  A = EvalExpr (pExpr->Args[0]);
        if (ToFloat) {
            // [ #fN x ]: an int converts to float (FILD), a float re-rounds to the new width.
            Value R = A.Float ? CastTo ((Bits >= A.Bits) ? CastFExt : CastFTrunc, A, Bits)
                              : CastTo (CastSIToF, A, Bits);
            R.Float = true;
            return R;
        }
        if (A.Float) {                                   // [ #iN x ] of a float: truncate to int (FIST)
            Value R = CastTo (CastFToSI, A, Bits);
            R.Float = false;
            return R;
        }
        return Coerce (A, Bits, false);
    }

    case ExprMem: {
        UINT32 Bits = (pExpr->VType != nullptr) ? pExpr->VType->Width : m_WordBits;
        bool   IsFloat = (pExpr->VType != nullptr && pExpr->VType->Kind == TypeFloat);
        Value Addr = EvalExpr (pExpr->Args[0]);
        // A load-linked (%LL) reads memory AND establishes a reservation on the address (the
        // hardware LLbit + reserved address) -- a later %SC to the same address succeeds only while
        // that reservation holds. The reservation is synthesised architectural state.
        if (pExpr->Linked) { SetReservation (Addr); }
        ComPtr<ICpuValue> V;
        m_pE->Load (Addr.V, Bits, &V);
        Value Out = Pool (std::move (V), Bits);
        // A float-typed memory load (`#f32 %M[..]`) reads the raw IEEE bytes, so reinterpret the
        // loaded integer bits as a float of that width (an 8087 FLD m32real / m64real).
        if (IsFloat) { Out = CastTo (CastIToFBits, Out, Bits); Out.Float = true; }
        return Out;
    }

    case ExprStoreCond: {
        // %SC[addr] <- value : store-conditional. Succeeds (yields 1) only if the reservation set by
        // the matching %LL is still held for this address; otherwise it yields 0 and leaves memory
        // unchanged. Lowered with the existing Load/Store/Select -- no atomic emitter primitive
        // needed -- which models the uniprocessor (no foreign writer) case faithfully.
        UINT32 Bits = (pExpr->VType != nullptr) ? pExpr->VType->Width : m_WordBits;
        Value  Addr = EvalExpr (pExpr->Args[0]);
        Value  Val  = Coerce (EvalExpr (pExpr->Args[1]), Bits, false);
        // ok = llbit AND (lladdr == addr)
        Value  LlBit  = EvalName (ReservationBitName ());
        Value  LlAddr = EvalName (ReservationAddrName ());
        Value  BitSet = Cmp (CmpNe, LlBit, Const (LlBit.Bits, 0));
        Value  Match  = Cmp (CmpEq, LlAddr, Coerce (Addr, LlAddr.Bits, false));
        Value  Ok     = Bin (BinAnd, BitSet, Match);
        // memory <- ok ? value : memory  (a conditional store via select on the current contents)
        ComPtr<ICpuValue> Cur;
        m_pE->Load (Addr.V, Bits, &Cur);
        Value  CurV = Pool (std::move (Cur), Bits);
        ComPtr<ICpuValue> Sel;
        m_pE->Select (Ok.V, Val.V, CurV.V, &Sel);
        Value  Stored = Pool (std::move (Sel), Bits);
        m_pE->Store (Stored.V, Addr.V, Bits);
        // The reservation is consumed whether or not the store happened.
        Value Zero = Const (m_WordBits, 0);
        WriteName (ReservationBitName (), Zero);
        return Coerce (Ok, m_WordBits, false);          // the 0/1 success bit
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
        RegArray CONST *pArr = FindArray (pExpr->Args[0]);
        if (pArr != nullptr) {                       // st[i]: a register-array element via the bank
            if (ZeroWiredSlot (*pArr, pExpr->Args[1])) {  // a hardwired-zero register (r0) reads 0
                return Const (pArr->Width, 0);
            }
            Value Idx  = EvalExpr (pExpr->Args[1]);
            Value Addr = RegBankAddr (*pArr, Idx);
            ComPtr<ICpuValue> V;
            m_pE->Load (Addr.V, pArr->Width, &V);
            Value Out = Pool (std::move (V), pArr->Width);
            Out.Float = pArr->Float;
            return Out;
        }
        Value Addr = EvalExpr (pExpr->Args[1]);      // r[addr]: the index is a memory address
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

    // Peek the root: an add/sub gives us both operands, so we can derive carry/overflow/aux
    // exactly. Anything else yields only the result-derived flags (Z/N/P).
    bool   HaveOperands = false;
    bool   IsAdd = false;
    Value  A {}, B {};        // the two DATA operands (carry/overflow/aux derive from these)
    Value  CarryBit {};       // the true carry-out / borrow
    Value  Result;
    if (Inner->Kind == ExprBinary && (Inner->Op == TokPlus || Inner->Op == TokMinus)) {
        IsAdd = (Inner->Op == TokPlus);
        Expr *Lhs = Inner->Args[0];
        Expr *Rhs = Inner->Args[1];
        // Add-with-carry / subtract-with-borrow is written `(P op Q) op R` -- the SAME op
        // nested -- e.g. `a + src + %C`. P and Q are the data operands; R is the carry/borrow-in.
        bool  WithCarry = (Lhs->Kind == ExprBinary && Lhs->Op == Inner->Op);
        Value Cin {};
        if (WithCarry) {
            A   = EvalExpr (Lhs->Args[0]);
            B   = Coerce (EvalExpr (Lhs->Args[1]), A.Bits, false);
            Cin = Coerce (EvalExpr (Rhs), A.Bits, false);
        } else {
            A = EvalExpr (Lhs);
            B = Coerce (EvalExpr (Rhs), A.Bits, false);
        }
        UINT32    W  = A.Bits;
        CPU_BINOP Op = IsAdd ? BinAdd : BinSub;
        // The architectural result wraps at W, as the hardware does.
        Result = Bin (Op, A, B);
        if (WithCarry) { Result = Bin (Op, Result, Cin); }
        // The true carry/borrow: redo the operation ONE BIT WIDER so an intermediate add does
        // not drop the carry before the carry-in is folded in (a + src wraps to 8 bits before
        // + %C, otherwise), then take bit W. For a 2-term op this equals the old Result <u A.
        Value Wide = Bin (Op, Coerce (A, W + 1, false), Coerce (B, W + 1, false));
        if (WithCarry) { Wide = Bin (Op, Wide, Coerce (Cin, W + 1, false)); }
        CarryBit = Extract (Wide, W, 1);
        HaveOperands = true;
    } else {
        Result = EvalExpr (Inner);
    }

    DeriveFlags (IsAdd, Result, A, B, CarryBit, HaveOperands, pExpr->CcFlags, pExpr->CcNeg);
    return Result;
}

void
Translator::DeriveFlags (bool IsAdd, Value CONST &Result, Value CONST &A, Value CONST &B,
                         Value CONST &CarryBit, bool HaveOperands,
                         std::vector<std::string> CONST &CcFlags, std::vector<bool> CONST &CcNeg)
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

    // Carry: the carry-out / borrow computed wide by the caller (so an add-with-carry's
    // intermediate wrap does not lose it).
    Apply ("C", "", CarryBit);

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
        // A meta-flag write (%C/%Z/%N/%V = ...) is a real flag write, so route it through
        // WriteName, which resolves flags (FindFlag -> SetFlagBit) and falls back to an env
        // local for names that are neither register nor flag -- preserving %result and other
        // inlined-macro locals.
        WriteName (pLhs->Name, Rhs);
        return;

    case ExprMember: {
        std::string Reg;
        if (PcField (pLhs, &Reg)) { WriteName (Reg, Rhs); return; }  // pc.off = ... -> ip
        WriteName (pLhs->Name, Rhs);         // flags.C = ...  ->  the named field
        return;
    }

    case ExprMem: {
        UINT32 Bits = (pLhs->VType != nullptr) ? pLhs->VType->Width : Rhs.Bits;
        bool   IsFloat = (pLhs->VType != nullptr && pLhs->VType->Kind == TypeFloat);
        Value Addr = EvalExpr (pLhs->Args[0]);
        Value V;
        if (IsFloat && Rhs.Float) {
            // A float-typed store (`#f32 %M[..] = st`): round to the target width, then write the
            // raw IEEE bytes (an 8087 FST m32real / m64real).
            V = (Bits >= Rhs.Bits) ? CastTo (CastFExt, Rhs, Bits) : CastTo (CastFTrunc, Rhs, Bits);
            V = CastTo (CastFToIBits, V, Bits);
        } else {
            V = Coerce (Rhs, Bits, false);
        }
        m_pE->Store (V.V, Addr.V, Bits);
        return;
    }

    case ExprIndex: {
        RegArray CONST *pArr = FindArray (pLhs->Args[0]);
        if (pArr != nullptr) {                       // st[i] = ... : a register-array element
            if (ZeroWiredSlot (*pArr, pLhs->Args[1])) { return; }  // writes to r0 are discarded
            Value Idx  = EvalExpr (pLhs->Args[1]);
            Value Addr = RegBankAddr (*pArr, Idx);
            Value V    = Coerce (Rhs, pArr->Width, false);   // float: widths match, a no-op
            m_pE->Store (V.V, Addr.V, pArr->Width);
            return;
        }
        Value Addr = EvalExpr (pLhs->Args[1]);       // r[addr] = ... : the index is a memory address
        Value V = Coerce (Rhs, m_WordBits, false);
        m_pE->Store (V.V, Addr.V, m_WordBits);
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
        if (Op.Kind == Operand::Mem) {               // store to the computed address
            UINT32 Bits = Op.Bits ? Op.Bits : m_WordBits;
            Value Addr = MemAddress (Op);
            Value V = Coerce (Rhs, Bits, false);
            m_pE->Store (V.V, Addr.V, Bits);
            return;
        }
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

// A load-linked establishes a reservation: remember the accessed address and raise the LLbit. A
// later %SC compares against these (see ExprStoreCond). The reservation registers were synthesised
// into the layout (see BuildRegisterLayout) when the description used %LL / %SC.
void
Translator::SetReservation (Value CONST &Addr)
{
    Value One = Const (m_WordBits, 1);
    WriteName (ReservationBitName (), One);
    WriteName (ReservationAddrName (), Addr);
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
    // @lea ( operand ) -- the effective address of a memory operand, without loading it. Lets an
    // instruction access the operand's bytes at a width other than the operand's own (an 8087
    // FLD m32real reads 4 bytes from the ModR/M address through `#f32 %M[ @lea(src) ]`).
    if (pCall->Name == "lea" && pCall->Args.size () == 1 && pCall->Args[0]->Kind == ExprName) {
        auto O = m_Operands.find (pCall->Args[0]->Name);
        if (O != m_Operands.end () && O->second.Kind == Operand::Mem) {
            return MemAddress (O->second);
        }
    }

    // Unary floating-point builtins. No operator spells these, so they map to the unary FP ops.
    //   @fabs / @fsqrt          -- 8087 FABS / FSQRT
    //   @f2xm1 / @flog2 / @ftan -- transcendental primitives of 8087 F2XM1 / FYL2X / FPTAN
    if (pCall->Args.size () == 1) {
        CPU_UNOP Op;
        bool Found = true;
        if (pCall->Name == "fabs") { Op = UnFAbs; }
        else if (pCall->Name == "fsqrt") { Op = UnFSqrt; }
        else if (pCall->Name == "f2xm1") { Op = UnF2xm1; }
        else if (pCall->Name == "flog2") { Op = UnFLog2; }
        else if (pCall->Name == "ftan") { Op = UnFTan; }
        else { Found = false; }
        if (Found) {
            Value A = EvalExpr (pCall->Args[0]);
            Value R = Un (Op, A);
            R.Float = true;
            return R;
        }
    }

    // @fatan2 ( y, x ) -- 8087 FPATAN, the two-argument arctangent (a binary FP op).
    if (pCall->Args.size () == 2 && pCall->Name == "fatan2") {
        Value A = EvalExpr (pCall->Args[0]);
        Value B = EvalExpr (pCall->Args[1]);
        Value R = Bin (BinFAtan2, A, B);
        R.Float = true;
        return R;
    }

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
