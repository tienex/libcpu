/** @file
  In-process CPython backend for LibCPU: emit Python source, compile it once with
  Py_CompileString into a code object, and run it on an embedded interpreter with
  PyEval_EvalCode -- no python subprocess. The guest state is a bytearray the code
  mutates in place via memoryviews.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_CPYTHONBACKEND_H
#define LIBCPU_CPYTHONBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateCPythonBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_CPYTHONBACKEND_H
