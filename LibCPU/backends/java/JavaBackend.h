/** @file
  Java backend for LibCPU: emit Java source, compile to JVM bytecode (JBC) with
  javac, and run it on the JVM. The guest RAM and register file are marshalled
  through a temp file.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_JAVABACKEND_H
#define LIBCPU_JAVABACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateJavaBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_JAVABACKEND_H
