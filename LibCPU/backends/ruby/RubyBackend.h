/** @file
  Ruby backend for LibCPU: emit Ruby, run it on the ruby interpreter. Guest state
  is marshalled through a temp file.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/
#ifndef LIBCPU_RUBYBACKEND_H
#define LIBCPU_RUBYBACKEND_H
#include "LibCPU/ICpu.h"
namespace LibCPU { ICpuBackend *CreateRubyBackend (VOID); }
#endif
