/** @file
  In-process JVM backend for LibCPU: generate JVM bytecode (JBC) by hand, define
  the class through JNI (DefineClass -- no javac), and invoke it on an embedded
  JVM. The guest RAM and register file are passed as byte[] arrays.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_JVMBACKEND_H
#define LIBCPU_JVMBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateJvmBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_JVMBACKEND_H
