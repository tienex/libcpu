/** @file
  C shell (tcsh) textual-emit backend. See TcshBackend.h.

  Each ICpuValue is a tcsh integer variable (tN). tcsh has no user functions, so
  the register/memory/flag accessors are inlined at each operation: the state byte
  array d is 1-indexed, so a guest byte at file-position P is d[P+1], and reads of
  multi-byte values are unrolled at emit time. Arithmetic uses tcsh "@"
  expressions (which support +,-,*,/,%,&,|,^,<<,>> and comparisons). tcsh "~" is
  filename expansion rather than bitwise-NOT, so complement is emitted as
  (mask - value).
**/
#include "TcshBackend.h"
#include "LibCPU/CpuState.h"

#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>

namespace LibCPU {
namespace {

static CONST UINT32 RAM_SIZE = 0x10000;   // assumed guest RAM (6502 / CHIP-8)

//
// Harness: read the state file into the 1-indexed array d, run the emitted body
// (%B%, which inlines all accesses), then write d back. The state byte at
// file-position P is element d[P+1].
//
static CHAR8 CONST *kTemplate =
    "set d = (`od -An -v -tu1 \"$argv[1]\"`)\n"
    "%B%"
    "set e = `printf '\\\\%03o' $d`\n"
    "printf \"$e\" > \"$argv[1]\"\n";

static UINT64
Mask (UINT64 Value, UINT32 Bits)
{
    return (Bits >= 64) ? Value : (Value & (((UINT64) 1 << Bits) - 1));
}

// 1-based index into d for the guest byte at register-file offset Off.
static UINT32 GrfPos (UINT32 Off) { return RAM_SIZE + Off + 1; }

class TcshValue final : public LcComObject<ICpuValue> {
public:
    TcshValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id;
    UINT32 m_Bits;
};

class TcshBlock final : public LcComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
};

static UINT32 IdOf   (ICpuValue *pValue) { return static_cast<TcshValue *> (pValue)->m_Id; }
static UINT32 BitsOf (ICpuValue *pValue) { return static_cast<TcshValue *> (pValue)->m_Bits; }

class TcshCode final : public LcComObject<ICpuCode> {
public:
    TcshCode (std::string Dir) : m_Dir (std::move (Dir)) {}

    ~TcshCode () override {
        if (!m_Dir.empty ()) {
            unlink ((m_Dir + "/insn.csh").c_str ());
            unlink ((m_Dir + "/state.bin").c_str ());
            rmdir (m_Dir.c_str ());
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }

    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        std::string State = m_Dir + "/state.bin";

        FILE *pFile = std::fopen (State.c_str (), "wb");
        if (pFile == nullptr) {
            return ExecTrap;
        }
        std::fwrite (pRAM, 1, RAM_SIZE, pFile);
        std::fwrite (pGRF, 1, sizeof (CPU_STATE), pFile);
        std::fclose (pFile);

        std::string Cmd = "tcsh '" + m_Dir + "/insn.csh' '" + State + "' 2>/dev/null";
        if (std::system (Cmd.c_str ()) != 0) {
            return ExecTrap;
        }

        pFile = std::fopen (State.c_str (), "rb");
        if (pFile == nullptr) {
            return ExecOk;
        }
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
};

class TcshEmitter final : public LcComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("@ t%u = %llu", Dest, (unsigned long long) Mask (Value, Bits));
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        std::string Expr;
        for (UINT32 k = 0; k < Bits / 8; k++) {
            char Term[64];
            UINT32 Pos = GrfPos (CPU_STATE_REG_OFFSET + Index * 8 + k);
            if (k == 0) {
                std::snprintf (Term, sizeof (Term), "$d[%u]", Pos);
            } else {
                std::snprintf (Term, sizeof (Term), " + ($d[%u] << %u)", Pos, 8 * k);
            }
            Expr += Term;
        }
        Line ("@ t%u = (%s)", Dest, Expr.c_str ());
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN /*Sext*/) override {
        UINT32 Width = Bits ? Bits : BitsOf (pValue);
        for (UINT32 k = 0; k < Width / 8; k++) {
            UINT32 Pos = GrfPos (CPU_STATE_REG_OFFSET + Index * 8 + k);
            if (k == 0) {
                Line ("@ d[%u] = ($t%u & 255)", Pos, IdOf (pValue));
            } else {
                Line ("@ d[%u] = (($t%u >> %u) & 255)", Pos, IdOf (pValue), 8 * k);
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        std::string Expr;
        for (UINT32 k = 0; k < Bits / 8; k++) {
            UINT32 IdxVar = Fresh ();
            Line ("@ t%u = ($t%u + %u)", IdxVar, IdOf (pAddr), k + 1);
            char Term[64];
            if (k == 0) {
                std::snprintf (Term, sizeof (Term), "$d[$t%u]", IdxVar);
            } else {
                std::snprintf (Term, sizeof (Term), " + ($d[$t%u] << %u)", IdxVar, 8 * k);
            }
            Expr += Term;
        }
        Line ("@ t%u = (%s)", Dest, Expr.c_str ());
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        for (UINT32 k = 0; k < Bits / 8; k++) {
            UINT32 IdxVar = Fresh ();
            Line ("@ t%u = ($t%u + %u)", IdxVar, IdOf (pAddr), k + 1);
            if (k == 0) {
                Line ("@ d[$t%u] = ($t%u & 255)", IdxVar, IdOf (pValue));
            } else {
                Line ("@ d[$t%u] = (($t%u >> %u) & 255)", IdxVar, IdOf (pValue), 8 * k);
            }
        }
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
        Line ("@ t%u = (($t%u %s $t%u) & %llu)", Dest, IdOf (pA), pOp, IdOf (pB),
              (unsigned long long) Mask (~0ull, Bits));
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        unsigned long long FullMask = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case UnNeg:
            Line ("@ t%u = ((-$t%u) & %llu)", Dest, IdOf (pA), FullMask);
            return Make (Dest, Bits, ppValue);
        case UnCom:
            // tcsh "~" is filename expansion, so complement of a masked value is mask - value.
            Line ("@ t%u = (%llu - $t%u)", Dest, FullMask, IdOf (pA));
            return Make (Dest, Bits, ppValue);
        case UnNot:
            Line ("@ t%u = ($t%u == 0)", Dest, IdOf (pA));
            return Make (Dest, 1, ppValue);
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
        Line ("@ t%u = ($t%u %s $t%u)", Dest, IdOf (pA), pOp, IdOf (pB));
        return Make (Dest, 1, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        unsigned long long FullMask = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case CastTrunc:
            Line ("@ t%u = ($t%u & %llu)", Dest, IdOf (pA), FullMask);
            break;
        case CastZExt:
            Line ("@ t%u = $t%u", Dest, IdOf (pA));
            break;
        case CastSExt:
            Line ("@ t%u = $t%u", Dest, IdOf (pA));
            Line ("if ($t%u & %llu) @ t%u = ($t%u - %llu)", Dest,
                  (unsigned long long) (1ull << (SrcBits - 1)), Dest, Dest, (unsigned long long) (1ull << SrcBits));
            Line ("@ t%u = ($t%u & %llu)", Dest, Dest, FullMask);
            break;
        }
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override {
        *ppValue = nullptr;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("@ t%u = ($d[%u] & 1)", Dest, GrfPos (CPU_STATE_FLAG_OFFSET + (UINT32) Flag));
        return Make (Dest, 1, ppValue);
    }

    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        Line ("@ d[%u] = ($t%u & 1)", GrfPos (CPU_STATE_FLAG_OFFSET + (UINT32) Flag), IdOf (pValue));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        for (UINT32 k = 0; k < 8; k++) {
            UINT32 Byte = (UINT32) ((Pc >> (8 * k)) & 0xff);
            Line ("@ d[%u] = %u", GrfPos (CPU_STATE_PC_OFFSET + k), Byte);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override {
        *ppBlock = new TcshBlock ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        char DirTemplate[] = "/tmp/libcpu_tcsh_XXXXXX";
        if (mkdtemp (DirTemplate) == nullptr) {
            return nullptr;
        }
        std::string Dir = DirTemplate;
        std::string Source = Dir + "/insn.csh";

        std::string Full = kTemplate;
        Subst (Full, "%B%", m_Body);

        int Fd = open (Source.c_str (), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
        if (Fd < 0) {
            rmdir (Dir.c_str ());
            return nullptr;
        }
        FILE *pFile = fdopen (Fd, "w");
        if (pFile == nullptr) {
            close (Fd);
            unlink (Source.c_str ());
            rmdir (Dir.c_str ());
            return nullptr;
        }
        std::fwrite (Full.data (), 1, Full.size (), pFile);
        std::fclose (pFile);
        return new TcshCode (Dir);
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
        *ppValue = new TcshValue (Id, Bits);
        return S_OK;
    }

    std::string m_Body;
    UINT32      m_Next = 0;
};

class TcshBackend final : public LcComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "tcsh"; }

    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new TcshEmitter ();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<TcshEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateTcshBackend (VOID)
{
    return new TcshBackend ();
}

} // namespace LibCPU
