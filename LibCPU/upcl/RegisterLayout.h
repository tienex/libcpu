/** @file
  The concrete register layout derived from a UPCL `register_file`.

  The .def register_file is declarative: groups of registers, some split into named
  sub-registers (`ax -> #i8 (ah:al)`), some carrying named flag bits (`flags -> %PSR #i1
  ( ... O->%V ... )`), some bound to the meta program counter / status word, some repeated
  (`8 ** #f80 st?`). BuildRegisterLayout flattens that into:

    * a list of PHYSICAL registers (the storage), each with an index + width,
    * the SUB-REGISTERS, each an aliased bit field of a physical register (ah = ax[15:8]),
    * the FLAG bits, each a single bit of a physical register (with its %meta mapping).

  Both the interpreter (drive ICpuEmitter) and the code generator consume this so the
  register model is computed once, from the description, in one place.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_UPCL_REGISTERLAYOUT_H
#define LIBCPU_UPCL_REGISTERLAYOUT_H

#include "Ast.h"
#include <map>
#include <string>
#include <vector>

namespace LibCPU {
namespace Upcl {

// A physical register: the actual storage. The index is its slot in the register file.
class RegPhys {
public:
    std::string Name;
    UINT32      Index = 0;
    UINT32      Width = 0;       // bits
    bool        IsPc  = false;   // bound to the meta PC
    bool        IsPsr = false;   // bound to the meta PSR (the flags word)
    bool        Float = false;   // a floating-point register (#f<width>, e.g. an 8087 st)
};

// A sub-register: a contiguous bit field of a physical register (e.g. ah = ax bits [15:8]).
class RegSub {
public:
    std::string Name;
    UINT32      Parent = 0;      // RegPhys index
    UINT32      Lo     = 0;      // lowest bit
    UINT32      Width  = 0;      // bits  -> field is [Lo+Width-1 : Lo]
};

// A flag bit: a single named bit of a physical register, with its optional %meta role.
class RegFlag {
public:
    std::string Name;
    UINT32      Parent = 0;      // RegPhys index
    UINT32      Bit    = 0;
    std::string Meta;           // the `-> %X` mapping (e.g. "V" for overflow), "" if none
};

class RegisterLayout {
public:
    std::vector<RegPhys> Phys;
    std::vector<RegSub>  Subs;
    std::vector<RegFlag> Flags;
    std::map<std::string, UINT32> PhysIndex;   // name -> Phys index (fast lookup)

    // The program counter's composition fields -> their source register. From the PC's
    // `evaluate (...) ( seg <- cs : off <-> ip )` binding: PcFields["off"] == "ip",
    // PcFields["seg"] == "cs". So `pc.off` resolves to the offset register (ip).
    std::map<std::string, std::string> PcFields;

    // The physical register holding the program counter (or ~0 if none).
    UINT32 PcIndex () CONST {
        for (RegPhys CONST &P : Phys) { if (P.IsPc) { return P.Index; } }
        return ~(UINT32) 0;
    }

    // The program counter's register name ("" if none).
    std::string PcName () CONST {
        UINT32 I = PcIndex ();
        return (I != ~(UINT32) 0) ? Phys[I].Name : std::string ();
    }
};

// Flatten pArch's register_file into a RegisterLayout. Empty if pArch has no register_file
// (a new-syntax description that used the flat `registers` list instead).
RegisterLayout BuildRegisterLayout (Arch *pArch);

} // namespace Upcl
} // namespace LibCPU

#endif // LIBCPU_UPCL_REGISTERLAYOUT_H
