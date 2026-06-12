/** @file
  Knowledge libraries: data-driven translation of guest "system calls" into native
  host calls.

  A knowledge library is an XML file that maps a TARGET system call (e.g. an MS-DOS
  INT 21h function, selected at run time by a register) onto a HOST native call
  (e.g. a libc function), describing how to convert the arguments -- registers, far
  pointers, '$'-terminated strings -- from the target's conventions to the host's.

  At run time, when the guest executes its trap instruction (INT/SVC/...), the
  frontend records the vector in CPU_STATE.SyscallVector and traps to the host (see
  ICpuSyscallEmitter). LcRunWithSyscalls drives a translate/execute loop and, on each
  such trap, asks the loaded library for the matching <syscall>, materialises its host
  arguments from the guest CPU_STATE + RAM, performs the native call, writes any
  result back, and resumes -- so an in-line DOS print becomes a real write(2) to host
  stdout. This works on any syscall-capable backend (the interpreter today) and, being
  trap-driven, composes with the JIT exactly like the far-jump path does.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_KNOWLEDGELIBRARY_H
#define LIBCPU_KNOWLEDGELIBRARY_H

#include "LibCPU/ICpu.h"
#include "LibCPU/CpuState.h"
#include <string>
#include <vector>

namespace LibCPU {

//
// One host-call argument, materialised from the guest at dispatch time.
//   FromValue : a register or far pointer named by From (e.g. "al", "ds:dx"),
//               optionally run through a conversion named by Conv.
//   ConstName : a named host constant (e.g. "stdout", "stderr").
//
typedef struct _KN_HOST_ARG {
    enum { FromValue, ConstName } Kind;
    std::string From;     // source operand ("al", "dl", "ds:dx", ...)
    std::string Conv;     // conversion ("dollar_to_nul", "" = none)
    std::string Const;    // named host constant ("stdout", ...)
} KN_HOST_ARG;

//
// The host call a target syscall maps onto: a named native function plus its argument
// recipe. Lib records the providing library ("libc") for documentation / future
// dynamic binding.
//
typedef struct _KN_HOST_CALL {
    std::string Call;     // host function name ("fputs", "fputc", "exit", ...)
    std::string Lib;      // providing library ("libc", ...)
    std::vector<KN_HOST_ARG> Args;
} KN_HOST_CALL;

//
// One target system call: vector (e.g. 0x21), the register the target dispatches on
// (Select, e.g. "ah") and the value that selects this entry, plus the host mapping.
//
typedef struct _KN_SYSCALL {
    UINT32       Vector;
    std::string  Select;  // selector register name ("ah") -- "" means the vector alone
    UINT32       Value;   // selector value this entry matches
    std::string  Name;    // human label ("PrintString")
    KN_HOST_CALL Host;
} KN_SYSCALL;

//
// A loaded knowledge library: the parsed <syscall> set plus its target/host class.
//
class LcKnowledgeLibrary {
public:
    // Parse an XML knowledge library file; returns false on read/parse error.
    bool Load (CHAR8 CONST *pPath);

    // Find the entry matching Vector and the Selector byte (the value of the entry's
    // Select register, supplied by the caller). Returns nullptr if none match.
    KN_SYSCALL CONST *Find (UINT32 Vector, UINT32 Selector) CONST;

    CHAR8 CONST *TargetClass () CONST { return m_Target.c_str (); }
    CHAR8 CONST *HostClass () CONST { return m_Host.c_str (); }
    UINT32       Count () CONST { return (UINT32) m_Syscalls.size (); }
    KN_SYSCALL CONST &At (UINT32 I) CONST { return m_Syscalls[I]; }

private:
    std::string             m_Target;
    std::string             m_Host;
    std::vector<KN_SYSCALL> m_Syscalls;
};

//
// Outcome of running the guest under a knowledge library.
//
typedef struct _KN_RUN_RESULT {
    bool   Exited;        // the guest invoked an "exit" host call
    INT32  ExitCode;      // its exit status (valid when Exited)
    UINT32 Syscalls;      // number of guest syscalls dispatched
    UINT32 Translations;  // number of translation passes (one per resume)
    bool   Unhandled;     // a syscall with no library entry was hit (then we stop)
} KN_RUN_RESULT;

//
// Translate and run the guest in [Entry, End) through the given backend (which must
// expose ICpuSyscallEmitter), dispatching each guest system call through the library.
// pRAM/RamSize are the guest memory; pState is the live CPU_STATE. The 8086/DOS
// register model is used to resolve operand names (this is the "dos" target).
//
KN_RUN_RESULT LcRunWithSyscalls (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                                 CPU_ADDR Entry, CPU_ADDR End,
                                 VOID *pRAM, CPU_STATE *pState,
                                 LcKnowledgeLibrary CONST &Library);

} // namespace LibCPU

#endif // LIBCPU_KNOWLEDGELIBRARY_H
