/** @file
  A live ICpuArchitecture backed by a UPCL description.

  Instead of generating a frontend, UPCL INTERPRETS its description at run time: the
  returned architecture decodes each instruction by matching the formats + opcode
  bindings, disassembles by filling the format string with the decoded fields, and
  translates by walking the instruction's semantics statements and driving the
  ICpuEmitter (reg[i] = expr -> GetRegister/BinaryOp/PutRegister, mem[a] = ... ->
  Store, ...). So any .upcl file becomes a working frontend for the new framework,
  runnable on any backend -- e.g. `lcx upcl run cpu.upcl prog.bin`.

  This first runtime covers straight-line and constant-target-branch semantics
  (register/memory assignment, pc = <const>); conditional and computed control flow
  is the next increment.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_UPCL_UPCLARCH_H
#define LIBCPU_UPCL_UPCLARCH_H

#include "LibCPU/ICpu.h"
#include "Ast.h"

namespace LibCPU {
namespace Upcl {

// Create an ICpuArchitecture that interprets pModule's ArchIndex-th architecture.
// pModule is borrowed -- the caller must keep it alive for the architecture's life.
// pCpu selects a CPU model (`cpu "..."` in the description): only instructions in the
// base ISA or in one of that model's features are decoded/translated. nullptr (or an
// unknown name) enables EVERY declared feature -- the permissive default for development
// and `lcx upcl check`. Returns nullptr if ArchIndex is out of range. Release() when done.
ICpuArchitecture *CreateUpclArch (Module *pModule, UINT32 ArchIndex, CHAR8 CONST *pCpu = nullptr);

} // namespace Upcl
} // namespace LibCPU

#endif // LIBCPU_UPCL_UPCLARCH_H
