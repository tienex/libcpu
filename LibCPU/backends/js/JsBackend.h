/** @file
  JavaScript backend for LibCPU: emit JS for instruction semantics and run it on
  any available JS engine (node, qjs, js/SpiderMonkey, ...).

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_JSBACKEND_H
#define LIBCPU_JSBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateJsBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_JSBACKEND_H
