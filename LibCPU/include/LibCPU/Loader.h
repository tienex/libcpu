/** @file
  LibCPU bundle loader (part of the framework core). Loads a backend bundle by
  path and returns its ICpuBackend.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_LOADER_H
#define LIBCPU_LOADER_H

#include "LibCPU/ICpu.h"
#include "LibCPU/ILoader.h"

namespace LibCPU {

//
// dlopen the given bundle path and create its backend. Returns an owned
// ICpuBackend (Release when done), or null on failure. The bundle stays loaded
// for the process lifetime.
//
ICpuBackend *LoadBackendBundle (CHAR8 CONST *pPath);

//
// Load a ".loader" bundle by path and create its ILoader (Release when done),
// or null on failure. Like the backend loader, the bundle stays mapped.
//
ILoader *LoadLoaderBundle (CHAR8 CONST *pPath);

//
// Load a ".abi" bundle by path and create its IAbi (Release when done), or null on failure. As
// with the loader/backend bundles, the bundle stays mapped for the process lifetime.
//
class IAbi;
IAbi *LoadAbiBundle (CHAR8 CONST *pPath);

} // namespace LibCPU

#endif // LIBCPU_LOADER_H
