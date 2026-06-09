/** @file
  LibCPU bundle loader (part of the framework core). Loads a backend bundle by
  path and returns its ICpuBackend.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_LOADER_H
#define LIBCPU_LOADER_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

//
// dlopen the given bundle path and create its backend. Returns an owned
// ICpuBackend (Release when done), or null on failure. The bundle stays loaded
// for the process lifetime.
//
ICpuBackend *LoadBackendBundle (CHAR8 CONST *pPath);

} // namespace LibCPU

#endif // LIBCPU_LOADER_H
