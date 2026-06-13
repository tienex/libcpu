/** @file
  Knowledge libraries: data-driven translation of guest "system calls" into native
  host calls.

  A knowledge library is an XML file that maps a TARGET system call (e.g. an MS-DOS
  INT 21h function, selected at run time by a register) onto a HOST native call
  (e.g. a libc function), describing how to convert the arguments -- registers, far
  pointers, '$'-terminated strings -- from the target's conventions to the host's.

  At run time, when the guest executes its trap instruction (INT/SVC/...), the
  frontend records the vector in CPU_STATE.SyscallVector and traps to the host (see
  ICpuSyscallEmitter). RunWithSyscalls drives a translate/execute loop and, on each
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

// Direction of a struct argument: whether the host reads it, writes it, or both.
enum { KnDirNone = 0, KnDirIn = 1, KnDirOut = 2, KnDirInOut = 3 };

//
// One host-call argument, materialised from the guest at dispatch time.
//   FromValue : a register or far pointer named by From (e.g. "al", "ds:dx"),
//               optionally run through a conversion named by Conv.
//   ConstName : a named host constant (e.g. "stdout", "stderr").
//   StructPtr : a far pointer (From) to a struct of type Struct; the dispatcher allocates
//               a host-layout buffer, marshals guest<->host by the catalog's field offsets
//               (per Dir), and passes the buffer's address to the call.
//
typedef struct _KN_HOST_ARG {
    enum { FromValue, ConstName, StructPtr } Kind;
    std::string From;     // source operand ("al", "dl", "ds:dx", ...)
    std::string Conv;     // conversion ("dollar_to_nul", "" = none)
    std::string Const;    // named host constant ("stdout", ...)
    std::string Struct;   // struct type name (resolved against the catalog) for StructPtr
    int         Dir;      // KnDirIn/Out/InOut for StructPtr
} KN_HOST_ARG;

//
// The host call a target syscall maps onto: a named native function plus its argument
// recipe. Lib records the providing library ("libc") for documentation / future
// dynamic binding.
//
typedef struct _KN_HOST_CALL {
    std::string Call;     // host function name ("fputs", "fputc", "exit", ...)
    std::string Lib;      // providing library ("libc", ...)
    std::string Result;   // operand to receive the call's integer return ("ax"/"al", "" = discard)
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
class KnowledgeLibrary {
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

    // Build a library programmatically (used by the derivation engine instead of Load):
    // set the target/host classes and append derived syscall entries.
    VOID SetClasses (CHAR8 CONST *pTarget, CHAR8 CONST *pHost) { m_Target = pTarget; m_Host = pHost; }
    VOID AddEntry (KN_SYSCALL CONST &Entry) { m_Syscalls.push_back (Entry); }

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
// One struct field's placement in the host layout vs the guest layout: a row of the
// conversion the marshaller applies. Host offsets/sizes come from the catalog (real
// toolchain layout); guest offsets/sizes follow the target's convention (DOS = 16-bit
// packed).
//
typedef struct _KN_FIELD_MAP {
    UINT32 HostOffset;
    UINT32 HostSize;
    UINT32 GuestOffset;
    UINT32 GuestSize;
} KN_FIELD_MAP;

//
// A host-call binder supplied by the caller so RunWithSyscalls can reach calls and struct
// layouts discovered in a derived knowledge catalog (dlsym'd / read at run time) WITHOUT
// KnowledgeLibrary depending on the catalog machinery. pCtx is passed back to each hook.
//   Bind   : host function name -> callable native pointer, or nullptr.
//   Layout : fill *pFields (<= Max) with struct pType's field map; return field count (0 =
//            unknown). *pHostSize/*pGuestSize receive the two total sizes.
//
typedef struct _HOST_BINDER {
    VOID  *pCtx;
    VOID  *(*Bind) (VOID *pCtx, CHAR8 CONST *pName);
    UINT32 (*Layout) (VOID *pCtx, CHAR8 CONST *pType, KN_FIELD_MAP *pFields, UINT32 Max,
                      UINT32 *pHostSize, UINT32 *pGuestSize);
} HOST_BINDER;

//
// Translate and run the guest in [Entry, End) through the given backend (which must
// expose ICpuSyscallEmitter), dispatching each guest system call through the library.
// pRAM/RamSize are the guest memory; pState is the live CPU_STATE. The 8086/DOS
// register model is used to resolve operand names (this is the "dos" target).
//
// pBinder is optional: when a syscall maps to a host call the built-in dispatcher does not
// implement, and a binder is supplied, the call is resolved through it and invoked natively
// (integer/pointer arguments, plus struct-pointer arguments marshalled field-by-field via
// the binder's Layout), with any integer return written back to KN_HOST_CALL::Result.
//
KN_RUN_RESULT RunWithSyscalls (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                                 CPU_ADDR Entry, CPU_ADDR End,
                                 VOID *pRAM, CPU_STATE *pState,
                                 KnowledgeLibrary CONST &Library,
                                 HOST_BINDER CONST *pBinder = nullptr);

} // namespace LibCPU

#endif // LIBCPU_KNOWLEDGELIBRARY_H
