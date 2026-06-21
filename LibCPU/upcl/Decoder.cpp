/** @file  The UPCL instruction decoder. See Decoder.h. */

#include "Decoder.h"

namespace LibCPU {
namespace Upcl {

Decoder::Decoder (Arch *pArch, RegisterLayout CONST *pLayout, std::set<std::string> CONST *pEnabled)
    : m_pArch (pArch), m_pLayout (pLayout), m_pEnabled (pEnabled)
{
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

    if (Field.RegMap.empty ()) {
        pOut->Kind     = Operand::Imm;
        pOut->Bits     = Field.Width;
        pOut->ImmValue = FieldVal;
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

bool
Decoder::MatchAlt (EncAlt *pAlt, UINT8 CONST *pBytes, UINT64 Avail, UINT64 NextBase, DecodedInsn *pOut) CONST
{
    UINT32 Bits = pAlt->WordBits;
    UINT32 Len  = (Bits + 7) / 8;
    if (Len == 0 || Len > Avail) { return false; }
    UINT64 NextPc = NextBase + Len;             // the successor address (for PC-relative fields)

    // First pass: every constant field must match. Second pass: resolve operands.
    UINT32 BitOff = 0;
    for (EncField CONST &F : pAlt->Fields) {
        if (F.HasConst) {
            UINT64 V = ExtractField (pBytes, BitOff, F.Width, m_pArch->Little);
            if (V != F.Const) { return false; }
        }
        BitOff += F.Width;
    }

    pOut->Operands.clear ();
    BitOff = 0;
    for (EncField CONST &F : pAlt->Fields) {
        if (!F.Operand.empty ()) {
            UINT64 V = ExtractField (pBytes, BitOff, F.Width, m_pArch->Little);
            Operand Op;
            if (!ResolveOperand (F, V, NextPc, &Op)) { return false; }
            pOut->Operands[F.Operand] = Op;
        }
        BitOff += F.Width;
    }

    pOut->pAlt   = pAlt;
    pOut->Length = Len;
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
