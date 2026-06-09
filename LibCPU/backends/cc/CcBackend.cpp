/** @file
  "cc" textual-emit backend implementation. See CcBackend.h.

  Each ICpuValue is a uint64_t C temporary "tN" holding the value masked to its
  width; ops append C statements. Build() wraps the body in a function, compiles
  it to a shared object with the system C compiler, and dlopen()s the result.
**/
#include "CcBackend.h"
#include "LibCPU/CpuState.h"

#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <atomic>

namespace LibCPU {
namespace {

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
        char Cmd[512];
        std::snprintf (Cmd, sizeof (Cmd), "clang %s-shared -O2 -fPIC -o '%s' '%s' 2>/dev/null",
                       kTargetFlag, LibPath.c_str (), SrcPath.c_str ());
        if (std::system (Cmd) != 0) {
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
    UINT32      m_NextTemp = 0;
};

class CcBackend final : public LcComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "cc"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new CcEmitter ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<CcEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateCcBackend (VOID)
{
    return new CcBackend ();
}

} // namespace LibCPU
