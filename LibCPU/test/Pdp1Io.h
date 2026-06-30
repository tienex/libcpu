#ifndef LIBCPU_TEST_PDP1IO_H
#define LIBCPU_TEST_PDP1IO_H

#include "LibCPU/CpuState.h"

//
// PDP-1 host I/O personality. The guest's IOT instruction traps here (State.SyscallVector ==
// 0o72). We re-read the IOT word at the trapping word index (TrapPc - 1), decode the device
// field (low 6 bits), and perform the I/O against the guest's AC / IO registers and the Type 30
// display point buffer. Returns TRUE to resume after the IOT, FALSE to stop the machine.
//
// REGISTER-ORDER CONTRACT: these indices MUST match the register_file order in pdp1.upcl.
//
enum {
    PDP1_AC = 0,
    PDP1_IO = 1,
    PDP1_PC = 2,
    PDP1_OV = 3
};

bool Pdp1IoTrap (CPU_STATE *pState, UINT8 *pRam, UINT64 RamSize);

#endif // LIBCPU_TEST_PDP1IO_H
