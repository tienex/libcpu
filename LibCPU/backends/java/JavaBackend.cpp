/** @file
  Java textual-emit backend. See JavaBackend.h.

  Each ICpuValue is a Java `long` local masked to its width. Build() emits a Java
  class, compiles it with javac (which generates JVM bytecode -- JBC -- in a
  .class file), and ICpuCode::Execute marshals the guest state through a temp file
  and runs the class on the JVM.
**/
#include "JavaBackend.h"
#include "LibCPU/CpuState.h"

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

static CONST UINT32 RAM_SIZE = 0x10000;   // assumed guest RAM (6502)

// Static helpers + main() harness; %S% is sizeof(CPU_STATE), %B% the body.
static CHAR8 CONST *kTemplate =
    "import java.nio.file.*;\n"
    "public class Insn {\n"
    "  static long gR(byte[] S,int i,int b){long v=0;for(int k=0;k<b/8;k++)v|=((long)(S[i*8+k]&0xff))<<(8*k);return v;}\n"
    "  static void pR(byte[] S,int i,long v,int b){for(int k=0;k<8;k++)S[i*8+k]=(byte)((k<b/8)?((v>>>(8*k))&0xff):0);}\n"
    "  static long rM(byte[] R,int a,int b){long v=0;for(int k=0;k<b/8;k++)v|=((long)(R[a+k]&0xff))<<(8*k);return v;}\n"
    "  static void wM(byte[] R,int a,long v,int b){for(int k=0;k<b/8;k++)R[a+k]=(byte)((v>>>(8*k))&0xff);}\n"
    "  static long gF(byte[] S,int f){return S[256+f]&1;}\n"
    "  static void sF(byte[] S,int f,long v){S[256+f]=(byte)(v&1);}\n"
    "  static void sP(byte[] S,long p){for(int k=0;k<8;k++)S[264+k]=(byte)((p>>>(8*k))&0xff);}\n"
    "  public static void main(String[] A) throws Exception {\n"
    "    byte[] X=Files.readAllBytes(Paths.get(A[0]));\n"
    "    byte[] R=java.util.Arrays.copyOfRange(X,0,65536);\n"
    "    byte[] S=java.util.Arrays.copyOfRange(X,65536,65536+%S%);\n"
    "%B%"
    "    System.arraycopy(R,0,X,0,65536);System.arraycopy(S,0,X,65536,%S%);\n"
    "    Files.write(Paths.get(A[0]),X);\n"
    "  }\n"
    "}\n";

static UINT64 Mask (UINT64 V, UINT32 Bits) { return (Bits >= 64) ? V : (V & (((UINT64) 1 << Bits) - 1)); }

class JavaValue final : public LcComObject<ICpuValue> {
public:
    JavaValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id, m_Bits;
};

class JavaBlock final : public LcComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
};

static UINT32 IdOf   (ICpuValue *pV) { return static_cast<JavaValue *> (pV)->m_Id; }
static UINT32 BitsOf (ICpuValue *pV) { return static_cast<JavaValue *> (pV)->m_Bits; }

typedef int (*Unused) ();

class JavaCode final : public LcComObject<ICpuCode> {
public:
    JavaCode (std::string Dir) : m_Dir (std::move (Dir)) {}
    ~JavaCode () override {
        if (!m_Dir.empty ()) {
            unlink ((m_Dir + "/Insn.java").c_str ());
            unlink ((m_Dir + "/Insn.class").c_str ());
            unlink ((m_Dir + "/state.bin").c_str ());
            rmdir (m_Dir.c_str ());
        }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }
    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        std::string State = m_Dir + "/state.bin";
        FILE *pF = std::fopen (State.c_str (), "wb");
        if (!pF) return ExecTrap;
        std::fwrite (pRAM, 1, RAM_SIZE, pF);
        std::fwrite (pGRF, 1, sizeof (CPU_STATE), pF);
        std::fclose (pF);

        std::string Cmd = "java -cp '" + m_Dir + "' Insn '" + State + "' 2>/dev/null";
        if (std::system (Cmd.c_str ()) != 0) return ExecTrap;

        pF = std::fopen (State.c_str (), "rb");
        if (!pF) return ExecOk;
        if (std::fread (pRAM, 1, RAM_SIZE, pF) != RAM_SIZE) { std::fclose (pF); return ExecOk; }
        (void) !std::fread (pGRF, 1, sizeof (CPU_STATE), pF);
        std::fclose (pF);
        return ExecOk;
    }
private:
    std::string m_Dir;
};

class JavaEmitter final : public LcComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("long t%u=%lluL;", D, (unsigned long long) Mask (Value, Bits)); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("long t%u=gR(S,%u,%u);", D, Index, Bits); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN /*Sext*/) override {
        Line ("pR(S,%u,t%u,%u);", Index, IdOf (pValue), Bits ? Bits : BitsOf (pValue)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("long t%u=rM(R,(int)t%u,%u);", D, IdOf (pAddr), Bits); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Line ("wM(R,(int)t%u,t%u,%u);", IdOf (pAddr), IdOf (pValue), Bits); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        CONST CHAR8 *pOp;
        switch (Op) {
        case BinAdd: pOp = "+"; break;  case BinSub: pOp = "-"; break;  case BinMul: pOp = "*"; break;
        case BinAnd: pOp = "&"; break;  case BinOr: pOp = "|"; break;   case BinXor: pOp = "^"; break;
        case BinShl: pOp = "<<"; break; case BinLShr: pOp = ">>>"; break; case BinAShr: pOp = ">>"; break;
        case BinUDiv: pOp = "/"; break; case BinURem: pOp = "%"; break;
        default: pOp = "+"; break;
        }
        UINT32 D = Fresh ();
        Line ("long t%u=(t%u%st%u)&%lluL;", D, IdOf (pA), pOp, IdOf (pB), (unsigned long long) Mask (~0ull, Bits));
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA), D = Fresh ();
        switch (Op) {
        case UnNeg: Line ("long t%u=(-t%u)&%lluL;", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); return Make (D, Bits, ppValue);
        case UnCom: Line ("long t%u=(~t%u)&%lluL;", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); return Make (D, Bits, ppValue);
        case UnNot: Line ("long t%u=(t%u==0)?1:0;", D, IdOf (pA)); return Make (D, 1, ppValue);
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 D = Fresh (), A = IdOf (pA), B = IdOf (pB);
        switch (Pred) {
        case CmpEq:  Line ("long t%u=(t%u==t%u)?1:0;", D, A, B); break;
        case CmpNe:  Line ("long t%u=(t%u!=t%u)?1:0;", D, A, B); break;
        case CmpULt: Line ("long t%u=(Long.compareUnsigned(t%u,t%u)<0)?1:0;", D, A, B); break;
        case CmpULe: Line ("long t%u=(Long.compareUnsigned(t%u,t%u)<=0)?1:0;", D, A, B); break;
        case CmpUGt: Line ("long t%u=(Long.compareUnsigned(t%u,t%u)>0)?1:0;", D, A, B); break;
        case CmpUGe: Line ("long t%u=(Long.compareUnsigned(t%u,t%u)>=0)?1:0;", D, A, B); break;
        case CmpSLt: Line ("long t%u=(t%u<t%u)?1:0;", D, A, B); break;
        case CmpSLe: Line ("long t%u=(t%u<=t%u)?1:0;", D, A, B); break;
        case CmpSGt: Line ("long t%u=(t%u>t%u)?1:0;", D, A, B); break;
        case CmpSGe: Line ("long t%u=(t%u>=t%u)?1:0;", D, A, B); break;
        }
        return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA), D = Fresh ();
        switch (Op) {
        case CastTrunc: Line ("long t%u=t%u&%lluL;", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); break;
        case CastZExt:  Line ("long t%u=t%u;", D, IdOf (pA)); break;
        case CastSExt:  Line ("long t%u=((t%u<<%u)>>%u)&%lluL;", D, IdOf (pA), 64 - SrcBits, 64 - SrcBits, (unsigned long long) Mask (~0ull, Bits)); break;
        }
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override { *ppValue = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("long t%u=gF(S,%u);", D, (UINT32) Flag); return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        Line ("sF(S,%u,t%u);", (UINT32) Flag, IdOf (pValue)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override { Line ("sP(S,%lluL);", (unsigned long long) Pc); return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = new JavaBlock (); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        char DirTmpl[] = "/tmp/libcpu_java_XXXXXX";
        if (mkdtemp (DirTmpl) == nullptr) return nullptr;
        std::string Dir = DirTmpl, Src = Dir + "/Insn.java";

        std::string Full = kTemplate;
        Subst (Full, "%S%", std::to_string (sizeof (CPU_STATE)));
        Subst (Full, "%B%", m_Body);

        int Fd = open (Src.c_str (), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
        if (Fd < 0) { rmdir (Dir.c_str ()); return nullptr; }
        FILE *pF = fdopen (Fd, "w");
        if (!pF) { close (Fd); unlink (Src.c_str ()); rmdir (Dir.c_str ()); return nullptr; }
        std::fwrite (Full.data (), 1, Full.size (), pF);
        std::fclose (pF);

        std::string Cmd = "javac -d '" + Dir + "' '" + Src + "' 2>/dev/null";
        if (std::system (Cmd.c_str ()) != 0) {
            unlink (Src.c_str ()); rmdir (Dir.c_str ()); return nullptr;
        }
        return new JavaCode (Dir);
    }

private:
    UINT32 Fresh () { return m_Next++; }
    void Line (CONST CHAR8 *pFmt, ...) {
        char Buf[256];
        va_list Args; va_start (Args, pFmt);
        std::vsnprintf (Buf, sizeof (Buf), pFmt, Args);
        va_end (Args);
        m_Body += "    "; m_Body += Buf; m_Body += "\n";
    }
    static VOID Subst (std::string &S, CHAR8 CONST *Key, std::string CONST &Val) {
        size_t Pos;
        while ((Pos = S.find (Key)) != std::string::npos) S.replace (Pos, std::strlen (Key), Val);
    }
    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) { *ppValue = new JavaValue (Id, Bits); return S_OK; }

    std::string m_Body;
    UINT32      m_Next = 0;
};

class JavaBackend final : public LcComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "java"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new JavaEmitter (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<JavaEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateJavaBackend (VOID)
{
    return new JavaBackend ();
}

} // namespace LibCPU
