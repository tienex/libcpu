/** @file  Perl textual-emit backend. See PerlBackend.h. Values are Perl scalars
  ($tN) masked to width; @d is the marshalled state byte array (RAM then state). **/
#include "PerlBackend.h"
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

static CONST UINT32 RAM_SIZE = 0x10000;

static CHAR8 CONST *kTemplate =
    "open(F,'<',$ARGV[0]);binmode(F);local $/;my $s=<F>;close(F);my @d=unpack('C*',$s);\n"
    "sub gR{my($i,$b)=@_;my $v=0;for my $k (0..$b/8-1){$v|=$d[65536+$i*8+$k]<<(8*$k);}$v}\n"
    "sub pR{my($i,$v,$b)=@_;for my $k (0..7){$d[65536+$i*8+$k]=($k<$b/8)?(($v>>(8*$k))&0xff):0;}}\n"
    "sub rM{my($a,$b)=@_;my $v=0;for my $k (0..$b/8-1){$v|=$d[$a+$k]<<(8*$k);}$v}\n"
    "sub wM{my($a,$v,$b)=@_;for my $k (0..$b/8-1){$d[$a+$k]=($v>>(8*$k))&0xff;}}\n"
    "sub gF{my($f)=@_;$d[65536+256+$f]&1}\n"
    "sub sF{my($f,$v)=@_;$d[65536+256+$f]=$v&1;}\n"
    "sub sP{my($p)=@_;for my $k (0..7){$d[65536+264+$k]=($p>>(8*$k))&0xff;}}\n"
    "sub sx{my($v,$s)=@_;($v&(1<<($s-1)))?($v-(1<<$s)):$v}\n"
    "%B%"
    "open(F,'>',$ARGV[0]);binmode(F);print F pack('C*',@d);close(F);\n";

static UINT64 Mask (UINT64 V, UINT32 Bits) { return (Bits >= 64) ? V : (V & (((UINT64) 1 << Bits) - 1)); }

class PlValue final : public LcComObject<ICpuValue> {
public:
    PlValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuValue, ppvObject); }
    UINT32 m_Id, m_Bits;
};
class PlBlock final : public LcComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuBlock, ppvObject); }
};
static UINT32 IdOf   (ICpuValue *pV) { return static_cast<PlValue *> (pV)->m_Id; }
static UINT32 BitsOf (ICpuValue *pV) { return static_cast<PlValue *> (pV)->m_Bits; }

class PlCode final : public LcComObject<ICpuCode> {
public:
    PlCode (std::string Dir) : m_Dir (std::move (Dir)) {}
    ~PlCode () override {
        if (!m_Dir.empty ()) { unlink ((m_Dir + "/insn.pl").c_str ()); unlink ((m_Dir + "/state.bin").c_str ()); rmdir (m_Dir.c_str ()); }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuCode, ppvObject); }
    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        std::string St = m_Dir + "/state.bin";
        FILE *pF = std::fopen (St.c_str (), "wb"); if (!pF) return ExecTrap;
        std::fwrite (pRAM, 1, RAM_SIZE, pF); std::fwrite (pGRF, 1, sizeof (CPU_STATE), pF); std::fclose (pF);
        std::string Cmd = "perl '" + m_Dir + "/insn.pl' '" + St + "' 2>/dev/null";
        if (std::system (Cmd.c_str ()) != 0) return ExecTrap;
        pF = std::fopen (St.c_str (), "rb"); if (!pF) return ExecOk;
        if (std::fread (pRAM, 1, RAM_SIZE, pF) != RAM_SIZE) { std::fclose (pF); return ExecOk; }
        (void) !std::fread (pGRF, 1, sizeof (CPU_STATE), pF); std::fclose (pF);
        return ExecOk;
    }
private:
    std::string m_Dir;
};

class PlEmitter final : public LcComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuEmitter, ppvObject); }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("my $t%u=%llu;", D, (unsigned long long) Mask (Value, Bits)); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("my $t%u=gR(%u,%u);", D, Index, Bits); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN) override {
        Line ("pR(%u,$t%u,%u);", Index, IdOf (pValue), Bits ? Bits : BitsOf (pValue)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("my $t%u=rM($t%u,%u);", D, IdOf (pAddr), Bits); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Line ("wM($t%u,$t%u,%u);", IdOf (pAddr), IdOf (pValue), Bits); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA), D = Fresh (), A = IdOf (pA), B = IdOf (pB);
        unsigned long long M = (unsigned long long) Mask (~0ull, Bits);
        CONST CHAR8 *o = nullptr;
        switch (Op) {
        case BinAdd:o="+";break; case BinSub:o="-";break; case BinMul:o="*";break;
        case BinAnd:o="&";break; case BinOr:o="|";break; case BinXor:o="^";break;
        case BinShl:o="<<";break; case BinLShr:o=">>";break; case BinAShr:o=">>";break;
        case BinURem:o="%";break;
        case BinUDiv: Line ("my $t%u=int($t%u/$t%u)&%llu;", D, A, B, M); return Make (D, Bits, ppValue);
        default:o="+";break;
        }
        Line ("my $t%u=($t%u%s$t%u)&%llu;", D, A, o, B, M);
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA), D = Fresh (); unsigned long long M = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case UnNeg: Line ("my $t%u=(-$t%u)&%llu;", D, IdOf (pA), M); return Make (D, Bits, ppValue);
        case UnCom: Line ("my $t%u=(~$t%u)&%llu;", D, IdOf (pA), M); return Make (D, Bits, ppValue);
        case UnNot: Line ("my $t%u=($t%u==0)?1:0;", D, IdOf (pA)); return Make (D, 1, ppValue);
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        CONST CHAR8 *o;
        switch (Pred) {
        case CmpEq:o="==";break; case CmpNe:o="!=";break;
        case CmpULt:case CmpSLt:o="<";break; case CmpULe:case CmpSLe:o="<=";break;
        case CmpUGt:case CmpSGt:o=">";break; case CmpUGe:case CmpSGe:o=">=";break; default:o="==";break;
        }
        UINT32 D = Fresh (); Line ("my $t%u=($t%u%s$t%u)?1:0;", D, IdOf (pA), o, IdOf (pB)); return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Src = BitsOf (pA), D = Fresh (); unsigned long long M = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case CastTrunc: Line ("my $t%u=$t%u&%llu;", D, IdOf (pA), M); break;
        case CastZExt:  Line ("my $t%u=$t%u;", D, IdOf (pA)); break;
        case CastSExt:  Line ("my $t%u=sx($t%u,%u)&%llu;", D, IdOf (pA), Src, M); break;
        }
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override { *ppValue = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("my $t%u=gF(%u);", D, (UINT32) Flag); return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override { Line ("sF(%u,$t%u);", (UINT32) Flag, IdOf (pValue)); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override { Line ("sP(%llu);", (unsigned long long) Pc); return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = new PlBlock (); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        char DirTmpl[] = "/tmp/libcpu_pl_XXXXXX";
        if (mkdtemp (DirTmpl) == nullptr) return nullptr;
        std::string Dir = DirTmpl, Src = Dir + "/insn.pl", Full = kTemplate;
        Subst (Full, "%B%", m_Body);
        int Fd = open (Src.c_str (), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
        if (Fd < 0) { rmdir (Dir.c_str ()); return nullptr; }
        FILE *pF = fdopen (Fd, "w"); if (!pF) { close (Fd); unlink (Src.c_str ()); rmdir (Dir.c_str ()); return nullptr; }
        std::fwrite (Full.data (), 1, Full.size (), pF); std::fclose (pF);
        return new PlCode (Dir);
    }
private:
    UINT32 Fresh () { return m_Next++; }
    void Line (CONST CHAR8 *pFmt, ...) {
        char Buf[256]; va_list A; va_start (A, pFmt); std::vsnprintf (Buf, sizeof (Buf), pFmt, A); va_end (A);
        m_Body += Buf; m_Body += "\n";
    }
    static VOID Subst (std::string &S, CHAR8 CONST *K, std::string CONST &V) { size_t P; while ((P = S.find (K)) != std::string::npos) S.replace (P, std::strlen (K), V); }
    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) { *ppValue = new PlValue (Id, Bits); return S_OK; }
    std::string m_Body; UINT32 m_Next = 0;
};

class PerlBackend final : public LcComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuBackend, ppvObject); }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "perl"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override { *ppEmitter = new PlEmitter (); return S_OK; }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<PlEmitter *> (pEmitter)->Build (); *ppCode = pCode; return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *CreatePerlBackend (VOID) { return new PerlBackend (); }

} // namespace LibCPU
