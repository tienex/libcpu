/** @file
  Embedded-Tcl backend. See TclEmbBackend.h.

  The in-process sibling of the `tcl` backend: it emits the IDENTICAL Tcl script (same value
  representation, same accessors, same %B% body) but executes it through a libtcl interpreter
  linked into this process -- Tcl_CreateInterp once per compiled block, then Tcl_EvalFile each
  run -- instead of spawning tclsh. No process per burst; the interpreter is cached. argv is set
  so the script's [lindex $argv 0] finds the marshalled state file, exactly as tclsh would pass it.
**/
#include "TclEmbBackend.h"
#include "LibCPU/CpuState.h"

#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <tcl.h>

namespace LibCPU {
namespace {

static CONST UINT32 RAM_SIZE = 0x10000;   // assumed guest RAM (matches the script's 65536 base)

//
// Identical harness to the tcl backend: read the state file into the byte list B, define
// register/memory/flag accessors, run the emitted body (%B%), then write B back.
//
static CHAR8 CONST *kTemplate =
    "set f [open [lindex $argv 0] rb];set data [read $f];close $f\n"
    "binary scan $data cu* B\n"
    "proc gR {i b} {global B;set v 0;for {set k 0} {$k<$b/8} {incr k} {set v [expr {$v|([lindex $B [expr {65536+$i*8+$k}]]<<(8*$k))}]};return $v}\n"
    "proc pR {i v b} {global B;for {set k 0} {$k<8} {incr k} {lset B [expr {65536+$i*8+$k}] [expr {$k<$b/8?(($v>>(8*$k))&0xff):0}]}}\n"
    "proc rM {a b} {global B;set v 0;for {set k 0} {$k<$b/8} {incr k} {set v [expr {$v|([lindex $B [expr {$a+$k}]]<<(8*$k))}]};return $v}\n"
    "proc wM {a v b} {global B;for {set k 0} {$k<$b/8} {incr k} {lset B [expr {$a+$k}] [expr {($v>>(8*$k))&0xff}]}}\n"
    "proc gF {f} {global B;expr {[lindex $B [expr {65536+256+$f}]]&1}}\n"
    "proc sF {f v} {global B;lset B [expr {65536+256+$f}] [expr {$v&1}]}\n"
    "proc sP {p} {global B;for {set k 0} {$k<8} {incr k} {lset B [expr {65536+264+$k}] [expr {($p>>(8*$k))&0xff}]}}\n"
    "proc sx {v s} {expr {($v&(1<<($s-1)))?$v-(1<<$s):$v}}\n"
    "%B%"
    "set out [binary format cu* $B]\n"
    "set f [open [lindex $argv 0] wb];puts -nonewline $f $out;close $f\n";

static UINT64
Mask (UINT64 Value, UINT32 Bits)
{
    return (Bits >= 64) ? Value : (Value & (((UINT64) 1 << Bits) - 1));
}

class TclValue final : public ComObject<ICpuValue> {
public:
    TclValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id;
    UINT32 m_Bits;
};

class TclBlock final : public ComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
};

static UINT32 IdOf   (ICpuValue *pValue) { return static_cast<TclValue *> (pValue)->m_Id; }
static UINT32 BitsOf (ICpuValue *pValue) { return static_cast<TclValue *> (pValue)->m_Bits; }

//
// The compiled block: owns its temp dir and a cached Tcl interpreter. Execute marshals the
// state, runs the script in-process (no spawn), and reads the state back.
//
class TclEmbCode final : public ComObject<ICpuCode> {
public:
    TclEmbCode (std::string Dir) : m_Dir (std::move (Dir)) {}

    ~TclEmbCode () override {
        if (m_pInterp != nullptr) { Tcl_DeleteInterp (m_pInterp); }
        if (!m_Dir.empty ()) {
            unlink ((m_Dir + "/insn.tcl").c_str ());
            unlink ((m_Dir + "/state.bin").c_str ());
            rmdir (m_Dir.c_str ());
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }

    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        std::string State  = m_Dir + "/state.bin";
        std::string Script = m_Dir + "/insn.tcl";

        FILE *pFile = std::fopen (State.c_str (), "wb");
        if (pFile == nullptr) { return ExecTrap; }
        std::fwrite (pRAM, 1, RAM_SIZE, pFile);
        std::fwrite (pGRF, 1, sizeof (CPU_STATE), pFile);
        std::fclose (pFile);

        if (m_pInterp == nullptr) {
            static bool FoundExe = false;
            if (!FoundExe) { Tcl_FindExecutable (nullptr); FoundExe = true; }   // Tcl init prerequisite
            m_pInterp = Tcl_CreateInterp ();
            if (m_pInterp == nullptr) { return ExecTrap; }
        }
        // Hand the script its state file the way tclsh would: argv = {state}, argc = 1.
        Tcl_SetVar2 (m_pInterp, "argv",  nullptr, State.c_str (), TCL_GLOBAL_ONLY);
        Tcl_SetVar2 (m_pInterp, "argc",  nullptr, "1",           TCL_GLOBAL_ONLY);
        Tcl_SetVar2 (m_pInterp, "argv0", nullptr, Script.c_str (), TCL_GLOBAL_ONLY);
        if (Tcl_EvalFile (m_pInterp, Script.c_str ()) != TCL_OK) { return ExecTrap; }

        pFile = std::fopen (State.c_str (), "rb");
        if (pFile == nullptr) { return ExecOk; }
        if (std::fread (pRAM, 1, RAM_SIZE, pFile) != RAM_SIZE) {
            std::fclose (pFile);
            return ExecOk;
        }
        (void) !std::fread (pGRF, 1, sizeof (CPU_STATE), pFile);
        std::fclose (pFile);
        return ExecOk;
    }

private:
    std::string m_Dir;
    Tcl_Interp *m_pInterp = nullptr;
};

class TclEmitter final : public ComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("set t%u %llu", Dest, (unsigned long long) Mask (Value, Bits));
        return Make (Dest, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("set t%u [gR %u %u]", Dest, Index, Bits);
        return Make (Dest, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN /*Sext*/) override {
        Line ("pR %u $t%u %u", Index, IdOf (pValue), Bits ? Bits : BitsOf (pValue));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("set t%u [rM $t%u %u]", Dest, IdOf (pAddr), Bits);
        return Make (Dest, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Line ("wM $t%u $t%u %u", IdOf (pAddr), IdOf (pValue), Bits);
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
        case BinUDiv: pOp = "/";  break;
        case BinURem: pOp = "%";  break;
        default:      pOp = "+";  break;
        }
        UINT32 Dest = Fresh ();
        Line ("set t%u [expr {($t%u%s$t%u)&%llu}]", Dest, IdOf (pA), pOp, IdOf (pB),
              (unsigned long long) Mask (~0ull, Bits));
        return Make (Dest, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        unsigned long long FullMask = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case UnNeg:
            Line ("set t%u [expr {(-$t%u)&%llu}]", Dest, IdOf (pA), FullMask);
            return Make (Dest, Bits, ppValue);
        case UnCom:
            Line ("set t%u [expr {(~$t%u)&%llu}]", Dest, IdOf (pA), FullMask);
            return Make (Dest, Bits, ppValue);
        case UnNot:
            Line ("set t%u [expr {$t%u==0}]", Dest, IdOf (pA));
            return Make (Dest, 1, ppValue);
        // floating-point ops: unsupported by this backend
        default: return E_INVALIDARG;
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
        UINT32 Dest = Fresh ();
        Line ("set t%u [expr {$t%u%s$t%u}]", Dest, IdOf (pA), pOp, IdOf (pB));
        return Make (Dest, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        unsigned long long FullMask = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case CastTrunc:
            Line ("set t%u [expr {$t%u&%llu}]", Dest, IdOf (pA), FullMask);
            break;
        case CastZExt:
            Line ("set t%u $t%u", Dest, IdOf (pA));
            break;
        case CastSExt:
            Line ("set t%u [expr {[sx $t%u %u]&%llu}]", Dest, IdOf (pA), SrcBits, FullMask);
            break;
        // floating-point ops: unsupported by this backend
        default: return E_INVALIDARG;
        }
        return Make (Dest, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override {
        *ppValue = nullptr;
        return E_NOTIMPL;                            // synthesized by the shadow layer
    }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("set t%u [gF %u]", Dest, (UINT32) Flag);
        return Make (Dest, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        Line ("sF %u $t%u", (UINT32) Flag, IdOf (pValue));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        Line ("sP %llu", (unsigned long long) Pc);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = new TclBlock (); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        char DirTemplate[] = "/tmp/libcpu_tclemb_XXXXXX";
        if (mkdtemp (DirTemplate) == nullptr) { return nullptr; }
        std::string Dir = DirTemplate;
        std::string Source = Dir + "/insn.tcl";

        std::string Full = kTemplate;
        Subst (Full, "%B%", m_Body);

        int Fd = open (Source.c_str (), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
        if (Fd < 0) { rmdir (Dir.c_str ()); return nullptr; }
        FILE *pFile = fdopen (Fd, "w");
        if (pFile == nullptr) { close (Fd); unlink (Source.c_str ()); rmdir (Dir.c_str ()); return nullptr; }
        std::fwrite (Full.data (), 1, Full.size (), pFile);
        std::fclose (pFile);
        return new TclEmbCode (Dir);
    }

private:
    UINT32 Fresh () { return m_Next++; }
    void Line (CONST CHAR8 *pFmt, ...) {
        char Buf[256];
        va_list Args;
        va_start (Args, pFmt);
        std::vsnprintf (Buf, sizeof (Buf), pFmt, Args);
        va_end (Args);
        m_Body += Buf;
        m_Body += "\n";
    }
    static VOID Subst (std::string &Str, CHAR8 CONST *pKey, std::string CONST &Val) {
        size_t Pos;
        while ((Pos = Str.find (pKey)) != std::string::npos) {
            Str.replace (Pos, std::strlen (pKey), Val);
        }
    }
    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) {
        *ppValue = new TclValue (Id, Bits);
        return S_OK;
    }
    std::string m_Body;
    UINT32      m_Next = 0;
};

class TclEmbBackend final : public ComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "tclemb"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new TclEmitter ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<TclEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateTclEmbBackend (VOID)
{
    return new TclEmbBackend ();
}

} // namespace LibCPU
