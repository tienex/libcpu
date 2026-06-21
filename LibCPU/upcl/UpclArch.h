/** @file
  A live ICpuArchitecture backed by a UPCL description.

  Instead of generating a frontend, UPCL INTERPRETS its description at run time, so any
  .upcl file becomes a working frontend runnable on any backend (e.g. `lcx run prog.bin
  --arch upcl:cpu.upcl`).

  A standard description -- a register_file with `encode` clauses -- drives the main path:
  the register file is flattened to a RegisterLayout, the generic Decoder matches each
  instruction's bit-field encoding and resolves its operand locations, and the semantics
  Translator walks the instruction body driving the ICpuEmitter (register / sub-register /
  flag access, the expression language, %CC flags, control flow, macros).

  A description that instead uses the experimental `formats { }` + flat `registers` is
  handled by the secondary path here (format-match decode + a simpler assignment walker).

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_UPCL_UPCLARCH_H
#define LIBCPU_UPCL_UPCLARCH_H

#include "LibCPU/ICpu.h"
#include "Ast.h"
#include <string>
#include <vector>

namespace LibCPU {
namespace Upcl {

// Create an ICpuArchitecture that interprets pModule's ArchIndex-th architecture.
// pModule is borrowed -- the caller must keep it alive for the architecture's life.
// pCpu selects a CPU model (`cpu "..."` in the description): only instructions in the
// base ISA or in one of that model's features are decoded/translated. nullptr (or an
// unknown name) enables EVERY declared feature -- the permissive default for development
// and `lcx upcl check`. If pRegNamesOut is non-null it receives the architecture's register
// names in index order (so the caller need not flatten the register file a second time).
// Returns nullptr if ArchIndex is out of range. Release() when done.
ICpuArchitecture *CreateUpclArch (Module *pModule, UINT32 ArchIndex, CHAR8 CONST *pCpu = nullptr,
                                  std::vector<std::string> *pRegNamesOut = nullptr);

} // namespace Upcl
} // namespace LibCPU

#endif // LIBCPU_UPCL_UPCLARCH_H
