/** @file
  UPCL -> C++ frontend generator.

  Where UpclArch interprets a UPCL description at run time, this AOT sibling emits the
  equivalent C++: a self-contained ICpuArchitecture source that decodes, disassembles,
  tags and translates one guest instruction with no interpreter in the loop. The decode
  is a specialised dispatch; the per-instruction translation is produced by driving the
  SAME proven Semantics::Translator against a printing ICpuEmitter, so the generated
  emitter-call sequence is behaviourally identical to the interpreter by construction.

  The generated source is compiled into a frontend, replacing the hand-written ones.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_UPCL_CPPGEN_H
#define LIBCPU_UPCL_CPPGEN_H

#include "Ast.h"
#include "LibCPU/Base.h"
#include <string>

namespace LibCPU {
namespace Upcl {

// Generate the C++ frontend source for architecture ArchIndex of pModule, with the CPU
// model pCpu selecting the enabled ISA features (nullptr = enable every feature). The
// emitted factory function is named CreateName (e.g. "Create6502"). Returns true and
// fills *pOut with the source on success; false (with *pOut a diagnostic) on failure.
bool GenerateCpp (Module *pModule, UINT32 ArchIndex, CHAR8 CONST *pCpu,
                  std::string CONST &CreateName, std::string *pOut);

} // namespace Upcl
} // namespace LibCPU

#endif // LIBCPU_UPCL_CPPGEN_H
