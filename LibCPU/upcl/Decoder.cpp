/** @file  The UPCL instruction decoder. See Decoder.h. */

#include "Decoder.h"

namespace LibCPU {
namespace Upcl {

Decoder::Decoder (Arch *pArch, RegisterLayout CONST *pLayout, std::set<std::string> CONST *pEnabled)
    : m_pArch (pArch), m_pLayout (pLayout), m_pEnabled (pEnabled)
{
    // Detect a little-endian, fixed-width (word-oriented) instruction set so its words get byte-
    // reversed before the MSB-first field extraction (see m_LeWord in Decoder.h). The signal is a
    // UNIFORM multi-byte encoding width across every instruction: a fixed word ISA (Alpha/MIPS/m88k,
    // all #i32) has exactly one width, whereas a variable-length byte-stream ISA (x86/6502/8080) mixes
    // #i8/#i16/#i24 and must keep its per-field little-endian handling instead.
    UINT32 Common = 0;     // the single width seen so far (0 = none yet)
    bool   Uniform = true;
    auto   Note = [&] (UINT32 Bits) {
        if (Bits == 0) { return; }
        if (Common == 0) { Common = Bits; }
        else if (Common != Bits) { Uniform = false; }
    };
    if (pArch != nullptr) {
        for (Insn *I : pArch->Insns) {
            for (EncAlt *A : I->Encodings) { Note (A->WordBits); }
        }
        for (JumpInsn *J : pArch->Jumps) {
            for (EncAlt *A : J->Encodings) { Note (A->WordBits); }
        }
    }
    if (pArch != nullptr && pArch->Little && Uniform && Common >= 16 && (Common % 8) == 0) {
        m_LeWord  = true;
        m_WordLen = Common / 8;
    }
}

// Extract Width bits starting at bit BitOff from a big-endian bit-stream over the bytes
// (byte 0 bit 7 is bit 0). A byte-aligned, byte-sized multi-byte field on a little-endian
// architecture is byte-swapped, so an x86 little-endian immediate reads correctly while a
// big-endian RISC word's packed fields read straight through.
static UINT64
ExtractField (UINT8 CONST *pBytes, UINT32 BitOff, UINT32 Width, bool Little)
{
    UINT64 Value = 0;
    for (UINT32 I = 0; I < Width; ++I) {
        UINT32 Bit = BitOff + I;
        UINT32 One = (pBytes[Bit / 8] >> (7 - (Bit % 8))) & 1u;
        Value = (Value << 1) | One;
    }
    if (Little && (BitOff % 8) == 0 && (Width % 8) == 0 && Width > 8) {
        UINT32 Bytes = Width / 8;
        UINT64 Swapped = 0;
        for (UINT32 B = 0; B < Bytes; ++B) {
            Swapped |= ((Value >> (8 * B)) & 0xFFu) << (8 * (Bytes - 1 - B));
        }
        Value = Swapped;
    }
    return Value;
}

// Flatten a register map (the names after `-> op[ ... ]`), expanding any regset alias into
// its registers; a name that is not a regset is taken literally.
void
Decoder::ExpandRegMap (std::vector<std::string> CONST &Map, std::vector<std::string> *pOut) CONST
{
    for (std::string CONST &Name : Map) {
        bool IsSet = false;
        for (RegSet *S : m_pArch->RegSets) {
            if (S->Name == Name) {
                for (std::string CONST &R : S->Regs) { pOut->push_back (R); }
                IsSet = true;
                break;
            }
        }
        if (!IsSet) { pOut->push_back (Name); }
    }
}

// Sign-extend Value from Width bits.
static INT64
SignExtend (UINT64 Value, UINT32 Width)
{
    if (Width == 0 || Width >= 64) { return (INT64) Value; }
    UINT64 Sign = UINT64_C (1) << (Width - 1);
    return (INT64) ((Value ^ Sign) - Sign);
}

// Build the operand a field feeds: a PC-relative branch target, a register selected from
// the (expanded) map by the field value, or -- with no map -- the immediate field value.
bool
Decoder::ResolveOperand (EncField CONST &Field, UINT64 FieldVal, UINT64 NextPc, Operand *pOut) CONST
{
    if (Field.Relative) {
        // The operand is the branch target: this instruction's successor plus the signed
        // displacement. (Under segmentation a near relative target's segment base cancels.)
        UINT32 Bits = m_pArch->AddressSize ? m_pArch->AddressSize : 16;
        pOut->Kind     = Operand::Imm;
        pOut->Bits     = Bits;
        pOut->ImmValue = (UINT64) ((INT64) NextPc + SignExtend (FieldVal, Field.Width));
        return true;
    }

    if (Field.HasImplicitImm) {                      // an implicit operand: a fixed immediate
        pOut->Kind = Operand::Imm;
        pOut->Bits = 8;
        pOut->ImmValue = Field.ImplicitImm;
        return true;
    }

    if (Field.RegMap.empty ()) {
        pOut->Kind = Operand::Imm;
        if (Field.SignExt) {
            // A short signed immediate widened to the machine word (e.g. 0x83's imm8 -> 16 bits).
            UINT32 Word = m_pArch->WordSize ? m_pArch->WordSize : Field.Width;
            UINT64 Mask = (Word >= 64) ? ~UINT64_C (0) : ((UINT64_C (1) << Word) - 1);
            pOut->Bits     = Word;
            pOut->ImmValue = (UINT64) SignExtend (FieldVal, Field.Width) & Mask;
        } else {
            pOut->Bits     = Field.Width;
            pOut->ImmValue = FieldVal;
        }
        return true;
    }

    std::vector<std::string> Regs;
    ExpandRegMap (Field.RegMap, &Regs);
    if (FieldVal >= Regs.size ()) { return false; }
    std::string CONST &Name = Regs[(size_t) FieldVal];

    auto P = m_pLayout->PhysIndex.find (Name);
    if (P != m_pLayout->PhysIndex.end ()) {
        pOut->Kind     = Operand::Reg;
        pOut->RegIndex = P->second;
        pOut->Bits     = m_pLayout->Phys[P->second].Width;
        pOut->SubWidth = 0;
        pOut->RegName  = Name;
        return true;
    }
    for (RegSub CONST &Sub : m_pLayout->Subs) {
        if (Sub.Name == Name) {
            pOut->Kind     = Operand::Reg;
            pOut->RegIndex = Sub.Parent;
            pOut->Bits     = Sub.Width;
            pOut->SubLo    = Sub.Lo;
            pOut->SubWidth = Sub.Width;
            pOut->RegName  = Name;
            return true;
        }
    }
    return false;                                    // a map naming an unknown register
}

// Resolve a register name to a register operand (a physical register or a sub-register). When Bits is
// nonzero and narrower than the register, the operand is the register's low Bits (a sub-view) -- the
// register-direct byte modes read/write the low byte of a 16-bit register without a named sub-register.
bool
Decoder::ResolveRegName (std::string CONST &Name, UINT32 Bits, Operand *pOut) CONST
{
    auto P = m_pLayout->PhysIndex.find (Name);
    if (P != m_pLayout->PhysIndex.end ()) {
        UINT32 Width = m_pLayout->Phys[P->second].Width;
        pOut->Kind = Operand::Reg; pOut->RegIndex = P->second; pOut->RegName = Name;
        if (Bits != 0 && Bits < Width) {
            pOut->Bits = Bits; pOut->SubLo = 0; pOut->SubWidth = Bits;
        } else {
            pOut->Bits = Width; pOut->SubWidth = 0;
        }
        return true;
    }
    for (RegSub CONST &Sub : m_pLayout->Subs) {
        if (Sub.Name == Name) {
            pOut->Kind = Operand::Reg; pOut->RegIndex = Sub.Parent; pOut->RegName = Name;
            if (Bits != 0 && Bits < Sub.Width) {
                pOut->Bits = Bits; pOut->SubLo = Sub.Lo; pOut->SubWidth = Bits;
            } else {
                pOut->Bits = Sub.Width; pOut->SubLo = Sub.Lo; pOut->SubWidth = Sub.Width;
            }
            return true;
        }
    }
    return false;
}

AddrMode *
Decoder::FindAddrMode (std::string CONST &Name) CONST
{
    for (AddrMode *A : m_pArch->AddrModes) { if (A->Name == Name) { return A; } }
    return nullptr;
}

// The data width of an addressing mode's operands -- the width of the registers its
// register-direct rule selects (so modrm16 is 16-bit, modrm8 is 8-bit).
UINT32
Decoder::AddrModeBits (AddrMode CONST *pMode) CONST
{
    for (AddrRule *R : pMode->Rules) {
        if (R->IsReg && !R->RegMap.empty ()) {
            std::vector<std::string> Regs;
            ExpandRegMap (R->RegMap, &Regs);
            Operand Tmp;
            if (!Regs.empty () && ResolveRegName (Regs[0], 0, &Tmp)) { return Tmp.Bits; }
        }
    }
    return m_pArch->WordSize ? m_pArch->WordSize : 16;
}

// Evaluate an addrmode condition / disp-size expression over the decoded field values.
UINT64
Decoder::EvalFieldExpr (Expr *E, std::map<std::string, UINT64> CONST &F) CONST
{
    switch (E->Kind) {
    case ExprInt:  return E->Int;
    case ExprName: { auto It = F.find (E->Name); return (It != F.end ()) ? It->second : 0; }
    case ExprUnary: {
        UINT64 A = EvalFieldExpr (E->Args[0], F);
        return (E->Op == TokTilde) ? ~A : (E->Op == TokNot) ? (UINT64) (!A)
             : (E->Op == TokMinus) ? (UINT64) (-(INT64) A) : A;
    }
    case ExprSelect:                                  // cond ? a : b
        return EvalFieldExpr (E->Args[0], F) ? EvalFieldExpr (E->Args[1], F) : EvalFieldExpr (E->Args[2], F);
    case ExprBinary: {
        UINT64 A = EvalFieldExpr (E->Args[0], F), B = EvalFieldExpr (E->Args[1], F);
        switch (E->Op) {
        case TokPlus:  return A + B;  case TokMinus: return A - B;  case TokStar: return A * B;
        case TokAmp:   return A & B;  case TokPipe:  return A | B;  case TokCaret: return A ^ B;
        case TokShl:   return A << B; case TokShr:   return A >> B;
        case TokEqEq:  return A == B; case TokNotEq: return A != B;
        case TokLt:    return A < B;  case TokLtEq:  return A <= B;
        case TokGt:    return A > B;  case TokGtEq:  return A >= B;
        case TokAndAnd:return A && B; case TokOrOr:  return A || B;
        default:       return 0;
        }
    }
    default: return 0;
    }
}

// Resolve an addressing-mode operand: pick the rule whose condition holds, then produce a
// register operand or a memory operand (reading a variable-length displacement from the
// tail and reporting the bytes consumed).
bool
Decoder::ResolveAddrMode (EncField CONST &Field, std::map<std::string, UINT64> CONST &Fields,
                          UINT8 CONST *pTail, UINT64 TailAvail, UINT32 *pExtraBytes, Operand *pOut) CONST
{
    AddrMode *AM = FindAddrMode (Field.AddrMode);
    if (AM == nullptr) { return false; }
    UINT64 Sel = Fields.count (Field.Name) ? Fields.at (Field.Name) : 0;
    UINT32 DataBits = AddrModeBits (AM);

    for (AddrRule *R : AM->Rules) {
        if (R->Cond != nullptr && EvalFieldExpr (R->Cond, Fields) == 0) { continue; }
        // A rule may pin its own operand width (the .B byte forms); otherwise the addrmode default.
        UINT32 RuleBits = R->DataBits ? R->DataBits : DataBits;
        // Expose the addrmode's selector fields (e.g. dm, dr) so a pre/post block can name the very
        // register the mode picked, via %REG[group, field] -- one field-indexed rule for all registers.
        for (std::string CONST &P : AM->Params) {
            auto F = Fields.find (P);
            if (F != Fields.end ()) { pOut->Fields[P] = F->second; }
        }
        if (R->IsReg) {
            std::vector<std::string> Regs;
            ExpandRegMap (R->RegMap, &Regs);
            if (Sel >= Regs.size ()) { return false; }
            return ResolveRegName (Regs[(size_t) Sel], RuleBits, pOut);
        }
        // memory: base registers + an optional displacement
        pOut->Kind = Operand::Mem;
        pOut->Bits = RuleBits;
        pOut->Base1 = ~(UINT32) 0; pOut->Base2 = ~(UINT32) 0; pOut->Disp = 0;
        std::string Text;
        UINT32 NBases = 0;
        for (AddrTerm CONST &T : R->Mem) {
            if (T.Disp) {
                UINT32 DispBits = T.DispBits ? T.DispBits
                                : (UINT32) (AM->DispSize ? EvalFieldExpr (AM->DispSize, Fields) : 0);
                if (DispBits > 0) {
                    UINT32 DispBytes = DispBits / 8;
                    if (DispBytes > TailAvail) { return false; }
                    bool DispLittle = T.DispBigEndian ? false : m_pArch->Little;
                    pOut->Disp = SignExtend (ExtractField (pTail, 0, DispBits, DispLittle), DispBits);
                    *pExtraBytes += DispBytes;
                    if (!Text.empty ()) { Text += "+"; }
                    char B[16]; std::snprintf (B, sizeof (B), "0x%llx", (unsigned long long) pOut->Disp);
                    Text += B;
                }
            } else if (!T.RegGroup.empty ()) {
                // %REG[group, field]: the base is group <group>'s element selected by field <field>.
                // Resolve via the group's positional alias (<group><N>), then fall back to a direct
                // physical name -- yielding the base register's physical index.
                UINT64 Idx = Fields.count (T.RegField) ? Fields.at (T.RegField) : 0;
                std::string Name = T.RegGroup + std::to_string ((unsigned long long) Idx);
                UINT32 Phys = ~(UINT32) 0;
                auto P = m_pLayout->PhysIndex.find (Name);
                if (P != m_pLayout->PhysIndex.end ()) { Phys = P->second; }
                else { for (RegSub CONST &S : m_pLayout->Subs) { if (S.Name == Name) { Phys = S.Parent; break; } } }
                if (Phys != ~(UINT32) 0) {
                    if (NBases == 0) { pOut->Base1 = Phys; NBases++; }
                    else if (NBases == 1) { pOut->Base2 = Phys; NBases++; }
                    if (!Text.empty ()) { Text += "+"; }
                    Text += Name;
                }
            } else {
                auto P = m_pLayout->PhysIndex.find (T.Reg);
                if (P != m_pLayout->PhysIndex.end ()) {
                    if (NBases == 0) { pOut->Base1 = P->second; NBases++; }
                    else if (NBases == 1) { pOut->Base2 = P->second; NBases++; }
                    if (!Text.empty ()) { Text += "+"; }
                    Text += T.Reg;
                }
            }
        }
        pOut->MemText = Text;
        // Autoincrement/decrement side-effect blocks: the translator runs pre { } before the EA and
        // post { } after the operand is used (see Translator::BindOperand / Emit).
        if (!R->Pre.empty ())  { pOut->Pre  = &R->Pre; }
        if (!R->Post.empty ()) { pOut->Post = &R->Post; }
        return true;
    }
    return false;
}

bool
Decoder::MatchAlt (EncAlt *pAlt, UINT8 CONST *pBytes, UINT64 Avail, UINT64 NextBase, DecodedInsn *pOut) CONST
{
    UINT32 Bits    = pAlt->WordBits;
    UINT32 WordLen = (Bits + 7) / 8;
    if (WordLen == 0 || WordLen > Avail) { return false; }

    // A little-endian fixed-width word (e.g. Alpha) is a little-endian integer in memory; byte-
    // reverse it so the MSB-first extractor reads the logical encoding, and then extract straight
    // (no per-field swap -- that is for the byte-stream CISC path). Other arches read in place.
    UINT8        Rev[16];
    UINT8 CONST *pWord  = pBytes;
    bool         Little = m_pArch->Little;
    if (m_LeWord && WordLen == m_WordLen && WordLen <= sizeof (Rev)) {
        for (UINT32 I = 0; I < WordLen; ++I) { Rev[I] = pBytes[WordLen - 1 - I]; }
        pWord  = Rev;
        Little = false;
    }

    // Extract the fixed opcode-word fields, checking the constant (opcode) fields. Tail fields
    // (immediates after a variable-length addressing mode) are NOT in the word -- read below.
    std::map<std::string, UINT64> FV;
    UINT32 BitOff = 0;
    for (EncField CONST &F : pAlt->Fields) {
        if (F.Tail) { continue; }
        UINT64 V = ExtractField (pWord, BitOff, F.Width, F.BigEndian ? false : Little);
        if (F.HasConst && V != F.Const) { return false; }
        FV[F.Name] = V;
        BitOff += F.Width;
    }

    pOut->Operands.clear ();
    UINT32 Extra = 0;

    // Addressing-mode operands first: they read the trailing displacement (variable length).
    for (EncField CONST &F : pAlt->Fields) {
        if (!F.Operand.empty () && !F.AddrMode.empty ()) {
            Operand Op;
            if (!ResolveAddrMode (F, FV, pBytes + WordLen + Extra, Avail - WordLen - Extra, &Extra, &Op)) { return false; }
            pOut->Operands[F.Operand] = Op;
        }
    }

    // Tail fields follow the displacement, byte-aligned and in declaration order (e.g. the
    // immediate of `0x81 /digit iw`). Each consumes its width from the tail and extends Length.
    UINT32 TailOff = WordLen + Extra;
    for (EncField CONST &F : pAlt->Fields) {
        if (!F.Tail) { continue; }
        UINT32 FieldBytes = (F.Width + 7) / 8;
        if ((UINT64) TailOff + FieldBytes > Avail) { return false; }
        UINT64 V = ExtractField (pBytes + TailOff, 0, F.Width, F.BigEndian ? false : m_pArch->Little);
        if (F.HasConst && V != F.Const) { return false; }
        FV[F.Name] = V;
        TailOff += FieldBytes;
    }

    UINT64 NextPc = NextBase + TailOff;               // the successor address (for PC-relative fields)
    for (EncField CONST &F : pAlt->Fields) {
        if (!F.Operand.empty () && F.AddrMode.empty ()) {
            Operand Op;
            if (!ResolveOperand (F, FV[F.Name], NextPc, &Op)) { return false; }
            pOut->Operands[F.Operand] = Op;
        }
    }

    pOut->pAlt   = pAlt;
    pOut->Length = TailOff;
    return true;
}

// How many bits an encoding fixes to constants -- its specificity, for tie-breaking when
// several encodings match the same bytes (the more constrained, the more specific).
static UINT32
ConstBits (EncAlt CONST *pAlt)
{
    UINT32 N = 0;
    for (EncField CONST &F : pAlt->Fields) { if (F.HasConst) { N += F.Width; } }
    return N;
}

bool
Decoder::Decode (UINT8 CONST *pBytes, UINT64 Len, UINT64 Pos, DecodedInsn *pOut) CONST
{
    if (Pos >= Len) { return false; }
    UINT8 CONST *p = pBytes + Pos;
    UINT64 Avail = Len - Pos;

    // Among all encodings that match, prefer the MOST SPECIFIC -- the one constraining the
    // most bits to constants -- so a full-opcode instruction (8080 HLT = 0x76) wins over a
    // general pattern that also matches (MOV r,r covers 0x40..0x7F). Declaration order does
    // not matter.
    DecodedInsn Try;
    INT32       BestSpec = -1;
    for (Insn *I : m_pArch->Insns) {
        if (!IsEnabled (I->Feature)) { continue; }
        for (EncAlt *A : I->Encodings) {
            if (MatchAlt (A, p, Avail, Pos, &Try)) {
                INT32 Spec = (INT32) ConstBits (A);
                if (Spec > BestSpec) { BestSpec = Spec; *pOut = Try; pOut->pInsn = I; pOut->pJump = nullptr; }
            }
        }
    }
    for (JumpInsn *J : m_pArch->Jumps) {
        if (!IsEnabled (J->Feature)) { continue; }
        for (EncAlt *A : J->Encodings) {
            if (MatchAlt (A, p, Avail, Pos, &Try)) {
                INT32 Spec = (INT32) ConstBits (A);
                if (Spec > BestSpec) { BestSpec = Spec; *pOut = Try; pOut->pInsn = nullptr; pOut->pJump = J; }
            }
        }
    }
    return BestSpec >= 0;
}

} // namespace Upcl
} // namespace LibCPU
