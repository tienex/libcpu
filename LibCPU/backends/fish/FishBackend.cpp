/** @file
  fish shell textual-emit backend. See FishBackend.h.

  Each ICpuValue is a fish variable (tN) masked to its width. fish's "math" lacks
  bitwise operators, so the harness defines band/bor/bxor that build the result a
  bit at a time from arithmetic (each bit of an operand is floor(x/p)%2); masks are
  emitted as modulo and shifts as multiply/divide by powers of two. The state byte
  array d is 1-indexed and mutated globally with "set -g".
**/
#include "FishBackend.h"
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
// Fixed harness: read the state file into the 1-indexed array d; define bitwise
// helpers (band/bor/bxor) and register/memory/flag accessors purely from
// arithmetic; run the emitted body (%B%); then write d back.
//
static CHAR8 CONST *kTemplate =
    // fish splits command substitution on newlines, so force one byte per line.
    "set -g d (od -An -v -tu1 $argv[1] | grep -oE '[0-9]+')\n"
    "function band; set a $argv[1]; set b $argv[2]; set r 0; set p 1; for i in (seq 0 15); set ba (math \"floor($a/$p)%2\"); set bb (math \"floor($b/$p)%2\"); set r (math \"$r+$ba*$bb*$p\"); set p (math \"$p*2\"); end; echo $r; end\n"
    "function bor; set a $argv[1]; set b $argv[2]; set r 0; set p 1; for i in (seq 0 15); set ba (math \"floor($a/$p)%2\"); set bb (math \"floor($b/$p)%2\"); set r (math \"$r+($ba+$bb-$ba*$bb)*$p\"); set p (math \"$p*2\"); end; echo $r; end\n"
    "function bxor; set a $argv[1]; set b $argv[2]; set r 0; set p 1; for i in (seq 0 15); set ba (math \"floor($a/$p)%2\"); set bb (math \"floor($b/$p)%2\"); set r (math \"$r+(($ba+$bb)%2)*$p\"); set p (math \"$p*2\"); end; echo $r; end\n"
    "function gR; set i $argv[1]; set b $argv[2]; set v 0; for k in (seq 0 (math \"$b/8-1\")); set pos (math \"65536+$i*8+$k+1\"); set v (math \"$v+$d[$pos]*pow(2,8*$k)\"); end; echo $v; end\n"
    "function pR; set i $argv[1]; set v $argv[2]; set b $argv[3]; for k in (seq 0 (math \"$b/8-1\")); set pos (math \"65536+$i*8+$k+1\"); set -g d[$pos] (math \"floor($v/pow(2,8*$k))%256\"); end; end\n"
    "function rM; set a $argv[1]; set b $argv[2]; set v 0; for k in (seq 0 (math \"$b/8-1\")); set pos (math \"$a+$k+1\"); set v (math \"$v+$d[$pos]*pow(2,8*$k)\"); end; echo $v; end\n"
    "function wM; set a $argv[1]; set v $argv[2]; set b $argv[3]; for k in (seq 0 (math \"$b/8-1\")); set pos (math \"$a+$k+1\"); set -g d[$pos] (math \"floor($v/pow(2,8*$k))%256\"); end; end\n"
    "function gF; set p (math \"65536+256+$argv[1]+1\"); echo (math \"$d[$p]%2\"); end\n"
    "function sF; set p (math \"65536+256+$argv[1]+1\"); set -g d[$p] (math \"$argv[2]%2\"); end\n"
    "function sP; set p $argv[1]; for k in (seq 0 7); set pp (math \"65536+264+$k+1\"); set -g d[$pp] (math \"floor($p/pow(2,8*$k))%256\"); end; end\n"
    "function sx; set v $argv[1]; set s $argv[2]; if test (math \"floor($v/pow(2,$s-1))%2\") -eq 1; echo (math \"$v-pow(2,$s)\"); else; echo $v; end; end\n"
    "%B%"
    // fish printf escapes a backslash placed before %, so build the octals first
    // then prepend a backslash to each with fish's "\\$array" expansion.
    "set oct (printf '%03o\\n' $d)\n"
    "set e (string join '' \\\\$oct)\n"
    "printf \"$e\" > $argv[1]\n";

static UINT64
Mask (UINT64 Value, UINT32 Bits)
{
    return (Bits >= 64) ? Value : (Value & (((UINT64) 1 << Bits) - 1));
}

class FishValue final : public ComObject<ICpuValue> {
public:
    FishValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id;
    UINT32 m_Bits;
};

class FishBlock final : public ComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
};

static UINT32 IdOf   (ICpuValue *pValue) { return static_cast<FishValue *> (pValue)->m_Id; }
static UINT32 BitsOf (ICpuValue *pValue) { return static_cast<FishValue *> (pValue)->m_Bits; }

class FishCode final : public ComObject<ICpuCode> {
public:
    FishCode (std::string Dir) : m_Dir (std::move (Dir)) {}

    ~FishCode () override {
        if (!m_Dir.empty ()) {
            unlink ((m_Dir + "/insn.fish").c_str ());
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

        std::string Cmd = "fish '" + m_Dir + "/insn.fish' '" + State + "' 2>/dev/null";
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

class FishEmitter final : public ComObject<ICpuEmitter> {
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
        Line ("set t%u (gR %u %u)", Dest, Index, Bits);
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN /*Sext*/) override {
        Line ("pR %u $t%u %u", Index, IdOf (pValue), Bits ? Bits : BitsOf (pValue));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("set t%u (rM $t%u %u)", Dest, IdOf (pAddr), Bits);
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Line ("wM $t%u $t%u %u", IdOf (pAddr), IdOf (pValue), Bits);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        UINT32 A = IdOf (pA);
        UINT32 B = IdOf (pB);
        unsigned long long Mod = (unsigned long long) Mask (~0ull, Bits) + 1;   // modulo for the width
        switch (Op) {
        case BinAdd:  Line ("set t%u (math \"($t%u + $t%u) %% %llu\")", Dest, A, B, Mod); break;
        case BinSub:  Line ("set t%u (math \"($t%u - $t%u + %llu) %% %llu\")", Dest, A, B, Mod, Mod); break;
        case BinMul:  Line ("set t%u (math \"($t%u * $t%u) %% %llu\")", Dest, A, B, Mod); break;
        case BinAnd:  Line ("set t%u (band $t%u $t%u)", Dest, A, B); break;
        case BinOr:   Line ("set t%u (bor $t%u $t%u)", Dest, A, B); break;
        case BinXor:  Line ("set t%u (bxor $t%u $t%u)", Dest, A, B); break;
        case BinShl:  Line ("set t%u (math \"($t%u * pow(2,$t%u)) %% %llu\")", Dest, A, B, Mod); break;
        case BinLShr: Line ("set t%u (math \"floor($t%u / pow(2,$t%u))\")", Dest, A, B); break;
        case BinAShr: Line ("set t%u (math \"floor($t%u / pow(2,$t%u))\")", Dest, A, B); break;
        case BinUDiv: Line ("set t%u (math \"floor($t%u / $t%u)\")", Dest, A, B); break;
        case BinURem: Line ("set t%u (math \"$t%u %% $t%u\")", Dest, A, B); break;
        default:      Line ("set t%u (math \"($t%u + $t%u) %% %llu\")", Dest, A, B, Mod); break;
        }
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        unsigned long long FullMask = (unsigned long long) Mask (~0ull, Bits);
        unsigned long long Mod = FullMask + 1;
        switch (Op) {
        case UnNeg:
            Line ("set t%u (math \"(%llu - $t%u) %% %llu\")", Dest, Mod, IdOf (pA), Mod);
            return Make (Dest, Bits, ppValue);
        case UnCom:
            Line ("set t%u (math \"%llu - $t%u\")", Dest, FullMask, IdOf (pA));
            return Make (Dest, Bits, ppValue);
        case UnNot:
            Line ("set t%u (test $t%u -eq 0; and echo 1; or echo 0)", Dest, IdOf (pA));
            return Make (Dest, 1, ppValue);
        // floating-point ops: unsupported by this backend
        default: return E_INVALIDARG;
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
        Line ("set t%u (test $t%u %s $t%u; and echo 1; or echo 0)", Dest, IdOf (pA), pOp, IdOf (pB));
        return Make (Dest, 1, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        unsigned long long Mod = (unsigned long long) Mask (~0ull, Bits) + 1;
        switch (Op) {
        case CastTrunc:
            Line ("set t%u (math \"$t%u %% %llu\")", Dest, IdOf (pA), Mod);
            break;
        case CastZExt:
            Line ("set t%u $t%u", Dest, IdOf (pA));
            break;
        case CastSExt:
            Line ("set t%u (sx $t%u %u)", Dest, IdOf (pA), SrcBits);
            Line ("set t%u (math \"$t%u %% %llu\")", Dest, Dest, Mod);
            break;
        // floating-point ops: unsupported by this backend
        default: return E_INVALIDARG;
        }
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override {
        *ppValue = nullptr;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("set t%u (gF %u)", Dest, (UINT32) Flag);
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
        *ppBlock = new FishBlock ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        char DirTemplate[] = "/tmp/libcpu_fish_XXXXXX";
        if (mkdtemp (DirTemplate) == nullptr) {
            return nullptr;
        }
        std::string Dir = DirTemplate;
        std::string Source = Dir + "/insn.fish";

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
        return new FishCode (Dir);
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
        *ppValue = new FishValue (Id, Bits);
        return S_OK;
    }

    std::string m_Body;
    UINT32      m_Next = 0;
};

class FishBackend final : public ComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "fish"; }

    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new FishEmitter ();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<FishEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateFishBackend (VOID)
{
    return new FishBackend ();
}

} // namespace LibCPU
