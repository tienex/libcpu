/** @file
  Windows cmd.exe backend for LibCPU (experimental). Emits a batch (.cmd) script
  and runs it on cmd.exe under wine. The guest state is marshalled as a text file
  of decimal bytes (cmd cannot do raw binary I/O), loaded into a "d<N>" pseudo
  array, processed with "set /a" (which provides +,-,*,/,%,&,|,^,<<,>>,~) and IF
  numeric comparisons, then written back as text.

  cmd's "set /a" cannot index an array by a runtime value, so memory addresses are
  constant-folded; this works for frontends whose load/store addresses are
  compile-time constants (e.g. the 6502 slice). The RAM window is small (cmd is
  slow under wine), large enough for the test programs.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_CMDBACKEND_H
#define LIBCPU_CMDBACKEND_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

ICpuBackend *CreateCmdBackend (VOID);

} // namespace LibCPU

#endif // LIBCPU_CMDBACKEND_H
