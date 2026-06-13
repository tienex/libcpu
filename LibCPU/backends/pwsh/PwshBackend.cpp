/** @file
  PowerShell textual-emit backend. See PwshBackend.h.

  Each ICpuValue is a PowerShell variable ($tN) masked to its width. PowerShell
  spells bitwise operators as -band/-bor/-bxor/-shl/-shr/-bnot and comparisons as
  -eq/-ne/-lt/-le/-gt/-ge (a comparison yields a boolean, cast to 1/0 with [int]).
  The state file is read and written as a byte array via [System.IO.File].
**/
#include "PwshBackend.h"
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
// Fixed harness: read the state file into the byte array $d, define
// register/memory/flag accessors, run the emitted body (%B%), then write $d back.
//
static CHAR8 CONST *kTemplate =
    "$d=[System.IO.File]::ReadAllBytes($args[0])\n"
    "function gR($i,$b){ $v=0; for($k=0;$k -lt $b/8;$k++){ $v=$v -bor ([int]$d[65536+$i*8+$k] -shl (8*$k)) }; return $v }\n"
    "function pR($i,$v,$b){ for($k=0;$k -lt 8;$k++){ if($k -lt $b/8){ $d[65536+$i*8+$k]=[byte]((($v -shr (8*$k)) -band 0xff)) } else { $d[65536+$i*8+$k]=0 } } }\n"
    "function rM($a,$b){ $v=0; for($k=0;$k -lt $b/8;$k++){ $v=$v -bor ([int]$d[$a+$k] -shl (8*$k)) }; return $v }\n"
    "function wM($a,$v,$b){ for($k=0;$k -lt $b/8;$k++){ $d[$a+$k]=[byte]((($v -shr (8*$k)) -band 0xff)) } }\n"
    "function gF($f){ return ([int]$d[65536+256+$f] -band 1) }\n"
    "function sF($f,$v){ $d[65536+256+$f]=[byte]($v -band 1) }\n"
    "function sP($p){ for($k=0;$k -lt 8;$k++){ $d[65536+264+$k]=[byte]((($p -shr (8*$k)) -band 0xff)) } }\n"
    "function sx($v,$s){ if(($v -band (1 -shl ($s-1))) -ne 0){ return ($v - (1 -shl $s)) } else { return $v } }\n"
    "%B%"
    "[System.IO.File]::WriteAllBytes($args[0],$d)\n";

static UINT64
Mask (UINT64 Value, UINT32 Bits)
{
    return (Bits >= 64) ? Value : (Value & (((UINT64) 1 << Bits) - 1));
}

class PwshValue final : public ComObject<ICpuValue> {
public:
    PwshValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id;
    UINT32 m_Bits;
};

class PwshBlock final : public ComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
};

static UINT32 IdOf   (ICpuValue *pValue) { return static_cast<PwshValue *> (pValue)->m_Id; }
static UINT32 BitsOf (ICpuValue *pValue) { return static_cast<PwshValue *> (pValue)->m_Bits; }

class PwshCode final : public ComObject<ICpuCode> {
public:
    PwshCode (std::string Dir) : m_Dir (std::move (Dir)) {}

    ~PwshCode () override {
        if (!m_Dir.empty ()) {
            unlink ((m_Dir + "/insn.ps1").c_str ());
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

        std::string Cmd = "pwsh -NoProfile -File '" + m_Dir + "/insn.ps1' '" + State + "' 2>/dev/null";
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

class PwshEmitter final : public ComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("$t%u=%llu", Dest, (unsigned long long) Mask (Value, Bits));
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("$t%u=(gR %u %u)", Dest, Index, Bits);
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN /*Sext*/) override {
        Line ("pR %u $t%u %u", Index, IdOf (pValue), Bits ? Bits : BitsOf (pValue));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("$t%u=(rM $t%u %u)", Dest, IdOf (pAddr), Bits);
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
        case BinAdd:  pOp = "+";     break;
        case BinSub:  pOp = "-";     break;
        case BinMul:  pOp = "*";     break;
        case BinAnd:  pOp = "-band"; break;
        case BinOr:   pOp = "-bor";  break;
        case BinXor:  pOp = "-bxor"; break;
        case BinShl:  pOp = "-shl";  break;
        case BinLShr: pOp = "-shr";  break;
        case BinAShr: pOp = "-shr";  break;
        case BinUDiv: pOp = "/";     break;
        case BinURem: pOp = "%";     break;
        default:      pOp = "+";     break;
        }
        UINT32 Dest = Fresh ();
        Line ("$t%u=(($t%u %s $t%u) -band %llu)", Dest, IdOf (pA), pOp, IdOf (pB),
              (unsigned long long) Mask (~0ull, Bits));
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        unsigned long long FullMask = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case UnNeg:
            Line ("$t%u=((-$t%u) -band %llu)", Dest, IdOf (pA), FullMask);
            return Make (Dest, Bits, ppValue);
        case UnCom:
            Line ("$t%u=((-bnot $t%u) -band %llu)", Dest, IdOf (pA), FullMask);
            return Make (Dest, Bits, ppValue);
        case UnNot:
            Line ("$t%u=[int]($t%u -eq 0)", Dest, IdOf (pA));
            return Make (Dest, 1, ppValue);
        }
        return E_INVALIDARG;
    }

    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        CONST CHAR8 *pOp;
        switch (Pred) {
        case CmpEq:  pOp = "-eq"; break;
        case CmpNe:  pOp = "-ne"; break;
        case CmpULt: case CmpSLt: pOp = "-lt"; break;
        case CmpULe: case CmpSLe: pOp = "-le"; break;
        case CmpUGt: case CmpSGt: pOp = "-gt"; break;
        case CmpUGe: case CmpSGe: pOp = "-ge"; break;
        default:     pOp = "-eq"; break;
        }
        UINT32 Dest = Fresh ();
        Line ("$t%u=[int]($t%u %s $t%u)", Dest, IdOf (pA), pOp, IdOf (pB));
        return Make (Dest, 1, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        unsigned long long FullMask = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case CastTrunc:
            Line ("$t%u=($t%u -band %llu)", Dest, IdOf (pA), FullMask);
            break;
        case CastZExt:
            Line ("$t%u=$t%u", Dest, IdOf (pA));
            break;
        case CastSExt:
            Line ("$t%u=((sx $t%u %u) -band %llu)", Dest, IdOf (pA), SrcBits, FullMask);
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
        Line ("$t%u=(gF %u)", Dest, (UINT32) Flag);
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

    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override {
        *ppBlock = new PwshBlock ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        char DirTemplate[] = "/tmp/libcpu_pwsh_XXXXXX";
        if (mkdtemp (DirTemplate) == nullptr) {
            return nullptr;
        }
        std::string Dir = DirTemplate;
        std::string Source = Dir + "/insn.ps1";

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
        return new PwshCode (Dir);
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
        *ppValue = new PwshValue (Id, Bits);
        return S_OK;
    }

    std::string m_Body;
    UINT32      m_Next = 0;
};

class PwshBackend final : public ComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "pwsh"; }

    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new PwshEmitter ();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<PwshEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreatePwshBackend (VOID)
{
    return new PwshBackend ();
}

} // namespace LibCPU
