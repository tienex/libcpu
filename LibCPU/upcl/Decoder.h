/** @file
  The UPCL instruction decoder -- architecture-neutral, driven by `encode` bit-fields.

  For a run of bytes it finds the instruction whose encoding matches: every constant field
  equals the bits at its position, and the operand fields resolve to operand locations (a
  register selected from a regset, or an immediate value). The field extraction is generic
  -- a big-endian bit-stream over the address-ordered bytes, with a byte-swap for byte-
  aligned multi-byte fields on a little-endian architecture -- so it serves a 32-bit big-
  endian RISC word and a little-endian x86 form alike, with no ISA assumptions baked in.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_UPCL_DECODER_H
#define LIBCPU_UPCL_DECODER_H

#include "Ast.h"
#include "RegisterLayout.h"
#include "Semantics.h"
#include <map>
#include <set>
#include <string>

namespace LibCPU {
namespace Upcl {

// A successfully decoded instruction: which insn/encoding matched, its byte length, and
// the resolved operand locations keyed by decoder-operand name (dst, src, ...). Exactly one
// of pInsn / pJump is set -- a jump insn carries its branch type, condition and action.
class DecodedInsn {
public:
    Insn     *pInsn  = nullptr;
    JumpInsn *pJump  = nullptr;
    EncAlt   *pAlt   = nullptr;
    UINT32    Length = 0;
    std::map<std::string, Operand> Operands;
};

class Decoder {
public:
    // pEnabled is the set of enabled ISA-feature names (from the selected CPU model); an
    // instruction gated on a feature not in the set is skipped. nullptr enables everything.
    Decoder (Arch *pArch, RegisterLayout CONST *pLayout, std::set<std::string> CONST *pEnabled = nullptr);

    // Decode the instruction at pBytes[Pos]. Returns true and fills pOut on a match; false
    // if no encoding matches (or the bytes run short). pEnabled, if set, restricts the
    // search to that instruction set (the selected CPU model's enabled instructions).
    bool Decode (UINT8 CONST *pBytes, UINT64 Len, UINT64 Pos, DecodedInsn *pOut) CONST;

    // Decode the instruction at WORD offset WordPos of a WORD-ADDRESSED machine: pWords is an
    // array of WordCount machine words, each a `word_size`-bit value (the smallest addressable
    // unit IS the word -- PDP-10/PDP-6). Field extraction reads MSB-first bits straight from the
    // word value; the reported Length is a WORD count. Only meaningful when the arch is declared
    // word-addressed (Arch::WordAddressed); the byte Decode above is untouched for every byte ISA.
    bool DecodeWord (UINT64 CONST *pWords, UINT64 WordCount, UINT64 WordPos, DecodedInsn *pOut) CONST;

    // True when this decoder's arch is word-addressed (mirrors Arch::WordAddressed); the byte
    // path is selected for every byte-addressed ISA.
    bool IsWordAddressed () CONST { return m_pArch != nullptr && m_pArch->WordAddressed; }

private:
    bool MatchAlt (EncAlt *pAlt, UINT8 CONST *pBytes, UINT64 Avail, UINT64 NextBase, DecodedInsn *pOut) CONST;
    bool MatchAltWord (EncAlt *pAlt, UINT64 Word, DecodedInsn *pOut) CONST;
    bool ResolveOperand (EncField CONST &Field, UINT64 FieldVal, UINT64 NextPc, Operand *pOut) CONST;
    bool ResolveAddrMode (EncField CONST &Field, std::map<std::string, UINT64> CONST &Fields,
                          UINT8 CONST *pTail, UINT64 TailAvail, UINT32 *pExtraBytes, Operand *pOut) CONST;
    bool ResolveRegName (std::string CONST &Name, UINT32 Bits, Operand *pOut) CONST;
    void ExpandRegMap (std::vector<std::string> CONST &Map, std::vector<std::string> *pOut) CONST;
    AddrMode *FindAddrMode (std::string CONST &Name) CONST;
    UINT32 AddrModeBits (AddrMode CONST *pMode) CONST;   // the operand data width
    UINT64 EvalFieldExpr (Expr *pExpr, std::map<std::string, UINT64> CONST &Fields) CONST;

    bool IsEnabled (std::string CONST &Feature) CONST {
        return Feature.empty () || m_pEnabled == nullptr || m_pEnabled->count (Feature) != 0;
    }

    // The size of one addressable unit in bits -- `byte_size` (8 on every byte-addressed ISA, 36 on
    // a PDP-10). Immediate / displacement byte counts are bits / Unit, so a word-addressed machine
    // measures them in words; guarded to 8 when the arch left byte_size unset (the prior default).
    UINT32 Unit () CONST {
        UINT32 B = (m_pArch != nullptr) ? m_pArch->ByteSize : 0;
        return B != 0 ? B : 8;
    }

    Arch                          *m_pArch;
    RegisterLayout CONST          *m_pLayout;
    std::set<std::string> CONST   *m_pEnabled;

    // A little-endian, fixed-width (word-oriented) instruction set -- e.g. DEC Alpha -- stores each
    // instruction as a little-endian integer of m_WordLen bytes. The field extractor reads bits
    // MSB-first (correct for big-endian RISC and for the byte-stream of a CISC like x86), so such a
    // word must be byte-reversed before extraction. Detected (in the constructor) as: the arch is
    // little-endian AND every encoding is the SAME multi-byte width -- which distinguishes a fixed
    // word ISA from a variable-length byte-stream ISA (x86/6502/8080 mix #i8/#i16/#i24).
    bool                           m_LeWord  = false;
    UINT32                         m_WordLen = 0;        // the fixed instruction width in bytes
};

} // namespace Upcl
} // namespace LibCPU

#endif // LIBCPU_UPCL_DECODER_H
