/** @file  The UPCL semantics translator. See Semantics.h. */

#include "Semantics.h"
#include "LibCPU/CpuState.h"     // CPU_REGBANK_FLAG: the runtime-indexed register-bank sentinel
#include <cstring>

namespace LibCPU {
namespace Upcl {

// Mask a value to a bit width (its low Bits bits). Used to keep folded constants in range.
static UINT64
MaskW (UINT64 V, UINT32 Bits)
{
    if (Bits == 0 || Bits >= 64) { return V; }
    return V & ((UINT64_C (1) << Bits) - 1);
}

// Sign-extend the low Bits bits of V to a full 64-bit value (for folding signed compares/shifts).
static UINT64
SignExtend (UINT64 V, UINT32 Bits)
{
    if (Bits == 0 || Bits >= 64) { return V; }
    UINT64 M = UINT64_C (1) << (Bits - 1);
    UINT64 L = MaskW (V, Bits);
    return (L ^ M) - M;
}

Translator::Translator (RegisterLayout CONST &Layout, Arch *pArch, ICpuEmitter *pEmitter,
                        UINT32 WordBits)
    : m_Layout (Layout), m_pArch (pArch), m_pE (pEmitter), m_WordBits (WordBits)
{
    for (Macro *M : pArch->Macros) { m_Macros[M->Name].push_back (M); }
    // Scratch registers for loop-carried body locals (PromoteLoopLocals) are allocated ABOVE the
    // architectural register file so they never overlap a real register slot.  They share the
    // runtime-indexed register bank, so they must fit within CPU_REGBANK_MASK like any bank slot.
    m_NextScratch = (UINT32) m_Layout.Phys.size ();
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
    // Expose this operand's addrmode selector fields (dm/dr/...) so its pre/post block can name the
    // register the mode chose, via %REG[group, field]. They are decode constants; left in scope for
    // the whole instruction (the body never reads raw fields, and src/dst addrmodes use distinct
    // selector names) so the DEFERRED post block still sees them when it runs after the body.
    for (auto CONST &Kv : Op.Fields) { m_Fields[Kv.first] = Kv.second; }
    // Autodecrement -(Rn): the `pre { }` block runs BEFORE the EA is taken (so the operand reads the
    // decremented address). Autoincrement (Rn)+: defer the `post { }` block until AFTER the body uses
    // the operand, so the base register is bumped exactly once regardless of read/write count.
    if (Op.Pre  != nullptr) { for (Stmt *S : *Op.Pre) { EmitStmt (S); } }
    if (Op.Post != nullptr) { m_PostBlocks.push_back (Op.Post); }
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

// A lazy compile-time constant: it carries its value but emits no node until consumed (see Use).
Value
Translator::ConstVal (UINT32 Bits, UINT64 K) CONST
{
    Value Out;
    Out.Bits    = Bits ? Bits : m_WordBits;
    Out.IsConst = true;
    Out.K       = MaskW (K, Out.Bits);
    return Out;
}

Value
Translator::Const (UINT32 Bits, UINT64 N)
{
    // Generate phase: emit the ConstInt eagerly and NON-folding. A generated frontend binds its
    // decoder operands as immediate "constants" that are really runtime sentinels the source emitter
    // maps back to C++ expressions -- they must materialise as nodes and must not be folded.
    if (m_Generate) {
        ComPtr<ICpuValue> V;
        m_pE->ConstInt (Bits ? Bits : m_WordBits, N, &V);
        return Pool (std::move (V), Bits ? Bits : m_WordBits);
    }
    return ConstVal (Bits, N);
}

// Materialise a value for the emitter: an ordinary value yields its node; a lazy constant emits its
// ConstInt now (so a constant that is never used emits nothing, and folded-away work disappears).
ICpuValue *
Translator::Use (Value CONST &V)
{
    if (V.V != nullptr) { return V.V; }
    if (V.IsConst) {
        ComPtr<ICpuValue> C;
        m_pE->ConstInt (V.Bits ? V.Bits : m_WordBits, V.K, &C);
        ICpuValue *R = C.Get ();
        m_Pool.push_back (std::move (C));
        return R;
    }
    return nullptr;
}

// Read a physical register, reusing a value already read in this block (so an effective-address base
// and the same register's autoincrement load it once). The cache is cleared by PutReg, a bank store,
// and every block boundary, so a memoized value is never used past a write or across control flow.
Value
Translator::GetReg (UINT32 Index, UINT32 Width)
{
    // Generate phase: a static frontend re-reads each register (its source emitter maps every
    // GetRegister to its own expression), so do not memoize there.
    if (!m_Generate) {
        auto It = m_RegCache.find (Index);
        if (It != m_RegCache.end ()) { return It->second; }
    }
    ComPtr<ICpuValue> V;
    m_pE->GetRegister (Index, Width, &V);
    Value Out = Pool (std::move (V), Width);
    if (!m_Generate) { m_RegCache[Index] = Out; }
    return Out;
}

void
Translator::PutReg (UINT32 Index, ICpuValue *pVal, UINT32 Width)
{
    m_RegCache.erase (Index);                 // the cached read is now stale
    m_pE->PutRegister (Index, pVal, Width, FALSE);
}

Value
Translator::Bin (CPU_BINOP Op, Value CONST &A, Value CONST &B)
{
    // Fold when both operands are integer constants -- the runtime op is not emitted.
    if (A.IsConst && B.IsConst && !A.Float && !B.Float && A.Lanes == 0 && B.Lanes == 0) {
        UINT64 a = A.K, b = B.K; UINT32 W = A.Bits ? A.Bits : m_WordBits;
        INT64  sa = (INT64) SignExtend (a, A.Bits), sb = (INT64) SignExtend (b, B.Bits);
        UINT64 R;
        bool   Ok = true;
        switch (Op) {
        case BinAdd:  R = a + b; break;
        case BinSub:  R = a - b; break;
        case BinMul:  R = a * b; break;
        case BinAnd:  R = a & b; break;
        case BinOr:   R = a | b; break;
        case BinXor:  R = a ^ b; break;
        case BinShl:  R = a << (b & 63); break;
        case BinLShr: R = a >> (b & 63); break;
        case BinAShr: R = (UINT64) (sa >> (b & 63)); break;
        case BinUDiv: if (b == 0) { Ok = false; R = 0; } else { R = a / b; } break;
        case BinSDiv: if (sb == 0) { Ok = false; R = 0; } else { R = (UINT64) (sa / sb); } break;
        case BinURem: if (b == 0) { Ok = false; R = 0; } else { R = a % b; } break;
        case BinSRem: if (sb == 0) { Ok = false; R = 0; } else { R = (UINT64) (sa % sb); } break;
        default:      Ok = false; R = 0; break;
        }
        if (Ok) { return ConstVal (W, R); }
    }
    // Algebraic identities when ONE operand is a constant (the other runtime): a zero displacement's
    // `0 + base`, `x | 0`, `x << 0`, `x * 1`, `x & ~0` -> the other operand; `x * 0` / `x & 0` -> 0.
    if (!A.Float && !B.Float && A.Lanes == 0 && B.Lanes == 0) {
        UINT32 W = A.Bits ? A.Bits : m_WordBits;
        UINT64 Ones = MaskW (~UINT64_C (0), W);
        if (B.IsConst) {
            switch (Op) {
            case BinAdd: case BinSub: case BinOr: case BinXor:
            case BinShl: case BinLShr: case BinAShr: case BinRol: case BinRor:
                if (B.K == 0) { return A; } break;
            case BinMul:  if (B.K == 1) { return A; } if (B.K == 0) { return ConstVal (W, 0); } break;
            case BinUDiv: case BinSDiv: if (B.K == 1) { return A; } break;
            case BinAnd:  if (B.K == Ones) { return A; } if (B.K == 0) { return ConstVal (W, 0); } break;
            default: break;
            }
        }
        if (A.IsConst) {
            switch (Op) {
            case BinAdd: case BinOr: case BinXor: if (A.K == 0) { return B; } break;
            case BinMul:  if (A.K == 1) { return B; } if (A.K == 0) { return ConstVal (W, 0); } break;
            case BinAnd:  if (A.K == Ones) { return B; } if (A.K == 0) { return ConstVal (W, 0); } break;
            default: break;
            }
        }
    }
    ComPtr<ICpuValue> V;
    m_pE->BinaryOp (Op, Use (A), Use (B), &V);
    return Pool (std::move (V), A.Bits);
}

Value
Translator::Un (CPU_UNOP Op, Value CONST &A)
{
    if (A.IsConst && !A.Float && A.Lanes == 0) {
        if (Op == UnNeg) { return ConstVal (A.Bits, (UINT64) (-(INT64) A.K)); }
        if (Op == UnCom) { return ConstVal (A.Bits, ~A.K); }
        if (Op == UnNot) { return ConstVal (1, (A.K != 0) ? 0 : 1); }
    }
    ComPtr<ICpuValue> V;
    m_pE->UnaryOp (Op, Use (A), &V);
    return Pool (std::move (V), Op == UnNot ? 1 : A.Bits);
}

Value
Translator::Cmp (CPU_CMP Pred, Value CONST &A, Value CONST &B)
{
    if (A.IsConst && B.IsConst && !A.Float && !B.Float && A.Lanes == 0 && B.Lanes == 0) {
        UINT64 a = A.K, b = B.K;
        INT64  sa = (INT64) SignExtend (a, A.Bits), sb = (INT64) SignExtend (b, B.Bits);
        bool   R; bool Ok = true;
        switch (Pred) {
        case CmpEq:  R = (a == b); break;
        case CmpNe:  R = (a != b); break;
        case CmpULt: R = (a <  b); break;
        case CmpULe: R = (a <= b); break;
        case CmpUGt: R = (a >  b); break;
        case CmpUGe: R = (a >= b); break;
        case CmpSLt: R = (sa <  sb); break;
        case CmpSLe: R = (sa <= sb); break;
        case CmpSGt: R = (sa >  sb); break;
        case CmpSGe: R = (sa >= sb); break;
        default:     Ok = false; R = false; break;
        }
        if (Ok) { return ConstVal (1, R ? 1 : 0); }
    }
    ComPtr<ICpuValue> V;
    m_pE->Compare (Pred, Use (A), Use (B), &V);
    return Pool (std::move (V), 1);
}

Value
Translator::CastTo (CPU_CAST Op, Value CONST &A, UINT32 Bits)
{
    // Integer width casts fold; the float bitcasts (IToFBits/FToIBits) keep their runtime node so a
    // float value is materialised correctly.
    if (A.IsConst && A.Lanes == 0) {
        if (Op == CastTrunc || Op == CastZExt) { return ConstVal (Bits, MaskW (A.K, Bits)); }
        if (Op == CastSExt) { return ConstVal (Bits, MaskW ((UINT64) SignExtend (A.K, A.Bits), Bits)); }
    }
    ComPtr<ICpuValue> V;
    m_pE->Cast (Op, Use (A), Bits, &V);
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
        auto F = m_Fields.find (pIdx->Name);     // an addrmode selector field in scope (pre/post block)
        if (F != m_Fields.end ()) { *pVal = F->second; return true; }
    }
    return false;
}

// %REG[group, field] : the register group <group>'s member selected by the (decode-constant) field.
// Resolve to that member's name -- the group's positional alias <group><N> (R0..R7) -- so the ordinary
// name path (EvalName / WriteName) reads or writes it. The field must fold to a constant, exactly as
// the decoder resolves the same form for an addressing-mode base register.
bool
Translator::RegSelName (Expr *pExpr, std::string *pName) CONST
{
    if (pExpr == nullptr || pExpr->Kind != ExprMeta || pExpr->Name != "REG" || pExpr->Args.size () != 2) {
        return false;
    }
    if (pExpr->Args[0]->Kind != ExprName) { return false; }
    UINT64 Idx = 0;
    if (!TryConstIndex (pExpr->Args[1], &Idx)) { return false; }
    *pName = pExpr->Args[0]->Name + std::to_string ((unsigned long long) Idx);
    return true;
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
    return Extract (GetReg (Psr.Index, Psr.Width), Flag.Bit, 1);
}

void
Translator::SetFlagBit (RegFlag CONST &Flag, Value CONST &Bit)
{
    Value One = Coerce (Bit, 1, false);
    CPU_FLAG Which;
    if (MapFlag (Flag, &Which)) {
        m_pE->SetFlag (Which, Use (One));
        m_RegCache.erase (Flag.Parent);      // a flag may be PSR-resident: drop any cached PSR read
        return;
    }
    RegPhys CONST &Psr = m_Layout.Phys[Flag.Parent];
    Value Merged = Insert (GetReg (Psr.Index, Psr.Width), One, Flag.Bit, 1);
    PutReg (Psr.Index, Use (Merged), Psr.Width);
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
        auto Sl = m_EnvSlotWidth.find (Name);
        if (Sl != m_EnvSlotWidth.end ()) { return Sl->second; }
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
        Addr = Bin (BinAdd, Addr, Coerce (GetReg (B.Index, B.Width), m_WordBits, false));
    }
    if (Op.Base2 != ~(UINT32) 0) {
        RegPhys CONST &B = m_Layout.Phys[Op.Base2];
        Addr = Bin (BinAdd, Addr, Coerce (GetReg (B.Index, B.Width), m_WordBits, false));
    }
    return Addr;
}

// On a WORD-ADDRESSED machine (PDP-10: byte_size == word_size != 8) guest RAM is an array of
// machine-word cells, each backed by a 64-bit (8-byte) host slot, and a `%M[addr]` address is a
// WORD INDEX into that array -- not a byte address. Translate the index to the byte offset of its
// cell (index * 8); the backend Load/Store then reads/writes the whole word_size-bit cell there
// (the interpreter's RamRead/RamWrite handle a non-byte-multiple width by spanning the full cell
// and masking to the width). A byte-addressed arch passes the address through unchanged, so every
// existing ISA keeps its plain byte-array memory model.
Value
Translator::WordCellAddr (Value CONST &Addr)
{
    if (m_pArch == nullptr || !m_pArch->WordAddressed) { return Addr; }
    return Bin (BinMul, Coerce (Addr, 64, false), Const (64, CPU_WORD_CELL_BYTES));
}

// Register/memory aliasing for `%M[addr]` on a word-addressed arch.
//
// When the arch has a register group declared with `aliases_memory`, memory word accesses
// whose index falls in the aliased range must read/write the SAME storage as the
// corresponding AC register (e.g. PDP-10: `%M[3]` and `ac3` share one physical slot).
//
// The mechanism: the interpreter's Load/Store already routes accesses to the register bank
// when the address has CPU_REGBANK_FLAG set (the runtime-indexed `ac[i]` path). We reuse
// that: for an aliased word index K, emit Load/Store at (CPU_REGBANK_FLAG | (PhysBase + K))
// instead of (K * CPU_WORD_CELL_BYTES).
//
// Compile-time constant index: fold directly -- no branch emitted, no RAM touched.
// Runtime index that MAY fall in the aliased range: emit a Select so that in-range accesses
// go to the register bank and out-of-range accesses go to RAM. The `Select` on the address
// is safe because both the bank-sentinel path and the memory path go through the same
// Load/Store call; the interpreter dispatches on the sentinel flag at runtime.
//
// Word-addressed but NO aliasing group declared: return the plain word-cell byte offset
// (identical to WordCellAddr).  Non-word-addressed arch: return the raw address unchanged.
Value
Translator::MemAliasAddr (Expr *pAddrExpr, Value CONST &WordAddr)
{
    if (m_pArch == nullptr || !m_pArch->WordAddressed) { return WordAddr; }
    if (!m_Layout.HasMemAlias ()) { return WordCellAddr (WordAddr); }

    UINT32 PhysBase  = m_Layout.MemAliasPhysBase;
    UINT32 AliasCount = m_Layout.MemAliasCount;
    UINT32 WordBase  = m_Layout.MemAliasWordBase;

    // Compile-time constant address: fold the branch away.
    UINT64 K = 0;
    if (!m_Generate && pAddrExpr != nullptr && TryConstIndex (pAddrExpr, &K)) {
        UINT32 PhysIdx = 0;
        if (m_Layout.AliasedWordToReg (K, &PhysIdx)) {
            // In the aliased range: return the register-bank sentinel address.
            return Bin (BinOr, Const (64, CPU_REGBANK_FLAG),
                               Const (64, (UINT64) PhysIdx));
        }
        // Outside the aliased range: plain word-cell byte offset.
        return WordCellAddr (WordAddr);
    }

    // Runtime address: emit a Select between the bank-sentinel path and the RAM path.
    // Condition: WordAddr >= WordBase AND WordAddr < WordBase + AliasCount.
    // The bank address for word index W is: CPU_REGBANK_FLAG | (PhysBase + W - WordBase).
    Value W64 = Coerce (WordAddr, 64, false);

    // In-range check: (WordAddr - WordBase) < AliasCount.  Using unsigned comparison so that
    // WordAddr < WordBase also falls out as "false" (wraps to a large unsigned number).
    Value Offset = Bin (BinSub, W64, Const (64, (UINT64) WordBase));
    Value InRange = Cmp (CmpULt, Offset, Const (64, (UINT64) AliasCount));

    // Bank address: CPU_REGBANK_FLAG | (PhysBase + Offset).
    Value BankAddr = Bin (BinOr, Const (64, CPU_REGBANK_FLAG),
                                 Bin (BinAdd, Const (64, (UINT64) PhysBase), Offset));

    // Memory address: the plain word-cell byte offset.
    Value MemAddr = WordCellAddr (WordAddr);

    // If InRange is a compile-time constant (e.g. both bounds folded), skip the Select.
    if (InRange.IsConst) { return (InRange.K != 0) ? BankAddr : MemAddr; }

    ComPtr<ICpuValue> Sel;
    m_pE->Select (Use (InRange), Use (BankAddr), Use (MemAddr), &Sel);
    return Pool (std::move (Sel), 64);
}

Value
Translator::ReadOperand (Operand CONST &Op)
{
    if (Op.Kind == Operand::Imm) { return Const (Op.Bits ? Op.Bits : m_WordBits, Op.ImmValue); }
    if (Op.Kind == Operand::Mem) {
        UINT32 Bits = Op.Bits ? Op.Bits : m_WordBits;
        Value Addr = MemAddress (Op);
        ComPtr<ICpuValue> V; m_pE->Load (Use (Addr), Bits, &V);
        return Pool (std::move (V), Bits);
    }
    RegPhys CONST &Phys = m_Layout.Phys[Op.RegIndex];
    Value Whole = GetReg (Phys.Index, Phys.Width);
    if (Op.SubWidth != 0 && Op.SubWidth != Phys.Width) { return Extract (Whole, Op.SubLo, Op.SubWidth); }
    return Whole;
}

Value
Translator::EvalName (std::string CONST &Name)
{
    auto O = m_Operands.find (Name);
    if (O != m_Operands.end ()) { return ReadOperand (O->second); }

    // A loop-carried local promoted to a scratch register (PromoteLoopLocals): read it from the bank.
    auto Sl = m_EnvSlot.find (Name);
    if (Sl != m_EnvSlot.end ()) {
        UINT32 Width = m_EnvSlotWidth[Name];
        Value  Addr  = Bin (BinOr, Const (64, CPU_REGBANK_FLAG), Const (64, (UINT64) Sl->second));
        ComPtr<ICpuValue> V; m_pE->Load (Use (Addr), Width, &V);
        return Pool (std::move (V), Width);
    }

    auto E = m_Env.find (Name);
    if (E != m_Env.end ()) { return E->second; }

    // An addrmode selector field in scope (during its pre/post block): a decode constant, so a byte
    // mode can compute its own step, e.g. `%REG[R, sr] += (sr >= 6) ? 2 : 1`.
    auto Fld = m_Fields.find (Name);
    if (Fld != m_Fields.end ()) { return Const (m_WordBits, Fld->second); }

    auto P = m_Layout.PhysIndex.find (Name);
    if (P != m_Layout.PhysIndex.end ()) {
        RegPhys CONST &Phys = m_Layout.Phys[P->second];
        Value Out = GetReg (Phys.Index, Phys.Width);
        Out.Float = Phys.Float;                          // a floating-point register (8087 st)
        return Out;
    }

    RegSub CONST *pSub;
    if (FindSub (Name, &pSub)) {
        RegPhys CONST &Phys = m_Layout.Phys[pSub->Parent];
        return Extract (GetReg (Phys.Index, Phys.Width), pSub->Lo, pSub->Width);
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

// Mask a value to its bit width, for an unsigned interpretation (a narrow field compared/shifted
// without sign).
static UINT64
MaskBits (INT64 V, UINT32 Bits)
{
    if (Bits == 0 || Bits >= 64) { return (UINT64) V; }
    return (UINT64) V & ((UINT64_C (1) << Bits) - 1);
}

// Evaluate a pure compile-phase expression to a constant: literals, immediate operands, and addrmode
// selector fields, combined by the integer operators. Returns false the moment a register / memory /
// flag read (a generate-phase value) is reached, so a mixed expression naturally splits -- the
// constant parts fold and the rest stays runtime. Signed (from %S) selects signed compares/shifts.
bool
Translator::FoldConst (Expr *pExpr, bool Signed, INT64 *pVal, UINT32 *pBits) CONST
{
    if (pExpr == nullptr) { return false; }
    switch (pExpr->Kind) {
    case ExprInt:
        *pVal = (INT64) pExpr->Int; *pBits = m_WordBits; return true;

    case ExprName: {
        // A decode-time constant: an immediate operand, or an addrmode selector field in scope. A
        // register/memory operand, an env local or a flag is a runtime value and does not fold. In
        // generate phase the field/immediate is itself runtime (a sentinel), so only literals fold.
        if (m_Generate) { return false; }
        auto O = m_Operands.find (pExpr->Name);
        if (O != m_Operands.end ()) {
            if (O->second.Kind != Operand::Imm) { return false; }
            *pVal = (INT64) O->second.ImmValue; *pBits = O->second.Bits ? O->second.Bits : m_WordBits;
            return true;
        }
        auto F = m_Fields.find (pExpr->Name);
        if (F != m_Fields.end ()) { *pVal = (INT64) F->second; *pBits = m_WordBits; return true; }
        return false;
    }

    case ExprAugment:
        if (pExpr->Args.empty ()) { return false; }
        if (pExpr->Name == "S")    { return FoldConst (pExpr->Args[0], true,   pVal, pBits); }
        if (pExpr->Name == "U" || pExpr->Name == "EVAL") { return FoldConst (pExpr->Args[0], Signed, pVal, pBits); }
        return false;                        // %GEN / %FLT / %INT etc.: not a foldable integer

    case ExprUnary: {
        INT64 A; UINT32 AB;
        if (!FoldConst (pExpr->Args[0], Signed, &A, &AB)) { return false; }
        if (pExpr->Op == TokNot)   { *pVal = (A != 0) ? 0 : 1; *pBits = 1;  return true; }
        if (pExpr->Op == TokTilde) { *pVal = ~A;               *pBits = AB; return true; }
        *pVal = -A; *pBits = AB; return true;
    }

    case ExprSelect: {
        INT64 C; UINT32 CB;
        if (!FoldConst (pExpr->Args[0], Signed, &C, &CB)) { return false; }
        return FoldConst ((C != 0) ? pExpr->Args[1] : pExpr->Args[2], Signed, pVal, pBits);
    }

    case ExprBinary: {
        bool S = Signed
              || (pExpr->Args[0]->Kind == ExprAugment && pExpr->Args[0]->Name == "S")
              || (pExpr->Args[1]->Kind == ExprAugment && pExpr->Args[1]->Name == "S");
        INT64 A, B; UINT32 AB, BB;
        if (!FoldConst (pExpr->Args[0], S, &A, &AB)) { return false; }
        if (!FoldConst (pExpr->Args[1], S, &B, &BB)) { return false; }
        CPU_CMP Pred;
        if (MapCompare (pExpr->Op, S, &Pred)) {
            UINT64 ua = MaskBits (A, AB), ub = MaskBits (B, BB);
            bool R;
            switch (pExpr->Op) {
            case TokEqEq:  R = (A == B); break;
            case TokNotEq: R = (A != B); break;
            case TokLt:    R = S ? (A <  B) : (ua <  ub); break;
            case TokLtEq:  R = S ? (A <= B) : (ua <= ub); break;
            case TokGt:    R = S ? (A >  B) : (ua >  ub); break;
            case TokGtEq:  R = S ? (A >= B) : (ua >= ub); break;
            default:       return false;
            }
            *pVal = R ? 1 : 0; *pBits = 1; return true;
        }
        INT64 R;
        switch (pExpr->Op) {
        case TokPlus:  R = A + B; break;
        case TokMinus: R = A - B; break;
        case TokStar:  R = A * B; break;
        case TokAmp:   R = A & B; break;
        case TokPipe:  R = A | B; break;
        case TokCaret: R = A ^ B; break;
        case TokAndAnd: R = (A && B) ? 1 : 0; *pBits = 1; *pVal = R; return true;
        case TokOrOr:   R = (A || B) ? 1 : 0; *pBits = 1; *pVal = R; return true;
        case TokShl:   R = A << (B & 63); break;
        case TokShr:   R = S ? (A >> (B & 63)) : (INT64) (MaskBits (A, AB) >> (B & 63)); break;
        case TokSlash:   if (B == 0) { return false; } R = A / B; break;
        case TokPercent: if (B == 0) { return false; } R = A % B; break;
        default: return false;
        }
        *pVal = R; *pBits = AB; return true;
    }

    default:
        return false;
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

    case ExprMeta: {
        // %REG[group, field]: read the group member the decode-constant field selects.
        std::string RegSel;
        if (RegSelName (pExpr, &RegSel)) { return EvalName (RegSel); }
        // a bare meta-register reference (%PC etc.); %result is an inlined-macro local.
        if (pExpr->Name == "result") { return EvalName ("result"); }
        return EvalName (pExpr->Name);
    }

    case ExprMember:
        return EvalMember (pExpr);

    case ExprUnary: {
        if (!m_Generate && m_NoFold == 0) {
            INT64 K; UINT32 KB;
            if (FoldConst (pExpr, false, &K, &KB)) { return Const (KB ? KB : m_WordBits, (UINT64) K); }
        }
        Value A = EvalExpr (pExpr->Args[0]);
        if (pExpr->Op == TokNot)   { return Un (UnNot, A); }
        if (pExpr->Op == TokTilde) { return Un (UnCom, A); }
        if (A.Float) { Value R = Un (UnFNeg, A); R.Float = true; return R; }   // float negate (FCHS)
        return Un (UnNeg, A);                            // unary minus
    }

    case ExprBinary: {
        if (!m_Generate && m_NoFold == 0) {
            INT64 K; UINT32 KB;
            if (FoldConst (pExpr, false, &K, &KB)) { return Const (KB ? KB : m_WordBits, (UINT64) K); }
        }
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
                    m_pE->Select (Use (c), Use (ones), Use (zero), &sel);
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
        // %EVAL(e): force a compile-phase fold of e (it must reduce to a constant -- a decode field,
        // immediate, or literal arithmetic); falls back to a normal evaluation if it cannot.
        if (pExpr->Name == "EVAL") {
            if (pExpr->Args.empty ()) { return Const (m_WordBits, 0); }
            INT64 K; UINT32 KB;
            if (FoldConst (pExpr->Args[0], false, &K, &KB)) { return Const (KB ? KB : m_WordBits, (UINT64) K); }
            return EvalExpr (pExpr->Args[0]);
        }
        // %GEN(e): suppress folding -- emit e as generate-phase (runtime) code even if it is constant.
        if (pExpr->Name == "GEN") {
            if (pExpr->Args.empty ()) { return Const (m_WordBits, 0); }
            m_NoFold++;
            Value V = EvalExpr (pExpr->Args[0]);
            m_NoFold--;
            return V;
        }
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
        // Route through MemAliasAddr: on a word-addressed arch with `aliases_memory`, an
        // access whose index is in the AC group's range goes to the register bank.  On all
        // other arches / groups this is identical to WordCellAddr (byte path is unchanged).
        Expr  *pAddrExpr = pExpr->Args[0];
        Value  WordAddr  = EvalExpr (pAddrExpr);
        Value  Addr      = MemAliasAddr (pAddrExpr, WordAddr);
        // A load-linked (%LL) reads memory AND establishes a reservation on the address (the
        // hardware LLbit + reserved address) -- a later %SC to the same address succeeds only while
        // that reservation holds. The reservation is synthesised architectural state.
        if (pExpr->Linked) { SetReservation (Addr); }
        ComPtr<ICpuValue> V;
        m_pE->Load (Use (Addr), Bits, &V);
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
        m_pE->Load (Use (Addr), Bits, &Cur);
        Value  CurV = Pool (std::move (Cur), Bits);
        ComPtr<ICpuValue> Sel;
        m_pE->Select (Use (Ok), Use (Val), Use (CurV), &Sel);
        Value  Stored = Pool (std::move (Sel), Bits);
        m_pE->Store (Use (Stored), Use (Addr), Bits);
        // The reservation is consumed whether or not the store happened.
        Value Zero = Const (m_WordBits, 0);
        WriteName (ReservationBitName (), Zero);
        return Coerce (Ok, m_WordBits, false);          // the 0/1 success bit
    }

    case ExprSelect: {
        if (!m_Generate && m_NoFold == 0) {
            INT64 K; UINT32 KB;
            if (FoldConst (pExpr, false, &K, &KB)) { return Const (KB ? KB : m_WordBits, (UINT64) K); }
        }
        Value C = EvalExpr (pExpr->Args[0]);
        Value T = EvalExpr (pExpr->Args[1]);
        Value F = EvalExpr (pExpr->Args[2]);
        F = Coerce (F, T.Bits, false);
        if (C.IsConst) { return (C.K != 0) ? T : F; }    // a known condition collapses to one arm
        ComPtr<ICpuValue> V;
        m_pE->Select (Use (C), Use (T), Use (F), &V);
        Value R = Pool (std::move (V), T.Bits);
        R.Float = T.Float;                               // propagate float-ness from the arms
        return R;
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
            m_pE->Load (Use (Addr), pArr->Width, &V);
            Value Out = Pool (std::move (V), pArr->Width);
            Out.Float = pArr->Float;
            return Out;
        }
        Value Addr = EvalExpr (pExpr->Args[1]);      // r[addr]: the index is a memory address
        ComPtr<ICpuValue> V;
        m_pE->Load (Use (Addr), m_WordBits, &V);
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
    // Run addressing-mode post { } blocks ((Rn)+) AFTER the body has used the operands, so each base
    // register is bumped exactly once regardless of how many times the operand was accessed.
    for (std::vector<Stmt *> CONST *Blk : m_PostBlocks) { for (Stmt *S : *Blk) { EmitStmt (S); } }
    m_PostBlocks.clear ();
    return Ok;
}

HRESULT
Translator::EmitCondition (Expr *pExpr, ICpuValue **ppOut)
{
    Value V = EvalExpr (pExpr);
    // A one-bit value (a flag, a compare) is the condition; anything wider is true when
    // non-zero (`if (reg)` == reg != 0).
    Value Bit = (V.Bits == 1) ? V : Cmp (CmpNe, V, Const (V.Bits ? V.Bits : m_WordBits, 0));
    ICpuValue *Out = Use (Bit);                      // materialise (a constant condition emits here)
    if (Out == nullptr) { *ppOut = nullptr; return E_FAIL; }
    Out->AddRef ();                                  // ownership transferred to the caller
    *ppOut = Out;
    return S_OK;
}

HRESULT
Translator::EmitExpr (Expr *pExpr, ICpuValue **ppOut)
{
    Value V = EvalExpr (pExpr);
    ICpuValue *Out = Use (V);                        // materialise (a constant result emits here)
    if (Out == nullptr) { *ppOut = nullptr; return E_FAIL; }
    Out->AddRef ();                                  // ownership transferred to the caller
    *ppOut = Out;
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

    m_pE->CondBranch (Use (Cond), Then.Get (), Else.Get () ? Else.Get () : End.Get ());

    m_pE->SetInsertBlock (Then.Get ()); ClearRegCache ();
    bool Ok = Emit (pStmt->Then);
    m_pE->Branch (End.Get ());

    if (Else.Get () != nullptr) {
        m_pE->SetInsertBlock (Else.Get ()); ClearRegCache ();
        Ok = Emit (pStmt->Else) && Ok;
        m_pE->Branch (End.Get ());
    }

    m_pE->SetInsertBlock (End.Get ()); ClearRegCache ();
    return Ok;
}

// Collect the names assigned by a statement (and its nested statements), mapping each to the
// widest declared `<type>` seen for it.  Used by PromoteLoopLocals to find a loop's carried
// body locals.  A `<type> name = ...` declaration carries the width in LhsType; a later bare
// `name = ...` carries none (0), so the declared width wins via the max below.
void
Translator::CollectAssignedNames (Stmt *pStmt, std::map<std::string, UINT32> &Out) CONST
{
    if (pStmt == nullptr) { return; }
    switch (pStmt->Kind) {
    case StmtAssign:
        if (pStmt->Lhs != nullptr && pStmt->Lhs->Kind == ExprName) {
            UINT32 W = (pStmt->LhsType != nullptr) ? pStmt->LhsType->Width : 0;
            auto It = Out.find (pStmt->Lhs->Name);
            if (It == Out.end () || W > It->second) { Out[pStmt->Lhs->Name] = W; }
        }
        break;
    case StmtBlock:
        CollectAssignedNames (pStmt->Body, Out);
        break;
    case StmtIf:
        CollectAssignedNames (pStmt->Then, Out);
        CollectAssignedNames (pStmt->Else, Out);
        break;
    case StmtWhile:
        CollectAssignedNames (pStmt->Body, Out);
        break;
    case StmtFor:
        CollectAssignedNames (pStmt->Init, Out);
        CollectAssignedNames (pStmt->Step, Out);
        CollectAssignedNames (pStmt->Body, Out);
        break;
    default:
        break;
    }
}

void
Translator::CollectAssignedNames (std::vector<Stmt *> CONST &Body,
                                  std::map<std::string, UINT32> &Out) CONST
{
    for (Stmt *S : Body) { CollectAssignedNames (S, Out); }
}

// Promote a loop's carried body locals to synthesised scratch registers.  For every env local
// assigned somewhere inside the loop, allocate a scratch slot (once) and spill the value that is
// live at loop entry into it, so reads on the first iteration and after a zero-iteration loop see
// the correct value.  Already-promoted names (an outer loop, or a name a macro re-enters) keep
// their slot.  Names that are decoder operands, registers, flags, or sub-registers are NOT env
// locals and are left to their own storage (registers already carry across loops).
void
Translator::PromoteLoopLocals (Stmt *pStmt)
{
    std::map<std::string, UINT32> Assigned;
    if (pStmt->Kind == StmtFor) {
        // The for-loop step/body iterate; the init runs once before promotion below, so the init's
        // value is what we spill.  Scan body + step (not init -- init declares the carried local).
        CollectAssignedNames (pStmt->Step, Assigned);
        CollectAssignedNames (pStmt->Body, Assigned);
    } else {
        CollectAssignedNames (pStmt->Body, Assigned);
    }

    for (auto CONST &Kv : Assigned) {
        std::string CONST &Name = Kv.first;
        if (m_EnvSlot.find (Name) != m_EnvSlot.end ()) { continue; }   // already register-backed
        // Only promote genuine env locals: a name that is a decoder operand, a register, a flag, or
        // a sub-register has real storage and is left alone (registers persist across loops natively).
        if (m_Operands.find (Name) != m_Operands.end ()) { continue; }
        if (m_Layout.PhysIndex.find (Name) != m_Layout.PhysIndex.end ()) { continue; }
        RegSub CONST *pSub; RegFlag CONST *pFlag;
        if (FindSub (Name, &pSub) || FindFlag (Name, &pFlag)) { continue; }

        // Determine the width: prefer a declared `<type>` width, else the live value's width, else
        // the word width.  Allocate a scratch slot; bail (leave as a plain SSA local) if the bank
        // is exhausted -- the deep multi-iteration case then stays a known deferral rather than
        // silently aliasing a real register.
        auto Live = m_Env.find (Name);
        UINT32 Width = Kv.second;
        if (Width == 0 && Live != m_Env.end ()) { Width = Live->second.Bits; }
        if (Width == 0) { Width = m_WordBits; }
        if (m_NextScratch > (UINT32) CPU_REGBANK_MASK) { continue; }     // no scratch slots left
        UINT32 Slot = m_NextScratch++;
        m_EnvSlot[Name]      = Slot;
        m_EnvSlotWidth[Name] = Width;

        // Spill the value live at loop entry into the scratch slot.  If the local was not yet
        // assigned before the loop, seed it to zero so a read sees a defined value.
        Value Init = (Live != m_Env.end ()) ? Live->second : Const (Width, 0);
        Value Addr = Bin (BinOr, Const (64, CPU_REGBANK_FLAG), Const (64, (UINT64) Slot));
        m_pE->Store (Use (Coerce (Init, Width, false)), Use (Addr), Width);
        m_Env.erase (Name);                       // its storage is now the scratch register
    }
}

// while (Cond) Body.  head tests, body runs and loops back, end continues.
bool
Translator::EmitWhile (Stmt *pStmt)
{
    PromoteLoopLocals (pStmt);
    ComPtr<ICpuBlock> Head = NewBlock ("while.head");
    ComPtr<ICpuBlock> Body = NewBlock ("while.body");
    ComPtr<ICpuBlock> End  = NewBlock ("while.end");

    m_pE->Branch (Head.Get ());
    m_pE->SetInsertBlock (Head.Get ()); ClearRegCache ();
    Value Cond = Coerce (EvalExpr (pStmt->Cond), 1, false);
    m_pE->CondBranch (Use (Cond), Body.Get (), End.Get ());

    m_pE->SetInsertBlock (Body.Get ()); ClearRegCache ();
    bool Ok = Emit (pStmt->Body);
    m_pE->Branch (Head.Get ());

    m_pE->SetInsertBlock (End.Get ()); ClearRegCache ();
    return Ok;
}

// for (Init; Cond; Step) Body.  Init runs once, then head/body/step/end as usual.
bool
Translator::EmitFor (Stmt *pStmt)
{
    bool Ok = Emit (pStmt->Init);
    PromoteLoopLocals (pStmt);                 // after Init: spill the init value into the scratch slot

    ComPtr<ICpuBlock> Head = NewBlock ("for.head");
    ComPtr<ICpuBlock> Body = NewBlock ("for.body");
    ComPtr<ICpuBlock> Step = NewBlock ("for.step");
    ComPtr<ICpuBlock> End  = NewBlock ("for.end");

    m_pE->Branch (Head.Get ());
    m_pE->SetInsertBlock (Head.Get ()); ClearRegCache ();
    if (pStmt->Cond != nullptr) {
        Value Cond = Coerce (EvalExpr (pStmt->Cond), 1, false);
        m_pE->CondBranch (Use (Cond), Body.Get (), End.Get ());
    } else {
        m_pE->Branch (Body.Get ());          // for (;;) with no test
    }

    m_pE->SetInsertBlock (Body.Get ()); ClearRegCache ();
    Ok = Emit (pStmt->Body) && Ok;
    m_pE->Branch (Step.Get ());

    m_pE->SetInsertBlock (Step.Get ()); ClearRegCache ();
    Ok = Emit (pStmt->Step) && Ok;
    m_pE->Branch (Head.Get ());

    m_pE->SetInsertBlock (End.Get ()); ClearRegCache ();
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
            // A local promoted to a scratch register (loop-carried) stores through the bank; an
            // ordinary local keeps its SSA value in m_Env.
            auto Sl = m_EnvSlot.find (pStmt->Lhs->Name);
            if (Sl != m_EnvSlot.end ()) {
                UINT32 Width = m_EnvSlotWidth[pStmt->Lhs->Name];
                Value  Addr  = Bin (BinOr, Const (64, CPU_REGBANK_FLAG), Const (64, (UINT64) Sl->second));
                m_pE->Store (Use (Coerce (Rhs, Width, false)), Use (Addr), Width);
            } else {
                m_Env[pStmt->Lhs->Name] = Coerce (Rhs, pStmt->LhsType->Width, false);
            }
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
        m_IndirectTarget = Use (Rhs);
        return;
    }

    switch (pLhs->Kind) {
    case ExprName:
        WriteName (pLhs->Name, Rhs);
        return;

    case ExprMeta: {
        // %REG[group, field] = ... : write the group member the decode-constant field selects (the
        // autoincrement/decrement post block bumping the very base register the mode named).
        std::string RegSel;
        if (RegSelName (pLhs, &RegSel)) { WriteName (RegSel, Rhs); return; }
        // %PA = ... is the MMU table-walk's output: store the physical address into the synthesised
        // result register that libcpu's TLB reads to install the translation.
        if (pLhs->Name == "PA") { WriteName (MmuResultName (), Rhs); return; }
        // A meta-flag write (%C/%Z/%N/%V = ...) is a real flag write, so route it through
        // WriteName, which resolves flags (FindFlag -> SetFlagBit) and falls back to an env
        // local for names that are neither register nor flag -- preserving %result and other
        // inlined-macro locals.
        WriteName (pLhs->Name, Rhs);
        return;
    }

    case ExprMember: {
        std::string Reg;
        if (PcField (pLhs, &Reg)) { WriteName (Reg, Rhs); return; }  // pc.off = ... -> ip
        WriteName (pLhs->Name, Rhs);         // flags.C = ...  ->  the named field
        return;
    }

    case ExprMem: {
        UINT32 Bits = (pLhs->VType != nullptr) ? pLhs->VType->Width : Rhs.Bits;
        bool   IsFloat = (pLhs->VType != nullptr && pLhs->VType->Kind == TypeFloat);
        // Same aliasing routing as the load path: in-range word indices go to the bank.
        Expr  *pAddrExpr = pLhs->Args[0];
        Value  WordAddr  = EvalExpr (pAddrExpr);
        Value  Addr      = MemAliasAddr (pAddrExpr, WordAddr);
        Value V;
        if (IsFloat && Rhs.Float) {
            // A float-typed store (`#f32 %M[..] = st`): round to the target width, then write the
            // raw IEEE bytes (an 8087 FST m32real / m64real).
            V = (Bits >= Rhs.Bits) ? CastTo (CastFExt, Rhs, Bits) : CastTo (CastFTrunc, Rhs, Bits);
            V = CastTo (CastFToIBits, V, Bits);
        } else {
            V = Coerce (Rhs, Bits, false);
        }
        m_pE->Store (Use (V), Use (Addr), Bits);
        return;
    }

    case ExprIndex: {
        RegArray CONST *pArr = FindArray (pLhs->Args[0]);
        if (pArr != nullptr) {                       // st[i] = ... : a register-array element
            if (ZeroWiredSlot (*pArr, pLhs->Args[1])) { return; }  // writes to r0 are discarded
            Value Idx  = EvalExpr (pLhs->Args[1]);
            Value Addr = RegBankAddr (*pArr, Idx);
            Value V    = Coerce (Rhs, pArr->Width, false);   // float: widths match, a no-op
            m_pE->Store (Use (V), Use (Addr), pArr->Width);
            ClearRegCache ();    // a runtime-indexed register write: any cached named read may be stale
            return;
        }
        Value Addr = EvalExpr (pLhs->Args[1]);       // r[addr] = ... : the index is a memory address
        Value V = Coerce (Rhs, m_WordBits, false);
        m_pE->Store (Use (V), Use (Addr), m_WordBits);
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
    // A loop-carried local promoted to a scratch register (PromoteLoopLocals): store via the bank
    // so the next iteration and the post-loop read observe the update.  Checked first: a promoted
    // name shadows any same-named operand/register only for the scope it was promoted in, but no
    // such collision exists in practice (locals are macro-internal names like `e`/`ci`).
    auto Sl = m_EnvSlot.find (Name);
    if (Sl != m_EnvSlot.end ()) {
        UINT32 Width = m_EnvSlotWidth[Name];
        Value  Addr  = Bin (BinOr, Const (64, CPU_REGBANK_FLAG), Const (64, (UINT64) Sl->second));
        m_pE->Store (Use (Coerce (Rhs, Width, false)), Use (Addr), Width);
        return;
    }

    auto O = m_Operands.find (Name);
    if (O != m_Operands.end ()) {
        Operand CONST &Op = O->second;
        if (Op.Kind == Operand::Imm) { return; }     // an immediate has no write-back
        if (Op.Kind == Operand::Mem) {               // store to the computed address
            UINT32 Bits = Op.Bits ? Op.Bits : m_WordBits;
            Value Addr = MemAddress (Op);
            Value V = Coerce (Rhs, Bits, false);
            m_pE->Store (Use (V), Use (Addr), Bits);
            return;
        }
        RegPhys CONST &Phys = m_Layout.Phys[Op.RegIndex];
        if (Op.SubWidth != 0 && Op.SubWidth != Phys.Width) {
            Value Merged = Insert (GetReg (Phys.Index, Phys.Width), Rhs, Op.SubLo, Op.SubWidth);
            PutReg (Phys.Index, Use (Merged), Phys.Width);
        } else {
            Value V = Coerce (Rhs, Phys.Width, false);
            PutReg (Phys.Index, Use (V), Phys.Width);
        }
        return;
    }

    if (m_Env.find (Name) != m_Env.end ()) { m_Env[Name] = Rhs; return; }

    auto P = m_Layout.PhysIndex.find (Name);
    if (P != m_Layout.PhysIndex.end ()) {
        RegPhys CONST &Phys = m_Layout.Phys[P->second];
        Value V = Coerce (Rhs, Phys.Width, false);
        PutReg (Phys.Index, Use (V), Phys.Width);
        return;
    }

    RegSub CONST *pSub;
    if (FindSub (Name, &pSub)) {
        RegPhys CONST &Phys = m_Layout.Phys[pSub->Parent];
        // A full-width sub-register (a group positional alias like R6 == sp) covers the whole parent,
        // so write it directly -- no read-modify-write. Only a narrower window needs the get/mask/or.
        if (pSub->Lo == 0 && pSub->Width == Phys.Width) {
            Value V = Coerce (Rhs, Phys.Width, false);
            PutReg (Phys.Index, Use (V), Phys.Width);
            return;
        }
        Value Merged = Insert (GetReg (Phys.Index, Phys.Width), Rhs, pSub->Lo, pSub->Width);
        PutReg (Phys.Index, Use (Merged), Phys.Width);
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
    // @fault ( vector ) is the MMU's translation-fault flavour -- same trap/resume mechanism.
    if (pCall->Name == "trap" || pCall->Name == "fault") {
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
            pSys->EmitSyscall ((UINT32) Vec, Use (Ret));
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

    // Save the loop-local scratch-register state so any promotion inside the macro body
    // (PromoteLoopLocals on a while/for) is rolled back on exit, preventing leaks into the
    // caller's scope. The scratch counter is also saved so slots are freed on return.
    auto SavedEnvSlot      = m_EnvSlot;
    auto SavedEnvSlotWidth = m_EnvSlotWidth;
    UINT32 SavedNextScratch = m_NextScratch;

    Emit (M->Body);

    // The macro body may contain a while/for loop that calls PromoteLoopLocals, which erases
    // "result" from m_Env and spills it to a scratch register in m_EnvSlot. Read "result" from
    // whichever storage it ended up in -- m_Env (not promoted) or the scratch register (promoted).
    Value Result;
    auto R = m_Env.find ("result");
    if (R != m_Env.end ()) {
        Result = R->second;
    } else {
        auto Sl = m_EnvSlot.find ("result");
        if (Sl != m_EnvSlot.end ()) {
            UINT32 Width = m_EnvSlotWidth["result"];
            Value  Addr  = Bin (BinOr, Const (64, CPU_REGBANK_FLAG), Const (64, (UINT64) Sl->second));
            ComPtr<ICpuValue> V; m_pE->Load (Use (Addr), Width, &V);
            Result = Pool (std::move (V), Width);
        } else {
            Result = Const (m_WordBits, 0);
        }
    }

    // Restore env + scratch state: erases all macro-body promoted locals, restoring the caller's
    // scratch allocator state so macros don't leak scratch slots to the outer instruction.
    for (std::string CONST &N : Names) {
        if (Saved.count (N)) { m_Env[N] = Saved[N]; } else { m_Env.erase (N); }
    }
    m_EnvSlot      = SavedEnvSlot;
    m_EnvSlotWidth = SavedEnvSlotWidth;
    m_NextScratch  = SavedNextScratch;
    return Result;
}

} // namespace Upcl
} // namespace LibCPU
