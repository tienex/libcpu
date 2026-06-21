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
#include <string>

namespace LibCPU {
namespace Upcl {

// A successfully decoded instruction: which insn/encoding matched, its byte length, and
// the resolved operand locations keyed by decoder-operand name (dst, src, ...).
class DecodedInsn {
public:
    Insn   *pInsn  = nullptr;
    EncAlt *pAlt   = nullptr;
    UINT32  Length = 0;
    std::map<std::string, Operand> Operands;
};

class Decoder {
public:
    Decoder (Arch *pArch, RegisterLayout CONST *pLayout);

    // Decode the instruction at pBytes[Pos]. Returns true and fills pOut on a match; false
    // if no encoding matches (or the bytes run short). pEnabled, if set, restricts the
    // search to that instruction set (the selected CPU model's enabled instructions).
    bool Decode (UINT8 CONST *pBytes, UINT64 Len, UINT64 Pos, DecodedInsn *pOut) CONST;

private:
    bool MatchAlt (EncAlt *pAlt, UINT8 CONST *pBytes, UINT64 Avail, DecodedInsn *pOut) CONST;
    bool ResolveOperand (EncField CONST &Field, UINT64 FieldVal, Operand *pOut) CONST;
    void ExpandRegMap (std::vector<std::string> CONST &Map, std::vector<std::string> *pOut) CONST;

    Arch                 *m_pArch;
    RegisterLayout CONST *m_pLayout;
};

} // namespace Upcl
} // namespace LibCPU

#endif // LIBCPU_UPCL_DECODER_H
