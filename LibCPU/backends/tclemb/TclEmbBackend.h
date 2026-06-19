/** @file
  Embedded-Tcl backend for LibCPU. The in-process sibling of the `tcl` backend: it emits the
  SAME Tcl script, but runs it through a libtcl interpreter linked into the process
  (Tcl_CreateInterp / Tcl_EvalFile) instead of spawning tclsh -- the library-embedding
  alternative (as jscore is to js, cpython is to python). The spawning `tcl` backend is kept.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_TCLEMBBACKEND_H
#define LIBCPU_TCLEMBBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateTclEmbBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_TCLEMBBACKEND_H
