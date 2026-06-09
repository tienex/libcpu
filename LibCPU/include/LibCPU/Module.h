/** @file
  LibCPU loadable-module entry contract. Each backend/frontend bundle exports one
  C symbol; the loader resolves it to obtain the backend object. See
  docs/dynamic-modules.md.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_MODULE_H
#define LIBCPU_MODULE_H

#include "LibCPU/ICpu.h"

#if defined(_WIN32)
#define LIBCPU_EXPORT __declspec(dllexport)
#else
#define LIBCPU_EXPORT __attribute__((visibility("default")))
#endif

//
// The single exported entry of a backend bundle: returns a new, owned
// ICpuBackend (Release when done). Define it in a small per-bundle shim:
//
//     LIBCPU_MODULE_CREATE_BACKEND (LibCPU::CreateInterpreterBackend)
//
#define LIBCPU_MODULE_CREATE_BACKEND(Creator) \
    extern "C" LIBCPU_EXPORT ::LibCPU::ICpuBackend *LibCPUModuleCreateBackend (void) { return (Creator) (); }

#define LIBCPU_MODULE_ENTRY_NAME "LibCPUModuleCreateBackend"

#endif // LIBCPU_MODULE_H
