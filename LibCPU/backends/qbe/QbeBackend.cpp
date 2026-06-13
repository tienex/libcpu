/** @file
  QBE textual-emit backend. See QbeBackend.h.

  Each ICpuValue is a QBE SSA temporary (%tN, type l/64-bit) masked to its width.
  Build() writes the IL, runs `qbe` to emit assembly, assembles+links it with the
  system compiler into a shared object, and dlopen()s it.
**/
#include "QbeBackend.h"
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

#if defined(__aarch64__)
static CONST CHAR8 *kQbeTarget = "arm64_apple";
static CONST CHAR8 *kArchFlag  = "-arch arm64 ";
#elif defined(__x86_64__)
static CONST CHAR8 *kQbeTarget = "amd64_apple";
static CONST CHAR8 *kArchFlag  = "-arch x86_64 ";
#else
static CONST CHAR8 *kQbeTarget = "";
static CONST CHAR8 *kArchFlag  = "";
#endif

static UINT64 Mask (UINT64 V, UINT32 Bits) { return (Bits >= 64) ? V : (V & (((UINT64) 1 << Bits) - 1)); }

class QbeValue final : public ComObject<ICpuValue> {
public:
    QbeValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id, m_Bits;
};

class QbeBlock final : public ComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
};

static UINT32 IdOf   (ICpuValue *pV) { return static_cast<QbeValue *> (pV)->m_Id; }
static UINT32 BitsOf (ICpuValue *pV) { return static_cast<QbeValue *> (pV)->m_Bits; }

typedef int (*JittedFn) (void *pRAM, void *pGRF, void *pFRF);

class QbeCode final : public ComObject<ICpuCode> {
public:
    QbeCode (void *pHandle, JittedFn Fn, std::string Dir) : m_pHandle (pHandle), m_Fn (Fn), m_Dir (std::move (Dir)) {}
    ~QbeCode () override {
        if (m_pHandle) dlclose (m_pHandle);
        if (!m_Dir.empty ()) {
            unlink ((m_Dir + "/insn.ssa").c_str ());
            unlink ((m_Dir + "/insn.s").c_str ());
            unlink ((m_Dir + "/insn.dylib").c_str ());
            rmdir (m_Dir.c_str ());
        }
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
    std::string m_Dir;
};

class QbeEmitter final : public ComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

    // ---- values -----------------------------------------------------------
    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 D = Fresh ();
        Line ("\t%%t%u =l copy %llu", D, (unsigned long long) Mask (Value, Bits));
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 P = AddrGrf (CPU_STATE_REG_OFFSET + Index * 8);
        UINT32 R = Fresh ();  Line ("\t%%t%u =l loadl %%t%u", R, P);
        return Make (MaskTemp (R, Bits), Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 /*Bits*/, BOOLEAN /*Sext*/) override {
        UINT32 P = AddrGrf (CPU_STATE_REG_OFFSET + Index * 8);
        Line ("\tstorel %%t%u, %%t%u", IdOf (pValue), P);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 P = Fresh ();  Line ("\t%%t%u =l add %%ram, %%t%u", P, IdOf (pAddr));
        UINT32 R = Fresh ();  Line ("\t%%t%u =l %s %%t%u", R, LoadOp (Bits), P);
        return Make (R, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        UINT32 P = Fresh ();  Line ("\t%%t%u =l add %%ram, %%t%u", P, IdOf (pAddr));
        Line ("\t%s %%t%u, %%t%u", StoreOp (Bits), IdOf (pValue), P);
        return S_OK;
    }

    // ---- arithmetic / compare / cast --------------------------------------
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        CONST CHAR8 *pOp;
        switch (Op) {
        case BinAdd: pOp = "add";  break;
        case BinSub: pOp = "sub";  break;
        case BinMul: pOp = "mul";  break;
        case BinUDiv:pOp = "udiv"; break;
        case BinSDiv:pOp = "div";  break;
        case BinURem:pOp = "urem"; break;
        case BinSRem:pOp = "rem";  break;
        case BinAnd: pOp = "and";  break;
        case BinOr:  pOp = "or";   break;
        case BinXor: pOp = "xor";  break;
        case BinShl: pOp = "shl";  break;
        case BinLShr:pOp = "shr";  break;   // logical
        case BinAShr:pOp = "sar";  break;   // arithmetic
        default:     pOp = "add";  break;
        }
        UINT32 R = Fresh ();
        Line ("\t%%t%u =l %s %%t%u, %%t%u", R, pOp, IdOf (pA), IdOf (pB));
        return Make (MaskTemp (R, Bits), Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA), R = Fresh ();
        switch (Op) {
        case UnNeg: Line ("\t%%t%u =l sub 0, %%t%u", R, IdOf (pA)); return Make (MaskTemp (R, Bits), Bits, ppValue);
        case UnCom: Line ("\t%%t%u =l xor %%t%u, -1", R, IdOf (pA)); return Make (MaskTemp (R, Bits), Bits, ppValue);
        case UnNot: { UINT32 C = Fresh (); Line ("\t%%t%u =w ceql %%t%u, 0", C, IdOf (pA));
                      Line ("\t%%t%u =l extuw %%t%u", R, C); return Make (R, 1, ppValue); }
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        CONST CHAR8 *pP;
        switch (Pred) {
        case CmpEq:  pP = "eq";  break;
        case CmpNe:  pP = "ne";  break;
        case CmpULt: pP = "ult"; break;
        case CmpULe: pP = "ule"; break;
        case CmpUGt: pP = "ugt"; break;
        case CmpUGe: pP = "uge"; break;
        case CmpSLt: pP = "slt"; break;
        case CmpSLe: pP = "sle"; break;
        case CmpSGt: pP = "sgt"; break;
        case CmpSGe: pP = "sge"; break;
        default:     pP = "eq";  break;
        }
        UINT32 C = Fresh ();  Line ("\t%%t%u =w c%sl %%t%u, %%t%u", C, pP, IdOf (pA), IdOf (pB));
        UINT32 R = Fresh ();  Line ("\t%%t%u =l extuw %%t%u", R, C);
        return Make (R, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA), R = Fresh ();
        switch (Op) {
        case CastTrunc: Line ("\t%%t%u =l and %%t%u, %llu", R, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); break;
        case CastZExt:  Line ("\t%%t%u =l copy %%t%u", R, IdOf (pA)); break;
        case CastSExt: {
            CONST CHAR8 *pExt = (SrcBits == 8) ? "extsb" : (SrcBits == 16) ? "extsh" : "extsw";
            Line ("\t%%t%u =l %s %%t%u", R, pExt, IdOf (pA));
            R = MaskTemp (R, Bits);
            break;
        }
        }
        return Make (R, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override {
        *ppValue = nullptr; return E_NOTIMPL;
    }

    // ---- flags ------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 P = AddrGrf (CPU_STATE_FLAG_OFFSET + (UINT32) Flag);
        UINT32 R = Fresh ();  Line ("\t%%t%u =l loadub %%t%u", R, P);
        return Make (MaskTemp (R, 1), 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        UINT32 P = AddrGrf (CPU_STATE_FLAG_OFFSET + (UINT32) Flag);
        UINT32 M = Fresh ();  Line ("\t%%t%u =l and %%t%u, 1", M, IdOf (pValue));
        Line ("\tstoreb %%t%u, %%t%u", M, P);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        UINT32 P = AddrGrf (CPU_STATE_PC_OFFSET);
        UINT32 C = Fresh ();  Line ("\t%%t%u =l copy %llu", C, (unsigned long long) Pc);
        Line ("\tstorel %%t%u, %%t%u", C, P);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = new QbeBlock (); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        char DirTmpl[] = "/tmp/libcpu_qbe_XXXXXX";
        if (mkdtemp (DirTmpl) == nullptr) return nullptr;
        std::string Dir = DirTmpl;
        std::string Ssa = Dir + "/insn.ssa", Asm = Dir + "/insn.s", Lib = Dir + "/insn.dylib";

        int Fd = open (Ssa.c_str (), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
        if (Fd < 0) { rmdir (Dir.c_str ()); return nullptr; }
        FILE *pF = fdopen (Fd, "w");
        if (!pF) { close (Fd); unlink (Ssa.c_str ()); rmdir (Dir.c_str ()); return nullptr; }
        std::fprintf (pF,
            "export function w $insn(l %%ram, l %%grf, l %%frf) {\n"
            "@start\n"
            "%s"
            "\t%%ret =w copy 0\n"
            "\tret %%ret\n"
            "}\n", m_Body.c_str ());
        std::fclose (pF);

        std::string Cmd = "qbe -t " + std::string (kQbeTarget) + " -o '" + Asm + "' '" + Ssa + "' 2>/dev/null && " +
                          "clang " + kArchFlag + "-shared -O2 -o '" + Lib + "' '" + Asm + "' 2>/dev/null";
        if (std::system (Cmd.c_str ()) != 0) { Cleanup (Dir); return nullptr; }

        void *pHandle = dlopen (Lib.c_str (), RTLD_NOW | RTLD_LOCAL);
        if (!pHandle) { Cleanup (Dir); return nullptr; }
        JittedFn Fn = (JittedFn) dlsym (pHandle, "insn");
        if (!Fn) { dlclose (pHandle); Cleanup (Dir); return nullptr; }
        return new QbeCode (pHandle, Fn, Dir);
    }

private:
    UINT32 Fresh () { return m_Next++; }
    UINT32 AddrGrf (UINT32 Offset) {
        UINT32 P = Fresh ();
        Line ("\t%%t%u =l add %%grf, %u", P, Offset);
        return P;
    }
    UINT32 MaskTemp (UINT32 Id, UINT32 Bits) {
        if (Bits >= 64) return Id;
        UINT32 M = Fresh ();
        Line ("\t%%t%u =l and %%t%u, %llu", M, Id, (unsigned long long) Mask (~0ull, Bits));
        return M;
    }
    static CONST CHAR8 *LoadOp (UINT32 Bits) {
        switch (Bits) { case 8: return "loadub"; case 16: return "loaduh"; case 32: return "loaduw"; default: return "loadl"; }
    }
    static CONST CHAR8 *StoreOp (UINT32 Bits) {
        switch (Bits) { case 8: return "storeb"; case 16: return "storeh"; case 32: return "storew"; default: return "storel"; }
    }
    void Line (CONST CHAR8 *pFmt, ...) {
        char Buf[256];
        va_list Args; va_start (Args, pFmt);
        std::vsnprintf (Buf, sizeof (Buf), pFmt, Args);
        va_end (Args);
        m_Body += Buf; m_Body += "\n";
    }
    static VOID Cleanup (std::string CONST &Dir) {
        unlink ((Dir + "/insn.ssa").c_str ());
        unlink ((Dir + "/insn.s").c_str ());
        unlink ((Dir + "/insn.dylib").c_str ());
        rmdir (Dir.c_str ());
    }
    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) { *ppValue = new QbeValue (Id, Bits); return S_OK; }

    std::string m_Body;
    UINT32      m_Next = 0;
};

class QbeBackend final : public ComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "qbe"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new QbeEmitter ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<QbeEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateQbeBackend (VOID)
{
    return new QbeBackend ();
}

} // namespace LibCPU
