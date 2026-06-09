/** @file
  Python backend for LibCPU: emit Python and run it on python3. The guest RAM and
  register file are marshalled through a temp file (mutated in place via
  memoryviews).

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_PYTHONBACKEND_H
#define LIBCPU_PYTHONBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreatePythonBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_PYTHONBACKEND_H
