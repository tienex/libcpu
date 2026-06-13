/** @file
  In-process CPython textual-emit backend. See CPythonBackend.h.

  Each ICpuValue is a Python int masked to its width. The op-emission is identical
  to the subprocess `python` backend, but the harness differs: instead of writing a
  script and spawning python3, Build() compiles the source once with
  Py_CompileString into a code object, and Execute() runs it with PyEval_EvalCode on
  an embedded interpreter. The guest state is handed in as a bytearray named d that
  the code mutates through memoryviews; it is copied back out afterwards.
**/
#include "CPythonBackend.h"
#include "LibCPU/CpuState.h"

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>

namespace LibCPU {
namespace {

static CONST UINT32 RAM_SIZE = 0x10000;   // assumed guest RAM (6502 / CHIP-8)

// %S% is sizeof(CPU_STATE), %B% the body. RAM/ST are memoryviews into the bytearray
// d (supplied by the host each call); the helpers mutate it in place.
static CHAR8 CONST *kTemplate =
    "RAM=memoryview(d)[0:_rs]\n"             // _rs = host-stated RAM size (dynamic)
    "ST=memoryview(d)[_rs:_rs+%S%]\n"
    "def gR(i,b):\n v=0\n for k in range(b//8):v|=ST[i*8+k]<<(8*k)\n return v\n"
    "def pR(i,v,b):\n for k in range(8):ST[i*8+k]=((v>>(8*k))&0xff) if k<b//8 else 0\n"
    "def rM(a,b):\n v=0\n for k in range(b//8):v|=RAM[a+k]<<(8*k)\n return v\n"
    // CPU_STATE SMC fields (offsets match CpuState.h): CodeStart 272, CodeEnd 280,
    // TrapPc 288, CodeDirty[] 296. cS/cE read the watched region; sT records the
    // trap PC; wM's last line is the per-page write-barrier (inert when cS()==cE()).
    "def cS():return int.from_bytes(ST[272:280],'little')\n"
    "def cE():return int.from_bytes(ST[280:288],'little')\n"
    "def sT(p):\n for k in range(8):ST[288+k]=(p>>(8*k))&0xff\n"
    "def sD(p):\n for k in range(8):ST[328+k]=(p>>(8*k))&0xff\n"   // dispatch scratch (DispPc)
    "def gD():return int.from_bytes(ST[328:336],'little')\n"
    // EdgeCount[] starts at 336 (CPU_STATE_EDGECOUNT_OFFSET): bump slot i in place.
    "def eC(i):\n o=336+i*8\n v=int.from_bytes(ST[o:o+8],'little')+1\n for k in range(8):ST[o+k]=(v>>(8*k))&0xff\n"
    "def wM(a,v,b):\n for k in range(b//8):RAM[a+k]=(v>>(8*k))&0xff\n if a>=cS() and a<cE():ST[296+((a>>11)&31)]|=1<<((a>>8)&7)\n"
    "def gF(f):return ST[256+f]&1\n"
    "def sF(f,v):ST[256+f]=v&1\n"
    "def sP(p):\n for k in range(8):ST[264+k]=(p>>(8*k))&0xff\n"
    "def sx(v,s):\n if v&(1<<(s-1)):v-=(1<<s)\n return v\n"
    "%B%"
    "RAM.release();ST.release()\n";

static UINT64
Mask (UINT64 Value, UINT32 Bits)
{
    return (Bits >= 64) ? Value : (Value & (((UINT64) 1 << Bits) - 1));
}

static BOOLEAN
InitPython (VOID)
{
    if (!Py_IsInitialized ()) {
        Py_Initialize ();
        // Py_Initialize leaves the GIL held by THIS thread, which would block every
        // other thread forever. Release it so any thread (notably the JIT's
        // background recompile thread) can acquire it through PyGILState_Ensure.
        PyEval_SaveThread ();
    }
    return (BOOLEAN) Py_IsInitialized ();
}

//
// RAII holder for the Global Interpreter Lock. The embedded interpreter has one
// GIL; every thread that touches the C-API must hold it. With the GIL released by
// InitPython, each entry point (compile, execute, teardown) brackets its work with
// one of these -- this is what lets the JIT recompile on a background thread while
// the main thread executes, without corrupting interpreter state.
//
class PyGilGuard {
public:
    PyGilGuard () : m_State (PyGILState_Ensure ()) {}
    ~PyGilGuard () { PyGILState_Release (m_State); }
    PyGilGuard (CONST PyGilGuard &) = delete;
    PyGilGuard &operator= (CONST PyGilGuard &) = delete;
private:
    PyGILState_STATE m_State;
};

class PyValue final : public ComObject<ICpuValue> {
public:
    PyValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id;
    UINT32 m_Bits;
};

class PyBlock final : public ComObject<ICpuBlock> {
public:
    explicit PyBlock (UINT32 Index) : m_Index (Index) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
    UINT32      m_Index;   // dispatch key: the "if _pc == <Index>:" arm
    std::string m_Buf;     // the block's Python statements (no indent)
};

static UINT32 BlkIndex (ICpuBlock *pB) { return static_cast<PyBlock *> (pB)->m_Index; }

static UINT32 IdOf   (ICpuValue *pValue) { return static_cast<PyValue *> (pValue)->m_Id; }
static UINT32 BitsOf (ICpuValue *pValue) { return static_cast<PyValue *> (pValue)->m_Bits; }

class PyCode final : public ComObject<ICpuCode> {
public:
    PyCode (PyObject *Code) : m_Code (Code) {}

    ~PyCode () override {
        if (m_Code != nullptr && Py_IsInitialized ()) {
            PyGilGuard Gil;   // refcount drop touches the interpreter; hold the GIL
            Py_DECREF (m_Code);
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }

    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        PyGilGuard Gil;   // hold the GIL for the duration of the evaluation
        UINT64 RamSize = ((CPU_STATE *) pGRF)->RamSize;   // host-stated; 0 -> 64 KiB
        if (RamSize == 0) { RamSize = CPU_RAM_DEFAULT; }
        size_t Ram   = (size_t) RamSize;
        size_t Total = Ram + sizeof (CPU_STATE);

        // Marshal the native RAM + register file into one contiguous bytearray.
        std::vector<char> Buffer (Total);
        std::memcpy (Buffer.data (), pRAM, Ram);
        std::memcpy (Buffer.data () + Ram, pGRF, sizeof (CPU_STATE));

        PyObject *Ba = PyByteArray_FromStringAndSize (Buffer.data (), (Py_ssize_t) Total);
        PyObject *Globals = PyDict_New ();
        PyDict_SetItemString (Globals, "__builtins__", PyEval_GetBuiltins ());
        PyDict_SetItemString (Globals, "d", Ba);
        PyObject *Rs = PyLong_FromUnsignedLongLong ((unsigned long long) Ram);
        PyDict_SetItemString (Globals, "_rs", Rs);   // RAM size for the RAM/ST split

        PyObject *Result = PyEval_EvalCode (m_Code, Globals, Globals);
        CPU_EXEC_STATUS Status = ExecOk;
        if (Result == nullptr) {
            PyErr_Clear ();
            Status = ExecTrap;
        } else {
            Py_DECREF (Result);
            CONST char *Out = PyByteArray_AS_STRING (Ba);
            std::memcpy (pRAM, Out, Ram);
            std::memcpy (pGRF, Out + Ram, sizeof (CPU_STATE));
        }
        Py_DECREF (Rs);

        Py_DECREF (Globals);
        Py_DECREF (Ba);
        return Status;
    }

private:
    PyObject *m_Code;
};

class PyEmitter final : public ComObject<ICpuEmitter>, public ICpuSmcEmitter, public ICpuProfileEmitter {
public:
    // Three interfaces (ICpuEmitter + ICpuSmcEmitter + ICpuProfileEmitter): resolve
    // QI here, forward refcounting to the ComObject base.
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        if (ppvObject == nullptr) {
            return E_POINTER;
        }
        if (CompareGuid (&riid, &IID_ICpuSmcEmitter)) {
            *ppvObject = static_cast<ICpuSmcEmitter *> (this);
            AddRef ();
            return S_OK;
        }
        if (CompareGuid (&riid, &IID_ICpuProfileEmitter)) {
            *ppvObject = static_cast<ICpuProfileEmitter *> (this);
            AddRef ();
            return S_OK;
        }
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }
    UINT32 STDMETHODCALLTYPE AddRef () override { return ComObject<ICpuEmitter>::AddRef (); }
    UINT32 STDMETHODCALLTYPE Release () override { return ComObject<ICpuEmitter>::Release (); }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 D = Fresh ();
        Line ("t%u=%llu", D, (unsigned long long) Mask (Value, Bits));
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh ();
        Line ("t%u=gR(%u,%u)", D, Index, Bits);
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN /*Sext*/) override {
        Line ("pR(%u,t%u,%u)", Index, IdOf (pValue), Bits ? Bits : BitsOf (pValue));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh ();
        Line ("t%u=rM(t%u,%u)", D, IdOf (pAddr), Bits);
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Line ("wM(t%u,t%u,%u)", IdOf (pAddr), IdOf (pValue), Bits);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        CONST CHAR8 *pOp;
        switch (Op) {
        case BinAdd:  pOp = "+";  break;
        case BinSub:  pOp = "-";  break;
        case BinMul:  pOp = "*";  break;
        case BinAnd:  pOp = "&";  break;
        case BinOr:   pOp = "|";  break;
        case BinXor:  pOp = "^";  break;
        case BinShl:  pOp = "<<"; break;
        case BinLShr: pOp = ">>"; break;
        case BinAShr: pOp = ">>"; break;
        case BinUDiv: pOp = "//"; break;
        case BinURem: pOp = "%";  break;
        default:      pOp = "+";  break;
        }
        UINT32 D = Fresh ();
        Line ("t%u=(t%u%st%u)&%llu", D, IdOf (pA), pOp, IdOf (pB), (unsigned long long) Mask (~0ull, Bits));
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA), D = Fresh ();
        switch (Op) {
        case UnNeg: Line ("t%u=(-t%u)&%llu", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); return Make (D, Bits, ppValue);
        case UnCom: Line ("t%u=(~t%u)&%llu", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); return Make (D, Bits, ppValue);
        case UnNot: Line ("t%u=1 if t%u==0 else 0", D, IdOf (pA)); return Make (D, 1, ppValue);
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        CONST CHAR8 *pOp;
        switch (Pred) {
        case CmpEq:  pOp = "==";  break;
        case CmpNe:  pOp = "!=";  break;
        case CmpULt: case CmpSLt: pOp = "<";  break;
        case CmpULe: case CmpSLe: pOp = "<="; break;
        case CmpUGt: case CmpSGt: pOp = ">";  break;
        case CmpUGe: case CmpSGe: pOp = ">="; break;
        default:     pOp = "==";  break;
        }
        UINT32 D = Fresh ();
        Line ("t%u=1 if (t%u%st%u) else 0", D, IdOf (pA), pOp, IdOf (pB));
        return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA), D = Fresh ();
        switch (Op) {
        case CastTrunc: Line ("t%u=t%u&%llu", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); break;
        case CastZExt:  Line ("t%u=t%u", D, IdOf (pA)); break;
        case CastSExt:  Line ("t%u=sx(t%u,%u)&%llu", D, IdOf (pA), SrcBits, (unsigned long long) Mask (~0ull, Bits)); break;
        }
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override { *ppValue = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 D = Fresh ();
        Line ("t%u=gF(%u)", D, (UINT32) Flag);
        return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        Line ("sF(%u,t%u)", (UINT32) Flag, IdOf (pValue));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override { Line ("sP(%llu)", (unsigned long long) Pc); return S_OK; }
    // ---- control flow: a structured PC-dispatch (Python has no goto) -------
    // Each block is an "if _pc == <index>:" arm of a "while True:" loop; an edge
    // sets _pc to the target's index and the loop re-dispatches.
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override {
        PyBlock *pB = new PyBlock ((UINT32) m_Blocks.size ());
        m_Blocks.push_back (pB);   // raw, kept in order for Build (not owned)
        *ppBlock = pB;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *pBlock) override {
        m_pCur = &static_cast<PyBlock *> (pBlock)->m_Buf;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *pTarget) override {
        Line ("_pc=%u", BlkIndex (pTarget));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *pCond, ICpuBlock *pTrue, ICpuBlock *pFalse) override {
        Line ("_pc=(%u) if t%u else (%u)", BlkIndex (pTrue), IdOf (pCond), BlkIndex (pFalse));
        return S_OK;
    }

    // ---- self-modifying-code guard (ICpuSmcEmitter) -----------------------
    HRESULT STDMETHODCALLTYPE EmitCodeGuard (CPU_ADDR Pc) override {
        // This page's dirty bit: bit (page&7) of ST[296 + (page>>3)]. On a hit,
        // record TrapPc and stop.
        UINT32 Page = (UINT32) ((Pc >> 8) & 255);
        Line ("if ST[%u]&%u:sT(%llu);break", (UINT32) (296 + (Page >> 3)), (1u << (Page & 7)), (unsigned long long) Pc);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IndirectBranch (ICpuValue *pTargetPc) override {
        Line ("sT(t%u)", IdOf (pTargetPc));   // record runtime target in TrapPc
        Line ("break");                       // leave the dispatch; host resumes at TrapPc
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetDispatchTarget (ICpuValue *pTargetPc) override {
        Line ("sD(t%u)", IdOf (pTargetPc));   // record runtime target in DispPc scratch
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDispatchTarget (ICpuValue **ppValue) override {
        UINT32 D = Fresh ();
        Line ("t%u=gD()", D);
        return Make (D, 64, ppValue);
    }

    // ---- runtime edge profiling (ICpuProfileEmitter) ----------------------
    HRESULT STDMETHODCALLTYPE EmitEdgeCounter (UINT32 Index) override {
        if (Index >= CPU_PROFILE_SLOTS) {
            return S_OK;
        }
        Line ("eC(%u)", Index);   // ++EdgeCount[Index]
        return S_OK;
    }

    ICpuCode *Build () {
        if (!InitPython ()) {
            return nullptr;
        }
        PyGilGuard Gil;   // hold the GIL while compiling (may run on a background thread)
        std::string Full = kTemplate;
        Subst (Full, "%S%", std::to_string (sizeof (CPU_STATE)));
        Subst (Full, "%B%", BuildBody ());

        PyObject *Code = Py_CompileString (Full.c_str (), "<insn>", Py_file_input);
        if (Code == nullptr) {
            PyErr_Clear ();
            return nullptr;
        }
        return new PyCode (Code);
    }

private:
    UINT32 Fresh () { return m_Next++; }

    void Line (CONST CHAR8 *pFmt, ...) {
        char Buf[256];
        va_list Args;
        va_start (Args, pFmt);
        std::vsnprintf (Buf, sizeof (Buf), pFmt, Args);
        va_end (Args);
        *m_pCur += Buf;                  // append to the current block (entry by default)
        *m_pCur += "\n";
    }

    // Straight-line (JIT) -> the entry body as-is. CFG -> a "while True:" PC-dispatch
    // over the blocks, each re-indented under its "if _pc == <index>:" arm.
    std::string BuildBody () {
        if (m_Blocks.empty ()) {
            return m_Body;
        }
        std::string Out = m_Body;        // the entry's "_pc=<entry>" set by Branch
        Out += "while True:\n";
        for (size_t i = 0; i < m_Blocks.size (); i++) {
            Out += (i == 0) ? " if _pc==" : " elif _pc==";
            Out += std::to_string (i) + ":\n";
            std::string CONST &Buf = m_Blocks[i]->m_Buf;
            if (Buf.empty ()) {
                Out += "  break\n";      // e.g. the AOT driver's empty exit block
                continue;
            }
            for (size_t p = 0; p < Buf.size (); ) {       // re-indent each line by 2 spaces
                size_t Nl = Buf.find ('\n', p);
                if (Nl == std::string::npos) { Nl = Buf.size (); }
                Out += "  ";
                Out.append (Buf, p, Nl - p);
                Out += "\n";
                p = Nl + 1;
            }
        }
        Out += " else:\n  break\n";
        return Out;
    }

    static VOID Subst (std::string &S, CHAR8 CONST *Key, std::string CONST &Val) {
        size_t Pos;
        while ((Pos = S.find (Key)) != std::string::npos) {
            S.replace (Pos, std::strlen (Key), Val);
        }
    }

    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) {
        *ppValue = new PyValue (Id, Bits);
        return S_OK;
    }

    std::string            m_Body;             // entry block (init); straight-line body for JIT
    std::string           *m_pCur = &m_Body;   // current emission target
    std::vector<PyBlock *> m_Blocks;           // CFG blocks in creation order (not owned)
    UINT32                 m_Next = 0;
};

class CPythonBackend final : public ComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "cpython"; }

    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new PyEmitter ();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<PyEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateCPythonBackend (VOID)
{
    return new CPythonBackend ();
}

} // namespace LibCPU
