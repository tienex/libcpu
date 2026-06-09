/** @file
  WebAssembly backend for LibCPU: emit a WASM module whose insn() function calls
  host-imported accessors for the guest RAM and register file, then run it
  in-process on the wasm3 interpreter.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_WASMBACKEND_H
#define LIBCPU_WASMBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateWasmBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_WASMBACKEND_H
