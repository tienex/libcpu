/** @file
  "cc" textual-emit backend implementation. See CcBackend.h.

  Each ICpuValue is a uint64_t C temporary "tN" holding the value masked to its
  width; ops append C statements. Build() wraps the body in a function, compiles
  it to a shared object with the system C compiler, and dlopen()s the result.
**/
#include "CcBackend.h"
#include "CcCompilers.h"
#include "LibCPU/CpuState.h"

#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cctype>
#include <string>
#include <vector>
#include <atomic>

namespace LibCPU {
namespace {

// ---------------------------------------------------------------------------
// Compiler discovery: scan PATH + common roots, categorize by family.
// ---------------------------------------------------------------------------
// True if Name ends with the compiler-driver stem at a boundary (whole word, or
// preceded by '-' as in a target triple). This rejects toolchain tools like
// gcc-ar, gccmakedep, clang-format, clangd, cgcc, msg++, etc.
static bool
EndsWithStem (std::string CONST &Name, CHAR8 CONST *pStem)
{
    UINTN N = std::strlen (pStem);
    if (Name.size () < N || Name.compare (Name.size () - N, N, pStem) != 0) {
        return false;
    }
    return (Name.size () == N) || (Name[Name.size () - N - 1] == '-');
}

static CC_FAMILY
CategorizeCompiler (std::string CONST &Name)
{
    std::string B = Name;
    // Strip a trailing .exe (Windows compilers, possibly run via wine).
    if (B.size () >= 4) {
        std::string Ext = B.substr (B.size () - 4);
        for (char &C : Ext) C = (char) std::tolower ((unsigned char) C);
        if (Ext == ".exe") B.resize (B.size () - 4);
    }
    // Strip a trailing -<version> (digits/dots) so gcc-15, clang-15,
    // x86_64-w64-mingw32-gcc-15.2.0 reduce to their driver stem; gcc-ar / gcc-nm
    // (non-numeric suffix) are NOT stripped and won't match below.
    UINTN Dash = B.rfind ('-');
    if (Dash != std::string::npos && Dash + 1 < B.size () && std::isdigit ((unsigned char) B[Dash + 1])) {
        bool NumDot = true;
        for (UINTN I = Dash + 1; I < B.size (); I++) {
            if (!std::isdigit ((unsigned char) B[I]) && B[I] != '.') { NumDot = false; break; }
        }
        if (NumDot) B = B.substr (0, Dash);
    }
    auto Eq = [&] (CHAR8 CONST *S) { return B == S; };

    if (EndsWithStem (B, "clang") || EndsWithStem (B, "clang++") || EndsWithStem (B, "clang-cl"))
        return CcFamilyClang;
    if (EndsWithStem (B, "gcc") || EndsWithStem (B, "g++"))
        return CcFamilyGcc;
    if (Eq ("iec"))                                 return CcFamilyEfiByteCode;
    if (Eq ("xlc") || Eq ("xlC") || Eq ("xlc++"))   return CcFamilyIbmXl;
    if (Eq ("wcc") || Eq ("wcc386") || Eq ("wpp") || Eq ("wpp386") || Eq ("wcl") ||
        Eq ("wcl386") || Eq ("wclppc") || Eq ("wclaxp") || Eq ("wclmps") || Eq ("owcc"))
        return CcFamilyWatcom;
    if (Eq ("cl") || Eq ("cl386") || Eq ("clarm") || Eq ("clsh") || Eq ("clmips") || Eq ("clppc"))
        return CcFamilyMsvc;
    if (Eq ("bcc") || Eq ("bcc32") || Eq ("bcc64"))  return CcFamilyBorland;
    if (Eq ("icc") || Eq ("icx") || Eq ("icl") || Eq ("icpc") || Eq ("icpx"))
        return CcFamilyIntel;
    if (Eq ("mwcc") || Eq ("mwccppc") || Eq ("mwcceppc") || Eq ("mwld"))
        return CcFamilyMetrowerks;
    if (Eq ("dmc"))                                 return CcFamilyDigitalMars;
    if (Eq ("cc") || Eq ("CC") || Eq ("c++"))       return CcFamilyGeneric;
    return CcFamilyUnknown;
}

// Target for compilers that do not support -dumpmachine, inferred from family/name.
static std::string
InferTarget (CC_FAMILY Family, std::string CONST &Name)
{
    std::string N = Name;
    for (char &C : N) C = (char) std::tolower ((unsigned char) C);
    switch (Family) {
    case CcFamilyEfiByteCode: return "ebc";
    case CcFamilyWatcom:
        if (N.find ("386") != std::string::npos) return "i386-pc-watcom";
        if (N.find ("ppc") != std::string::npos) return "powerpc-pc-watcom";
        if (N.find ("axp") != std::string::npos) return "alpha-pc-watcom";
        if (N.find ("mps") != std::string::npos) return "mips-pc-watcom";
        return "i86-pc-watcom";
    case CcFamilyMsvc:
        if (N.find ("386")    != std::string::npos) return "i386-pc-windows";
        if (N.find ("clarm")  != std::string::npos) return "arm-pc-windows";
        if (N.find ("clmips") != std::string::npos) return "mips-pc-windows";
        if (N.find ("clppc")  != std::string::npos) return "powerpc-pc-windows";
        if (N.find ("clsh")   != std::string::npos) return "sh-pc-windows";
        return "x86-pc-windows";
    case CcFamilyBorland:
        if (N.find ("64") != std::string::npos) return "x86_64-pc-windows";
        if (N.find ("32") != std::string::npos) return "i386-pc-windows";
        return "i86-pc-windows";
    default:
        return "";
    }
}

static std::string
RunCapture (std::string CONST &Cmd)
{
    std::string Out;
    FILE *pP = popen (Cmd.c_str (), "r");
    if (pP == nullptr) {
        return Out;
    }
    char Buf[256];
    if (std::fgets (Buf, sizeof (Buf), pP) != nullptr) {
        Out = Buf;
    }
    pclose (pP);
    while (!Out.empty () && (Out.back () == '\n' || Out.back () == '\r')) {
        Out.pop_back ();
    }
    return Out;
}

// Internal record owning the strings that public CC_COMPILER_INFO points into.
struct CcRec {
    std::string Path, Name, Version, Target;
    CC_FAMILY   Family = CcFamilyUnknown;
    bool        UsableForHost = false;
    bool        ViaWine = false;
};

static VOID
DiscoverCompilers (std::vector<CcRec> &Out)
{
    std::vector<std::string> Dirs;
    CHAR8 CONST *pPath = std::getenv ("PATH");
    if (pPath != nullptr) {
        std::string P = pPath, Cur;
        for (char C : P) {
            if (C == ':') { if (!Cur.empty ()) Dirs.push_back (Cur); Cur.clear (); }
            else Cur += C;
        }
        if (!Cur.empty ()) Dirs.push_back (Cur);
    }
    CHAR8 CONST *Extra[] = {
        "/usr/bin", "/usr/local/bin", "/opt/homebrew/bin", "/opt/local/bin",
        "/opt/homebrew/opt/llvm/bin", "/opt/watcom/binl64", "/opt/watcom/binl",
        "/usr/lib/watcom/binl64", nullptr
    };
    for (UINTN I = 0; Extra[I] != nullptr; I++) Dirs.push_back (Extra[I]);

    // Determine this host's compiler target once, up front.
    std::string HostTarget = RunCapture ("cc -dumpmachine 2>/dev/null");
    if (HostTarget.empty ()) HostTarget = RunCapture ("clang -dumpmachine 2>/dev/null");
    // Is wine available to run Windows .exe compilers on this Unix host?
    bool WineAvail = !RunCapture ("command -v wine 2>/dev/null").empty ();

    std::vector<std::string> Seen;
    for (std::string CONST &Dir : Dirs) {
        DIR *pD = opendir (Dir.c_str ());
        if (pD == nullptr) continue;
        struct dirent *pE;
        while ((pE = readdir (pD)) != nullptr) {
            std::string Name = pE->d_name;
            CC_FAMILY Fam = CategorizeCompiler (Name);
            if (Fam == CcFamilyUnknown) continue;
            bool IsExe = false;
            if (Name.size () >= 4) {
                std::string Ext = Name.substr (Name.size () - 4);
                for (char &C : Ext) C = (char) std::tolower ((unsigned char) C);
                IsExe = (Ext == ".exe");
            }
            std::string Full = Dir + "/" + Name;
            // .exe binaries need not be executable (they run under wine): require readable.
            if (access (Full.c_str (), IsExe ? R_OK : X_OK) != 0) continue;
            struct stat St;
            if (stat (Full.c_str (), &St) != 0 || !S_ISREG (St.st_mode)) continue;
            char Real[1024];
            std::string Key = realpath (Full.c_str (), Real) ? std::string (Real) : Full;
            bool Dup = false;
            for (std::string CONST &S : Seen) if (S == Key) { Dup = true; break; }
            if (Dup) continue;
            Seen.push_back (Key);

            bool ViaWine = IsExe && WineAvail;       // Unix host -> run Windows .exe via wine
            bool CanRun  = !IsExe || WineAvail;
            std::string Inv = ViaWine ? ("wine \"" + Full + "\"") : ("\"" + Full + "\"");

            CcRec Rec;
            Rec.Path = Full;
            Rec.Name = Name;
            Rec.Family = Fam;
            Rec.ViaWine = ViaWine;

            if (Fam == CcFamilyClang || Fam == CcFamilyGcc ||
                Fam == CcFamilyIntel || Fam == CcFamilyGeneric) {
                if (CanRun) {
                    Rec.Version = RunCapture (Inv + " --version 2>/dev/null");
                    std::string Tgt = RunCapture (Inv + " -dumpmachine 2>/dev/null");
                    if (Tgt.find (' ') != std::string::npos || Tgt.find ('/') != std::string::npos) {
                        Tgt.clear ();
                    }
                    Rec.Target = Tgt;
                    Rec.UsableForHost = (!ViaWine && !Tgt.empty () && Tgt == HostTarget);
                }
            } else {
                // Compilers without -dumpmachine (EBC, Watcom, MSVC, Borland, ...).
                Rec.Target = InferTarget (Fam, Name);
            }
            Out.push_back (std::move (Rec));
        }
        closedir (pD);
    }
}

class CcValue final : public LcComObject<ICpuValue> {
public:
    CcValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id;
    UINT32 m_Bits;
};

class CcBlock final : public LcComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
};

static UINT32 IdOf   (ICpuValue *pV) { return static_cast<CcValue *> (pV)->m_Id; }
static UINT32 BitsOf (ICpuValue *pV) { return static_cast<CcValue *> (pV)->m_Bits; }

typedef int (*JittedFn) (void *pRAM, void *pGRF, void *pFRF);

//
// The compiled code object: owns the dlopen handle and the temp files.
//
class CcCode final : public LcComObject<ICpuCode> {
public:
    CcCode (void *pHandle, JittedFn Fn, std::string Dir, std::string SrcPath, std::string LibPath)
        : m_pHandle (pHandle), m_Fn (Fn), m_Dir (std::move (Dir)),
          m_SrcPath (std::move (SrcPath)), m_LibPath (std::move (LibPath)) {}
    ~CcCode () override {
        if (m_pHandle) dlclose (m_pHandle);
        if (!m_SrcPath.empty ()) unlink (m_SrcPath.c_str ());
        if (!m_LibPath.empty ()) unlink (m_LibPath.c_str ());
        if (!m_Dir.empty ())     rmdir (m_Dir.c_str ());
    }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }
    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID *pFRF) override {
        return (CPU_EXEC_STATUS) m_Fn (pRAM, pGRF, pFRF);
    }
private:
    void       *m_pHandle;
    JittedFn    m_Fn;
    std::string m_Dir, m_SrcPath, m_LibPath;
};

class CcEmitter final : public LcComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

    // ---- values -----------------------------------------------------------
    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 D = Decl ();
        Line ("t%u = (uint64_t)0x%llxULL;", D, (unsigned long long) Mask (Value, Bits));
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Decl ();
        Line ("t%u = (*(uint64_t*)((char*)GRF+%u)) & 0x%llxULL;", D, CPU_STATE_REG_OFFSET + Index * 8, (unsigned long long) Mask (~0ull, Bits));
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 /*RegBits*/, BOOLEAN /*Sext*/) override {
        Line ("*(uint64_t*)((char*)GRF+%u) = t%u;", CPU_STATE_REG_OFFSET + Index * 8, IdOf (pValue));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Decl ();
        Line ("t%u = *(%s*)((char*)RAM + t%u);", D, CType (Bits), IdOf (pAddr));
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Line ("*(%s*)((char*)RAM + t%u) = (%s)t%u;", CType (Bits), IdOf (pAddr), CType (Bits), IdOf (pValue));
        return S_OK;
    }

    // ---- arithmetic / compare / cast --------------------------------------
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        CONST CHAR8 *pOp;
        switch (Op) {
        case BinAdd: pOp = "+";  break;
        case BinSub: pOp = "-";  break;
        case BinMul: pOp = "*";  break;
        case BinUDiv:pOp = "/";  break;
        case BinURem:pOp = "%";  break;
        case BinAnd: pOp = "&";  break;
        case BinOr:  pOp = "|";  break;
        case BinXor: pOp = "^";  break;
        case BinShl: pOp = "<<"; break;
        case BinLShr:pOp = ">>"; break;  // unsigned temps -> logical shift
        default:     pOp = "+";  break;  // SDiv/SRem/AShr/Rol/Ror: TODO
        }
        UINT32 D = Decl ();
        Line ("t%u = (t%u %s t%u) & 0x%llxULL;", D, IdOf (pA), pOp, IdOf (pB), (unsigned long long) Mask (~0ull, Bits));
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA), D = Decl ();
        switch (Op) {
        case UnNeg: Line ("t%u = (-t%u) & 0x%llxULL;", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); return Make (D, Bits, ppValue);
        case UnCom: Line ("t%u = (~t%u) & 0x%llxULL;", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); return Make (D, Bits, ppValue);
        case UnNot: Line ("t%u = (t%u == 0) ? 1 : 0;", D, IdOf (pA)); return Make (D, 1, ppValue);
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        CONST CHAR8 *pOp; bool Signed = false;
        switch (Pred) {
        case CmpEq:  pOp = "=="; break;
        case CmpNe:  pOp = "!="; break;
        case CmpULt: pOp = "<";  break;
        case CmpULe: pOp = "<="; break;
        case CmpUGt: pOp = ">";  break;
        case CmpUGe: pOp = ">="; break;
        case CmpSLt: pOp = "<";  Signed = true; break;
        case CmpSLe: pOp = "<="; Signed = true; break;
        case CmpSGt: pOp = ">";  Signed = true; break;
        case CmpSGe: pOp = ">="; Signed = true; break;
        default:     pOp = "=="; break;
        }
        UINT32 D = Decl ();
        if (Signed) {
            Line ("t%u = ((int64_t)t%u %s (int64_t)t%u) ? 1 : 0;", D, IdOf (pA), pOp, IdOf (pB));
        } else {
            Line ("t%u = (t%u %s t%u) ? 1 : 0;", D, IdOf (pA), pOp, IdOf (pB));
        }
        return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA), D = Decl ();
        switch (Op) {
        case CastTrunc: Line ("t%u = t%u & 0x%llxULL;", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); break;
        case CastZExt:  Line ("t%u = t%u;", D, IdOf (pA)); break;
        case CastSExt:  Line ("t%u = (uint64_t)(((int64_t)(t%u << %u)) >> %u) & 0x%llxULL;",
                              D, IdOf (pA), 64 - SrcBits, 64 - SrcBits, (unsigned long long) Mask (~0ull, Bits)); break;
        }
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *pCond, ICpuValue *pTrue, ICpuValue *pFalse, ICpuValue **ppValue) override {
        UINT32 D = Decl ();
        Line ("t%u = t%u ? t%u : t%u;", D, IdOf (pCond), IdOf (pTrue), IdOf (pFalse));
        return Make (D, BitsOf (pTrue), ppValue);
    }

    // ---- flags ------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 D = Decl ();
        Line ("t%u = (*(uint8_t*)((char*)GRF+%u)) & 1;", D, CPU_STATE_FLAG_OFFSET + (UINT32) Flag);
        return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        Line ("*(uint8_t*)((char*)GRF+%u) = (uint8_t)(t%u & 1);", CPU_STATE_FLAG_OFFSET + (UINT32) Flag, IdOf (pValue));
        return S_OK;
    }

    // ---- control flow (unused by the straight-line slice) -----------------
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        Line ("*(uint64_t*)((char*)GRF+%u) = 0x%llxULL;", CPU_STATE_PC_OFFSET, (unsigned long long) Pc);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = new CcBlock (); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    void SetCompiler (std::string Path, CC_FAMILY Family) {
        m_CompilerPath = std::move (Path);
        m_CompFamily   = Family;
    }

    ICpuCode *Build () {
        // Create a private, owner-only (0700), randomly-named directory so an
        // attacker cannot pre-create symlinks for the source/lib we then compile
        // and dlopen(). The source is opened O_EXCL|O_NOFOLLOW for good measure.
        char DirTmpl[] = "/tmp/libcpu_cc_XXXXXX";
        if (mkdtemp (DirTmpl) == nullptr) {
            return nullptr;
        }
        std::string Dir     = DirTmpl;
        std::string SrcPath = Dir + "/insn.c";
        std::string LibPath = Dir + "/insn.dylib";

        int Fd = open (SrcPath.c_str (), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
        if (Fd < 0) {
            rmdir (Dir.c_str ());
            return nullptr;
        }
        FILE *pF = fdopen (Fd, "w");
        if (!pF) {
            close (Fd); unlink (SrcPath.c_str ()); rmdir (Dir.c_str ());
            return nullptr;
        }
        std::fprintf (pF,
            "#include <stdint.h>\n"
            "int insn(void* RAM, void* GRF, void* FRF) {\n"
            "  uint64_t %s = 0;\n"   // declare all temps to keep emission simple
            "%s"
            "  (void)FRF;\n"
            "  return 0;\n"
            "}\n",
            m_TempDecls.empty () ? "t_unused" : m_TempDecls.c_str (),
            m_Body.c_str ());
        std::fclose (pF);   // closes Fd

        // Target the architecture THIS slice is running as, so a universal
        // cc.backend compiles for the right arch (macOS: -arch; else: host default).
#if defined(__APPLE__)
        // Every architecture macOS / Mac OS X has run on. Each universal slice
        // bakes in its own arch macro, so the right -arch is chosen at runtime.
#  if defined(__aarch64__)
        static CONST CHAR8 *kTargetFlag = "-arch arm64 ";
#  elif defined(__x86_64__)
        static CONST CHAR8 *kTargetFlag = "-arch x86_64 ";
#  elif defined(__i386__)
        static CONST CHAR8 *kTargetFlag = "-arch i386 ";
#  elif defined(__ppc64__)
        static CONST CHAR8 *kTargetFlag = "-arch ppc64 ";
#  elif defined(__ppc__) || defined(__powerpc__)
        static CONST CHAR8 *kTargetFlag = "-arch ppc ";
#  else
        static CONST CHAR8 *kTargetFlag = "";
#  endif
#else
        static CONST CHAR8 *kTargetFlag = "";
#endif
        // -arch applies to clang only; gcc/others use their own default target.
        CONST CHAR8 *pArchFlag = (m_CompFamily == CcFamilyClang) ? kTargetFlag : "";
        std::string Cmd = "\"" + m_CompilerPath + "\" " + pArchFlag +
                          "-shared -O2 -fPIC -o '" + LibPath + "' '" + SrcPath + "' 2>/dev/null";
        if (std::system (Cmd.c_str ()) != 0) {
            unlink (SrcPath.c_str ()); rmdir (Dir.c_str ());
            return nullptr;
        }

        void *pHandle = dlopen (LibPath.c_str (), RTLD_NOW | RTLD_LOCAL);
        if (!pHandle) {
            unlink (SrcPath.c_str ()); unlink (LibPath.c_str ()); rmdir (Dir.c_str ());
            return nullptr;
        }
        JittedFn Fn = (JittedFn) dlsym (pHandle, "insn");
        if (!Fn) {
            dlclose (pHandle); unlink (SrcPath.c_str ()); unlink (LibPath.c_str ()); rmdir (Dir.c_str ());
            return nullptr;
        }
        return new CcCode (pHandle, Fn, Dir, SrcPath, LibPath);
    }

private:
    UINT32 Decl () {
        UINT32 Id = m_NextTemp++;
        char Name[24];
        std::snprintf (Name, sizeof (Name), "%st%u", m_TempDecls.empty () ? "" : ", ", Id);
        m_TempDecls += Name;
        return Id;
    }
    void Line (CONST CHAR8 *pFmt, ...) {
        char Buf[256];
        va_list Args;
        va_start (Args, pFmt);
        std::vsnprintf (Buf, sizeof (Buf), pFmt, Args);
        va_end (Args);
        m_Body += "  ";
        m_Body += Buf;
        m_Body += "\n";
    }
    static CONST CHAR8 *CType (UINT32 Bits) {
        switch (Bits) {
        case 8:  return "uint8_t";
        case 16: return "uint16_t";
        case 32: return "uint32_t";
        default: return "uint64_t";
        }
    }
    static UINT64 Mask (UINT64 V, UINT32 Bits) { return (Bits >= 64) ? V : (V & (((UINT64) 1 << Bits) - 1)); }
    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) {
        *ppValue = new CcValue (Id, Bits);
        return S_OK;
    }

    std::string m_Body;
    std::string m_TempDecls;
    std::string m_CompilerPath = "clang";
    CC_FAMILY   m_CompFamily   = CcFamilyClang;
    UINT32      m_NextTemp = 0;
};

//
// The cc backend exposes both ICpuBackend and the ICpuCcCompilers query/select
// interface, so IUnknown is implemented manually (one set of methods overrides
// both interfaces' IUnknown).
//
class CcBackend final : public ICpuBackend, public ICpuCcCompilers {
public:
    CcBackend () : m_Ref (1) {
        DiscoverCompilers (m_Compilers);
        // Default selection: first host-usable clang, else first host-usable, else 0.
        for (UINT32 I = 0; I < (UINT32) m_Compilers.size (); I++) {
            if (m_Compilers[I].Family == CcFamilyClang && m_Compilers[I].UsableForHost) { m_Selected = I; break; }
        }
        if (m_Selected == ~0u) {
            for (UINT32 I = 0; I < (UINT32) m_Compilers.size (); I++) {
                if (m_Compilers[I].UsableForHost) { m_Selected = I; break; }
            }
        }
        if (m_Selected == ~0u) m_Selected = 0;
    }
    virtual ~CcBackend () = default;

    // ---- IUnknown (shared by both interface vtables) ----
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        if (ppvObject == nullptr) return E_POINTER;
        if (LcIsEqualGUID (&riid, &IID_IUnknown) || LcIsEqualGUID (&riid, &IID_ICpuBackend)) {
            *ppvObject = static_cast<ICpuBackend *> (this);
        } else if (LcIsEqualGUID (&riid, &IID_ICpuCcCompilers)) {
            *ppvObject = static_cast<ICpuCcCompilers *> (this);
        } else {
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }
        AddRef ();
        return S_OK;
    }
    UINT32 STDMETHODCALLTYPE AddRef () override { return (UINT32) ++m_Ref; }
    UINT32 STDMETHODCALLTYPE Release () override {
        UINT32 C = (UINT32) --m_Ref;
        if (C == 0) delete this;
        return C;
    }

    // ---- ICpuBackend ----
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "cc"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        CcEmitter *pE = new CcEmitter ();
        if (m_Selected < m_Compilers.size ()) {
            pE->SetCompiler (m_Compilers[m_Selected].Path, m_Compilers[m_Selected].Family);
        }
        *ppEmitter = pE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<CcEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }

    // ---- ICpuCcCompilers ----
    UINT32 STDMETHODCALLTYPE GetCompilerCount () override { return (UINT32) m_Compilers.size (); }
    HRESULT STDMETHODCALLTYPE GetCompilerInfo (UINT32 Index, CC_COMPILER_INFO *pInfo) override {
        if (Index >= m_Compilers.size () || pInfo == nullptr) return E_INVALIDARG;
        CcRec CONST &R = m_Compilers[Index];   // non-owning pointers, valid for backend lifetime
        pInfo->Path          = R.Path.c_str ();
        pInfo->Name          = R.Name.c_str ();
        pInfo->Version       = R.Version.c_str ();
        pInfo->Target        = R.Target.c_str ();
        pInfo->Family        = R.Family;
        pInfo->UsableForHost = R.UsableForHost ? TRUE : FALSE;
        pInfo->ViaWine       = R.ViaWine ? TRUE : FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SelectCompiler (UINT32 Index) override {
        if (Index >= m_Compilers.size ()) return E_INVALIDARG;
        m_Selected = Index;
        return S_OK;
    }
    UINT32 STDMETHODCALLTYPE GetSelectedCompiler () override { return m_Selected; }

private:
    std::atomic<INT32>            m_Ref;
    std::vector<CcRec>            m_Compilers;
    UINT32                        m_Selected = ~0u;
};

} // anonymous namespace

ICpuBackend *
CreateCcBackend (VOID)
{
    return new CcBackend ();
}

} // namespace LibCPU
