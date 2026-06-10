/** @file
  Windows cmd.exe textual-emit backend (experimental, runs under wine). See
  CmdBackend.h.

  Each ICpuValue is a cmd variable (tN). cmd's "set /a" provides the arithmetic and
  bitwise operators but cannot compare or index an array by a runtime value, so
  comparisons are emitted with IF (EQU/NEQ/LSS/...) over delayed-expansion values,
  and memory addresses are constant-folded (the ConstInt that produced them is
  tracked). The state is a small RAM window plus CPU_STATE, marshalled as a text
  file of decimal bytes loaded into the d<N> pseudo array.
**/
#include "CmdBackend.h"
#include "LibCPU/CpuState.h"

#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <unordered_map>

namespace LibCPU {
namespace {

static CONST UINT32 RAM_WINDOW = 0x20;    // tiny RAM window (cmd is slow under wine)
static CONST UINT32 GRF_BASE   = RAM_WINDOW;
static CONST UINT32 TOTAL      = RAM_WINDOW + (UINT32) sizeof (CPU_STATE);

//
// Harness: load the text state file into the d<N> array, run the emitted body
// (%B%), then write the array back. %T% is the highest array index.
//
static CHAR8 CONST *kTemplate =
    "@echo off\r\n"
    "setlocal enabledelayedexpansion\r\n"
    "set i=0\r\n"
    "for /f %%a in (state.txt) do (set \"d!i!=%%a\" & set /a i+=1)\r\n"
    "%B%"
    // wine cmd's block redirect "(...) > file" is unreliable, so append per line.
    "del out.txt 2>nul\r\n"
    "for /l %%i in (0,1,%T%) do call echo %%d%%i%%>>out.txt\r\n";

static UINT64
Mask (UINT64 Value, UINT32 Bits)
{
    return (Bits >= 64) ? Value : (Value & (((UINT64) 1 << Bits) - 1));
}

static UINT32 RegPos  (UINT32 Index, UINT32 Byte) { return GRF_BASE + CPU_STATE_REG_OFFSET + Index * 8 + Byte; }
static UINT32 FlagPos (UINT32 Flag)               { return GRF_BASE + CPU_STATE_FLAG_OFFSET + Flag; }
static UINT32 PcPos   (UINT32 Byte)               { return GRF_BASE + CPU_STATE_PC_OFFSET + Byte; }

class CmdValue final : public LcComObject<ICpuValue> {
public:
    CmdValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id;
    UINT32 m_Bits;
};

class CmdBlock final : public LcComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
};

static UINT32 IdOf   (ICpuValue *pValue) { return static_cast<CmdValue *> (pValue)->m_Id; }
static UINT32 BitsOf (ICpuValue *pValue) { return static_cast<CmdValue *> (pValue)->m_Bits; }

class CmdCode final : public LcComObject<ICpuCode> {
public:
    CmdCode (std::string Dir) : m_Dir (std::move (Dir)) {}

    ~CmdCode () override {
        if (!m_Dir.empty ()) {
            unlink ((m_Dir + "/insn.cmd").c_str ());
            unlink ((m_Dir + "/state.txt").c_str ());
            unlink ((m_Dir + "/out.txt").c_str ());
            rmdir (m_Dir.c_str ());
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }

    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        UINT8 *pRamBytes = (UINT8 *) pRAM;
        UINT8 *pGrfBytes = (UINT8 *) pGRF;
        std::string State = m_Dir + "/state.txt";

        // Marshal the RAM window + CPU_STATE out as decimal text (one byte per line).
        FILE *pFile = std::fopen (State.c_str (), "wb");
        if (pFile == nullptr) {
            return ExecTrap;
        }
        for (UINT32 i = 0; i < RAM_WINDOW; i++) {
            std::fprintf (pFile, "%u\r\n", pRamBytes[i]);
        }
        for (UINT32 i = 0; i < sizeof (CPU_STATE); i++) {
            std::fprintf (pFile, "%u\r\n", pGrfBytes[i]);
        }
        std::fclose (pFile);

        // Run cmd.exe under wine with the temp dir as the working directory.
        std::string Cmd = "cd '" + m_Dir + "' && wine cmd /c insn.cmd >/dev/null 2>&1";
        (void) std::system (Cmd.c_str ());

        // Marshal back from out.txt (best effort: wine exit codes are unreliable).
        std::string Out = m_Dir + "/out.txt";
        pFile = std::fopen (Out.c_str (), "rb");
        if (pFile == nullptr) {
            return ExecOk;
        }
        for (UINT32 i = 0; i < RAM_WINDOW; i++) {
            unsigned Tmp = 0;
            if (std::fscanf (pFile, "%u", &Tmp) != 1) { std::fclose (pFile); return ExecOk; }
            pRamBytes[i] = (UINT8) Tmp;
        }
        for (UINT32 i = 0; i < sizeof (CPU_STATE); i++) {
            unsigned Tmp = 0;
            if (std::fscanf (pFile, "%u", &Tmp) != 1) { break; }
            pGrfBytes[i] = (UINT8) Tmp;
        }
        std::fclose (pFile);
        return ExecOk;
    }

private:
    std::string m_Dir;
};

class CmdEmitter final : public LcComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        UINT64 Masked = Mask (Value, Bits);
        Line ("set /a \"t%u=%llu\"", Dest, (unsigned long long) Masked);
        m_Const[Dest] = Masked;   // remember the constant for address folding
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        std::string Expr;
        for (UINT32 k = 0; k < Bits / 8; k++) {
            char Term[48];
            if (k == 0) {
                std::snprintf (Term, sizeof (Term), "d%u", RegPos (Index, k));
            } else {
                std::snprintf (Term, sizeof (Term), " + (d%u * %llu)", RegPos (Index, k), (unsigned long long) (1ull << (8 * k)));
            }
            Expr += Term;
        }
        Line ("set /a \"t%u=%s\"", Dest, Expr.c_str ());
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN /*Sext*/) override {
        UINT32 Width = Bits ? Bits : BitsOf (pValue);
        for (UINT32 k = 0; k < Width / 8; k++) {
            if (k == 0) {
                Line ("set /a \"d%u=t%u & 255\"", RegPos (Index, k), IdOf (pValue));
            } else {
                Line ("set /a \"d%u=(t%u / %llu) & 255\"", RegPos (Index, k), IdOf (pValue), (unsigned long long) (1ull << (8 * k)));
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        UINT32 Addr = ConstAddr (pAddr);
        std::string Expr;
        for (UINT32 k = 0; k < Bits / 8; k++) {
            char Term[48];
            if (k == 0) {
                std::snprintf (Term, sizeof (Term), "d%u", Addr + k);
            } else {
                std::snprintf (Term, sizeof (Term), " + (d%u * %llu)", Addr + k, (unsigned long long) (1ull << (8 * k)));
            }
            Expr += Term;
        }
        Line ("set /a \"t%u=%s\"", Dest, Expr.c_str ());
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        UINT32 Addr = ConstAddr (pAddr);
        for (UINT32 k = 0; k < Bits / 8; k++) {
            if (k == 0) {
                Line ("set /a \"d%u=t%u & 255\"", Addr + k, IdOf (pValue));
            } else {
                Line ("set /a \"d%u=(t%u / %llu) & 255\"", Addr + k, IdOf (pValue), (unsigned long long) (1ull << (8 * k)));
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
        Line ("set /a \"t%u=(t%u %s t%u) & %llu\"", Dest, IdOf (pA), pOp, IdOf (pB),
              (unsigned long long) Mask (~0ull, Bits));
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        unsigned long long FullMask = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case UnNeg:
            Line ("set /a \"t%u=(-t%u) & %llu\"", Dest, IdOf (pA), FullMask);
            return Make (Dest, Bits, ppValue);
        case UnCom:
            Line ("set /a \"t%u=(~t%u) & %llu\"", Dest, IdOf (pA), FullMask);
            return Make (Dest, Bits, ppValue);
        case UnNot:
            Line ("set t%u=0", Dest);
            Line ("if !t%u! EQU 0 set t%u=1", IdOf (pA), Dest);
            return Make (Dest, 1, ppValue);
        }
        return E_INVALIDARG;
    }

    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        CONST CHAR8 *pOp;
        switch (Pred) {
        case CmpEq:  pOp = "EQU"; break;
        case CmpNe:  pOp = "NEQ"; break;
        case CmpULt: case CmpSLt: pOp = "LSS"; break;
        case CmpULe: case CmpSLe: pOp = "LEQ"; break;
        case CmpUGt: case CmpSGt: pOp = "GTR"; break;
        case CmpUGe: case CmpSGe: pOp = "GEQ"; break;
        default:     pOp = "EQU"; break;
        }
        UINT32 Dest = Fresh ();
        // set /a has no comparison operators; use IF over delayed-expansion values.
        Line ("set t%u=0", Dest);
        Line ("if !t%u! %s !t%u! set t%u=1", IdOf (pA), pOp, IdOf (pB), Dest);
        return Make (Dest, 1, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        unsigned long long FullMask = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case CastTrunc:
            Line ("set /a \"t%u=t%u & %llu\"", Dest, IdOf (pA), FullMask);
            break;
        case CastZExt:
            Line ("set /a \"t%u=t%u\"", Dest, IdOf (pA));
            break;
        case CastSExt: {
            UINT32 Sign = Fresh ();
            Line ("set /a \"t%u=t%u\"", Dest, IdOf (pA));
            Line ("set /a \"t%u=t%u & %llu\"", Sign, Dest, (unsigned long long) (1ull << (SrcBits - 1)));
            Line ("if !t%u! NEQ 0 set /a \"t%u=t%u - %llu\"", Sign, Dest, Dest, (unsigned long long) (1ull << SrcBits));
            Line ("set /a \"t%u=t%u & %llu\"", Dest, Dest, FullMask);
            break;
        }
        }
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override {
        *ppValue = nullptr;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("set /a \"t%u=d%u & 1\"", Dest, FlagPos ((UINT32) Flag));
        return Make (Dest, 1, ppValue);
    }

    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        Line ("set /a \"d%u=t%u & 1\"", FlagPos ((UINT32) Flag), IdOf (pValue));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        for (UINT32 k = 0; k < 8; k++) {
            Line ("set /a \"d%u=%u\"", PcPos (k), (UINT32) ((Pc >> (8 * k)) & 0xff));
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override {
        *ppBlock = new CmdBlock ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        char DirTemplate[] = "/tmp/libcpu_cmd_XXXXXX";
        if (mkdtemp (DirTemplate) == nullptr) {
            return nullptr;
        }
        std::string Dir = DirTemplate;
        std::string Source = Dir + "/insn.cmd";

        std::string Full = kTemplate;
        Subst (Full, "%B%", m_Body);
        Subst (Full, "%T%", std::to_string (TOTAL - 1));

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
        return new CmdCode (Dir);
    }

private:
    UINT32 Fresh () { return m_Next++; }

    // The RAM byte index for a (constant) address value; 0 if the address was not
    // a tracked constant (cmd cannot index by a runtime value).
    UINT32 ConstAddr (ICpuValue *pAddr) {
        auto It = m_Const.find (IdOf (pAddr));
        return (It != m_Const.end ()) ? (UINT32) It->second : 0;
    }

    void Line (CONST CHAR8 *pFmt, ...) {
        char Buf[256];
        va_list Args;
        va_start (Args, pFmt);
        std::vsnprintf (Buf, sizeof (Buf), pFmt, Args);
        va_end (Args);
        m_Body += Buf;
        m_Body += "\r\n";
    }

    static VOID Subst (std::string &Str, CHAR8 CONST *pKey, std::string CONST &Val) {
        size_t Pos;
        while ((Pos = Str.find (pKey)) != std::string::npos) {
            Str.replace (Pos, std::strlen (pKey), Val);
        }
    }

    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) {
        *ppValue = new CmdValue (Id, Bits);
        return S_OK;
    }

    std::string                          m_Body;
    UINT32                               m_Next = 0;
    std::unordered_map<UINT32, UINT64>   m_Const;
};

class CmdBackend final : public LcComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "cmd"; }

    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new CmdEmitter ();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<CmdEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateCmdBackend (VOID)
{
    return new CmdBackend ();
}

} // namespace LibCPU
