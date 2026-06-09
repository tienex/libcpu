/** @file
  In-process JavaScriptCore backend for LibCPU: emit JS for the instruction, then
  run it inside the process on Apple's JavaScriptCore engine, with the guest RAM
  and register file exposed to JS as NO-COPY typed arrays over the native buffers
  (no marshalling, no subprocess).

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_JSCOREBACKEND_H
#define LIBCPU_JSCOREBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateJscoreBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_JSCOREBACKEND_H
