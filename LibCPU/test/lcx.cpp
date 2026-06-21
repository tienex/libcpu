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
#include "../upcl/UpclArch.h"
#include "../upcl/RegisterLayout.h"
#include "../upcl/Semantics.h"
#include "../upcl/Decoder.h"
#include "RunSystem.h"
#include "RunDosSyscall.h"
#include "RunHostCall.h"
#include "RunStruct.h"
#include "RunDerivedMap.h"
#include "RunMethodCall.h"
#include "LibCPU/PCom.h"
#include <cstdio>
#include <cstring>
#if defined (__unix__) || defined (__APPLE__)
#  include <sys/stat.h>
#endif
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
                std::strcmp (argv[I], "--result") == 0) {
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
MakeArch (CHAR8 CONST *pName, UINT8 *pRam, CPU_STATE *pState)
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
        A.pArch->SetCodeMemory (pRam, 65536);
    }
    return A;
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

// --- subcommands -----------------------------------------------------------

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
    CHAR8 CONST *pArchName = Opt (argc, argv, "--arch", "v20");
    ArchSetup A = MakeArch (pArchName, Ram, &State);
    if (A.pArch == nullptr) {
        std::printf ("lcx %s: could not build the '%s' architecture\n", pVerb, pArchName);
        pBackend->Release ();
        return 1;
    }
    CPU_ADDR Entry = (CPU_ADDR) std::strtoull (Opt (argc, argv, "--entry", "0"), nullptr, 0);
    CPU_ADDR End   = (CPU_ADDR) Len;
    bool Cache = Flag (argc, argv, "--cache");

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
                break;                                   // a HLT/INT trap with no host handler: stop
            }
            Pc = (CPU_ADDR) State.TrapPc;
        }
    }
    DumpRegs (A, &State);
    if (CHAR8 CONST *pDump = Opt (argc, argv, "--dump", nullptr)) {
        CPU_ADDR Addr = (CPU_ADDR) std::strtoull (pDump, nullptr, 0);
        std::printf ("[0x%llx] = 0x%04x\n", (unsigned long long) Addr,
                     (unsigned) (Ram[Addr] | (Ram[Addr + 1] << 8)));
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
    ArchSetup A = MakeArch (Opt (argc, argv, "--arch", "v20"), Ram, &State);
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
    ArchSetup A = MakeArch (Opt (argc, argv, "--arch", "v20"), Ram, &State);
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
    ArchSetup A = MakeArch (Opt (argc, argv, "--arch", "v20"), Ram, &State);
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

class RecordingEmitter final : public ComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppv) override {
        return DefaultQuery (riid, IID_IUnknown, ppv);
    }

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
        static CHAR8 CONST *N[] = { "add", "sub", "mul", "udiv", "sdiv", "urem", "srem",
                                    "and", "or", "xor", "shl", "lshr", "ashr", "rol", "ror" };
        return N[Op];
    }
    static CHAR8 CONST *UnopName (CPU_UNOP Op) {
        static CHAR8 CONST *N[] = { "neg", "com", "not" };
        return N[Op];
    }
    static CHAR8 CONST *CmpName (CPU_CMP Pred) {
        static CHAR8 CONST *N[] = { "eq", "ne", "ult", "ule", "ugt", "uge", "slt", "sle", "sgt", "sge" };
        return N[Pred];
    }
    static CHAR8 CONST *CastName (CPU_CAST Op) {
        static CHAR8 CONST *N[] = { "trunc", "zext", "sext" };
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
    if (pFile == nullptr || (!Check && !Produce && !Lex && !Emit && !Decode)) {
        std::printf ("usage: lcx upcl check   <file.upcl>            validate + summarise\n"
                     "       lcx upcl produce <file.upcl>            build the frontend + round-trip its encodings\n"
                     "       lcx upcl lex     <file.upcl>            dump the token stream (lexer development aid)\n"
                     "       lcx upcl emit    <file.upcl> <insn>     translate one instruction body to emitter SSA\n"
                     "       lcx upcl decode  <file.upcl> <bytes..>  decode a byte stream + translate each insn\n"
                     "  (to execute a program: lcx run|translate <image> --arch upcl:<file.upcl>)\n");
        return 2;
    }
    if (Decode) {
        Upcl::SourceManager Sm;
        Upcl::Module *pMod = UpclParse (pFile, Sm);
        if (pMod == nullptr || pMod->Archs.empty ()) { return 1; }
        Upcl::Arch *pArch = pMod->Archs[0];
        Upcl::RegisterLayout Layout = Upcl::BuildRegisterLayout (pArch);
        Upcl::Decoder Dec (pArch, &Layout);
        UINT32 WordBits = pArch->WordSize ? pArch->WordSize : 16;

        // The byte stream follows the file name: each positional is one byte (0x.. or dec).
        std::vector<UINT8> Bytes;
        for (int I = 2; ; I++) {
            CHAR8 CONST *pTok = Positional (argc, argv, I);
            if (pTok == nullptr) { break; }
            Bytes.push_back ((UINT8) std::strtoul (pTok, nullptr, 0));
        }
        if (Bytes.empty ()) { std::printf ("lcx upcl decode: need at least one byte\n"); return 2; }

        UINT64 Pos = 0;
        while (Pos < Bytes.size ()) {
            Upcl::DecodedInsn D;
            if (!Dec.Decode (Bytes.data (), Bytes.size (), Pos, &D)) {
                std::printf ("0x%04llx: db 0x%02x  (no encoding matched)\n",
                             (unsigned long long) Pos, Bytes[(size_t) Pos]);
                Pos += 1;
                continue;
            }
            // Disassembly line: the instruction and each resolved operand.
            std::printf ("0x%04llx: %-6s", (unsigned long long) Pos, D.pInsn->Name.c_str ());
            for (auto CONST &Kv : D.Operands) {
                Upcl::Operand CONST &Op = Kv.second;
                if (Op.Kind == Upcl::Operand::Reg) {
                    std::printf (" %s=%s", Kv.first.c_str (), Layout.Phys[Op.RegIndex].Name.c_str ());
                } else {
                    std::printf (" %s=0x%llx", Kv.first.c_str (), (unsigned long long) Op.ImmValue);
                }
            }
            std::printf ("   (%u byte(s))\n", D.Length);

            // Translate the body with the decoded operands bound to their locations.
            RecordingEmitter Em;
            Upcl::Translator Tr (Layout, pArch, &Em, WordBits);
            for (auto CONST &Kv : D.Operands) { Upcl::Operand Op = Kv.second; Tr.BindOperand (Kv.first, Op); }
            Tr.Emit (D.pInsn->Semantics);

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
    ArchSetup A = MakeArch ("v20", Ram, &State);

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
