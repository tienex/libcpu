/** @file
  lcx -- the unified LibCPU command, in the spirit of qemu/gxemul/tme: one entry
  point over JIT/AOT execution, the debugger, disassembly, the translation cache,
  system emulation, and knowledge-library (syscall) runs.

    lcx run     <image> [--arch v20|6502] [--aot|--jit] [--cache] [--dump <addr>]
    lcx disasm  <image> [--arch ...] [--count N] [--entry N]
    lcx debug   <image> [--arch ...]
    lcx system  <image> [--arch v20]                 (8086 device bus + timer IRQ)
    lcx know    <image> <library.xml> [--arch v20]   (in-line syscalls -> host)
    lcx cache   ls | info | clean                    (shared translation-cache dir)
    lcx cache   dump | compress | uncompress | clear <file>   (a DBM code-cache file)
    lcx help | version

  The backend defaults to the interpreter bundle next to this executable (override
  with --backend <path> or $LCX_BACKEND).
**/
#include "CpuV20.h"
#include "CpuI8080.h"
#include "Cpu6502.h"
#include "LibCPU/CpuState.h"
#include "LibCPU/Loader.h"
#include "nix.h"            // libnix host layer -- drives the PDP-11 UNIX syscall personalities
#include "../aot/AotGenerator.h"
#include "../core/Debugger.h"
#include "../core/TranslationCache.h"
#include "../core/CodeCache.h"
#include "../aot/ShadowCfg.h"
#include "../core/KnowledgeLibrary.h"
#include "../core/System.h"
#include "../core/NativeAot.h"
#include "../core/SymbolReader.h"
#include "../core/HeaderParser.h"
#include "../core/DerivationEngine.h"
#include "../core/DeviceTree.h"
#include "../core/MachineBuilder.h"
#include "RunMachine.h"
#ifdef LIBCPU_HAVE_ZSTD
#include "../core/ZooArchive.h"
#endif
#include "../upcl/Parser.h"
#include "../upcl/CppGen.h"
#include "../upcl/UpclArch.h"
#include "../upcl/RegisterLayout.h"
#include "../upcl/Semantics.h"
#include "../upcl/Decoder.h"
#include "RunSystem.h"
#include "RunDosSyscall.h"
#include "Pdp1Io.h"
#include "RunHostCall.h"
#include "RunStruct.h"
#include "RunDerivedMap.h"
#include "RunMethodCall.h"
#include "LibCPU/PCom.h"
#include <cstdio>
#include <unistd.h>   // write/read/close/getpid for the obsd-m88k user-space syscall personality
#include <fcntl.h>    // open
#include <cerrno>     // errno -> the m88k carry/r2 error convention
#include <ctime>      // clock_gettime for gettimeofday/clock_gettime
#include <sys/time.h> // struct timeval / gettimeofday
#include <sys/uio.h>  // writev/readv iovec marshalling
#include <csignal>    // kill(2) for the obsd-m88k personality
#include <cstring>
#include <cctype>     // std::tolower for the case-insensitive .SAV extension match
#if defined (__unix__) || defined (__APPLE__)
#  include <sys/stat.h>
#endif
#include <set>
#include <string>
#include <vector>

using namespace LibCPU;

namespace {

// --- small option parsing --------------------------------------------------

static CHAR8 CONST *
Opt (int argc, char **argv, CHAR8 CONST *pName, CHAR8 CONST *pDefault)
{
    for (int I = 0; I < argc; I++) {
        if (std::strcmp (argv[I], pName) == 0 && I + 1 < argc) {
            return argv[I + 1];
        }
    }
    return pDefault;
}

static bool
Flag (int argc, char **argv, CHAR8 CONST *pName)
{
    for (int I = 0; I < argc; I++) {
        if (std::strcmp (argv[I], pName) == 0) {
            return true;
        }
    }
    return false;
}

// First argument that is not an option or option-value (the positional image path).
static CHAR8 CONST *
Positional (int argc, char **argv, int Which)
{
    int Seen = 0;
    for (int I = 0; I < argc; I++) {
        if (argv[I][0] == '-') {
            // Skip options that take a value.
            if (std::strcmp (argv[I], "--arch") == 0 || std::strcmp (argv[I], "--backend") == 0 ||
                std::strcmp (argv[I], "--count") == 0 || std::strcmp (argv[I], "--entry") == 0 ||
                std::strcmp (argv[I], "--dump") == 0 || std::strcmp (argv[I], "-o") == 0 ||
                std::strcmp (argv[I], "--result") == 0 || std::strcmp (argv[I], "--start") == 0) {
                I++;
            }
            continue;
        }
        if (Seen++ == Which) {
            return argv[I];
        }
    }
    return nullptr;
}

// Parse a numeric string that may carry a Python-style 0o.. octal prefix (C's strtoull does not
// understand 0o; only 0x hex, 0 octal, and decimal). Accepts 0o.., 0O.., 0x.., plain 0.., decimal.
static UINT64
ParseNum (CHAR8 CONST *pStr)
{
    if (pStr == nullptr) { return 0; }
    if (pStr[0] == '0' && (pStr[1] == 'o' || pStr[1] == 'O')) {
        return (UINT64) std::strtoull (pStr + 2, nullptr, 8);
    }
    return (UINT64) std::strtoull (pStr, nullptr, 0);
}

// --- backend / image / arch resolution -------------------------------------

static std::string
BackendPath (int argc, char **argv, CHAR8 CONST *pArgv0)
{
    if (CHAR8 CONST *p = Opt (argc, argv, "--backend", nullptr)) {
        return std::string (p);
    }
    if (CHAR8 CONST *p = std::getenv ("LCX_BACKEND")) {
        return std::string (p);
    }
    std::string Dir (pArgv0);
    size_t Slash = Dir.find_last_of ('/');
    Dir = (Slash == std::string::npos) ? std::string (".") : Dir.substr (0, Slash);
    return Dir + "/interp.backend";
}

static bool
LoadImage (CHAR8 CONST *pPath, UINT8 *pRam, UINT64 RamSize, UINT64 *pLen)
{
    std::FILE *pf = std::fopen (pPath, "rb");
    if (pf == nullptr) {
        return false;
    }
    *pLen = std::fread (pRam, 1, (size_t) RamSize, pf);
    std::fclose (pf);
    return true;
}

struct ArchSetup {
    ICpuArchitecture        *pArch;
    std::vector<std::string>  Regs;
    UINT32                    RegBytes;
    std::vector<std::string>  Flags;
};

static Upcl::Module *UpclParse (CHAR8 CONST *pFile, Upcl::SourceManager &Sm);   // fwd

static ArchSetup
MakeArch (CHAR8 CONST *pName, UINT8 *pRam, UINT64 RamSize, CPU_STATE *pState)
{
    ArchSetup A;
    A.pArch = nullptr;
    A.Flags = { "N", "V", "Z", "C" };
    if (std::strncmp (pName, "upcl:", 5) == 0) {
        // --arch upcl:<file>[@<cpu>] -- interpret a UPCL description as the frontend.
        // An optional @<cpu> selects a CPU model (e.g. upcl:x86.upcl@v30): only base-ISA
        // and that model's feature instructions decode. The SourceManager + Module are
        // leaked for the process lifetime (the arch borrows the module).
        std::string Spec = pName + 5;
        std::string File = Spec, Cpu;
        std::string::size_type At = Spec.rfind ('@');
        if (At != std::string::npos) { File = Spec.substr (0, At); Cpu = Spec.substr (At + 1); }
        Upcl::SourceManager *pSm = new Upcl::SourceManager ();
        Upcl::Module *pMod = UpclParse (File.c_str (), *pSm);
        if (pMod != nullptr) {
            // The arch flattens the register file once; take its register names back out
            // for DumpRegs rather than building the layout a second time here.
            A.pArch = Upcl::CreateUpclArch (pMod, 0, Cpu.empty () ? nullptr : Cpu.c_str (), &A.Regs);
            CPU_ARCH_INFO Info;
            std::memset (&Info, 0, sizeof (Info));
            A.pArch->GetInfo (&Info);
            A.RegBytes = (Info.GprBits + 7) / 8;
        }
    } else if (std::strcmp (pName, "6502") == 0) {
        A.pArch = Create6502 ();
        A.Regs = { "A", "X", "Y", "S" };
        A.RegBytes = 1;
        pState->Reg[Reg6502S] = 0xFF;
    } else if (std::strcmp (pName, "8080") == 0 || std::strcmp (pName, "8085") == 0) {
        A.pArch = CreateI8080 ();
        A.Regs = { "A", "B", "C", "D", "E", "H", "L", "SP" };
        A.RegBytes = 1;
    } else if (std::strcmp (pName, "v30") == 0) {
        A.pArch = CreateV30 ();
        A.Regs = { "AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI", "ES", "CS", "SS", "DS" };
        A.RegBytes = 2;
    } else {
        A.pArch = CreateV20 ();
        A.Regs = { "AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI", "ES", "CS", "SS", "DS" };
        A.RegBytes = 2;
    }
    if (A.pArch != nullptr) {
        A.pArch->SetCodeMemory (pRam, RamSize);
    }
    return A;
}

// OpenBSD/m88k process break (heap) pointer, tracked across brk(2)/sbrk calls. g_BrkBase is the
// initial break set by the loader to the end of bss; g_BrkCur is the current break. A flat-RAM
// emulator has no page tables, so brk just bumps a pointer and validates it against RAM.
static UINT64 g_BrkBase = 0;
static UINT64 g_BrkCur  = 0;

// m88k r[N] maps directly to CPU_STATE.Reg[N] (r0 is the hardwired-zero slot Reg[0]; the frontend
// reads it as 0 and discards writes). These index the SP and link registers the entry state needs.
#define M88K_REG(n)  ((UINT32) (n))                           // m88k r[n] -> CPU_STATE.Reg[n]
#define M88K_SP      M88K_REG (31)                            // r31 stack pointer  -> Reg[31]
#define M88K_LINK    M88K_REG (1)                             // r1  link register  -> Reg[1]

// One OpenBSD/m88k user-space system call. The guest issues `tb0 0, r0, 450` after loading the
// syscall number in r13 and arguments in r2.. ; the result is returned in r2. (m88k r[N] maps
// directly to CPU_STATE Reg[N]; r0 is the hardwired-zero slot.)
//
// The OpenBSD/m88k kernel ABI signals an error by SETTING THE CARRY FLAG (PSR C, bit 28) and
// returning the errno in r2; on success the carry is clear and r2 holds the return value. libc's
// cerror stub branches on that carry, so this shim must drive Flag[FlagCarry] -- not just r2 --
// for errno propagation to work. Host errno values are BSD-derived and match OpenBSD's for the
// common range, so they pass through directly.
//
// This is a host-backed personality (the real obsd41 + libnix marshalling is wired on top of the
// same seam); it covers the syscalls a static OpenBSD/m88k user binary issues at startup and for
// basic I/O. The numbers are OpenBSD 4.1's (test/libnix/obsd41/obsd41.sc). Returns false when the
// guest should stop (exit), true to resume after the trap.
static bool
ObsdM88kSyscall (CPU_STATE *pState, UINT8 *pRam, UINT64 RamSize)
{
    UINT64 Sc  = pState->Reg[13];                         // r13 = syscall number
    auto   Arg = [&] (int I) -> UINT64 { return pState->Reg[2 + I]; };   // r2=arg0, r3=arg1, ...
    if (std::getenv ("LCX_STRACE") != nullptr) {
        std::fprintf (stderr, "lcx strace: @%#llx vector=%#llx r13=%llu (%#llx, %#llx, %#llx)\n",
                      (unsigned long long) pState->TrapPc, (unsigned long long) pState->SyscallVector,
                      (unsigned long long) Sc, (unsigned long long) Arg (0),
                      (unsigned long long) Arg (1), (unsigned long long) Arg (2));
        if (std::getenv ("LCX_STRACE_REGS") != nullptr) {
            for (int K = 0; K < 32; K++) {
                std::fprintf (stderr, "  Reg[%2d]=%08llx%s", K,
                              (unsigned long long) (pState->Reg[K] & 0xffffffff), (K % 4 == 3) ? "\n" : "");
            }
            UINT64 Tp = pState->TrapPc;
            if (Tp >= 8 && Tp + 4 <= RamSize) {
                std::fprintf (stderr, "  code @%#llx: %02x%02x%02x%02x %02x%02x%02x%02x [tb0:%02x%02x%02x%02x]\n",
                              (unsigned long long) (Tp - 8),
                              pRam[Tp - 8], pRam[Tp - 7], pRam[Tp - 6], pRam[Tp - 5],
                              pRam[Tp - 4], pRam[Tp - 3], pRam[Tp - 2], pRam[Tp - 1],
                              pRam[Tp], pRam[Tp + 1], pRam[Tp + 2], pRam[Tp + 3]);
            }
        }
    }
    auto   Ok  = [&] (UINT64 V) -> bool {                 // success: r2 = value, carry clear
        pState->Reg[2] = V;
        pState->Flag[FlagCarry] = 0;
        return true;
    };
    auto   Fail = [&] (int E) -> bool {                   // error: r2 = errno, carry set
        pState->Reg[2] = (UINT64) (UINT32) E;
        pState->Flag[FlagCarry] = 1;
        return true;
    };
    auto   InRam = [&] (UINT64 A, UINT64 N) -> bool { return A <= RamSize && A + N <= RamSize; };
    auto   WBe32 = [&] (UINT64 A, UINT32 V) {
        if (InRam (A, 4)) {
            pRam[A] = (UINT8) (V >> 24); pRam[A + 1] = (UINT8) (V >> 16);
            pRam[A + 2] = (UINT8) (V >> 8); pRam[A + 3] = (UINT8) V;
        }
    };
    auto   RBe32 = [&] (UINT64 A) -> UINT32 {
        return InRam (A, 4) ? (UINT32) ((pRam[A] << 24) | (pRam[A + 1] << 16) | (pRam[A + 2] << 8) | pRam[A + 3]) : 0;
    };

    switch (Sc) {
    case 1:                                               // exit(code)
        return false;

    case 3:                                               // read(fd, buf, len)
    case 4: {                                             // write(fd, buf, len)
        UINT64 Fd = Arg (0), Buf = Arg (1), Len = Arg (2);
        if (!InRam (Buf, Len)) { return Fail (EFAULT); }
        long N = (Sc == 4) ? (long) write ((int) Fd, pRam + Buf, (size_t) Len)
                           : (long) read ((int) Fd, pRam + Buf, (size_t) Len);
        return (N < 0) ? Fail (errno) : Ok ((UINT64) N);
    }

    case 5: {                                             // open(path, flags, mode)
        UINT64 PathA = Arg (0);
        if (PathA >= RamSize) { return Fail (EFAULT); }
        long Fd = (long) open ((char CONST *) (pRam + PathA), (int) Arg (1), (int) Arg (2));
        return (Fd < 0) ? Fail (errno) : Ok ((UINT64) Fd);
    }

    case 6: {                                             // close(fd)
        long R = (long) close ((int) Arg (0));
        return (R < 0) ? Fail (errno) : Ok (0);
    }

    case 17: {                                            // brk(addr) -> new break (or current if 0)
        UINT64 Addr = Arg (0);
        if (Addr == 0) { return Ok (g_BrkCur); }
        if (Addr < g_BrkBase || Addr >= RamSize - 0x10000) { return Fail (ENOMEM); }
        g_BrkCur = Addr;
        return Ok (Addr);
    }

    case 20: return Ok ((UINT64) (long) getpid ());       // getpid
    case 24: return Ok ((UINT64) (long) getuid ());       // getuid
    case 25: return Ok ((UINT64) (long) geteuid ());      // geteuid
    case 43: return Ok ((UINT64) (long) getegid ());      // getegid
    case 47: return Ok ((UINT64) (long) getgid ());       // getgid
    case 39: return Ok ((UINT64) (long) getppid ());      // getppid

    case 36:                                              // sync(void)
        sync ();
        return Ok (0);

    case 37: {                                            // kill(pid, sig) -- only allow self/own group
        long R = (long) kill ((int) Arg (0), (int) Arg (1));
        return (R < 0) ? Fail (errno) : Ok (0);
    }

    case 41: {                                            // dup(fd)
        long R = (long) dup ((int) Arg (0));
        return (R < 0) ? Fail (errno) : Ok ((UINT64) R);
    }
    case 90: {                                            // dup2(from, to)
        long R = (long) dup2 ((int) Arg (0), (int) Arg (1));
        return (R < 0) ? Fail (errno) : Ok ((UINT64) R);
    }

    case 92: {                                            // fcntl(fd, cmd, arg)
        long R = (long) fcntl ((int) Arg (0), (int) Arg (1), (long) Arg (2));
        return (R < 0) ? Fail (errno) : Ok ((UINT64) R);
    }

    case 199: {                                           // lseek(fd, pad, off_hi, off_lo, whence)
        // OpenBSD m88k passes the 64-bit offset as a register pair after a padding word; the
        // whence follows. r2=fd, r3=pad, r4:r5 = offset, r6 = whence.
        UINT64 Off = ((UINT64) Arg (2) << 32) | (UINT32) Arg (3);
        long long R = (long long) lseek ((int) Arg (0), (off_t) Off, (int) Arg (4));
        if (R < 0) { return Fail (errno); }
        // 64-bit result returns in r2:r3 (high:low).
        pState->Reg[3] = (UINT64) (UINT32) ((UINT64) R & 0xffffffff);  // r3 = low
        return Ok ((UINT64) (UINT32) ((UINT64) R >> 32));              // r2 = high
    }

    case 120:                                             // readv(fd, iov, iovcnt)
    case 121: {                                           // writev(fd, iov, iovcnt)
        UINT64 Fd = Arg (0), IovA = Arg (1), Cnt = Arg (2);
        long Total = 0;
        for (UINT64 I = 0; I < Cnt; I++) {
            UINT64 Base = RBe32 (IovA + I * 8);
            UINT64 Ln   = RBe32 (IovA + I * 8 + 4);
            if (Ln == 0) { continue; }
            if (!InRam (Base, Ln)) { return Fail (EFAULT); }
            long N = (Sc == 121) ? (long) write ((int) Fd, pRam + Base, (size_t) Ln)
                                 : (long) read ((int) Fd, pRam + Base, (size_t) Ln);
            if (N < 0) { return (Total > 0) ? Ok ((UINT64) Total) : Fail (errno); }
            Total += N;
            if ((UINT64) N < Ln) { break; }
        }
        return Ok ((UINT64) Total);
    }

    case 116: {                                           // gettimeofday(tv, tz)
        UINT64 Tv = Arg (0);
        struct timeval Now;
        gettimeofday (&Now, nullptr);
        if (Tv != 0) {
            WBe32 (Tv, (UINT32) Now.tv_sec);              // struct timeval { int32 tv_sec; int32 tv_usec }
            WBe32 (Tv + 4, (UINT32) Now.tv_usec);
        }
        return Ok (0);
    }

    case 232: {                                           // clock_gettime(clk_id, tp)
        UINT64 Tp = Arg (1);
        struct timespec Ts;
        clock_gettime (CLOCK_REALTIME, &Ts);
        if (Tp != 0) {
            WBe32 (Tp, (UINT32) Ts.tv_sec);              // struct timespec { int32 tv_sec; int32 tv_nsec }
            WBe32 (Tp + 4, (UINT32) Ts.tv_nsec);
        }
        return Ok (0);
    }

    case 253: return Ok (0);                              // issetugid -> not set-uid/gid
    case 60:  return Ok (0);                              // umask -> report previous mask 0 (no-op)
    case 298: return Ok (0);                              // sched_yield
    case 299: return Ok ((UINT64) (long) getpid ());      // getthrid -> the single thread id

    // Signal + memory-protection calls a static binary issues at startup that a flat-RAM,
    // single-threaded model can satisfy as a no-op success (carry clear, r2 = 0).
    case 46:                                              // sigaction
    case 48:                                              // sigprocmask
    case 53:                                              // osigaltstack
    case 288:                                             // sigaltstack
    case 74:                                              // mprotect
    case 75:                                              // madvise
    case 73:                                              // munmap
        return Ok (0);

    case 54: {                                            // ioctl(fd, request, arg)
        // The only ioctls a basic CLI binary issues are terminal queries (isatty -> TIOCGETA);
        // report "not a tty" so output goes to the block path. ENOTTY keeps stdio happy.
        return Fail (ENOTTY);
    }

    default:
        // Unimplemented: return ENOSYS rather than killing the guest, so it can decide how to
        // cope (most abort, but some probe-and-fall-back). Logged once-per-call for triage.
        std::fprintf (stderr, "lcx: unhandled obsd-m88k syscall %llu (returning ENOSYS)\n",
                      (unsigned long long) Sc);
        return Fail (78);                                 // OpenBSD ENOSYS
    }
}

static void
DumpRegs (ArchSetup CONST &A, CPU_STATE CONST *pState)
{
    UINT64 Mask = (A.RegBytes >= 8) ? ~UINT64_C (0) : ((UINT64_C (1) << (A.RegBytes * 8)) - 1);
    int W = (int) (A.RegBytes * 2), Col = 0;
    for (UINT32 I = 0; I < A.Regs.size (); I++) {
        std::printf ("%-3s=%0*llx  ", A.Regs[I].c_str (), W, (unsigned long long) (pState->Reg[I] & Mask));
        if (++Col % 4 == 0) { std::printf ("\n"); }
    }
    if (Col % 4 != 0) { std::printf ("\n"); }
}

// The host environment, for passing envp through to an OpenBSD/m88k guest. (An executable may
// reference `environ` directly; macOS declares it for the main program image.)
extern "C" char **environ;

// Build the OpenBSD/m88k user-space entry stack ("uframe") near the top of RAM and return the guest
// stack pointer. At process entry r31 points at argc, immediately followed by the argv[] pointer
// vector (NULL-terminated), then the envp[] pointer vector (NULL-terminated); the argument and
// environment strings are packed above the vectors. argc and every guest pointer are big-endian
// 32-bit. (Mirrors the old run88 openbsd_m88k_setup_uframe.)
static UINT64
SetupObsdM88kStack (UINT8 *pRam, UINT64 RamSize, std::vector<std::string> CONST &Args, char **ppEnv)
{
    auto WBe32 = [&] (UINT64 A, UINT32 V) {
        pRam[A] = (UINT8) (V >> 24); pRam[A + 1] = (UINT8) (V >> 16);
        pRam[A + 2] = (UINT8) (V >> 8); pRam[A + 3] = (UINT8) V;
    };
    std::vector<char CONST *> Env;
    for (char **p = ppEnv; p != nullptr && *p != nullptr; ++p) { Env.push_back (*p); }

    // Pack the strings downward from a 16-aligned point just below the top of RAM.
    UINT64 P = (RamSize - 16) & ~UINT64_C (15);
    std::vector<UINT64> ArgAddr, EnvAddr;
    for (std::string CONST &S : Args) {
        P -= S.size () + 1;
        std::memcpy (pRam + P, S.c_str (), S.size () + 1);
        ArgAddr.push_back (P);
    }
    for (char CONST *e : Env) {
        size_t L = std::strlen (e);
        P -= L + 1;
        std::memcpy (pRam + P, e, L + 1);
        EnvAddr.push_back (P);
    }

    // The pointer vector -- argc, argv[0..], NULL, envp[0..], NULL -- sits below the strings with the
    // stack pointer 16-aligned. The returned SP points at argc, exactly where m88k crt0 expects r31.
    UINT64 NWords = 1 + Args.size () + 1 + Env.size () + 1;
    UINT64 Sp = (P - NWords * 4) & ~UINT64_C (15);
    UINT64 A  = Sp;
    WBe32 (A, (UINT32) Args.size ()); A += 4;
    for (UINT64 Ad : ArgAddr) { WBe32 (A, (UINT32) Ad); A += 4; }
    WBe32 (A, 0); A += 4;
    for (UINT64 Ad : EnvAddr) { WBe32 (A, (UINT32) Ad); A += 4; }
    WBe32 (A, 0); A += 4;
    return Sp;
}

// --- subcommands -----------------------------------------------------------

// PDP-11 classic-UNIX (V6/V7 lineage) syscall personality, dispatched through libnix's nix_ host
// layer. The `sys` call is the TRAP instruction (0o104400+N); like the kernel, we read the call
// number from that instruction's low byte at TrapPc-2. Arguments follow inline (the kernel reads
// them and advances the saved PC); `sys 0` (indir) takes a pointer to an argument block holding the
// real `sys N` word + its arguments -- how the C library passes runtime values. Results return in
// r0 (r1 for the second of a pair); the carry flag signals an error, with errno in r0.
struct Pdp11Sysent { CHAR8 CONST *Name; int Nargs; };

// The V7 syscall table (number -> name + inline argument count); the shared V6 core matches it, and
// the later PDP-11 systems (System III, PWB, 2.xBSD, Ultrix-11, Venix) extend it. One table drives
// every `--abi unix*` personality for the common core.
static Pdp11Sysent CONST *
Pdp11SyscallTable (UINT32 *pCount)
{
    static Pdp11Sysent CONST T[] = {
        { "indir", 0 }, { "exit", 1 }, { "fork", 0 }, { "read", 3 }, { "write", 3 }, { "open", 2 },
        { "close", 1 }, { "wait", 0 }, { "creat", 2 }, { "link", 2 }, { "unlink", 1 }, { "exec", 2 },
        { "chdir", 1 }, { "time", 0 }, { "mknod", 3 }, { "chmod", 2 }, { "chown", 3 }, { "break", 1 },
        { "stat", 2 }, { "lseek", 3 }, { "getpid", 0 }, { "mount", 3 }, { "umount", 1 }, { "setuid", 1 },
        { "getuid", 0 }, { "stime", 1 }, { "ptrace", 4 }, { "alarm", 1 }, { "fstat", 2 }, { "pause", 0 },
        { "utime", 2 }, { "", 0 }, { "", 0 }, { "access", 2 }, { "nice", 1 }, { "ftime", 1 },
        { "sync", 0 }, { "kill", 2 }, { "", 0 }, { "", 0 }, { "", 0 }, { "dup", 1 }, { "pipe", 0 },
        { "times", 1 }, { "profil", 4 }, { "", 0 }, { "setgid", 1 }, { "getgid", 0 }, { "signal", 2 },
    };
    *pCount = (UINT32) (sizeof (T) / sizeof (T[0]));
    return T;
}

static nix_env_t *g_pdp11_env = nullptr;

// Returns false when the guest should stop (exit), true to resume after the trap.
static bool
Pdp11UnixSyscall (CPU_STATE *pState, UINT8 *pRam, UINT64 RamSize)
{
    if (g_pdp11_env == nullptr) { g_pdp11_env = nix_env_create (nullptr); }
    nix_env_t *env = g_pdp11_env;

    auto M16 = [&] (UINT64 A) -> UINT32 { return (A + 1 < RamSize) ? (UINT32) (pRam[A] | (pRam[A + 1] << 8)) : 0; };
    UINT32 Count; Pdp11Sysent CONST *Tab = Pdp11SyscallTable (&Count);

    UINT64 Resume = pState->TrapPc;            // PC just past the 2-byte trap instruction
    UINT32 Num    = M16 (Resume - 2) & 0xff;   // the trap's low byte = syscall number
    UINT64 ArgBase;
    bool   InlineArgs;
    if (Num == 0) {                            // indir: the operand points to [ sys N | args... ]
        UINT64 Blk = M16 (Resume);
        pState->TrapPc = Resume + 2;           // step past the indir operand word
        Num = M16 (Blk) & 0xff;
        ArgBase = Blk + 2; InlineArgs = false;
    } else {
        ArgBase = Resume; InlineArgs = true;
    }
    int    Nargs = (Num < Count) ? Tab[Num].Nargs : 0;
    UINT64 a[6] = { 0 };
    for (int I = 0; I < Nargs && I < 6; I++) { a[I] = M16 (ArgBase + 2 * I); }
    if (InlineArgs) { pState->TrapPc = Resume + 2 * (UINT64) Nargs; }   // advance past inline args

    auto InRam = [&] (UINT64 A, UINT64 N) -> bool { return A <= RamSize && A + N <= RamSize; };
    auto Ok    = [&] (UINT64 V) -> bool { pState->Reg[0] = V & 0xffff; pState->Flag[FlagCarry] = 0; return true; };
    auto Fail  = [&] () -> bool {
        pState->Reg[0] = (UINT64) (UINT32) nix_env_get_errno (env) & 0xffff;
        pState->Flag[FlagCarry] = 1;
        return true;
    };
    if (std::getenv ("LCX_STRACE") != nullptr) {
        std::fprintf (stderr, "lcx pdp11 sys %u (%s) %#llx %#llx %#llx\n", Num,
                      (Num < Count && Tab[Num].Name[0]) ? Tab[Num].Name : "?",
                      (unsigned long long) a[0], (unsigned long long) a[1], (unsigned long long) a[2]);
    }

    nix_env_set_errno (env, 0);
    switch (Num) {
    case 1:  return false;                                                 // exit
    case 3: {                                                              // read(fd, buf, n)
        if (!InRam (a[1], a[2])) { return Fail (); }
        nix_ssize_t R = nix_read ((int) a[0], pRam + a[1], (size_t) a[2], env);
        return (R < 0) ? Fail () : Ok ((UINT64) R);
    }
    case 4: {                                                              // write(fd, buf, n)
        if (!InRam (a[1], a[2])) { return Fail (); }
        nix_ssize_t R = nix_write ((int) a[0], pRam + a[1], (size_t) a[2], env);
        return (R < 0) ? Fail () : Ok ((UINT64) R);
    }
    case 5: {                                                              // open(path, flags, mode)
        if (a[0] >= RamSize) { return Fail (); }
        int Fd = nix_open ((char CONST *) (pRam + a[0]), (int) a[1], (int) a[2], env);
        return (Fd < 0) ? Fail () : Ok ((UINT64) Fd);
    }
    case 6:  { int R = nix_close ((int) a[0], env); return (R < 0) ? Fail () : Ok (0); }
    case 8: {                                                              // creat(path, mode)
        if (a[0] >= RamSize) { return Fail (); }
        int Fd = nix_creat ((char CONST *) (pRam + a[0]), (int) a[1], env);
        return (Fd < 0) ? Fail () : Ok ((UINT64) Fd);
    }
    case 10: { if (a[0] >= RamSize) { return Fail (); }
               int R = nix_unlink ((char CONST *) (pRam + a[0]), env); return (R < 0) ? Fail () : Ok (0); }
    case 12: { if (a[0] >= RamSize) { return Fail (); }
               int R = nix_chdir ((char CONST *) (pRam + a[0]), env); return (R < 0) ? Fail () : Ok (0); }
    case 19: {                                                             // lseek(fd, off, whence)
        nix_off_t Off = nix_lseek ((int) a[0], (nix_off_t) (INT16) a[1], (int) a[2], env);
        return (Off < 0) ? Fail () : Ok ((UINT64) Off);
    }
    case 20: return Ok ((UINT64) nix_getpid (env));                        // getpid
    case 24: return Ok ((UINT64) nix_getuid (env));                        // getuid
    case 41: { int Fd = nix_dup ((int) a[0], env); return (Fd < 0) ? Fail () : Ok ((UINT64) Fd); }
    default:
        std::fprintf (stderr, "lcx: unhandled pdp11 unix syscall %u (%s)\n", Num,
                      (Num < Count && Tab[Num].Name[0]) ? Tab[Num].Name : "?");
        return Fail ();
    }
}

// The runtime parameters a loaded a.out yields.
struct AoutInfo {
    bool        Ok      = false;
    UINT64      Entry   = 0;      // program entry point
    UINT64      BrkBase = 0;      // initial heap break (aligned end of bss)
    UINT64      LoadedLen = 0;    // bytes of text+data laid into RAM
    CHAR8 CONST *Kind   = "";     // magic label, for the load message
};

// Generalized loader for the classic Bell-Labs a.out family. It lays the exec image already read into
// `pRam` out IN PLACE per its header + magic and reports the entry point and initial break. Two
// members of the family are recognised from the first header bytes:
//
//   * 16-bit little-endian (PDP-11): an 8-word (16-byte) header
//        a_magic a_text a_data a_bss a_syms a_entry a_unused a_flag
//     0407 OMAGIC  -- impure: text+data contiguous at 0 (text writable);
//     0410 NMAGIC  -- pure:   read-only text at 0, data at the next 8 KiB click, bss above;
//     0411 split I&D / 0413  -- modelled like NMAGIC in one flat space (closest a non-split-I/D
//                               address space allows), data at the next click.
//
//   * 32-bit big-endian (m88k, mid=153): a 32-byte header; ZMAGIC 0413 maps the whole file at 0x1000,
//     OMAGIC/NMAGIC strip the header and load at 0.
//
// They are told apart by the leading bytes: an m88k header begins 00 99 (mid 153, big-endian); a
// PDP-11 header begins with a little-endian magic word in the 04xx range.
static AoutInfo
LoadAout (UINT8 *pRam, UINT64 RamSize, UINT64 ImageLen)
{
    AoutInfo R;
    if (ImageLen < 16) { std::printf ("lcx: a.out too small\n"); return R; }

    bool   IsM88k     = (pRam[0] == 0x00 && pRam[1] == 0x99);
    UINT16 Pdp11Magic = (UINT16) (pRam[0] | (pRam[1] << 8));
    bool   IsPdp11    = !IsM88k && (Pdp11Magic == 0407 || Pdp11Magic == 0410 || Pdp11Magic == 0411
                                    || Pdp11Magic == 0405 || Pdp11Magic == 0413
                                    || Pdp11Magic == 0430 || Pdp11Magic == 0431);

    if (IsPdp11) {
        auto   W   = [&] (UINT64 O) -> UINT32 { return (UINT32) (pRam[O] | (pRam[O + 1] << 8)); };
        UINT32 Mag = W (0), ATxt = W (2), AData = W (4), ABss = W (6), AEntry = W (10);
        UINT64 Hdr = 16;
        UINT64 Seg = (UINT64) ATxt + AData;
        if (Hdr + Seg > ImageLen) { std::printf ("lcx: a.out truncated (text+data)\n"); return R; }

        UINT64 DataBase, BssEnd;
        if (Mag == 0407) {                                   // OMAGIC: text+data contiguous at 0
            if (Seg + ABss > RamSize) { std::printf ("lcx: a.out too large\n"); return R; }
            std::memmove (pRam, pRam + Hdr, (size_t) Seg);
            std::memset (pRam + Seg, 0, (size_t) (RamSize - Seg));
            DataBase = ATxt; BssEnd = Seg + ABss; R.Kind = "OMAGIC";
        } else {                                             // NMAGIC / split I&D: data at next click
            UINT64 Click = 020000;                           // 8 KiB PDP-11 page
            DataBase = (ATxt + Click - 1) & ~(Click - 1);
            BssEnd   = DataBase + AData + ABss;
            if (BssEnd > RamSize) { std::printf ("lcx: a.out too large\n"); return R; }
            std::vector<UINT8> Img (pRam + Hdr, pRam + Hdr + Seg);   // copy out before overwriting
            std::memset (pRam, 0, (size_t) BssEnd);
            std::memcpy (pRam, Img.data (), (size_t) ATxt);
            std::memcpy (pRam + DataBase, Img.data () + ATxt, (size_t) AData);
            R.Kind = (Mag == 0410) ? "NMAGIC" : (Mag == 0411 ? "0411 split-I/D" : "0413");
        }
        R.Entry     = AEntry;                                // PDP-11 _start is at 0 for OMAGIC
        R.LoadedLen = (Mag == 0407) ? Seg : (DataBase + AData);
        R.BrkBase   = (BssEnd + 1) & ~UINT64_C (1);          // word-align the heap
        R.Ok        = true;
        std::printf ("lcx: loaded pdp11 a.out (%s): text=0%o data=0%o bss=0%o entry=0%o\n",
                     R.Kind, (unsigned) ATxt, (unsigned) AData, (unsigned) ABss, (unsigned) AEntry);
        return R;
    }

    // 32-bit big-endian m88k a.out.
    if (ImageLen < 32) { std::printf ("lcx: a.out too small\n"); return R; }
    auto   Be32 = [&] (UINT64 O) -> UINT32 {
        return (UINT32) ((pRam[O] << 24) | (pRam[O + 1] << 16) | (pRam[O + 2] << 8) | pRam[O + 3]);
    };
    UINT32 MidMag = Be32 (0);
    UINT32 Mid    = (MidMag >> 16) & 0x3ff;
    UINT32 Magic  = MidMag & 0xffff;
    UINT32 ATxt = Be32 (4), AData = Be32 (8), ABss = Be32 (12), AEntry = Be32 (20);
    if (Mid != 153) { std::printf ("lcx: not a recognised a.out (mid=%u)\n", (unsigned) Mid); return R; }
    UINT64 Seg = (UINT64) ATxt + AData;
    UINT64 TxtBase, BssEnd;
    if (Magic == 0x10b) {                                    // ZMAGIC: map whole file at the text base
        TxtBase = 0x1000;
        if (ImageLen < Seg) { std::printf ("lcx: a.out truncated (text+data)\n"); return R; }
        if (TxtBase + Seg + ABss > RamSize) { std::printf ("lcx: a.out too large for RAM\n"); return R; }
        std::memmove (pRam + TxtBase, pRam, (size_t) Seg);
        std::memset (pRam, 0, (size_t) TxtBase);
        std::memset (pRam + TxtBase + Seg, 0, (size_t) (RamSize - (TxtBase + Seg)));
        BssEnd  = TxtBase + Seg + ABss;
        R.Kind  = "ZMAGIC";
    } else {                                                 // OMAGIC/NMAGIC: strip header, load at 0
        TxtBase = 0;
        if (32 + Seg > RamSize) { std::printf ("lcx: a.out too large for RAM\n"); return R; }
        std::memmove (pRam, pRam + 32, (size_t) Seg);
        std::memset (pRam + Seg, 0, (size_t) (RamSize - Seg));
        BssEnd = Seg + ABss;
        R.Kind = (Magic == 0x108) ? "NMAGIC" : "OMAGIC";
    }
    R.Entry     = AEntry;
    R.LoadedLen = TxtBase + Seg;
    R.BrkBase   = (BssEnd + 0xfff) & ~UINT64_C (0xfff);      // page-align the heap base
    R.Ok        = true;
    std::printf ("lcx: loaded m88k a.out (%s): text=0x%x data=0x%x bss=0x%x entry=0x%x base=0x%llx\n",
                 R.Kind, (unsigned) ATxt, (unsigned) AData, (unsigned) ABss, (unsigned) AEntry,
                 (unsigned long long) TxtBase);
    return R;
}

// The runtime parameters a loaded DEC .SAV image yields (word-addressed: every value is a WORD
// index, not a byte offset).
struct SavInfo {
    bool   Ok    = false;
    UINT64 Entry = 0;      // start PC (word index), from the final JRST start word
    UINT64 End   = 0;      // one past the highest word index touched (word count for the CFG walk)
};

// Loader for a classic DEC PDP-10 .SAV binary into WORD-ADDRESSED guest memory. A .SAV file is a
// sequence of load blocks terminated by a start word:
//
//   IOWD  -count,,addr-1     left half = negative word count, right half = (load address - 1)
//   word[0]                  count data words, placed at addr, addr+1, ...
//   ...
//   word[count-1]
//   ...(more blocks)...
//   JRST  ,,start            a JRST (opcode 0o254) word; its right half is the entry PC
//
// (DEC PDP-10 software toolkit / TOPS-10 LOADER; verified against the SIMH pdp10 SAV loader in
// pdp10_sys.c, which reads each 36-bit word as one host cell and iterates the block with the
// AOB -- "add one to both halves" -- idiom until the negative count reaches zero, then stops at
// the first JRST it reads as a block header.)
//
// ON-DISK ENCODING: one 36-bit word per 8 bytes, LITTLE-ENDIAN, the value held in the low 36 bits
// (the top 28 bits of each 8-byte slot are zero). This is the DATA8 convention SIMH's `d10`/`fxread`
// use AND is byte-identical to LibCPU's own word-cell layout (CPU_WORD_CELL_BYTES little-endian
// cells), so a data word loads by a direct copy into its destination cell with no repacking.
//
// The words are laid into `pRam` interpreted as an array of CPU_WORD_CELL_BYTES word cells: word
// index i occupies pRam[i * CPU_WORD_CELL_BYTES ..]. `pFileBytes`/`FileLen` is the raw .SAV file
// already read into a buffer (NOT the in-memory image -- the .SAV layout differs from core).
static SavInfo
LoadSav (UINT8 CONST *pFileBytes, UINT64 FileLen, UINT8 *pRam, UINT64 RamSize)
{
    SavInfo R;
    if (FileLen == 0 || (FileLen % 8) != 0) {
        std::printf ("lcx: .SAV size %llu is not a whole number of 8-byte words\n",
                     (unsigned long long) FileLen);
        return R;
    }
    UINT64 CONST NWords  = FileLen / 8;
    UINT64 CONST CellCnt = RamSize / CPU_WORD_CELL_BYTES;   // word cells the guest RAM holds

    auto FileWord = [&] (UINT64 I) -> UINT64 {              // 36-bit word from the I-th 8-byte slot
        UINT64 V = 0;
        for (UINT32 B = 0; B < 8; B++) { V |= (UINT64) pFileBytes[I * 8 + B] << (8 * B); }
        return V & ((UINT64_C (1) << 36) - 1);
    };
    auto StoreCell = [&] (UINT64 WordIdx, UINT64 Value) -> bool {
        if (WordIdx >= CellCnt) { return false; }
        UINT64 Off = WordIdx * CPU_WORD_CELL_BYTES;
        for (UINT32 B = 0; B < CPU_WORD_CELL_BYTES; B++) {
            pRam[Off + B] = (UINT8) ((Value >> (8 * B)) & 0xff);
        }
        return true;
    };

    UINT64 CONST Left18Mask  = UINT64_C (0o777777) << 18;   // bits 35..18: the IOWD left half
    UINT64 CONST Right18Mask = UINT64_C (0o777777);         // bits 17..0:  the IOWD right half
    UINT64 CONST SignBit     = UINT64_C (1) << 35;          // sign of the left half (negative count)
    UINT64 CONST OpJrst      = UINT64_C (0o254);            // JRST opcode (bits 35..27) marks the start word

    UINT64 I        = 0;
    UINT64 HighWord = 0;
    bool   GotStart = false;
    while (I < NWords) {
        UINT64 Header = FileWord (I++);
        if ((Header & SignBit) != 0) {
            // IOWD block header: AOB iteration. The left half is the negative word count and the
            // right half is (load address - 1); each step increments BOTH halves (the count toward
            // zero, the address up by one) and stores the next data word at the new address.
            UINT64 Cur = Header;
            while ((Cur & SignBit) != 0 && I < NWords) {
                Cur = ((Cur + (UINT64_C (1) << 18)) & Left18Mask)   // bump the left half (count -> 0)
                      | ((Cur + 1) & Right18Mask);                  // bump the right half (address up)
                UINT64 Addr = Cur & Right18Mask;                    // destination = right half after AOB
                UINT64 Data = FileWord (I++);
                if (!StoreCell (Addr, Data)) {
                    std::printf ("lcx: .SAV load address 0%llo exceeds guest word memory\n",
                                 (unsigned long long) Addr);
                    return R;
                }
                if (Addr + 1 > HighWord) { HighWord = Addr + 1; }
            }
        } else if (((Header >> 27) & UINT64_C (0o777)) == OpJrst) {
            // Start word: a JRST whose right half is the entry PC. (Reached as a block header, not
            // as a data word inside a block -- the in-program HALT is also a JRST, but it lives in
            // the data stream, never at a header position.)
            R.Entry  = Header & Right18Mask;
            GotStart = true;
            break;
        } else {
            std::printf ("lcx: .SAV: unexpected word 0%llo at index %llu (not an IOWD or a JRST start)\n",
                         (unsigned long long) Header, (unsigned long long) (I - 1));
            return R;
        }
    }

    if (!GotStart) {
        std::printf ("lcx: .SAV: no JRST start word found\n");
        return R;
    }
    // The CFG walk reads instruction words up to End; cover both the loaded image and the entry so
    // a program whose entry sits above its last loaded word still has a word to decode there.
    R.End = (HighWord > R.Entry + 1) ? HighWord : (R.Entry + 1);
    R.Ok  = true;
    std::printf ("lcx: loaded pdp10 .SAV: %llu word(s), entry=0%llo\n",
                 (unsigned long long) HighWord, (unsigned long long) R.Entry);
    return R;
}

// DEC PDP-1 RIM (Read-In Mode) paper-tape loader. Tapes are streams of 6-bit frames;
// channel-8 bit (0200) marks a DATA frame (non-data frames are skipped). Three consecutive
// data frames assemble one 18-bit word MSB-first. Words arrive in control+datum pairs:
//   DIO (0320000) or DAC (0240000):  opcode | Y -- store next word at M[Y].
//   JMP (0600000):                   opcode | Y -- Y is the start address; tape ends here.
// The assembled words are deposited into RAM word cells (word index I at byte offset
// I * CPU_WORD_CELL_BYTES, little-endian). Returns the start address and an Ok flag.
struct RimInfo { UINT32 Start; bool Ok; };

static RimInfo
LoadRim (UINT8 CONST *pFileBytes, UINT64 FileLen, UINT8 *pRam, UINT64 RamSize)
{
    RimInfo R = { 0, false };
    UINT64 P = 0;
    auto GetWord = [&] (UINT32 *pW) -> bool {
        UINT32 W = 0; int Got = 0;
        while (P < FileLen && Got < 3) {
            UINT8 Frame = pFileBytes[P++];
            if (Frame & 0200) { W = (W << 6) | (UINT32) (Frame & 077); Got++; }
        }
        if (Got < 3) { return false; }
        *pW = W & 0777777;
        return true;
    };
    auto Store = [&] (UINT32 Idx, UINT32 V) {
        UINT64 Off = (UINT64) Idx * CPU_WORD_CELL_BYTES;
        if (Off + CPU_WORD_CELL_BYTES > RamSize) { return; }
        for (UINT32 B = 0; B < CPU_WORD_CELL_BYTES; B++) { pRam[Off + B] = (UINT8) ((UINT64) V >> (8 * B)); }
    };
    for (;;) {
        UINT32 Ctl;
        if (!GetWord (&Ctl)) { break; }
        UINT32 Op = Ctl & 0760000;
        if (Op == 0320000 || Op == 0240000) {           // DIO / DAC : address + datum
            UINT32 Datum;
            if (!GetWord (&Datum)) { break; }
            Store (Ctl & 07777, Datum);
        } else if (Op == 0600000) {                     // JMP : start address, end of tape
            R.Start = Ctl & 07777; R.Ok = true; break;
        } else {
            break;                                      // malformed
        }
    }
    return R;
}

// Does a path end (case-insensitively) with the given extension (e.g. ".sav")?
static bool
HasExtension (CHAR8 CONST *pPath, CHAR8 CONST *pExt)
{
    size_t PathLen = std::strlen (pPath);
    size_t ExtLen  = std::strlen (pExt);
    if (PathLen < ExtLen) { return false; }
    CHAR8 CONST *pTail = pPath + (PathLen - ExtLen);
    for (size_t I = 0; I < ExtLen; I++) {
        if (std::tolower ((unsigned char) pTail[I]) != std::tolower ((unsigned char) pExt[I])) {
            return false;
        }
    }
    return true;
}

// Shared by `run` (Aot=false: JIT) and `translate` (Aot=true: AOT). The --arch value
// may be v20, 6502, or upcl:<file> (interpret a UPCL description as the frontend).
static int
CmdRun (int argc, char **argv, CHAR8 CONST *pArgv0, bool Aot)
{
    CHAR8 CONST *pVerb = Aot ? "translate" : "run";
    CHAR8 CONST *pImage = Positional (argc, argv, 0);
    if (pImage == nullptr) {
        std::printf ("usage: lcx %s <image> [--arch v20|6502|upcl:<file>] [--cache] [--dump <addr>]\n", pVerb);
        return 2;
    }
    ICpuBackend *pBackend = LoadBackendBundle (BackendPath (argc, argv, pArgv0).c_str ());
    if (pBackend == nullptr) {
        std::printf ("lcx: cannot load backend '%s'\n", BackendPath (argc, argv, pArgv0).c_str ());
        return 2;
    }
    // A user-space guest (an OpenBSD/m88k a.out) needs a real address space: text at a low base,
    // a heap above bss, and a stack near the top. 64 MiB holds every binary in test/bin/m88k plus
    // its stack and heap. (v20/6502 images use a fraction of this; the cost is one zeroing memset.)
    static UINT8 Ram[64u * 1024 * 1024];
    std::memset (Ram, 0, sizeof (Ram));
    UINT64 Len = 0;
    if (!LoadImage (pImage, Ram, sizeof (Ram), &Len)) {
        std::printf ("lcx: cannot read image '%s'\n", pImage);
        return 2;
    }
    // --load aout: lay out a classic a.out exec image (the generalized loader auto-detects 16-bit
    // little-endian PDP-11 vs 32-bit big-endian m88k from the header). The break starts at end-of-bss.
    UINT64 AoutEntry = ~(UINT64) 0;
    if (std::strcmp (Opt (argc, argv, "--load", ""), "aout") == 0) {
        AoutInfo Ao = LoadAout (Ram, sizeof (Ram), Len);
        if (!Ao.Ok) { return 2; }
        AoutEntry = Ao.Entry;
        Len       = Ao.LoadedLen;
        g_BrkBase = g_BrkCur = Ao.BrkBase;
    }
    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));
    State.RamSize = sizeof (Ram);
    CHAR8 CONST *pArchName = Opt (argc, argv, "--arch", "v20");
    ArchSetup A = MakeArch (pArchName, Ram, sizeof (Ram), &State);
    if (A.pArch == nullptr) {
        std::printf ("lcx %s: could not build the '%s' architecture\n", pVerb, pArchName);
        pBackend->Release ();
        return 1;
    }
    // Word-addressed arches (byte_size != 8, e.g. PDP-10 with 36-bit words) are only correct on
    // the interpreter backend: every other backend computes RAM widths as Bits/8, silently
    // truncating to 32 bits and dropping the top 4 bits of a 36-bit word. Fail loudly here so a
    // wrong backend is never silently mis-executed; decode/disasm (no execution) are unaffected.
    bool WordAddressed = false;
    CPU_ARCH_INFO ArchInfo;
    std::memset (&ArchInfo, 0, sizeof (ArchInfo));
    A.pArch->GetInfo (&ArchInfo);
    WordAddressed = (ArchInfo.ByteSize != 0 && ArchInfo.ByteSize != 8);
    if (WordAddressed) {
        CHAR8 CONST *pBeName = pBackend->GetName ();
        if (std::strcmp (pBeName, "interpreter") != 0) {
            std::printf ("lcx %s: error: word-addressed architectures (byte_size=%u, != 8)"
                         " currently require the interpreter backend;"
                         " backend '%s' does not support >8-bit addressable units\n",
                         pVerb, (unsigned) ArchInfo.ByteSize, pBeName);
            A.pArch->Release ();
            pBackend->Release ();
            return 1;
        }
    }

    // DEC .SAV loader: on a word-addressed arch, an image named *.sav (or `--load sav`) is a classic
    // PDP-10 save image, NOT a raw core dump. LoadImage already read the raw .SAV bytes into Ram; the
    // SAV block layout differs from core memory, so copy the file bytes out, re-zero the word memory,
    // and lay each block's words into their word cells. The entry PC and the CFG end come from the
    // image (the start word / highest word loaded), in WORD units.
    UINT64 SavEntry = ~(UINT64) 0;
    UINT64 SavEnd   = 0;
    bool   WantSav  = WordAddressed
                      && (HasExtension (pImage, ".sav")
                          || std::strcmp (Opt (argc, argv, "--load", ""), "sav") == 0);
    if (WantSav) {
        std::vector<UINT8> File (Ram, Ram + Len);              // the raw .SAV bytes, before relayout
        std::memset (Ram, 0, sizeof (Ram));                    // clear the word memory we load into
        SavInfo Sv = LoadSav (File.data (), Len, Ram, sizeof (Ram));
        if (!Sv.Ok) {
            A.pArch->Release ();
            pBackend->Release ();
            return 2;
        }
        SavEntry = Sv.Entry;
        SavEnd   = Sv.End;
    }

    // --rim: interpret the file as a DEC RIM (Read-In Mode) paper-tape image. The raw file bytes
    // are still in Ram from LoadImage; copy them out, re-zero word memory, then run LoadRim to
    // deposit each datum into its target word cell. The start address comes from the tape JMP word.
    UINT64 RimEntry = ~(UINT64) 0;
    UINT64 RimEnd   = 0;
    bool   WantRim  = Flag (argc, argv, "--rim");
    if (WantRim) {
        std::vector<UINT8> File (Ram, Ram + Len);              // raw tape bytes, before relayout
        std::memset (Ram, 0, sizeof (Ram));
        RimInfo Ri = LoadRim (File.data (), Len, Ram, sizeof (Ram));
        if (!Ri.Ok) {
            std::printf ("lcx: RIM tape parse failed\n");
            A.pArch->Release ();
            pBackend->Release ();
            return 2;
        }
        RimEntry = Ri.Start;
        RimEnd   = Ri.Start + 1;                               // end: at least past the start word
    }

    // --raw18: the file is already a flat array of 8-byte word cells (as written by pdp1_asm.py);
    // LoadImage already placed the bytes verbatim in Ram, so no relayout is needed. --start N
    // (default 0) gives the entry word index; octal (0o..) and decimal both accepted.
    UINT64 Raw18Entry = ~(UINT64) 0;
    bool   WantRaw18  = Flag (argc, argv, "--raw18");
    if (WantRaw18) {
        Raw18Entry = ParseNum (Opt (argc, argv, "--start", "0"));
    }

    CPU_ADDR Entry = (SavEntry   != ~(UINT64) 0) ? (CPU_ADDR) SavEntry
                   : (RimEntry   != ~(UINT64) 0) ? (CPU_ADDR) RimEntry
                   : (Raw18Entry != ~(UINT64) 0) ? (CPU_ADDR) Raw18Entry
                   : (AoutEntry  != ~(UINT64) 0) ? (CPU_ADDR) AoutEntry
                   : (CPU_ADDR) std::strtoull (Opt (argc, argv, "--entry", "0"), nullptr, 0);
    // The CFG walk reads units of the arch's addressable size: bytes for a byte ISA, WORDS for a
    // word-addressed arch. A .SAV gives the end in words directly; otherwise Len is a byte count.
    // End is in word units for word-addressed arches and byte units for byte-addressed arches.
    // --raw18 files are 8-byte cells: convert the byte length to a word count.
    CPU_ADDR End   = WantSav   ? (CPU_ADDR) SavEnd
                   : WantRim   ? (CPU_ADDR) RimEnd
                   : WantRaw18 ? (CPU_ADDR) (Len / CPU_WORD_CELL_BYTES)
                   : (CPU_ADDR) Len;
    bool Cache = Flag (argc, argv, "--cache");

    // --reg <i>=<v>: seed an initial general register before running (entry-state setup for a
    // user-space run -- e.g. an argument in r2, a return/exit address in the link register).
    for (int I = 1; I + 1 < argc; I++) {
        if (std::strcmp (argv[I], "--reg") == 0) {
            CHAR8 CONST *pEq = std::strchr (argv[I + 1], '=');
            if (pEq != nullptr) {
                UINT32 Idx = (UINT32) std::strtoul (argv[I + 1], nullptr, 0);
                if (Idx < 32) { State.Reg[Idx] = (UINT64) std::strtoull (pEq + 1, nullptr, 0); }
            }
        }
    }

    // OpenBSD/m88k user-space entry state: build the argc/argv/envp stack frame and point the stack
    // (r31) and link (r1) registers at it. Guest argv[0] is the image path; tokens after --args
    // become argv[1..]. (Done after --reg so an explicit --reg can still override if needed.)
    if (std::strcmp (Opt (argc, argv, "--abi", ""), "obsd-m88k") == 0) {
        std::vector<std::string> GuestArgs;
        GuestArgs.push_back (pImage);
        bool After = false;
        for (int I = 1; I < argc; I++) {
            if (After) {
                GuestArgs.push_back (argv[I]);
            } else if (std::strcmp (argv[I], "--args") == 0) {
                After = true;
            }
        }
        UINT64 Sp = SetupObsdM88kStack (Ram, sizeof (Ram), GuestArgs, environ);
        State.Reg[M88K_SP]   = Sp;        // r31 -> argc
        State.Reg[M88K_LINK] = 0;         // r1: crt0 calls exit(), so the link is never returned through
        if (g_BrkBase == 0) { g_BrkBase = g_BrkCur = ((UINT64) End + 0xfff) & ~UINT64_C (0xfff); }
    }

    std::printf ("lcx %s: %s, %llu bytes, %s%s\n", pVerb, pArchName,
                 (unsigned long long) Len, Aot ? "AOT" : "JIT", Cache ? " +cache" : "");

    if (Aot) {
        ComPtr<ICpuCode> Code;
        bool Hit = false;
        if (Cache) {
            TranslationCache TCache (TranslationCache::DefaultDir ());
            CachedTranslate (TCache, A.pArch, pBackend, Ram, Entry, End, &Code, &Hit);
            std::printf ("  cache: %s\n", Hit ? "hit (reloaded)" : "miss (translated + stored)");
        } else {
            GenerateAotCfg (A.pArch, pBackend, Entry, End, &Code, nullptr);
        }
        if (Code != nullptr) {
            Code->Execute (Ram, &State, nullptr);
        }
    } else {
        // JIT: translate a region, run to a trap, resume there, until it runs off.
        CPU_ADDR Pc = Entry;
        for (int I = 0; I < 100000; I++) {
            ComPtr<ICpuCode> Code;
            if (FAILED (GenerateAotCfg (A.pArch, pBackend, Pc, End, &Code, nullptr)) || Code == nullptr) {
                break;
            }
            State.TrapPc = CPU_SMC_NO_TRAP;
            State.SyscallVector = CPU_NO_SYSCALL;
            Code->Execute (Ram, &State, nullptr);
            if (State.TrapPc == CPU_SMC_NO_TRAP) {
                break;
            }
            if (State.SyscallVector != CPU_NO_SYSCALL) {
                CHAR8 CONST *Abi = Opt (argc, argv, "--abi", "");
                if (std::strcmp (Abi, "obsd-m88k") == 0) {
                    if (!ObsdM88kSyscall (&State, Ram, sizeof (Ram))) { break; }   // exit -> stop
                    Pc = (CPU_ADDR) State.TrapPc;        // resume after the syscall trap
                    continue;
                }
                // The PDP-11 UNIX personalities (V6/V7 lineage) share the classic `sys`-trap dispatch
                // through libnix. A UNIX `sys` is a TRAP (vector 0o34 = 0x1c); HALT (0x04) and the
                // other traps stop. One handler serves every --abi unix* variant.
                static CHAR8 CONST *CONST Pdp11Abis[] = {
                    "unixv6", "unixv7", "sysiii", "pwb1", "pwb2", "bsd1", "bsd2", "bsd211",
                    "ultrix11", "venix" };
                bool IsPdp11 = false;
                for (CHAR8 CONST *N : Pdp11Abis) { if (std::strcmp (Abi, N) == 0) { IsPdp11 = true; break; } }
                if (IsPdp11 && State.SyscallVector == 0x1c) {        // 0x1c = TRAP vector (the `sys` gate)
                    if (!Pdp11UnixSyscall (&State, Ram, sizeof (Ram))) { break; }
                    Pc = (CPU_ADDR) State.TrapPc;
                    continue;
                }
                // Match the PDP-1 frontend without colliding with "pdp11" (which contains "pdp1"
                // as a substring): require "pdp1" present and "pdp11" absent.
                bool IsPdp1 = std::strstr (pArchName, "pdp1") != nullptr
                              && std::strstr (pArchName, "pdp11") == nullptr;
                if (IsPdp1 && State.SyscallVector == 0072) {        // PDP-1 IOT -> host I/O
                    if (!Pdp1IoTrap (&State, Ram, sizeof (Ram))) { break; }
                    Pc = (CPU_ADDR) State.TrapPc;
                    continue;
                }
                if (IsPdp1 && State.SyscallVector == 0077) { break; }   // PDP-1 HLT -> stop
                break;                                   // a HLT/INT trap with no host handler: stop
            }
            Pc = (CPU_ADDR) State.TrapPc;
        }
    }
    DumpRegs (A, &State);
    if (CHAR8 CONST *pDump = Opt (argc, argv, "--dump", nullptr)) {
        CPU_ADDR Addr = (CPU_ADDR) ParseNum (pDump);
        if (WordAddressed) {
            // Word-addressed arch: Addr is a word index; print in octal, value from the word cell.
            UINT64 Off = (UINT64) Addr * CPU_WORD_CELL_BYTES;
            UINT64 Val = 0;
            for (UINT32 B = 0; B < CPU_WORD_CELL_BYTES; B++) { Val |= (UINT64) Ram[Off + B] << (8 * B); }
            UINT32 CONST Digits = (ArchInfo.ByteSize + 3) / 4;
            std::printf ("[0o%llo] = 0x%0*llx\n", (unsigned long long) Addr,
                         (int) Digits, (unsigned long long) Val);
        } else {
            std::printf ("[0x%llx] = 0x%04x\n", (unsigned long long) Addr,
                         (unsigned) (Ram[Addr] | (Ram[Addr + 1] << 8)));
        }
    }
    A.pArch->Release ();
    pBackend->Release ();
    return 0;
}

// Render a code address per the arch's display rule (CPU_ARCH_INFO). Flat is "$%04x:"; a
// segmented arch (AddrSegShift != 0, e.g. x86 real-mode CS:IP) is "seg:off:" -- off is the low
// AddrOffBits bits, seg is the rest shifted down by AddrSegShift (so seg*2^shift + off == linear).
static void
FormatAddr (CPU_ARCH_INFO CONST &Info, UINT64 Linear, char *pBuf, size_t Max)
{
    if (Info.AddrSegShift == 0 || Info.AddrOffBits == 0) {
        std::snprintf (pBuf, Max, "$%04llx:", (unsigned long long) Linear);
        return;
    }
    UINT32 B       = Info.AddrOffBits;
    UINT64 OffMask = (B >= 64) ? ~UINT64_C (0) : ((UINT64_C (1) << B) - 1);
    UINT64 Off     = Linear & OffMask;
    UINT64 Seg     = (Linear & ~OffMask) >> Info.AddrSegShift;
    // The segment is shown the same width as the offset (CS and IP are both 16-bit); the field
    // width is only a minimum, so a larger segment still prints in full.
    int Digits = (int) ((B + 3) / 4);
    std::snprintf (pBuf, Max, "%0*llx:%0*llx:", Digits, (unsigned long long) Seg,
                   Digits, (unsigned long long) Off);
}

static int
CmdDisasm (int argc, char **argv, CHAR8 CONST *pArgv0)
{
    CHAR8 CONST *pImage = Positional (argc, argv, 0);
    if (pImage == nullptr) {
        std::printf ("usage: lcx disasm <image> [--arch v20|6502] [--count N] [--entry N]\n");
        return 2;
    }
    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    UINT64 Len = 0;
    if (!LoadImage (pImage, Ram, sizeof (Ram), &Len)) {
        std::printf ("lcx: cannot read image '%s'\n", pImage);
        return 2;
    }
    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));
    ArchSetup A = MakeArch (Opt (argc, argv, "--arch", "v20"), Ram, sizeof (Ram), &State);
    CPU_ARCH_INFO Info;
    std::memset (&Info, 0, sizeof (Info));
    A.pArch->GetInfo (&Info);
    CPU_ADDR Pc = (CPU_ADDR) std::strtoull (Opt (argc, argv, "--entry", "0"), nullptr, 0);
    UINT32 Count = (UINT32) std::strtoul (Opt (argc, argv, "--count", "16"), nullptr, 0);
    for (UINT32 I = 0; I < Count; I++) {
        char Line[64], Addr[32];
        A.pArch->Disassemble (Pc, Line, sizeof (Line));
        FormatAddr (Info, Pc, Addr, sizeof (Addr));
        UINT32 Tag = 0; CPU_ADDR NewPc = 0, NextPc = 0;
        if (FAILED (A.pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc)) || NextPc <= Pc || NextPc > Len) {
            std::printf ("  %s  %s\n", Addr, Line);
            break;
        }
        std::printf ("  %s  %s\n", Addr, Line);
        Pc = NextPc;
    }
    A.pArch->Release ();
    return 0;
}

static int
CmdDebug (int argc, char **argv, CHAR8 CONST *pArgv0)
{
    CHAR8 CONST *pImage = Positional (argc, argv, 0);
    if (pImage == nullptr) {
        std::printf ("usage: lcx debug <image> [--arch v20|6502]\n");
        return 2;
    }
    ICpuBackend *pBackend = LoadBackendBundle (BackendPath (argc, argv, pArgv0).c_str ());
    if (pBackend == nullptr) {
        std::printf ("lcx: cannot load backend\n");
        return 2;
    }
    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    UINT64 Len = 0;
    if (!LoadImage (pImage, Ram, sizeof (Ram), &Len)) {
        std::printf ("lcx: cannot read image '%s'\n", pImage);
        return 2;
    }
    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));
    State.RamSize = sizeof (Ram);
    ArchSetup A = MakeArch (Opt (argc, argv, "--arch", "v20"), Ram, sizeof (Ram), &State);
    Debugger Debugger (A.pArch, pBackend, Ram, sizeof (Ram), &State, 0, (CPU_ADDR) Len,
                         A.Regs, A.RegBytes, A.Flags);
    int Rc = Debugger.Repl ();
    A.pArch->Release ();
    pBackend->Release ();
    return Rc;
}

static int
CmdAot (int argc, char **argv, CHAR8 CONST * /*pArgv0*/)
{
    CHAR8 CONST *pImage = Positional (argc, argv, 0);
    if (pImage == nullptr) {
        std::printf ("usage: lcx aot <image> -o <exe> [--arch v20|6502] [--result <addr>]\n");
        return 2;
    }
    CHAR8 CONST *pOut = Opt (argc, argv, "-o", "a.out");
    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    UINT64 Len = 0;
    if (!LoadImage (pImage, Ram, sizeof (Ram), &Len)) {
        std::printf ("lcx: cannot read image '%s'\n", pImage);
        return 2;
    }
    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));
    ArchSetup A = MakeArch (Opt (argc, argv, "--arch", "v20"), Ram, sizeof (Ram), &State);
    CPU_ADDR Entry = (CPU_ADDR) std::strtoull (Opt (argc, argv, "--entry", "0"), nullptr, 0);
    LC_NATIVE_OPTIONS NOpt;
    CHAR8 CONST *pRes = Opt (argc, argv, "--result", nullptr);
    NOpt.DumpResult = pRes != nullptr;
    NOpt.ResultAddr = pRes ? (CPU_ADDR) std::strtoull (pRes, nullptr, 0) : 0;

    std::string Source = GenerateNativeC (A.pArch, Ram, (UINT32) Len, Entry, (CPU_ADDR) Len, NOpt);
    A.pArch->Release ();
    if (Source.empty ()) {
        std::printf ("lcx aot: code generation failed\n");
        return 1;
    }
    std::string Error;
    if (!CompileNative (Source, pOut, &Error)) {
        std::printf ("lcx aot: host cc failed: %s\n", Error.c_str ());
        return 1;
    }
    std::printf ("lcx aot: wrote standalone executable '%s' (%zu bytes of C, guest regs -> host regs)\n",
                 pOut, Source.size ());
    return 0;
}

// Parse a .upcl file, reporting clang-style diagnostics. Returns the module (caller
// deletes) or nullptr on error.
static Upcl::Module *
UpclParse (CHAR8 CONST *pFile, Upcl::SourceManager &Sm)
{
    std::string Err;
    Upcl::FILE_ID Fid = Sm.LoadFile (pFile, &Err);
    if (Fid == Upcl::InvalidFile) {
        std::printf ("lcx upcl: %s\n", Err.c_str ());
        return nullptr;
    }
    Upcl::DiagnosticEngine Diag (&Sm, stderr);
    Upcl::Parser Parser (&Sm, Fid, &Diag);
    Upcl::Module *pMod = Parser.ParseModule ();
    if (Diag.HadError ()) {
        std::printf ("%u error(s); '%s' is not valid UPCL.\n", Diag.ErrorCount (), pFile);
        delete pMod;
        return nullptr;
    }
    return pMod;
}

// Lay out an instruction's canonical bytes from its format + opcode bindings (operand
// fields left zero) -- the inverse of the decoder's field extraction. Used by
// `produce` to round-trip the description's own encodings.
static void
UpclSynthesize (Upcl::Arch *pArch, Upcl::Insn *pInsn, UINT8 *pBytes, UINT32 *pLen)
{
    Upcl::Format *pFmt = nullptr;
    for (Upcl::Format *F : pArch->Formats) { if (F->Name == pInsn->Format) { pFmt = F; break; } }
    *pLen = 0;
    if (pFmt == nullptr) { return; }
    UINT32 Total = pFmt->TotalBits ();
    *pLen = (Total + 7) / 8;
    UINT32 BitPos = 0;
    for (Upcl::FormatField CONST &FF : pFmt->Fields) {
        UINT64 Val = 0;
        for (Upcl::Field *B : pInsn->Bindings) {
            if (B->Name == FF.Name && B->Value && B->Value->Kind == Upcl::ExprInt) { Val = B->Value->Int; }
        }
        UINT32 ByteOff = BitPos / 8;
        if ((BitPos % 8) == 0 && (FF.Width % 8) == 0) {
            UINT32 Bytes = FF.Width / 8;
            for (UINT32 b = 0; b < Bytes; b++) {
                UINT8 Byte = pArch->Little ? (UINT8) (Val >> (8 * b)) : (UINT8) (Val >> (8 * (Bytes - 1 - b)));
                pBytes[ByteOff + b] = Byte;
            }
        } else {
            UINT32 Shift = 8 - (BitPos % 8) - FF.Width;
            pBytes[ByteOff] |= (UINT8) ((Val & ((1u << FF.Width) - 1)) << Shift);
        }
        BitPos += FF.Width;
    }
}

// ---- a recording ICpuEmitter: prints each builder call as readable SSA ---------------
//
// Used by `lcx upcl emit` to inspect what the UPCL semantics translator produces for an
// instruction body, without needing a real backend. Every value gets an SSA number; each
// operation prints its result and operands. This is a development aid for the translator.

namespace {

class RecValue final : public ComObject<ICpuValue> {
public:
    explicit RecValue (UINT32 Id) : m_Id (Id) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppv) override {
        return DefaultQuery (riid, IID_IUnknown, ppv);
    }
    UINT32 m_Id;
};

class RecBlock final : public ComObject<ICpuBlock> {
public:
    RecBlock (UINT32 Id, CHAR8 CONST *pName) : m_Id (Id), m_Name (pName ? pName : "blk") {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppv) override {
        return DefaultQuery (riid, IID_IUnknown, ppv);
    }
    UINT32      m_Id;
    std::string m_Name;
};

class RecordingEmitter final : public ComObject<ICpuEmitter>,
                               public ICpuSmcEmitter,
                               public ICpuSyscallEmitter {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppv) override {
        if (ppv == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_ICpuSmcEmitter))     { AddRef (); *ppv = static_cast<ICpuSmcEmitter *>     (this); return S_OK; }
        if (CompareGuid (&riid, &IID_ICpuSyscallEmitter)) { AddRef (); *ppv = static_cast<ICpuSyscallEmitter *> (this); return S_OK; }
        return DefaultQuery (riid, IID_IUnknown, ppv);
    }
    UINT32 STDMETHODCALLTYPE AddRef  () override { return ComObject<ICpuEmitter>::AddRef ();  }
    UINT32 STDMETHODCALLTYPE Release () override { return ComObject<ICpuEmitter>::Release (); }

    // Make an SSA input for a decoder operand / parameter (returned to the caller to bind).
    ICpuValue *Input (CHAR8 CONST *pName, UINT32 Bits) {
        RecValue *V = new RecValue (m_Next++);
        std::printf ("  v%u = operand %-6s i%u\n", V->m_Id, pName, Bits);
        return V;
    }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Val, ICpuValue **ppV) override {
        UINT32 Id = Make (ppV);
        std::printf ("  v%u = const.i%u 0x%llx\n", Id, Bits, (unsigned long long) Val);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppV) override {
        UINT32 Id = Make (ppV);
        std::printf ("  v%u = get r%u:i%u\n", Id, Index, Bits);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pV, UINT32 Bits, BOOLEAN Sext) override {
        std::printf ("  put r%u <- v%u  (i%u%s)\n", Index, Id (pV), Bits, Sext ? " sext" : "");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppV) override {
        UINT32 R = Make (ppV);
        std::printf ("  v%u = load.i%u [v%u]\n", R, Bits, Id (pAddr));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pV, ICpuValue *pAddr, UINT32 Bits) override {
        std::printf ("  store.i%u [v%u] <- v%u\n", Bits, Id (pAddr), Id (pV));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppV) override {
        UINT32 R = Make (ppV);
        std::printf ("  v%u = %s v%u, v%u\n", R, BinopName (Op), Id (pA), Id (pB));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppV) override {
        UINT32 R = Make (ppV);
        std::printf ("  v%u = %s v%u\n", R, UnopName (Op), Id (pA));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppV) override {
        UINT32 R = Make (ppV);
        std::printf ("  v%u = %s v%u, v%u\n", R, CmpName (Pred), Id (pA), Id (pB));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppV) override {
        UINT32 R = Make (ppV);
        std::printf ("  v%u = %s.i%u v%u\n", R, CastName (Op), Bits, Id (pA));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *pC, ICpuValue *pT, ICpuValue *pF, ICpuValue **ppV) override {
        UINT32 R = Make (ppV);
        std::printf ("  v%u = select v%u ? v%u : v%u\n", R, Id (pC), Id (pT), Id (pF));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppV) override {
        UINT32 R = Make (ppV);
        std::printf ("  v%u = getflag %s\n", R, FlagName (Flag));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pV) override {
        std::printf ("  setflag %s <- v%u\n", FlagName (Flag), Id (pV));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *pName, ICpuBlock **ppB) override {
        *ppB = new RecBlock (m_NextBlk++, pName);   // labelled where it is entered, not here
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *pB) override {
        m_pCur = static_cast<RecBlock *> (pB);
        std::printf ("%s:\n", Label (pB).c_str ());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppB) override { *ppB = m_pCur; return S_OK; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *pB) override {
        std::printf ("  br %s\n", Label (pB).c_str ()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *pC, ICpuBlock *pT, ICpuBlock *pF) override {
        std::printf ("  condbr v%u ? %s : %s\n", Id (pC), Label (pT).c_str (), Label (pF).c_str ());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        std::printf ("  setpc 0x%llx\n", (unsigned long long) Pc); return S_OK;
    }

    // ICpuSmcEmitter -- record the dispatch target and other SMC/indirect operations so
    // the `lcx upcl decode` output shows $exec / indirect-branch lowering.
    HRESULT STDMETHODCALLTYPE EmitCodeGuard  (CPU_ADDR /*Pc*/) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE IndirectBranch (ICpuValue *pTargetPc) override {
        std::printf ("  indbr v%u\n", Id (pTargetPc)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetDispatchTarget (ICpuValue *pTargetPc) override {
        std::printf ("  dispatch_target v%u\n", Id (pTargetPc)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDispatchTarget (ICpuValue **ppValue) override {
        UINT32 Id2 = Make (ppValue);
        std::printf ("  v%u = get_dispatch_target\n", Id2); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCodeBase (ICpuValue **ppValue) override {
        UINT32 Id2 = Make (ppValue);
        std::printf ("  v%u = get_code_base\n", Id2); return S_OK;
    }

    // ICpuSyscallEmitter -- record EmitSyscall so the $exec / @trap lowering is visible.
    HRESULT STDMETHODCALLTYPE EmitSyscall (UINT32 Vector, ICpuValue *pReturnPc) override {
        std::printf ("  syscall 0x%x v%u\n", Vector, Id (pReturnPc)); return S_OK;
    }

private:
    UINT32 Make (ICpuValue **ppV) { RecValue *V = new RecValue (m_Next++); *ppV = V; return V->m_Id; }
    static UINT32 Id (ICpuValue *pV) { return pV ? static_cast<RecValue *> (pV)->m_Id : ~(UINT32) 0; }
    static std::string Label (ICpuBlock *pB) {
        RecBlock *B = static_cast<RecBlock *> (pB);
        char Buf[64];
        std::snprintf (Buf, sizeof (Buf), "%s#%u", B->m_Name.c_str (), B->m_Id);
        return Buf;
    }

    static CHAR8 CONST *BinopName (CPU_BINOP Op) {
        // One entry per CPU_BINOP value, in enum order. The trailing float ops (fadd..fatan2) must be
        // present or a floating BinaryOp reads past the array end -- printing the adjacent UnopName
        // table's "neg"/"not" for an fadd/fmul (the same out-of-bounds class as the CastName fix).
        static CHAR8 CONST *N[] = { "add", "sub", "mul", "udiv", "sdiv", "urem", "srem",
                                    "and", "or", "xor", "shl", "lshr", "ashr", "rol", "ror",
                                    "fadd", "fsub", "fmul", "fdiv", "fatan2" };
        return N[Op];
    }
    static CHAR8 CONST *UnopName (CPU_UNOP Op) {
        // Includes the floating unary ops (fneg..ftan); omitting them read past the array end.
        static CHAR8 CONST *N[] = { "neg", "com", "not",
                                    "fneg", "fabs", "fsqrt", "f2xm1", "flog2", "ftan" };
        return N[Op];
    }
    static CHAR8 CONST *CmpName (CPU_CMP Pred) {
        // Includes the floating compares (foeq..funo); omitting them read into CastName's "trunc".
        static CHAR8 CONST *N[] = { "eq", "ne", "ult", "ule", "ugt", "uge", "slt", "sle", "sgt", "sge",
                                    "foeq", "folt", "fogt", "funo" };
        return N[Pred];
    }
    static CHAR8 CONST *CastName (CPU_CAST Op) {
        // One entry per CPU_CAST value, in enum order. (Missing the float casts here previously read
        // past the array end -- printing garbage like "P" for a reinterpret bitcast.)
        static CHAR8 CONST *N[] = { "trunc", "zext", "sext",
                                    "sitofp", "fptosi", "fpext", "fptrunc",
                                    "bitcast.itof", "bitcast.ftoi" };
        return N[Op];
    }
    static CHAR8 CONST *FlagName (CPU_FLAG Flag) {
        static CHAR8 CONST *N[] = { "N", "O", "Z", "C", "P", "D", "A" };
        return N[Flag];
    }

    UINT32    m_Next = 0;
    UINT32    m_NextBlk = 0;
    RecBlock *m_pCur = nullptr;
};

} // anonymous namespace

static int
CmdUpcl (int argc, char **argv, CHAR8 CONST * /*pArgv0*/)
{
    CHAR8 CONST *pVerb = Positional (argc, argv, 0);
    CHAR8 CONST *pFile = Positional (argc, argv, 1);
    bool Produce = pVerb != nullptr && std::strcmp (pVerb, "produce") == 0;
    bool Check   = pVerb != nullptr && std::strcmp (pVerb, "check") == 0;
    bool Lex     = pVerb != nullptr && std::strcmp (pVerb, "lex") == 0;
    bool Emit    = pVerb != nullptr && std::strcmp (pVerb, "emit") == 0;
    bool Decode  = pVerb != nullptr && std::strcmp (pVerb, "decode") == 0;
    bool Gen     = pVerb != nullptr && std::strcmp (pVerb, "gen") == 0;
    bool MmuV    = pVerb != nullptr && std::strcmp (pVerb, "mmu") == 0;
    if (pFile == nullptr || (!Check && !Produce && !Lex && !Emit && !Decode && !Gen && !MmuV)) {
        std::printf ("usage: lcx upcl check   <file.upcl>            validate + summarise\n"
                     "       lcx upcl produce <file.upcl>            build the frontend + round-trip its encodings\n"
                     "       lcx upcl lex     <file.upcl>            dump the token stream (lexer development aid)\n"
                     "       lcx upcl emit    <file.upcl> <insn>     translate one instruction body to emitter SSA\n"
                     "       lcx upcl decode  <file.upcl> <bytes..>  decode a byte stream + translate each insn\n"
                     "       lcx upcl mmu     <file.upcl>            translate the MMU page-table walk to SSA\n"
                     "       lcx upcl gen     <file.upcl> <factory> [cpu]   generate a C++ frontend (to stdout)\n"
                     "  (to execute a program: lcx run|translate <image> --arch upcl:<file.upcl>)\n");
        return 2;
    }
    if (MmuV) {
        Upcl::SourceManager Sm;
        Upcl::Module *pMod = UpclParse (pFile, Sm);
        if (pMod == nullptr || pMod->Archs.empty ()) { return 1; }
        Upcl::Arch *pArch = pMod->Archs[0];
        if (pArch->Mmu == nullptr) { std::printf ("lcx upcl mmu: arch '%s' has no mmu { } block\n", pArch->Name.c_str ()); return 1; }
        Upcl::RegisterLayout Layout = Upcl::BuildRegisterLayout (pArch);
        UINT32 WordBits = pArch->WordSize ? pArch->WordSize : 32;
        RecordingEmitter Em;
        Upcl::Translator Tr (Layout, pArch, &Em, WordBits);
        std::printf ("mmu translate (page_size %u):\n", pArch->Mmu->PageSize);
        std::vector<ComPtr<ICpuValue>> Inputs;
        for (Upcl::DecoderOperand *P : pArch->Mmu->Params) {
            UINT32 Bits = (P->VType != nullptr) ? P->VType->Width : WordBits;
            ComPtr<ICpuValue> V (Em.Input (P->Name.c_str (), Bits));
            Upcl::Value Bound; Bound.V = V.Get (); Bound.Bits = Bits;
            Tr.Bind (P->Name, Bound);
            Inputs.push_back (std::move (V));
        }
        bool Ok = Tr.Emit (pArch->Mmu->Body);
        if (!Ok) { std::printf ("  ; (some statements not translated)\n"); }
        return 0;
    }
    if (Gen) {
        CHAR8 CONST *pCreate = Positional (argc, argv, 2);
        CHAR8 CONST *pCpu    = Positional (argc, argv, 3);
        if (pCreate == nullptr) { std::printf ("lcx upcl gen: need a factory name (e.g. Create6502)\n"); return 2; }
        Upcl::SourceManager Sm;
        Upcl::Module *pMod = UpclParse (pFile, Sm);
        if (pMod == nullptr || pMod->Archs.empty ()) { return 1; }
        std::string Out;
        if (!Upcl::GenerateCpp (pMod, 0, pCpu, std::string (pCreate), &Out)) {
            std::fprintf (stderr, "lcx upcl gen: %s\n", Out.c_str ());
            return 1;
        }
        std::fwrite (Out.data (), 1, Out.size (), stdout);
        return 0;
    }
    if (Decode) {
        // The file argument may carry a CPU-model selector: `<file.upcl>[@cpu]` or `<file.upcl>@cpu`.
        // Selecting a model restricts the decode to the base ISA plus exactly that model's features
        // (so a `feature(extended)` KL10 instruction decodes under [@kl10] but not under [@ka10]),
        // matching the gate the executing CreateUpclArch applies. No selector decodes everything.
        std::string Spec (pFile), FilePart (Spec), Cpu;
        std::string::size_type At = Spec.rfind ('@');
        if (At != std::string::npos) {
            Cpu = Spec.substr (At + 1);
            std::string::size_type LB = (At > 0) ? At - 1 : std::string::npos;
            // Accept the bracketed form `name[@cpu]`: drop a trailing ']' and the matching '['.
            if (!Cpu.empty () && Cpu.back () == ']') { Cpu.pop_back (); }
            FilePart = (LB != std::string::npos && Spec[LB] == '[') ? Spec.substr (0, LB)
                                                                    : Spec.substr (0, At);
        }
        Upcl::SourceManager Sm;
        Upcl::Module *pMod = UpclParse (FilePart.c_str (), Sm);
        if (pMod == nullptr || pMod->Archs.empty ()) { return 1; }
        Upcl::Arch *pArch = pMod->Archs[0];
        Upcl::RegisterLayout Layout = Upcl::BuildRegisterLayout (pArch);
        std::set<std::string> Enabled = Upcl::ResolveCpuFeatures (pArch, Cpu.empty () ? nullptr : Cpu.c_str ());
        Upcl::Decoder Dec (pArch, &Layout, &Enabled);
        UINT32 WordBits = pArch->WordSize ? pArch->WordSize : 16;
        bool CONST WordAddressed = Dec.IsWordAddressed ();

        // The operand stream follows the file name. On a byte-addressed arch each positional is one
        // byte; on a WORD-ADDRESSED arch (PDP-10) each positional is a full machine word value, so the
        // 36-bit instruction word is supplied as a single number (e.g. octal 0o201040000005). Numbers
        // accept 0x.. hex, 0b.. binary, 0o.. / 0.. octal (the PDP-10 word convention), or decimal.
        auto ParseNum = [] (CHAR8 CONST *pTok) -> UINT64 {
            if ((pTok[0] == '0') && (pTok[1] == 'o' || pTok[1] == 'O')) {
                return (UINT64) std::strtoull (pTok + 2, nullptr, 8);
            }
            return (UINT64) std::strtoull (pTok, nullptr, 0);
        };

        std::vector<UINT8>  Bytes;
        std::vector<UINT64> Words;
        for (int I = 2; ; I++) {
            CHAR8 CONST *pTok = Positional (argc, argv, I);
            if (pTok == nullptr) { break; }
            UINT64 V = ParseNum (pTok);
            if (WordAddressed) { Words.push_back (V); } else { Bytes.push_back ((UINT8) V); }
        }
        std::vector<UINT8> CONST &Stream = Bytes;        // alias for the byte-path printing below
        UINT64 CONST Count = WordAddressed ? Words.size () : Bytes.size ();
        if (Count == 0) {
            std::printf ("lcx upcl decode: need at least one %s\n", WordAddressed ? "word" : "byte");
            return 2;
        }

        UINT64 Pos = 0;
        while (Pos < Count) {
            Upcl::DecodedInsn D;
            bool Ok = WordAddressed ? Dec.DecodeWord (Words.data (), Words.size (), Pos, &D)
                                    : Dec.Decode (Stream.data (), Stream.size (), Pos, &D);
            if (!Ok) {
                if (WordAddressed) {
                    std::printf ("0x%04llx: dw 0o%llo  (no encoding matched)\n",
                                 (unsigned long long) Pos, (unsigned long long) Words[(size_t) Pos]);
                } else {
                    std::printf ("0x%04llx: db 0x%02x  (no encoding matched)\n",
                                 (unsigned long long) Pos, Stream[(size_t) Pos]);
                }
                Pos += 1;
                continue;
            }
            // Disassembly line: the instruction and each resolved operand. A decoded entry is
            // either a regular insn (pInsn) or a jump insn (pJump) -- print whichever was matched.
            CHAR8 CONST *pName = (D.pInsn != nullptr) ? D.pInsn->Name.c_str ()
                               : (D.pJump != nullptr) ? D.pJump->Name.c_str () : "?";
            std::printf ("0x%04llx: %-6s", (unsigned long long) Pos, pName);
            for (auto CONST &Kv : D.Operands) {
                Upcl::Operand CONST &Op = Kv.second;
                if (Op.Kind == Upcl::Operand::Reg) {
                    std::printf (" %s=%s", Kv.first.c_str (), Layout.Phys[Op.RegIndex].Name.c_str ());
                } else if (Op.Kind == Upcl::Operand::Mem) {
                    // A computed memory address: render its base+displacement text (e.g. "r0+0x2000")
                    // and the signed displacement value so a variable-length disp is visible at decode.
                    std::printf (" %s=[%s] disp=0x%llx", Kv.first.c_str (), Op.MemText.c_str (),
                                 (unsigned long long) Op.Disp);
                } else {
                    std::printf (" %s=0x%llx", Kv.first.c_str (), (unsigned long long) Op.ImmValue);
                }
            }
            std::printf ("   (%u %s)\n", D.Length, WordAddressed ? "word(s)" : "byte(s)");

            // Translate the body with the decoded operands bound to their locations. A jump insn
            // keeps its body in Pre + Action (regular insns keep theirs in Semantics).
            RecordingEmitter Em;
            Upcl::Translator Tr (Layout, pArch, &Em, WordBits);
            for (auto CONST &Kv : D.Operands) { Upcl::Operand Op = Kv.second; Tr.BindOperand (Kv.first, Op); }
            if (D.pInsn != nullptr) {
                Tr.Emit (D.pInsn->Semantics);
            } else if (D.pJump != nullptr) {
                Tr.Emit (D.pJump->Pre);
                Tr.Emit (D.pJump->Action);
            }

            Pos += D.Length;
        }
        return 0;
    }
    if (Emit) {
        CHAR8 CONST *pInsnName = Positional (argc, argv, 2);
        if (pInsnName == nullptr) { std::printf ("lcx upcl emit: need an instruction name\n"); return 2; }
        Upcl::SourceManager Sm;
        Upcl::Module *pMod = UpclParse (pFile, Sm);
        if (pMod == nullptr || pMod->Archs.empty ()) { return 1; }
        Upcl::Arch *pArch = pMod->Archs[0];
        Upcl::Insn *pInsn = nullptr;
        for (Upcl::Insn *I : pArch->Insns) {
            if (I->Name == pInsnName) { pInsn = I; break; }
        }
        if (pInsn == nullptr) { std::printf ("lcx upcl emit: no instruction '%s'\n", pInsnName); return 1; }

        Upcl::RegisterLayout Layout = Upcl::BuildRegisterLayout (pArch);
        UINT32 WordBits = pArch->WordSize ? pArch->WordSize : 16;
        RecordingEmitter Em;
        Upcl::Translator Tr (Layout, pArch, &Em, WordBits);

        // Decoder operands (src, dst, ...) are the instruction's inputs: bind each to a
        // recorded SSA operand so the body can read and write them.
        std::vector<ComPtr<ICpuValue>> Inputs;
        std::printf ("insn %s:\n", pInsn->Name.c_str ());
        for (Upcl::DecoderOperand *D : pArch->DecoderOps) {
            UINT32 Bits = (D->VType != nullptr) ? D->VType->Width : WordBits;
            ComPtr<ICpuValue> V (Em.Input (D->Name.c_str (), Bits));
            Upcl::Value Bound; Bound.V = V.Get (); Bound.Bits = Bits;
            Tr.Bind (D->Name, Bound);
            Inputs.push_back (std::move (V));
        }
        bool Ok = Tr.Emit (pInsn->Semantics);
        if (!Ok) { std::printf ("  ; (some statements not yet translated -- e.g. control flow)\n"); }
        return 0;
    }
    if (Lex) {
        Upcl::SourceManager Sm;
        std::string Err;
        Upcl::FILE_ID Fid = Sm.LoadFile (pFile, &Err);
        if (Fid == Upcl::InvalidFile) { std::printf ("lcx upcl: %s\n", Err.c_str ()); return 1; }
        Upcl::DiagnosticEngine Diag (&Sm, stderr);
        Upcl::Lexer Lx (&Sm, Fid, &Diag);
        for (;;) {
            Upcl::Token T = Lx.Next ();
            if (T.Kind == Upcl::TokEof) { break; }
            std::printf ("%-24s %s\n", Upcl::TokenName (T.Kind), T.Text.c_str ());
        }
        return Diag.HadError () ? 1 : 0;
    }
    Upcl::SourceManager Sm;
    Upcl::Module *pMod = UpclParse (pFile, Sm);
    if (pMod == nullptr) { return 1; }

    std::printf ("%s: ok -- %zu architecture(s)\n", pFile, pMod->Archs.size ());
    for (Upcl::Arch *pArch : pMod->Archs) {
        std::printf ("  arch \"%s\" (%s): %s-endian, word=%u addr=%u, %zu register(s), %zu format(s), %zu instruction(s)\n",
                     pArch->Name.c_str (), pArch->FullName.c_str (), pArch->Little ? "little" : "big",
                     pArch->WordSize, pArch->AddressSize, pArch->Registers.size (),
                     pArch->Formats.size (), pArch->Insns.size ());
        for (Upcl::Format *pFmt : pArch->Formats) {
            std::printf ("    format %-8s ", pFmt->Name.c_str ());
            for (Upcl::FormatField CONST &Ff : pFmt->Fields) { std::printf ("%s:%u ", Ff.Name.c_str (), Ff.Width); }
            std::printf (" (%u bits)\n", pFmt->TotalBits ());
        }
        if (pArch->RegFile != nullptr) {
            for (Upcl::Group *pG : pArch->RegFile->Groups) {
                std::printf ("    group %-4s", pG->Name.c_str ());
                for (Upcl::RegDecl *pR : pG->Regs) {
                    std::printf (" %s%s", pR->VType ? pR->VType->Spelling.c_str () : "?", pR->VType ? "" : "");
                    if (pR->RepeatCount != nullptr) { std::printf ("[x]"); }
                    std::printf (":%s", pR->Name.c_str ());
                    if (pR->Binding != nullptr) {
                        Upcl::RegBinding *pB = pR->Binding;
                        if (!pB->Meta.empty ()) { std::printf ("->%%%s", pB->Meta.c_str ()); }
                        if (pB->Split != nullptr) {
                            std::printf ("{");
                            for (size_t i = 0; i < pB->Split->Binds.size (); i++) {
                                Upcl::BitBind *pBb = pB->Split->Binds[i];
                                if (i) { std::printf (pB->Split->Union ? "," : ":"); }
                                std::printf ("%s", pBb->IsConst ? "0" : pBb->Name.c_str ());
                                if (!pBb->MetaMap.empty ()) { std::printf ("->%%%s", pBb->MetaMap.c_str ()); }
                                if (!pBb->SrcBind.empty ()) { std::printf ("%s%s", pBb->Bidi ? "<->" : "<-", pBb->SrcBind.c_str ()); }
                            }
                            std::printf ("}");
                        }
                    }
                }
                std::printf ("\n");
            }

            // The flattened layout the interpreter / codegen consume: physical storage,
            // sub-registers (aliased bit fields), and flag bits (with their %meta roles).
            Upcl::RegisterLayout Layout = Upcl::BuildRegisterLayout (pArch);
            std::printf ("    layout: %zu physical, %zu sub-register(s), %zu flag(s)\n",
                         Layout.Phys.size (), Layout.Subs.size (), Layout.Flags.size ());
            for (Upcl::RegPhys CONST &P : Layout.Phys) {
                std::printf ("      r%-3u %-6s #i%-3u%s%s\n", P.Index, P.Name.c_str (), P.Width,
                             P.IsPc ? " [PC]" : "", P.IsPsr ? " [PSR]" : "");
            }
            for (Upcl::RegSub CONST &S : Layout.Subs) {
                std::printf ("      sub  %-6s = %s[%u:%u]\n", S.Name.c_str (),
                             Layout.Phys[S.Parent].Name.c_str (), S.Lo + S.Width - 1, S.Lo);
            }
            for (Upcl::RegFlag CONST &F : Layout.Flags) {
                std::printf ("      flag %-6s = %s[%u]%s%s\n", F.Name.c_str (),
                             Layout.Phys[F.Parent].Name.c_str (), F.Bit,
                             F.Meta.empty () ? "" : " -> %", F.Meta.c_str ());
            }
        }
        for (Upcl::RegSet *pRs : pArch->RegSets) {
            std::printf ("    regset %-8s [", pRs->Name.c_str ());
            for (size_t K = 0; K < pRs->Regs.size (); K++) { std::printf ("%s%s", K ? "," : "", pRs->Regs[K].c_str ()); }
            std::printf ("]\n");
        }
        for (Upcl::Feature CONST &Ft : pArch->Features) {
            std::printf ("    feature %-10s %s\n", Ft.Name.c_str (), Ft.Doc.c_str ());
        }
        for (Upcl::Cpu *pC : pArch->Cpus) {
            std::printf ("    cpu \"%s\" =", pC->Name.c_str ());
            for (std::string CONST &F : pC->Features) { std::printf (" %s", F.c_str ()); }
            std::printf ("\n");
        }
        for (Upcl::Insn *pInsn : pArch->Insns) {
            std::printf ("    insn %-14s format %-8s ", pInsn->Name.c_str (), pInsn->Format.c_str ());
            for (Upcl::Field *pB : pInsn->Bindings) {
                std::printf ("%s=0x%llx ", pB->Name.c_str (),
                             (unsigned long long) (pB->Value && pB->Value->Kind == Upcl::ExprInt ? pB->Value->Int : 0));
            }
            if (!pInsn->Feature.empty ()) { std::printf ("[%s] ", pInsn->Feature.c_str ()); }
            if (!pInsn->Super.empty ()) { std::printf (": %s ", pInsn->Super.c_str ()); }
            std::printf (" disasm \"%s\"  %zu stmt(s)\n", pInsn->Disasm.c_str (), pInsn->Semantics.size ());
            for (Upcl::EncAlt *pEnc : pInsn->Encodings) {
                std::printf ("        encode #i%u (", pEnc->WordBits);
                for (size_t I = 0; I < pEnc->Fields.size (); I++) {
                    Upcl::EncField CONST &F = pEnc->Fields[I];
                    std::printf ("%s%s:%u", I ? " " : " ", F.Name.c_str (), F.Width);
                    if (F.HasConst)            { std::printf ("=0x%llx", (unsigned long long) F.Const); }
                    else if (!F.Operand.empty ()) {
                        std::printf ("->%s", F.Operand.c_str ());
                        if (!F.RegMap.empty ()) {
                            std::printf ("[");
                            for (size_t K = 0; K < F.RegMap.size (); K++) { std::printf ("%s%s", K ? "," : "", F.RegMap[K].c_str ()); }
                            std::printf ("]");
                        }
                    }
                }
                std::printf (" )\n");
            }
        }
        // Conflict check: per CPU model, two enabled instructions sharing the same format
        // AND the same fixed-field bindings would decode the same bytes -- an ambiguity.
        // (This is what "any of their features, if not conflicting" guards against.)
        auto Enabled = [] (Upcl::Insn *I, Upcl::Cpu *C) -> bool {
            if (I->Feature.empty ()) { return true; }
            for (std::string CONST &F : C->Features) { if (F == I->Feature) { return true; } }
            return false;
        };
        auto SameEncoding = [] (Upcl::Insn *A, Upcl::Insn *B) -> bool {
            // This check is for the experimental `format`/binding model. Standard-syntax
            // instructions carry their own `encode` bit-fields (no shared format), so an
            // empty format here means "not comparable" rather than "the same".
            if (A->Format.empty () || B->Format.empty ()) { return false; }
            if (A->Format != B->Format || A->Bindings.size () != B->Bindings.size ()) { return false; }
            for (Upcl::Field *Ba : A->Bindings) {
                bool Found = false;
                for (Upcl::Field *Bb : B->Bindings) {
                    UINT64 Va = (Ba->Value && Ba->Value->Kind == Upcl::ExprInt) ? Ba->Value->Int : 0;
                    UINT64 Vb = (Bb->Value && Bb->Value->Kind == Upcl::ExprInt) ? Bb->Value->Int : 0;
                    if (Ba->Name == Bb->Name && Va == Vb) { Found = true; break; }
                }
                if (!Found) { return false; }
            }
            return true;
        };
        for (Upcl::Cpu *pC : pArch->Cpus) {
            for (size_t I = 0; I < pArch->Insns.size (); I++) {
                for (size_t J = I + 1; J < pArch->Insns.size (); J++) {
                    if (Enabled (pArch->Insns[I], pC) && Enabled (pArch->Insns[J], pC)
                        && SameEncoding (pArch->Insns[I], pArch->Insns[J])) {
                        std::printf ("    CONFLICT in cpu \"%s\": '%s' and '%s' decode the same bytes\n",
                                     pC->Name.c_str (), pArch->Insns[I]->Name.c_str (), pArch->Insns[J]->Name.c_str ());
                    }
                }
            }
        }
    }

    if (Produce) {
        // Build the live frontend and self-test it: synthesise each instruction's
        // canonical bytes and disassemble them back through the produced architecture.
        ICpuArchitecture *pArch = Upcl::CreateUpclArch (pMod, 0);
        static UINT8 Bytes[64];
        std::printf ("  produced frontend '%s' -- round-tripping encodings:\n", pMod->Archs[0]->Name.c_str ());
        for (Upcl::Insn *pInsn : pMod->Archs[0]->Insns) {
            std::memset (Bytes, 0, sizeof (Bytes));
            UINT32 Len = 0;
            UpclSynthesize (pMod->Archs[0], pInsn, Bytes, &Len);
            pArch->SetCodeMemory (Bytes, sizeof (Bytes));
            char Line[64];
            pArch->Disassemble (0, Line, sizeof (Line));
            std::printf ("    %-14s = ", pInsn->Name.c_str ());
            for (UINT32 b = 0; b < Len; b++) { std::printf ("%02x ", Bytes[b]); }
            std::printf (" -> \"%s\"\n", Line);
        }
        pArch->Release ();
    }
    delete pMod;
    return 0;
}

static int
CmdSystem (int argc, char **argv, CHAR8 CONST *pArgv0)
{
    ICpuBackend *pBackend = LoadBackendBundle (BackendPath (argc, argv, pArgv0).c_str ());
    if (pBackend == nullptr) {
        std::printf ("lcx: cannot load backend\n");
        return 2;
    }
    int Rc = RunSystemDemo (pBackend);                 // the built-in 8086 machine
    pBackend->Release ();
    return Rc;
}

// Whether a header should be parsed as C++: a C++ header extension, or C++ markers in the
// text (so a C++ library shipping a ".h" is still handled). C headers stay C, unchanged.
static bool
HeaderLooksCpp (CHAR8 CONST *pPath)
{
    std::string P = pPath;
    for (CHAR8 CONST *pExt : { ".hpp", ".hh", ".hxx", ".h++", ".ipp", ".tcc", ".cpp", ".cc", ".cxx", ".C", ".H" }) {
        size_t L = std::strlen (pExt);
        if (P.size () >= L && P.compare (P.size () - L, L, pExt) == 0) { return true; }
    }
    std::FILE *pf = std::fopen (pPath, "rb");
    if (pf == nullptr) { return false; }
    char Buf[65536];
    size_t N = std::fread (Buf, 1, sizeof (Buf) - 1, pf);
    std::fclose (pf);
    Buf[N] = '\0';
    std::string S (Buf, N);
    return S.find ("namespace") != std::string::npos || S.find ("template") != std::string::npos ||
           S.find ("class ") != std::string::npos || S.find ("extern \"C\"") != std::string::npos ||
           S.find ("public:") != std::string::npos;
}

// Collect the -I/-D/-std=/-isysroot/--target args to forward to libclang (shared by the
// `headers` and `know derive` paths). The input language is C, or C++ when the header
// looks like C++ -- enabling namespace / extern "C" / class parsing.
static std::vector<CHAR8 CONST *>
CollectClangArgs (int argc, char **argv, CHAR8 CONST *pHeader)
{
    std::vector<CHAR8 CONST *> Out = { "-x", HeaderLooksCpp (pHeader) ? "c++" : "c" };
    for (int I = 0; I < argc; I++) {
        if (argv[I][0] == '-' && (argv[I][1] == 'I' || argv[I][1] == 'D' ||
            std::strncmp (argv[I], "-std", 4) == 0 || std::strcmp (argv[I], "-isysroot") == 0 ||
            std::strncmp (argv[I], "--target", 8) == 0)) {
            Out.push_back (argv[I]);
            if ((std::strcmp (argv[I], "-isysroot") == 0 || std::strcmp (argv[I], "-I") == 0 ||
                 std::strcmp (argv[I], "-D") == 0) && I + 1 < argc) {
                Out.push_back (argv[++I]);
            }
        }
    }
    return Out;
}

// lcx know derive <header> <lib> [-o out.klib] [clang args] -- join signatures + symbols
// into a host-call catalog (v2 step d), optionally saved as a ZOO/zstd knowledge archive.
static int
CmdKnowDerive (int argc, char **argv)
{
    CHAR8 CONST *pHeader = Positional (argc, argv, 1);
    CHAR8 CONST *pLib    = Positional (argc, argv, 2);
    if (pHeader == nullptr || pLib == nullptr) {
        std::printf ("usage: lcx know derive <header.h> <lib.tbd|dylib> [-o out.klib] [-I/-D/...]\n");
        return 2;
    }
    HeaderParser Parser;
    SymbolReader Reader;
    std::string Error;
    std::vector<CHAR8 CONST *> Args = CollectClangArgs (argc, argv, pHeader);
    if (!Parser.Parse (pHeader, Args.data (), (UINT32) Args.size (), &Error)) {
        std::printf ("lcx know: %s\n", Error.c_str ());
        return 2;
    }
    if (!Reader.Read (pLib, &Error)) {
        std::printf ("lcx know: %s\n", Error.c_str ());
        return 2;
    }
    KnowledgeCatalog Catalog;
    Catalog.Derive (Parser, Reader);
    for (HOST_ENTITY CONST &E : Catalog.Functions ()) {
        std::printf ("  [%s] %s %s(%s%s)\n", E.Exported ? "x" : " ",
                     E.ReturnType.c_str (), E.Name.c_str (),
                     E.Params.empty () && !E.Variadic ? "void" : "", E.Variadic ? "..." : "");
    }
    std::printf ("derived %u entit(y/ies): %u exported, %u struct(s)%s\n",
                 (UINT32) Catalog.Functions ().size (), Catalog.ExportedCount (),
                 (UINT32) Catalog.Structs ().size (),
                 Parser.UsedClang () ? " [libclang]" : " [built-in]");
    if (CHAR8 CONST *pOut = Opt (argc, argv, "-o", nullptr)) {
        if (!Catalog.Save (pOut, &Error)) {
            std::printf ("lcx know: %s\n", Error.c_str ());
            return 2;
        }
        std::printf ("wrote %s\n", pOut);
    }
    return 0;
}

// lcx know catalog <archive.klib> -- load and list a derived host-call catalog.
static int
CmdKnowCatalog (int argc, char **argv)
{
    CHAR8 CONST *pArchive = Positional (argc, argv, 1);
    if (pArchive == nullptr) {
        std::printf ("usage: lcx know catalog <archive.klib>\n");
        return 2;
    }
    KnowledgeCatalog Catalog;
    std::string Error;
    if (!Catalog.Load (pArchive, &Error)) {
        std::printf ("lcx know: %s\n", Error.c_str ());
        return 2;
    }
    if (!Catalog.Library ().empty ()) {
        std::printf ("library: %s\n", Catalog.Library ().c_str ());
    }
    for (HEADER_STRUCT CONST &S : Catalog.Structs ()) {
        std::printf ("struct %s (%llu bytes)\n", S.Name.c_str (), (unsigned long long) S.Size);
        for (HEADER_FIELD CONST &F : S.Fields) {
            std::printf ("    +%-4llu %s %s\n", (unsigned long long) F.Offset, F.Type.c_str (), F.Name.c_str ());
        }
    }
    for (HOST_ENTITY CONST &E : Catalog.Functions ()) {
        std::printf ("  [%s] %s %s", E.Exported ? "x" : " ", E.ReturnType.c_str (), E.Name.c_str ());
        if (E.Exported && !E.Symbol.empty () && E.Symbol != E.Name) {
            std::printf ("  -> %s", E.Symbol.c_str ());     // the (mangled) export it binds to
        }
        std::printf ("\n");
    }
    std::printf ("%u entit(y/ies), %u exported, %u struct(s)\n",
                 (UINT32) Catalog.Functions ().size (), Catalog.ExportedCount (),
                 (UINT32) Catalog.Structs ().size ());
    return 0;
}

static int
CmdKnowledge (int argc, char **argv, CHAR8 CONST *pArgv0)
{
    CHAR8 CONST *pVerb = Positional (argc, argv, 0);
    if (pVerb != nullptr && std::strcmp (pVerb, "derive") == 0) {
        return CmdKnowDerive (argc, argv);
    }
    if (pVerb != nullptr && std::strcmp (pVerb, "catalog") == 0) {
        return CmdKnowCatalog (argc, argv);
    }
    if (pVerb != nullptr && std::strcmp (pVerb, "bind") == 0) {
        // lcx know bind <header> <lib> <xml> -- derive a catalog, then run the built-in
        // demo guest whose syscall is dispatched to a dynamically-bound host call.
        CHAR8 CONST *pHeader = Positional (argc, argv, 1);
        CHAR8 CONST *pLib    = Positional (argc, argv, 2);
        CHAR8 CONST *pXml    = Positional (argc, argv, 3);
        if (pHeader == nullptr || pLib == nullptr || pXml == nullptr) {
            std::printf ("usage: lcx know bind <header.h> <lib.tbd|dylib> <mapping.xml>\n");
            return 2;
        }
        ICpuBackend *pBackend = LoadBackendBundle (BackendPath (argc, argv, pArgv0).c_str ());
        if (pBackend == nullptr) {
            std::printf ("lcx: cannot load backend\n");
            return 2;
        }
        int Rc = RunHostCallDemo (pBackend, pHeader, pLib, pXml);
        pBackend->Release ();
        return Rc;
    }
    if (pVerb != nullptr && std::strcmp (pVerb, "marshal") == 0) {
        // lcx know marshal <header> <dylib> <xml> -- derive a catalog including a struct
        // layout, then dispatch a guest call whose struct out-parameter is marshalled back.
        CHAR8 CONST *pHeader = Positional (argc, argv, 1);
        CHAR8 CONST *pDylib  = Positional (argc, argv, 2);
        CHAR8 CONST *pXml    = Positional (argc, argv, 3);
        if (pHeader == nullptr || pDylib == nullptr || pXml == nullptr) {
            std::printf ("usage: lcx know marshal <header.h> <lib.dylib> <mapping.xml>\n");
            return 2;
        }
        ICpuBackend *pBackend = LoadBackendBundle (BackendPath (argc, argv, pArgv0).c_str ());
        if (pBackend == nullptr) {
            std::printf ("lcx: cannot load backend\n");
            return 2;
        }
        int Rc = RunStructDemo (pBackend, pHeader, pDylib, pXml);
        pBackend->Release ();
        return Rc;
    }
    if (pVerb != nullptr && std::strcmp (pVerb, "invoke") == 0) {
        // lcx know invoke <header> <lib> -- derive a catalog from a C++ class header and its
        // library, then actually CALL the constructor and member functions through their bound
        // symbols with a real "this" pointer. No CPU backend: these are direct native calls.
        CHAR8 CONST *pHeader = Positional (argc, argv, 1);
        CHAR8 CONST *pLib    = Positional (argc, argv, 2);
        if (pHeader == nullptr || pLib == nullptr) {
            std::printf ("usage: lcx know invoke <header.hpp> <lib.dylib>\n");
            return 2;
        }
        return RunMethodCallDemo (pHeader, pLib);
    }
    if (pVerb != nullptr && std::strcmp (pVerb, "derive-map") == 0) {
        // lcx know derive-map <header> <lib> <target.abi> -- derive the target->host
        // mapping from a target ABI table + the host catalog, then run a real program.
        CHAR8 CONST *pHeader = Positional (argc, argv, 1);
        CHAR8 CONST *pLib    = Positional (argc, argv, 2);
        CHAR8 CONST *pAbi    = Positional (argc, argv, 3);
        if (pHeader == nullptr || pLib == nullptr || pAbi == nullptr) {
            std::printf ("usage: lcx know derive-map <header.h> <lib.tbd|dylib> <target.abi>\n");
            return 2;
        }
        ICpuBackend *pBackend = LoadBackendBundle (BackendPath (argc, argv, pArgv0).c_str ());
        if (pBackend == nullptr) {
            std::printf ("lcx: cannot load backend\n");
            return 2;
        }
        int Rc = RunDerivedMapDemo (pBackend, pHeader, pLib, pAbi);
        pBackend->Release ();
        return Rc;
    }
    // Default: run the built-in DOS program through the XML knowledge library.
    CHAR8 CONST *pXml = pVerb;
    if (pXml == nullptr) {
        std::printf ("usage: lcx know <library.xml> [--arch v20]\n"
                     "       lcx know derive <header.h> <lib> [-o out.klib]\n"
                     "       lcx know catalog <archive.klib>\n");
        return 2;
    }
    ICpuBackend *pBackend = LoadBackendBundle (BackendPath (argc, argv, pArgv0).c_str ());
    if (pBackend == nullptr) {
        std::printf ("lcx: cannot load backend\n");
        return 2;
    }
    int Rc = RunDosSyscallDemo (pBackend, pXml);       // the built-in DOS program
    pBackend->Release ();
    return Rc;
}

// Physical (allocated) size of a file in bytes, or 0 if unknown. Reveals the space a
// hole-punched/compressed cache actually occupies vs its logical size.
static UINT64
FilePhysicalSize (CHAR8 CONST *pPath)
{
#if defined (__unix__) || defined (__APPLE__)
    struct stat St;
    if (stat (pPath, &St) == 0) { return (UINT64) St.st_blocks * 512; }
#endif
    (VOID) pPath;
    return 0;
}

static UINT64
FileLogicalSize (CHAR8 CONST *pPath)
{
    std::FILE *pf = std::fopen (pPath, "rb");
    if (pf == nullptr) { return 0; }
    std::fseek (pf, 0, SEEK_END);
    long Sz = std::ftell (pf);
    std::fclose (pf);
    return Sz > 0 ? (UINT64) Sz : 0;
}

// Re-pack a DBM cache, storing every artifact verbatim (Compress=FALSE) or zstd+hole-punched
// (Compress=TRUE). Reads each (already-inflated) artifact and its profile, re-stores it under
// the new policy, and flushes. Returns 0 on success.
static int
RepackCache (CHAR8 CONST *pFile, BOOLEAN Compress)
{
    CodeCache Cache (pFile);
    std::string Err;
    if (!Cache.Load (&Err)) { std::printf ("lcx cache: %s\n", Err.c_str ()); return 1; }
    std::vector<std::string> Keys = Cache.Keys ();
    for (std::string CONST &Key : Keys) {
        UINT8 CONST *pB = nullptr;
        UINT64 Len = 0;
        LC_CACHE_PROFILE Prof;
        if (Cache.Lookup (Key, &pB, &Len, &Prof)) {
            Cache.Store (Key, pB, Len, Prof, Compress);
        }
    }
    if (!Cache.Flush (&Err)) { std::printf ("lcx cache: %s\n", Err.c_str ()); return 1; }
    std::printf ("  re-packed %zu artifact(s) %s -> %llu bytes logical, %llu physical\n",
                 Keys.size (), Compress ? "compressed" : "verbatim",
                 (unsigned long long) FileLogicalSize (pFile),
                 (unsigned long long) FilePhysicalSize (pFile));
    return 0;
}

static int
CmdCache (int argc, char **argv)
{
    CHAR8 CONST *pVerb = Positional (argc, argv, 0);

    // File-based DBM (CodeCache) verbs: each takes a <file> argument.
    if (pVerb != nullptr
        && (std::strcmp (pVerb, "dump") == 0 || std::strcmp (pVerb, "compress") == 0
            || std::strcmp (pVerb, "uncompress") == 0 || std::strcmp (pVerb, "clear") == 0)) {
        CHAR8 CONST *pFile = Positional (argc, argv, 1);
        if (pFile == nullptr) {
            std::printf ("usage: lcx cache %s <file>\n", pVerb);
            return 2;
        }
        if (std::strcmp (pVerb, "clear") == 0) {
            int Rc = std::remove (pFile);
            std::printf ("  %s %s\n", Rc == 0 ? "removed" : "could not remove", pFile);
            return Rc == 0 ? 0 : 1;
        }
        if (std::strcmp (pVerb, "compress") == 0)   { return RepackCache (pFile, TRUE); }
        if (std::strcmp (pVerb, "uncompress") == 0) { return RepackCache (pFile, FALSE); }

        // dump / analyze: decode every artifact key, its sizes, compression, and profile.
        CodeCache Cache (pFile);
        std::string Err;
        if (!Cache.Load (&Err)) { std::printf ("lcx cache: %s\n", Err.c_str ()); return 1; }
        std::vector<std::string> Keys = Cache.Keys ();
        std::printf ("cache: %s  (%u artifact(s); %llu bytes logical, %llu physical)\n",
                     pFile, Cache.Count (),
                     (unsigned long long) FileLogicalSize (pFile),
                     (unsigned long long) FilePhysicalSize (pFile));
        for (std::string CONST &Key : Keys) {
            UINT64 Len = 0, CompLen = 0;
            UINT32 Align = 0;
            Cache.RawInfo (Key, &Len, &CompLen, &Align);
            LC_CACHE_PROFILE Prof;
            UINT8 CONST *pB = nullptr;
            UINT64 BLen = 0;
            std::memset (&Prof, 0, sizeof (Prof));
            Cache.Lookup (Key, &pB, &BLen, &Prof);
            std::printf ("  %s\n", Key.c_str ());
            std::printf ("      %llu bytes", (unsigned long long) Len);
            if (CompLen != 0) {
                std::printf (" (zstd %llu, %.0f%%)", (unsigned long long) CompLen,
                             Len ? 100.0 * (double) CompLen / (double) Len : 0.0);
            } else {
                std::printf (" (verbatim)");
            }
            std::printf ("  align=%uK  runs=%llu tier=%u opt=%u\n",
                         (unsigned) (Align / 1024), (unsigned long long) Prof.Runs,
                         Prof.Tier, Prof.OptLevel);
        }
        return 0;
    }

    // Diagnose a backend's host-target fingerprint (ICpuBackendTarget): the host half of the
    // cache key. Prints the native and baseline fingerprints; they must be non-zero and differ.
    if (pVerb != nullptr && std::strcmp (pVerb, "fingerprint") == 0) {
        CHAR8 CONST *pBackendPath = Positional (argc, argv, 1);
        if (pBackendPath == nullptr) {
            std::printf ("usage: lcx cache fingerprint <backend.bundle>\n");
            return 2;
        }
        ICpuBackend *pBackend = LoadBackendBundle (pBackendPath);
        if (pBackend == nullptr) {
            std::printf ("lcx cache: cannot load backend '%s'\n", pBackendPath);
            return 1;
        }
        ICpuBackendTarget *pTarget = nullptr;
        if (FAILED (pBackend->QueryInterface (IID_ICpuBackendTarget, (VOID **) &pTarget)) || pTarget == nullptr) {
            std::printf ("  %s: host-independent (no ICpuBackendTarget)\n", pBackend->GetName ());
            pBackend->Release ();
            return 0;
        }
        pTarget->SetTargetFeatures (LC_FEAT_NATIVE);
        UINT64 Native = pTarget->GetTargetFingerprint ();
        pTarget->SetTargetFeatures (LC_FEAT_BASELINE);
        UINT64 Baseline = pTarget->GetTargetFingerprint ();
        std::printf ("  %s: native fp=%016llx  baseline fp=%016llx\n",
                     pBackend->GetName (), (unsigned long long) Native, (unsigned long long) Baseline);
        int Rc = 0;
        if (Native == 0) {
            std::printf ("  FAIL: native fingerprint is zero\n");
            Rc = 1;
        } else if (Native == Baseline) {
            std::printf ("  FAIL: native and baseline fingerprints are identical\n");
            Rc = 1;
        } else {
            std::printf ("  RESULT: PASS\n");
        }
        pTarget->Release ();
        pBackend->Release ();
        return Rc;
    }

    // Directory-based translation cache (TranslationCache): info / ls / clean.
    TranslationCache Cache (TranslationCache::DefaultDir ());
    std::printf ("cache dir: %s\n", TranslationCache::DefaultDir ().c_str ());
    if (pVerb == nullptr || std::strcmp (pVerb, "info") == 0) {
        auto E = Cache.List ();
        std::printf ("  %zu artifact(s), %llu bytes\n", E.size (), (unsigned long long) Cache.TotalSize ());
    } else if (std::strcmp (pVerb, "ls") == 0) {
        for (auto CONST &E : Cache.List ()) {
            std::printf ("  %s  arch=%s backend=%s  %llu bytes\n",
                         E.Path.substr (E.Path.find_last_of ('/') + 1).c_str (),
                         E.Arch.c_str (), E.Backend.c_str (), (unsigned long long) E.Size);
        }
    } else if (std::strcmp (pVerb, "clean") == 0) {
        std::printf ("  removed %u artifact(s)\n", Cache.Clean ());
    } else {
        std::printf ("usage: lcx cache info | ls | clean            (the shared translation-cache dir)\n");
        std::printf ("       lcx cache dump | compress | uncompress | clear <file>   (a DBM code-cache file)\n");
        return 2;
    }
    return 0;
}

#ifdef LIBCPU_HAVE_ZSTD
// Read a whole file into a byte vector. Returns false on open failure.
static bool
SlurpFile (CHAR8 CONST *pPath, std::vector<UINT8> *pOut)
{
    std::FILE *pf = std::fopen (pPath, "rb");
    if (pf == nullptr) {
        return false;
    }
    std::fseek (pf, 0, SEEK_END);
    long Len = std::ftell (pf);
    std::fseek (pf, 0, SEEK_SET);
    pOut->resize (Len > 0 ? (size_t) Len : 0);
    if (Len > 0) {
        size_t Got = std::fread (pOut->data (), 1, (size_t) Len, pf);
        pOut->resize (Got);
    }
    std::fclose (pf);
    return true;
}

// The member name is the file's basename (knowledge libraries are flat namespaces).
static std::string
BaseName (CHAR8 CONST *pPath)
{
    std::string S (pPath);
    size_t Slash = S.find_last_of ('/');
    return Slash == std::string::npos ? S : S.substr (Slash + 1);
}

//
// lcx klib -- the knowledge-library container (a ZOO-style directory with solid zstd
// compression). create/ls/extract over the archive that v2 knowledge libraries live in.
//
static int
CmdKlib (int argc, char **argv)
{
    CHAR8 CONST *pVerb = Positional (argc, argv, 0);
    if (pVerb == nullptr) {
        std::printf ("usage: lcx klib create <archive> <file...> | ls <archive> | extract <archive> <member> [-o <out>]\n");
        return 2;
    }

    if (std::strcmp (pVerb, "create") == 0) {
        CHAR8 CONST *pArchive = Positional (argc, argv, 1);
        if (pArchive == nullptr || Positional (argc, argv, 2) == nullptr) {
            std::printf ("usage: lcx klib create <archive> <file...>\n");
            return 2;
        }
        ZooWriter Writer;
        for (int I = 2; ; I++) {                       // members start at positional 2
            CHAR8 CONST *pFile = Positional (argc, argv, I);
            if (pFile == nullptr) {
                break;
            }
            std::vector<UINT8> Data;
            if (!SlurpFile (pFile, &Data)) {
                std::printf ("lcx klib: cannot read '%s'\n", pFile);
                return 2;
            }
            Writer.Add (BaseName (pFile), Data.data (), Data.size ());
            std::printf ("  + %-24s %llu bytes\n", BaseName (pFile).c_str (), (unsigned long long) Data.size ());
        }
        std::string Error;
        INT32 Level = (INT32) std::atoi (Opt (argc, argv, "--level", "19"));
        if (!Writer.Save (pArchive, Level, &Error)) {
            std::printf ("lcx klib: %s\n", Error.c_str ());
            return 2;
        }
        std::printf ("wrote %s (%u member(s), zstd level %d)\n", pArchive, Writer.MemberCount (), Level);
        return 0;
    }

    if (std::strcmp (pVerb, "ls") == 0) {
        CHAR8 CONST *pArchive = Positional (argc, argv, 1);
        if (pArchive == nullptr) {
            std::printf ("usage: lcx klib ls <archive>\n");
            return 2;
        }
        ZooArchive Archive;
        std::string Error;
        if (!Archive.Load (pArchive, &Error)) {
            std::printf ("lcx klib: %s\n", Error.c_str ());
            return 2;
        }
        for (std::string CONST &Name : Archive.List ()) {
            std::vector<UINT8> Data;
            Archive.Extract (Name, &Data);
            std::printf ("  %-28s %llu bytes\n", Name.c_str (), (unsigned long long) Data.size ());
        }
        UINT64 Raw = Archive.UncompressedSize ();
        UINT64 Comp = Archive.CompressedSize ();
        double Ratio = Comp ? (double) Raw / (double) Comp : 0.0;
        std::printf ("%u member(s): %llu bytes -> %llu compressed (%.2fx, solid zstd)\n",
                     Archive.MemberCount (), (unsigned long long) Raw, (unsigned long long) Comp, Ratio);
        return 0;
    }

    if (std::strcmp (pVerb, "extract") == 0) {
        CHAR8 CONST *pArchive = Positional (argc, argv, 1);
        CHAR8 CONST *pMember  = Positional (argc, argv, 2);
        if (pArchive == nullptr || pMember == nullptr) {
            std::printf ("usage: lcx klib extract <archive> <member> [-o <out>]\n");
            return 2;
        }
        ZooArchive Archive;
        std::string Error;
        if (!Archive.Load (pArchive, &Error)) {
            std::printf ("lcx klib: %s\n", Error.c_str ());
            return 2;
        }
        std::vector<UINT8> Data;
        if (!Archive.Extract (std::string (pMember), &Data)) {
            std::printf ("lcx klib: no member '%s'\n", pMember);
            return 2;
        }
        CHAR8 CONST *pOut = Opt (argc, argv, "-o", nullptr);
        if (pOut == nullptr) {
            std::fwrite (Data.data (), 1, Data.size (), stdout);   // to stdout by default
        } else {
            std::FILE *pf = std::fopen (pOut, "wb");
            if (pf == nullptr) {
                std::printf ("lcx klib: cannot create '%s'\n", pOut);
                return 2;
            }
            std::fwrite (Data.data (), 1, Data.size (), pf);
            std::fclose (pf);
            std::printf ("extracted %s (%llu bytes) -> %s\n", pMember, (unsigned long long) Data.size (), pOut);
        }
        return 0;
    }

    if (std::strcmp (pVerb, "symbols") == 0) {
        // Derive a library's exported-symbol set from a real toolchain artifact (a .tbd
        // stub or a Mach-O binary) -- the symbol half of a knowledge entity (v2 step b).
        CHAR8 CONST *pLib = Positional (argc, argv, 1);
        if (pLib == nullptr) {
            std::printf ("usage: lcx klib symbols <lib.tbd|dylib> [--grep <substr>]\n");
            return 2;
        }
        SymbolReader Reader;
        std::string Error;
        if (!Reader.Read (pLib, &Error)) {
            std::printf ("lcx klib: %s\n", Error.c_str ());
            return 2;
        }
        CHAR8 CONST *pFmt = SymbolFormatName (Reader.Format ());
        if (!Reader.InstallName ().empty ()) {
            std::printf ("install-name: %s\n", Reader.InstallName ().c_str ());
        }
        CHAR8 CONST *pGrep = Opt (argc, argv, "--grep", nullptr);
        UINT32 Shown = 0;
        bool Raw = Flag (argc, argv, "--raw");               // --raw keeps the mangled form
        for (std::string CONST &S : Reader.Symbols ()) {
            std::string Disp = Raw ? S : DemangleSymbol (S);
            if (pGrep == nullptr || Disp.find (pGrep) != std::string::npos || S.find (pGrep) != std::string::npos) {
                std::printf ("  %s\n", Disp.c_str ());
                Shown++;
            }
        }
        std::printf ("%s: %u exported symbol(s)%s\n", pFmt, (UINT32) Reader.Symbols ().size (),
                     Reader.Symbols ().empty () && !SymbolFormatHasExtractor (Reader.Format ())
                         ? " (format recognised; symbol extraction not yet implemented)" : "");
        if (pGrep != nullptr) {
            std::printf ("  (%u matched '%s')\n", Shown, pGrep);
        }
        return 0;
    }

    if (std::strcmp (pVerb, "demangle") == 0) {
        // Demangle each positional argument (a C++ Itanium symbol) and print the result.
        for (int K = 1;; K++) {
            CHAR8 CONST *pArg = Positional (argc, argv, K);
            if (pArg == nullptr) { break; }
            std::string M = pArg;
            std::printf ("%s\n", DemangleSymbol (M).c_str ());
        }
        return 0;
    }

    if (std::strcmp (pVerb, "headers") == 0) {
        // Derive function signatures + struct layouts from a C header (v2 step c). Uses
        // libclang when present (dlopen'd), else a built-in scanner. Any argv token after
        // the path that looks like a compiler flag (-I.../-D.../-std=.../-isysroot ...) is
        // forwarded to libclang.
        CHAR8 CONST *pHdr = Positional (argc, argv, 1);
        if (pHdr == nullptr) {
            std::printf ("usage: lcx klib headers <file.h> [-I dir] [-D macro] [-std=...] ...\n");
            return 2;
        }
        std::vector<CHAR8 CONST *> ClangArgs = CollectClangArgs (argc, argv, pHdr);
        HeaderParser Parser;
        std::string Error;
        if (!Parser.Parse (pHdr, ClangArgs.data (), (UINT32) ClangArgs.size (), &Error)) {
            std::printf ("lcx klib: %s\n", Error.c_str ());
            return 2;
        }
        for (HEADER_STRUCT CONST &S : Parser.Structs ()) {
            std::printf ("struct %s (%llu bytes)\n", S.Name.c_str (), (unsigned long long) S.Size);
            for (HEADER_FIELD CONST &F : S.Fields) {
                std::printf ("    +%-4llu %s %s\n", (unsigned long long) F.Offset, F.Type.c_str (), F.Name.c_str ());
            }
        }
        for (HEADER_FUNCTION CONST &Fn : Parser.Functions ()) {
            std::printf ("%s %s(", Fn.ReturnType.c_str (), Fn.Name.c_str ());
            for (UINT32 I = 0; I < Fn.Params.size (); I++) {
                std::printf ("%s%s%s%s", I ? ", " : "", Fn.Params[I].Type.c_str (),
                             Fn.Params[I].Name.empty () ? "" : " ", Fn.Params[I].Name.c_str ());
            }
            std::printf ("%s%s)\n", Fn.Variadic ? (Fn.Params.empty () ? "..." : ", ...") : "",
                         Fn.Params.empty () && !Fn.Variadic ? "void" : "");
        }
        std::printf ("%s: %u function(s), %u struct(s)\n", Parser.UsedClang () ? "libclang" : "built-in",
                     (UINT32) Parser.Functions ().size (), (UINT32) Parser.Structs ().size ());
        return 0;
    }

    std::printf ("lcx klib: unknown verb '%s' (create | ls | extract | symbols | headers)\n", pVerb);
    return 2;
}
#endif // LIBCPU_HAVE_ZSTD

// Pick the output device-tree format from a file name's extension (.dtb -> blob), defaulting
// to textual source.
static LibCPU::DT_FORMAT
DtFormatForPath (CHAR8 CONST *pPath)
{
    std::string S = pPath != nullptr ? pPath : "";
    if (S.size () >= 4 && S.compare (S.size () - 4, 4, ".dtb") == 0) { return LibCPU::DtFormatFdtBlob; }
    if (S.size () >= 4 && S.compare (S.size () - 4, 4, ".adt") == 0) { return LibCPU::DtFormatAppleBinary; }
    if (S.size () >= 4 && S.compare (S.size () - 4, 4, ".ofd") == 0) { return LibCPU::DtFormatOpenFirmware; }
    return LibCPU::DtFormatFdtSource;
}

// Write a tree to pOut (by format) or, when pOut is null, print its textual form to stdout.
static int
DtEmit (LibCPU::DeviceTree CONST &Tree, CHAR8 CONST *pOut, LibCPU::DT_FORMAT OutFmt)
{
    std::string Error;
    if (pOut != nullptr) {
        if (!Tree.Save (pOut, OutFmt, &Error)) { std::printf ("lcx dt: %s\n", Error.c_str ()); return 2; }
        std::printf ("lcx dt: wrote %s\n", pOut);
        return 0;
    }
    if (OutFmt == LibCPU::DtFormatFdtBlob || OutFmt == LibCPU::DtFormatAppleBinary) {
        std::printf ("lcx dt: binary output requires -o <file>\n");
        return 2;
    }
    std::string Text;
    if (!Tree.EmitText (OutFmt, &Text, &Error)) { std::printf ("lcx dt: %s\n", Error.c_str ()); return 2; }
    std::fputs (Text.c_str (), stdout);
    return 0;
}

// lcx dt -- compile/decompile/dump/overlay device trees. Input format is auto-detected; output
// format follows the verb (compile -> DTB, decompile/dump -> DTS) or the -o extension.
static int
CmdDt (int argc, char **argv)
{
    using namespace LibCPU;
    CHAR8 CONST *pVerb = Positional (argc, argv, 0);
    CHAR8 CONST *pOut  = Opt (argc, argv, "-o", nullptr);
    if (pVerb == nullptr) {
        std::printf ("usage: lcx dt compile   <in.dts> -o <out.dtb>      compile source to a blob\n"
                     "       lcx dt decompile <in.dtb> [-o <out.dts>]    decompile a blob to source\n"
                     "       lcx dt dump      <in>                       auto-detect and print as source\n"
                     "       lcx dt overlay   <base> <frag> [-o <out>]   compose a fragment onto a base\n");
        return 2;
    }
    std::string Verb = pVerb;
    std::string Error;

    if (Verb == "compile" || Verb == "decompile" || Verb == "dump") {
        CHAR8 CONST *pIn = Positional (argc, argv, 1);
        if (pIn == nullptr) { std::printf ("lcx dt: missing input file\n"); return 2; }
        DeviceTree Tree;
        if (!Tree.Load (pIn, &Error)) { std::printf ("lcx dt: %s\n", Error.c_str ()); return 2; }
        DT_FORMAT OutFmt = (Verb == "compile") ? DtFormatFdtBlob : DtFormatFdtSource;
        if (pOut != nullptr) {
            DT_FORMAT ByExt = DtFormatForPath (pOut);
            // compile produces a binary (a non-binary extension still means a blob); decompile
            // produces text but an explicit binary extension is honoured.
            if (Verb == "compile") { OutFmt = (ByExt == DtFormatFdtSource) ? DtFormatFdtBlob : ByExt; }
            else                   { OutFmt = ByExt; }
        }
        return DtEmit (Tree, pOut, OutFmt);
    }
    if (Verb == "overlay") {
        CHAR8 CONST *pBase = Positional (argc, argv, 1);
        CHAR8 CONST *pFrag = Positional (argc, argv, 2);
        if (pBase == nullptr || pFrag == nullptr) { std::printf ("lcx dt: overlay needs <base> <frag>\n"); return 2; }
        DeviceTree Base, Frag;
        if (!Base.Load (pBase, &Error) || !Frag.Load (pFrag, &Error)) {
            std::printf ("lcx dt: %s\n", Error.c_str ());
            return 2;
        }
        if (!Base.ApplyOverlay (Frag, &Error)) { std::printf ("lcx dt: %s\n", Error.c_str ()); return 2; }
        return DtEmit (Base, pOut, pOut != nullptr ? DtFormatForPath (pOut) : DtFormatFdtSource);
    }
    std::printf ("lcx dt: unknown verb '%s'\n", Verb.c_str ());
    return 2;
}

// Big-endian cell 0 of node property pName (e.g. "phandle"); false if absent or too short.
static bool
NodeCell0 (LibCPU::DtNode &Node, CHAR8 CONST *pName, UINT32 *pOut)
{
    std::string Key = pName;
    LibCPU::DT_PROP *pProp = Node.FindProp (Key);
    if (pProp == nullptr || pProp->Value.size () < 4) { return false; }
    UINT8 CONST *p = pProp->Value.data ();
    *pOut = ((UINT32) p[0] << 24) | ((UINT32) p[1] << 16) | ((UINT32) p[2] << 8) | (UINT32) p[3];
    return true;
}

// Collect every assigned phandle -> node name, so a "signals" reference can be shown by name.
static void
CollectPhandles (LibCPU::DtNode &Node, std::vector<std::pair<UINT32, std::string>> *pOut)
{
    UINT32 Ph = 0;
    if (NodeCell0 (Node, "phandle", &Ph)) { pOut->push_back (std::make_pair (Ph, Node.Name)); }
    for (LibCPU::DtNode &C : Node.Children) { CollectPhandles (C, pOut); }
}

// Describe how a node bound: the COM component and capabilities it exposes, or its compatible if no
// bundle matched, plus any explicitly-wired signal edges resolved to "<- source(line)".
static std::string
DescribeNode (LibCPU::DtNode &Node, LibCPU::MachineBuilder &Builder,
              std::vector<std::pair<UINT32, std::string>> CONST &Phandles)
{
    using namespace LibCPU;
    std::string Out;

    MATCHED_DEVICE CONST *pM = nullptr;
    for (MATCHED_DEVICE CONST &D : Builder.Devices ()) {
        if (D.pNode == &Node) { pM = &D; break; }
    }

    if (pM != nullptr) {
        IDevice *pDev = pM->pDevice;
        IPortDevice         *pPort = nullptr;
        IInterruptSource    *pIrq  = nullptr;
        IInterruptController *pPic = nullptr;
        IMemoryDevice       *pMem  = nullptr;
        IDisplayDevice      *pDisp = nullptr;
        ISignalSource       *pSrc  = nullptr;
        ISignalSink         *pSnk  = nullptr;
        IOptionRomHost      *pRom  = nullptr;
        pDev->QueryInterface (IID_IPortDevice, (VOID **) &pPort);
        pDev->QueryInterface (IID_IInterruptSource, (VOID **) &pIrq);
        pDev->QueryInterface (IID_IInterruptController, (VOID **) &pPic);
        pDev->QueryInterface (IID_IMemoryDevice, (VOID **) &pMem);
        pDev->QueryInterface (IID_IDisplayDevice, (VOID **) &pDisp);
        pDev->QueryInterface (IID_ISignalSource, (VOID **) &pSrc);
        pDev->QueryInterface (IID_ISignalSink, (VOID **) &pSnk);
        pDev->QueryInterface (IID_IOptionRomHost, (VOID **) &pRom);

        std::string Caps;
        if (pPort != nullptr) { Caps += "ports "; }
        if (pIrq  != nullptr) { Caps += "irq-source "; }
        if (pPic  != nullptr) { Caps += "irq-ctrl "; }
        if (pSrc  != nullptr) { Caps += "signal-source "; }
        if (pSnk  != nullptr) { Caps += "signal-sink "; }
        if (pDisp != nullptr) { Caps += "display "; }
        if (pRom  != nullptr) { Caps += "rom-host "; pRom->Release (); }
        char Buf[160];
        if (pMem != nullptr) {
            UINT32 Base = pMem->GetBase (), Size = pMem->GetSize ();
            std::snprintf (Buf, sizeof (Buf), "mem 0x%05x..0x%05x %s ",
                           Base, Base + Size - 1, pMem->IsReadOnly () ? "ro" : "rw");
            Caps += Buf;
        }
        if (!Caps.empty () && Caps.back () == ' ') { Caps.pop_back (); }

        std::snprintf (Buf, sizeof (Buf), "%-9s %s  [%s]", pM->BundleName.c_str (),
                       pDev->GetName (), Caps.c_str ());
        Out = Buf;

        if (pPort != nullptr) { pPort->Release (); }
        if (pIrq  != nullptr) { pIrq->Release (); }
        if (pPic  != nullptr) { pPic->Release (); }
        if (pMem  != nullptr) { pMem->Release (); }
        if (pDisp != nullptr) { pDisp->Release (); }
        if (pSrc  != nullptr) { pSrc->Release (); }
        if (pSnk  != nullptr) { pSnk->Release (); }
    } else {
        std::string Key = "compatible";
        DT_PROP *pCompat = Node.FindProp (Key);
        std::string Dev  = "device_type";
        DT_PROP *pType   = Node.FindProp (Dev);
        if (pCompat != nullptr && !pCompat->Value.empty ()) {
            Out = std::string ("· ") + (CHAR8 CONST *) pCompat->Value.data ();
            if (pType != nullptr && !pType->Value.empty ()) {
                Out += std::string (" (") + (CHAR8 CONST *) pType->Value.data () + ")";
            } else {
                Out += " (no bundle)";
            }
        } else if (pType != nullptr && !pType->Value.empty ()) {
            Out = std::string ("· (") + (CHAR8 CONST *) pType->Value.data () + ")";
        }
    }

    // Resolve any explicit signal wiring ("signals = <&source LINE>, ...") to readable edges.
    std::string SigKey = "signals";
    DT_PROP *pSig = Node.FindProp (SigKey);
    if (pSig != nullptr && pSig->Value.size () >= 8) {
        size_t Cells = pSig->Value.size () / 4;
        UINT8 CONST *p = pSig->Value.data ();
        std::string Wires;
        for (size_t I = 0; I + 1 < Cells; I += 2) {
            UINT32 Ph   = ((UINT32) p[I*4] << 24) | ((UINT32) p[I*4+1] << 16) |
                          ((UINT32) p[I*4+2] << 8) | (UINT32) p[I*4+3];
            UINT32 Line = ((UINT32) p[(I+1)*4] << 24) | ((UINT32) p[(I+1)*4+1] << 16) |
                          ((UINT32) p[(I+1)*4+2] << 8) | (UINT32) p[(I+1)*4+3];
            std::string Name = "?";
            for (std::pair<UINT32, std::string> CONST &E : Phandles) {
                if (E.first == Ph) { Name = E.second; break; }
            }
            char Buf[64];
            std::snprintf (Buf, sizeof (Buf), "%s%s(line%u)", Wires.empty () ? "" : " ",
                           Name.c_str (), Line);
            Wires += Buf;
        }
        if (!Wires.empty ()) { Out += "  <- " + Wires; }
    }
    return Out;
}

// Render a node and its subtree with box-drawing connectors -- the assembled machine as a true tree.
static void
PrintTree (LibCPU::DtNode &Node, std::string CONST &Prefix, bool IsLast, int Depth,
           LibCPU::MachineBuilder &Builder, std::vector<std::pair<UINT32, std::string>> CONST &Phandles)
{
    std::string Branch = Prefix + (IsLast ? "└── " : "├── ");
    std::string Desc   = DescribeNode (Node, Builder, Phandles);
    int Pad = 22 - Depth * 4 - (int) Node.Name.size ();
    if (Pad < 1) { Pad = 1; }
    std::printf ("%s%s%*s%s\n", Branch.c_str (), Node.Name.c_str (), Pad, "", Desc.c_str ());

    std::string ChildPrefix = Prefix + (IsLast ? "    " : "│   ");
    for (size_t I = 0; I < Node.Children.size (); I++) {
        PrintTree (Node.Children[I], ChildPrefix, I + 1 == Node.Children.size (), Depth + 1, Builder, Phandles);
    }
}

// Rewrite each node's relative "libcpu,firmware" path to be relative to the device-tree directory,
// so a ROM image referenced by the machine description resolves no matter where lcx is run from.
static void
ResolveFirmwarePaths (LibCPU::DtNode &Node, std::string CONST &Dir)
{
    std::string Key = "libcpu,firmware";
    LibCPU::DT_PROP *pProp = Node.FindProp (Key);
    if (pProp != nullptr && !pProp->Value.empty ()) {
        std::string Path ((CHAR8 CONST *) pProp->Value.data ());
        if (!Path.empty () && Path[0] != '/') {              // relative -> prepend the DTS directory
            std::string Full = Dir + "/" + Path;
            Node.SetPropString (Key, Full);
        }
    }
    for (LibCPU::DtNode &Child : Node.Children) { ResolveFirmwarePaths (Child, Dir); }
}

// lcx machine -- assemble a machine from hardware-component bundles by matching a device tree
// against the bundles' Info.plist personalities, then list the components COM made.
static int
CmdMachine (int argc, char **argv, CHAR8 CONST *pArgv0)
{
    using namespace LibCPU;
    CHAR8 CONST *pDts = Positional (argc, argv, 0);
    if (pDts == nullptr) {
        std::printf ("usage: lcx machine <machine.dts> [--bundles <dir>]\n");
        return 2;
    }
    CHAR8 CONST *pBundles = Opt (argc, argv, "--bundles", nullptr);
    std::string BundlesDir;
    if (pBundles != nullptr) {
        BundlesDir = pBundles;
    } else {
        std::string Exe = pArgv0 != nullptr ? pArgv0 : ".";
        size_t Slash = Exe.find_last_of ('/');
        BundlesDir = (Slash == std::string::npos) ? std::string (".") : Exe.substr (0, Slash);
    }

    DeviceTree Tree;
    std::string Error;
    if (!Tree.Load (pDts, &Error)) { std::printf ("lcx machine: %s\n", Error.c_str ()); return 2; }

    // Resolve relative "libcpu,firmware" paths against the machine-description's directory, so a
    // ROM image referenced by the device tree is found regardless of the working directory.
    std::string DtsPath = pDts;
    std::string DtsDir  = DtsPath.find_last_of ('/') == std::string::npos ?
                          std::string (".") : DtsPath.substr (0, DtsPath.find_last_of ('/'));
    ResolveFirmwarePaths (Tree.Root, DtsDir);

    MachineBuilder Builder;
    Builder.AddBundleDirectory (BundlesDir.c_str ());
    if (!Builder.Build (Tree, &Error)) { std::printf ("lcx machine: %s\n", Error.c_str ()); return 2; }

    std::string Model = "machine";
    DT_PROP *pModel = Tree.Root.FindProp (Model = "model");
    std::printf ("== %s\n", pModel != nullptr && !pModel->Value.empty () ?
                 (CHAR8 CONST *) pModel->Value.data () : pDts);
    std::printf ("   bundles: %s (%zu match rules)\n", BundlesDir.c_str (), Builder.MatchCount ());
    std::printf ("   components: %zu matched, %zu unmatched\n\n",
                 Builder.Devices ().size (), Builder.Unmatched ().size ());

    bool ShowTree = false;
    for (int I = 0; I < argc; I++) { if (std::strcmp (argv[I], "--tree") == 0) { ShowTree = true; } }
    if (ShowTree) {
        // The assembled machine as a true tree: the device-tree hierarchy, annotated with the COM
        // component each node bound to, its discovered capabilities, and the resolved signal wiring.
        std::vector<std::pair<UINT32, std::string>> Phandles;
        CollectPhandles (Tree.Root, &Phandles);
        std::printf ("/\n");
        for (size_t I = 0; I < Tree.Root.Children.size (); I++) {
            PrintTree (Tree.Root.Children[I], "", I + 1 == Tree.Root.Children.size (), 1, Builder, Phandles);
        }
        std::printf ("\n");
        return 0;
    }

    bool ListRoms = false;
    for (int I = 0; I < argc; I++) { if (std::strcmp (argv[I], "--roms") == 0) { ListRoms = true; } }
    if (ListRoms) {
        // The devices that can host an option ROM (expansion cards), and their conventional address.
        std::printf ("== devices that accept an option ROM (use: --rom <device>=<file>)\n");
        for (MATCHED_DEVICE CONST &D : Builder.Devices ()) {
            IOptionRomHost *pHost = nullptr;
            D.pDevice->QueryInterface (IID_IOptionRomHost, (VOID **) &pHost);
            if (pHost == nullptr) { continue; }
            UINT32 Addr = pHost->GetRomAddress ();
            if (Addr != 0) { std::printf ("  %-12s %-10s  conventional ROM @ 0x%05x\n", D.NodeName.c_str (), D.BundleName.c_str (), Addr); }
            else           { std::printf ("  %-12s %-10s  ROM auto-assigned\n", D.NodeName.c_str (), D.BundleName.c_str ()); }
            pHost->Release ();
        }
        return 0;
    }

    for (MATCHED_DEVICE CONST &D : Builder.Devices ()) {
        IPortDevice      *pPort = nullptr;
        IInterruptSource *pIrq  = nullptr;
        ISignalSource    *pSrc  = nullptr;
        ISignalSink      *pSnk  = nullptr;
        D.pDevice->QueryInterface (IID_IPortDevice, (VOID **) &pPort);
        D.pDevice->QueryInterface (IID_IInterruptSource, (VOID **) &pIrq);
        D.pDevice->QueryInterface (IID_ISignalSource, (VOID **) &pSrc);
        D.pDevice->QueryInterface (IID_ISignalSink, (VOID **) &pSnk);
        std::printf ("  %-16s -> %-9s  %-20s [%s%s%s%s]  (matched \"%s\")\n",
                     D.NodeName.c_str (), D.BundleName.c_str (), D.pDevice->GetName (),
                     pPort != nullptr ? "ports " : "", pIrq != nullptr ? "irq-source " : "",
                     pSrc != nullptr ? "signal-source " : "", pSnk != nullptr ? "signal-sink" : "",
                     D.MatchedOn.c_str ());
        if (pPort != nullptr) { pPort->Release (); }
        if (pIrq != nullptr) { pIrq->Release (); }
        if (pSrc != nullptr) { pSrc->Release (); }
        if (pSnk != nullptr) { pSnk->Release (); }
    }
    for (std::string CONST &U : Builder.Unmatched ()) {
        std::printf ("  (no bundle) %s\n", U.c_str ());
    }

    bool Run = false;
    int  Demo = 0;                                                          // 0 none, 1 bank-switch, 2 keyboard IRQ
    for (int I = 0; I < argc; I++) {
        if (std::strcmp (argv[I], "--run") == 0) { Run = true; }
        if (std::strcmp (argv[I], "--demo") == 0) { Demo = 1; Run = true; }      // bank-switch + open-bus demo
        if (std::strcmp (argv[I], "--demo-kbd") == 0) { Demo = 2; Run = true; }  // 8042 keyboard IRQ1 demo
        if (std::strcmp (argv[I], "--demo-rtc") == 0) { Demo = 3; Run = true; }  // MC146818 RTC/CMOS read demo
        if (std::strcmp (argv[I], "--demo-rtc-irq") == 0) { Demo = 4; Run = true; }  // RTC IRQ8 via the slave PIC
        if (std::strcmp (argv[I], "--demo-pit") == 0) { Demo = 5; Run = true; }      // 8254 counter-latch read-back
        if (std::strcmp (argv[I], "--demo-fdc") == 0) { Demo = 6; Run = true; }      // DMA-driven floppy sector read
        if (std::strcmp (argv[I], "--demo-fdc-write") == 0) { Demo = 7; Run = true; }  // DMA floppy write round-trip
        if (std::strcmp (argv[I], "--demo-pic") == 0) { Demo = 8; Run = true; }      // 8259 in-service register read-back
        if (std::strcmp (argv[I], "--demo-ide") == 0) { Demo = 9; Run = true; }      // XT-IDE PIO sector read
        if (std::strcmp (argv[I], "--demo-rom") == 0) { Demo = 10; Run = true; }     // controller option-ROM firmware
        if (std::strcmp (argv[I], "--demo-hdc") == 0) { Demo = 12; Run = true; }     // DMA-driven ST-506 sector read
    }
    // --rom <device>=<path> (repeatable): load a firmware image for the named controller into a
    // free slot of the option-ROM area (the address is auto-assigned). The device is matched by
    // its bundle name or node name.
    std::vector<std::pair<std::string, std::string>> CliRoms;
    for (int I = 0; I + 1 < argc; I++) {
        if (std::strcmp (argv[I], "--rom") != 0) { continue; }
        std::string Spec = argv[I + 1];
        size_t Eq = Spec.find ('=');
        if (Eq == std::string::npos) { CliRoms.push_back (std::make_pair (std::string (), Spec)); }
        else { CliRoms.push_back (std::make_pair (Spec.substr (0, Eq), Spec.substr (Eq + 1))); }
    }
    CHAR8 CONST *pImage = Opt (argc, argv, "--image", nullptr);
    // --bios <file>: map a real system-BIOS image at the top of memory and boot the reset vector
    // (0xFFFF0). The BIOS image stands in for the --image payload, so the boot path runs POST.
    CHAR8 CONST *pBios = Opt (argc, argv, "--bios", nullptr);
    bool BiosBoot = pBios != nullptr;
    if (BiosBoot) { pImage = pBios; Run = true; }
    // --console: run the interactive typewriter through the console seam (host keyboard -> 8042 ->
    // IRQ1, framebuffer -> terminal). --keys <text> drives it headlessly (scripted) for tests.
    bool ConsoleMode = false;
    for (int I = 0; I < argc; I++) { if (std::strcmp (argv[I], "--console") == 0) { ConsoleMode = true; Run = true; } }
    CHAR8 CONST *pKeys = Opt (argc, argv, "--keys", nullptr);
    std::string ConsoleKeys = pKeys != nullptr ? std::string (pKeys) : std::string ();
    // --boot: run the option-ROM bootstrap (scan the UMA, far-call each ROM's init), like a POST.
    bool BootScan = false;
    for (int I = 0; I < argc; I++) { if (std::strcmp (argv[I], "--boot") == 0) { BootScan = true; Run = true; } }
    // Loading option ROMs and then running the machine means "run these ROMs", not "run the canned
    // self-test". A real PC powers on, the BIOS scans the UMA and far-calls each option ROM's init.
    // So when --rom firmware is present and the user picked no explicit --demo-*, no system --bios,
    // and no --console, default to the boot scan (the same path as --boot). A bare --demo-* keeps
    // its own program; --bios runs the real firmware which does its own scan.
    if (!CliRoms.empty () && Demo == 0 && !BiosBoot && !ConsoleMode && !BootScan) {
        std::printf ("   --rom firmware present and no explicit demo: running the option-ROM boot scan\n");
        BootScan = true;
        Run      = true;
    }
    if (Run || pImage != nullptr || !CliRoms.empty ()) {
        CHAR8 CONST *pLoad = Opt (argc, argv, "--load", nullptr);
        UINT32 LoadAddr = pLoad != nullptr ? (UINT32) std::strtoul (pLoad, nullptr, 0) : 0x0600;
        ICpuBackend *pBackend = LoadBackendBundle (BackendPath (argc, argv, pArgv0).c_str ());
        if (pBackend == nullptr) { std::printf ("lcx machine: cannot load backend\n"); return 2; }
        CHAR8 CONST *pSteps = Opt (argc, argv, "--steps", nullptr);
        UINT64 BiosSteps = pSteps != nullptr ? (UINT64) std::strtoull (pSteps, nullptr, 0) : 4000;
        // --jit <bundle>: tiered execution. The machine runs on the interpreter (instant translation)
        // and promotes hot ROM regions to this optimizing JIT backend, which only pays off where its
        // compile cost amortizes -- a BIOS POST is mostly cold, so this keeps it fast while still
        // JIT-compiling genuine hot loops.
        // --jit takes a COMMA-SEPARATED tier ladder, cold->hot (e.g. --jit asmjit.backend,sljit.backend,
        // mir.backend): a hot region climbs them in the background, each tier optimizing harder. A single
        // bundle is the common one-tier case.
        CHAR8 CONST *pJit = Opt (argc, argv, "--jit", nullptr);
        std::vector<ICpuBackend *> HotTiers;
        if (pJit != nullptr) {
            std::string Spec (pJit);
            size_t Pos = 0;
            for (;;) {
                size_t Comma = Spec.find (',', Pos);
                std::string One = Spec.substr (Pos, (Comma == std::string::npos) ? std::string::npos : Comma - Pos);
                if (!One.empty ()) {
                    ICpuBackend *pB = LoadBackendBundle (One.c_str ());
                    if (pB != nullptr) { HotTiers.push_back (pB); }
                    else { std::printf ("lcx machine: cannot load --jit backend '%s'\n", One.c_str ()); }
                }
                if (Comma == std::string::npos) { break; }
                Pos = Comma + 1;
            }
        }
        // Shadow-path fallback (hybrid rung A): a native-CFG interpreter, loaded from the same
        // bundle directory, that runs blocks the shadow layer cannot lower (e.g. REP). Used only
        // when the chosen backend lacks native control flow; held unused otherwise. Best-effort.
        std::string Bp = BackendPath (argc, argv, pArgv0);
        std::string FbPath = Bp.substr (0, Bp.find_last_of ('/') + 1) + "interp.backend";
        ICpuBackend *pFallback = LoadBackendBundle (FbPath.c_str ());

        int Rc = RunMachineDemo (Builder, pBackend, pImage, LoadAddr, Demo, CliRoms, BiosBoot, BiosSteps,
                                 ConsoleMode, ConsoleKeys, BootScan, HotTiers, pFallback);
        if (pFallback != nullptr) { pFallback->Release (); }
        for (ICpuBackend *pB : HotTiers) { pB->Release (); }
        pBackend->Release ();
        return Rc;
    }
    return 0;
}

static int
CmdHelp ()
{
    std::printf (
        "lcx -- the LibCPU machine (run/debug/disasm/system/knowledge/cache)\n\n"
        "  lcx run       <image> [--arch v20|6502|upcl:<file>] [--cache] [--dump <addr>]   (JIT)\n"
        "  lcx translate <image> [--arch ...] [--cache] [--dump <addr>]                   (AOT)\n"
        "  lcx aot    <image> -o <exe> [--arch ...] [--result <addr>]   standalone native exe\n"
        "  lcx disasm <image> [--arch ...] [--count N] [--entry N]\n"
        "  lcx debug  <image> [--arch ...]\n"
        "  lcx system <image> [--arch v20]            8086 device bus + timer interrupt\n"
        "  lcx know   <library.xml> [--arch v20]      in-line syscalls -> host calls\n"
        "  lcx know   derive <header.h> <lib> [-o a.klib] | catalog <a.klib>   derived host catalog\n"
        "  lcx know   bind <header.h> <lib> <mapping.xml>            dispatch a guest call to a bound host fn\n"
        "  lcx know   marshal <header.h> <lib.dylib> <mapping.xml>   marshal a struct out-param to guest layout\n"
        "  lcx know   derive-map <header.h> <lib> <target.abi>       derive the target->host mapping\n"
        "  lcx upcl   check <file.upcl> | run <file.upcl> <image.bin>  UPCL CPU description\n"
#ifdef LIBCPU_HAVE_ZSTD
        "  lcx klib   create <a> <file...> | ls <a> | extract <a> <m>  knowledge-library archive\n"
        "  lcx klib   symbols <lib.tbd|dylib> [--grep <s>]            exported symbols of a library\n"
        "  lcx klib   headers <file.h> [-I dir] [-D macro] ...        prototypes + struct layouts\n"
#endif
        "  lcx dt     compile <in.dts> -o <out.dtb> | decompile <in.dtb> [-o <out.dts>]\n"
        "  lcx dt     dump <in> | overlay <base> <frag> [-o <out>]    device-tree compile/decompile\n"
        "  lcx machine <machine.dts> [--bundles <dir>] [--tree] [--roms] [--run] [--rom <dev>=<file>]   assemble/run a machine\n"
        "  lcx machine <machine.dts> --bios <bios.bin>   boot a real system BIOS at the reset vector (0xFFFF0)\n"
        "  lcx machine <machine.dts> --console [--keys <text>]   interactive typewriter via the console seam\n"
        "  lcx machine <machine.dts> --boot [--rom <dev>=<file>]   POST: scan the UMA and run option ROMs\n"
        "  lcx cache  ls | info | clean\n"
        "  lcx version | help\n\n"
        "backend: --backend <bundle> | $LCX_BACKEND | <exe-dir>/interp.backend\n");
    return 0;
}

// Self-test the shadow CFG: build ONE shadow unit over a hand-assembled V20 basic block and
// run it through the chosen backend, verifying the shadow ABI -- the data ops landed in the
// guest registers and the unit left its next-PC in the scratch registers. This proves a
// backend's straight-line core alone drives control flow, with no native branch capability.
static int
CmdShadow (int argc, char **argv, char *pArgv0)
{
    ICpuBackend *pBackend = LoadBackendBundle (BackendPath (argc, argv, pArgv0).c_str ());
    if (pBackend == nullptr) {
        std::printf ("lcx shadow: cannot load backend\nRESULT: FAIL\n");
        return 1;
    }

    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    // MOV AX,0x1234 ; MOV BX,0x5678 ; JMP $+0x18  (-> offset 0x20)
    UINT8 CONST Prog[] = { 0xB8, 0x34, 0x12, 0xBB, 0x78, 0x56, 0xEB, 0x18 };
    std::memcpy (Ram, Prog, sizeof (Prog));

    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));
    State.RamSize = sizeof (Ram);
    ArchSetup A = MakeArch ("v20", Ram, sizeof (Ram), &State);

    ICpuCode *pCode = nullptr;
    HRESULT hr = GenerateShadowUnit (A.pArch, pBackend, 0, 0x100, &pCode);
    int Rc = 1;
    if (FAILED (hr) || pCode == nullptr) {
        std::printf ("lcx shadow: GenerateShadowUnit failed (0x%08x)\nRESULT: FAIL\n", (unsigned) hr);
    } else {
        pCode->Execute (Ram, &State, nullptr);
        UINT64 Ax   = State.Reg[0] & 0xFFFF;
        UINT64 Bx   = State.Reg[3] & 0xFFFF;
        UINT64 St   = State.Reg[SHADOW_REG_STATUS];
        UINT64 Next = State.Reg[SHADOW_REG_NEXTPC];
        bool Pass = (Ax == 0x1234) && (Bx == 0x5678) && (St == SHADOW_ST_NEXT) && (Next == 0x20);
        std::printf ("backend=%s  AX=0x%04llx BX=0x%04llx  status=%llu nextpc=0x%llx\nRESULT: %s\n",
                     pBackend->GetName (), (unsigned long long) Ax, (unsigned long long) Bx,
                     (unsigned long long) St, (unsigned long long) Next, Pass ? "PASS" : "FAIL");
        Rc = Pass ? 0 : 1;
        pCode->Release ();
    }
    if (A.pArch != nullptr) { A.pArch->Release (); }
    pBackend->Release ();
    return Rc;
}

} // anonymous namespace

int
main (int argc, char **argv)
{
    if (argc < 2) {
        return CmdHelp ();
    }
    std::string Cmd = argv[1];
    int      SubArgc = argc - 2;
    char   **SubArgv = argv + 2;
    if (Cmd == "shadow")       { return CmdShadow (SubArgc, SubArgv, argv[0]); }
    if (Cmd == "run")          { return CmdRun (SubArgc, SubArgv, argv[0], /*Aot=*/ false); }   // JIT
    if (Cmd == "translate")    { return CmdRun (SubArgc, SubArgv, argv[0], /*Aot=*/ true); }    // AOT
    if (Cmd == "aot")          { return CmdAot (SubArgc, SubArgv, argv[0]); }
    if (Cmd == "disasm")       { return CmdDisasm (SubArgc, SubArgv, argv[0]); }
    if (Cmd == "debug")        { return CmdDebug (SubArgc, SubArgv, argv[0]); }
    if (Cmd == "system")       { return CmdSystem (SubArgc, SubArgv, argv[0]); }
    if (Cmd == "upcl")         { return CmdUpcl (SubArgc, SubArgv, argv[0]); }
    if (Cmd == "know")         { return CmdKnowledge (SubArgc, SubArgv, argv[0]); }
#ifdef LIBCPU_HAVE_ZSTD
    if (Cmd == "klib")         { return CmdKlib (SubArgc, SubArgv); }
#endif
    if (Cmd == "dt")           { return CmdDt (SubArgc, SubArgv); }
    if (Cmd == "machine")      { return CmdMachine (SubArgc, SubArgv, argv[0]); }
    if (Cmd == "cache")        { return CmdCache (SubArgc, SubArgv); }
    if (Cmd == "version")      { std::printf ("lcx (LibCPU) -- unified machine driver\n"); return 0; }
    if (Cmd == "help" || Cmd == "-h" || Cmd == "--help") { return CmdHelp (); }
    std::printf ("lcx: unknown command '%s' (try 'lcx help')\n", Cmd.c_str ());
    return 2;
}
