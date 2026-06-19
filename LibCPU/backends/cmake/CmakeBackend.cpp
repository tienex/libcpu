/** @file
  CMake textual-emit backend. See CmakeBackend.h.

  Each ICpuValue is a CMake variable tN (a math-able integer) masked to its width.
  Arithmetic/bitwise uses math(EXPR ...) (CMake >= 3.13); comparisons use if() (CMake math
  has no relational operators). The marshalled state -- guest RAM followed by CPU_STATE -- is
  read with file(READ ... HEX) into one big lowercase hex string H; bytes are read with
  string(SUBSTRING) (O(1)) and written by splicing H (the register writes are few). The script
  emits H back as a hex TEXT file (CMake cannot write binary), which the host converts to bytes.
**/
#include "CmakeBackend.h"
#include "LibCPU/CpuState.h"

#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>

namespace LibCPU {
namespace {

//
// Fixed harness (CMake script). RAMSZ is the guest RAM size (the register file begins at byte
// RAMSZ in the marshalled state); IN is the binary state file, OUT the hex text written back.
// %B% is replaced by the emitted instruction body. Mirrors the bash backend's accessor set:
// gR/pR registers, rM/wM memory, gF/sF flags, sP program counter, sx sign-extend.
//
static CHAR8 CONST *kTemplate = R"CMK(cmake_minimum_required(VERSION 3.13)
file(READ "${IN}" H HEX)

# Splice byte index i of H to (by & 0xff). Edits H in the caller's scope.
function(SB i by)
  math(EXPR hx "${by} & 255" OUTPUT_FORMAT HEXADECIMAL)
  string(SUBSTRING "${hx}" 2 -1 hx)
  string(TOLOWER "${hx}" hx)
  string(LENGTH "${hx}" L)
  if(L EQUAL 1)
    set(hx "0${hx}")
  endif()
  math(EXPR o "${i}*2")
  string(SUBSTRING "${H}" 0 ${o} pre)
  math(EXPR o2 "${o}+2")
  string(SUBSTRING "${H}" ${o2} -1 post)
  set(H "${pre}${hx}${post}" PARENT_SCOPE)
endfunction()

# Read b bits little-endian starting at byte BASE.
function(rbytes out base b)
  set(v 0)
  math(EXPR n "${b}/8")
  if(n GREATER 0)
    math(EXPR last "${n}-1")
    foreach(k RANGE 0 ${last})
      math(EXPR o "(${base}+${k})*2")
      string(SUBSTRING "${H}" ${o} 2 c)
      math(EXPR v "${v} | (0x${c} << (8*${k}))")
    endforeach()
  endif()
  set(${out} ${v} PARENT_SCOPE)
endfunction()

# Write the low b bits of v little-endian starting at byte BASE (clears up to 8 bytes for regs).
function(wbytes base v b total)
  math(EXPR n "${b}/8")
  math(EXPR last "${total}-1")
  foreach(k RANGE 0 ${last})
    if(k LESS ${n})
      math(EXPR by "(${v}>>(8*${k})) & 255")
    else()
      set(by 0)
    endif()
    math(EXPR bi "${base}+${k}")
    SB(${bi} ${by})
  endforeach()
  set(H "${H}" PARENT_SCOPE)
endfunction()

function(gR out i b)
  math(EXPR base "${RAMSZ}+${i}*8")
  rbytes(r ${base} ${b})
  set(${out} ${r} PARENT_SCOPE)
endfunction()
function(pR i v b)
  math(EXPR base "${RAMSZ}+${i}*8")
  wbytes(${base} ${v} ${b} 8)
  set(H "${H}" PARENT_SCOPE)
endfunction()
function(rM out a b)
  rbytes(r ${a} ${b})
  set(${out} ${r} PARENT_SCOPE)
endfunction()
function(wM a v b)
  math(EXPR n "${b}/8")
  wbytes(${a} ${v} ${b} ${n})
  set(H "${H}" PARENT_SCOPE)
endfunction()
function(gF out i)
  math(EXPR o "(${RAMSZ}+256+${i})*2")
  string(SUBSTRING "${H}" ${o} 2 c)
  math(EXPR v "0x${c} & 1")
  set(${out} ${v} PARENT_SCOPE)
endfunction()
function(sF i v)
  math(EXPR bi "${RAMSZ}+256+${i}")
  math(EXPR by "${v} & 1")
  SB(${bi} ${by})
  set(H "${H}" PARENT_SCOPE)
endfunction()
function(sP v)
  math(EXPR base "${RAMSZ}+264")
  wbytes(${base} ${v} 64 8)
  set(H "${H}" PARENT_SCOPE)
endfunction()
function(sx out v s)
  math(EXPR half "1 << (${s}-1)")
  math(EXPR sb "${v} & ${half}")
  if(sb)
    math(EXPR r "${v} - (1 << ${s})")
  else()
    set(r ${v})
  endif()
  set(${out} ${r} PARENT_SCOPE)
endfunction()

%B%
file(WRITE "${OUT}" "${H}")
)CMK";

static UINT64
Mask (UINT64 Value, UINT32 Bits)
{
    return (Bits >= 64) ? Value : (Value & (((UINT64) 1 << Bits) - 1));
}

class CmakeValue final : public ComObject<ICpuValue> {
public:
    CmakeValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id;
    UINT32 m_Bits;
};

class CmakeBlock final : public ComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
};

static UINT32 IdOf   (ICpuValue *pValue) { return static_cast<CmakeValue *> (pValue)->m_Id; }
static UINT32 BitsOf (ICpuValue *pValue) { return static_cast<CmakeValue *> (pValue)->m_Bits; }

class CmakeCode final : public ComObject<ICpuCode> {
public:
    CmakeCode (std::string Dir) : m_Dir (std::move (Dir)) {}
    ~CmakeCode () override {
        if (!m_Dir.empty ()) {
            unlink ((m_Dir + "/insn.cmake").c_str ());
            unlink ((m_Dir + "/state.bin").c_str ());
            unlink ((m_Dir + "/out.hex").c_str ());
            rmdir (m_Dir.c_str ());
        }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }

    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        CPU_STATE *pState = (CPU_STATE *) pGRF;
        UINT64     RamSz  = pState->RamSize;
        std::string In  = m_Dir + "/state.bin";
        std::string Out = m_Dir + "/out.hex";

        // Marshal: RAM bytes, then the CPU_STATE bytes (register file begins at byte RamSz).
        FILE *pFile = std::fopen (In.c_str (), "wb");
        if (pFile == nullptr) { return ExecTrap; }
        std::fwrite (pRAM, 1, (size_t) RamSz, pFile);
        std::fwrite (pGRF, 1, sizeof (CPU_STATE), pFile);
        std::fclose (pFile);

        char Cmd[1024];
        std::snprintf (Cmd, sizeof (Cmd),
                       "cmake -DIN='%s' -DOUT='%s' -DRAMSZ=%llu -P '%s/insn.cmake' 2>/dev/null",
                       In.c_str (), Out.c_str (), (unsigned long long) RamSz, m_Dir.c_str ());
        if (std::system (Cmd) != 0) { return ExecTrap; }

        // The script wrote H back as a lowercase hex string; parse it to bytes and apply.
        pFile = std::fopen (Out.c_str (), "rb");
        if (pFile == nullptr) { return ExecOk; }
        std::string Hex;
        char Chunk[4096];
        size_t Got;
        while ((Got = std::fread (Chunk, 1, sizeof (Chunk), pFile)) > 0) { Hex.append (Chunk, Got); }
        std::fclose (pFile);

        UINT64 Total = RamSz + sizeof (CPU_STATE);
        if (Hex.size () < (size_t) Total * 2) { return ExecOk; }   // short/garbled: leave state as-is
        std::vector<UINT8> Bytes (Total);
        for (UINT64 I = 0; I < Total; I++) {
            unsigned B = 0;
            std::sscanf (Hex.c_str () + I * 2, "%2x", &B);
            Bytes[I] = (UINT8) B;
        }
        std::memcpy (pRAM, Bytes.data (), (size_t) RamSz);
        std::memcpy (pGRF, Bytes.data () + RamSz, sizeof (CPU_STATE));
        return ExecOk;
    }

private:
    std::string m_Dir;
};

class CmakeEmitter final : public ComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("set(t%u %llu)", Dest, (unsigned long long) Mask (Value, Bits));
        return Make (Dest, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("gR(t%u %u %u)", Dest, Index, Bits);
        return Make (Dest, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN /*Sext*/) override {
        Line ("pR(%u ${t%u} %u)", Index, IdOf (pValue), Bits ? Bits : BitsOf (pValue));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("rM(t%u ${t%u} %u)", Dest, IdOf (pAddr), Bits);
        return Make (Dest, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Line ("wM(${t%u} ${t%u} %u)", IdOf (pAddr), IdOf (pValue), Bits);
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
        Line ("math(EXPR t%u \"(${t%u} %s ${t%u}) & %llu\")", Dest, IdOf (pA), pOp, IdOf (pB),
              (unsigned long long) Mask (~0ull, Bits));
        return Make (Dest, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        unsigned long long FullMask = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case UnNeg:
            Line ("math(EXPR t%u \"(0 - ${t%u}) & %llu\")", Dest, IdOf (pA), FullMask);
            return Make (Dest, Bits, ppValue);
        case UnCom:
            Line ("math(EXPR t%u \"(~${t%u}) & %llu\")", Dest, IdOf (pA), FullMask);
            return Make (Dest, Bits, ppValue);
        case UnNot:
            Line ("if(${t%u} EQUAL 0)\n  set(t%u 1)\nelse()\n  set(t%u 0)\nendif()", IdOf (pA), Dest, Dest);
            return Make (Dest, 1, ppValue);
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        // CMake math() has no relational operators, so branch with if(). (Signed comparisons use
        // the same numeric test as the bash backend -- values are width-masked.)
        switch (Pred) {
        case CmpNe:
            Line ("if(NOT (${t%u} EQUAL ${t%u}))\n  set(t%u 1)\nelse()\n  set(t%u 0)\nendif()",
                  IdOf (pA), IdOf (pB), Dest, Dest);
            return Make (Dest, 1, ppValue);
        default: {
            CONST CHAR8 *pOp;
            switch (Pred) {
            case CmpEq:                pOp = "EQUAL";         break;
            case CmpULt: case CmpSLt:  pOp = "LESS";          break;
            case CmpULe: case CmpSLe:  pOp = "LESS_EQUAL";    break;
            case CmpUGt: case CmpSGt:  pOp = "GREATER";       break;
            case CmpUGe: case CmpSGe:  pOp = "GREATER_EQUAL"; break;
            default:                   pOp = "EQUAL";         break;
            }
            Line ("if(${t%u} %s ${t%u})\n  set(t%u 1)\nelse()\n  set(t%u 0)\nendif()",
                  IdOf (pA), pOp, IdOf (pB), Dest, Dest);
            return Make (Dest, 1, ppValue);
        }
        }
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        unsigned long long FullMask = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case CastTrunc:
            Line ("math(EXPR t%u \"${t%u} & %llu\")", Dest, IdOf (pA), FullMask);
            break;
        case CastZExt:
            Line ("set(t%u ${t%u})", Dest, IdOf (pA));
            break;
        case CastSExt:
            Line ("sx(t%u ${t%u} %u)\nmath(EXPR t%u \"${t%u} & %llu\")",
                  Dest, IdOf (pA), SrcBits, Dest, Dest, FullMask);
            break;
        }
        return Make (Dest, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override {
        *ppValue = nullptr;
        return E_NOTIMPL;                            // synthesized by the shadow layer
    }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("gF(t%u %u)", Dest, (UINT32) Flag);
        return Make (Dest, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        Line ("sF(%u ${t%u})", (UINT32) Flag, IdOf (pValue));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        Line ("sP(%llu)", (unsigned long long) Pc);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = new CmakeBlock (); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        char DirTemplate[] = "/tmp/libcpu_cmake_XXXXXX";
        if (mkdtemp (DirTemplate) == nullptr) { return nullptr; }
        std::string Dir = DirTemplate;
        std::string Source = Dir + "/insn.cmake";

        std::string Full = kTemplate;
        Subst (Full, "%B%", m_Body);

        int Fd = open (Source.c_str (), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
        if (Fd < 0) { rmdir (Dir.c_str ()); return nullptr; }
        FILE *pFile = fdopen (Fd, "w");
        if (pFile == nullptr) { close (Fd); unlink (Source.c_str ()); rmdir (Dir.c_str ()); return nullptr; }
        std::fwrite (Full.data (), 1, Full.size (), pFile);
        std::fclose (pFile);
        return new CmakeCode (Dir);
    }

private:
    UINT32 Fresh () { return m_Next++; }
    void Line (CONST CHAR8 *pFmt, ...) {
        char Buf[512];
        va_list Args;
        va_start (Args, pFmt);
        std::vsnprintf (Buf, sizeof (Buf), pFmt, Args);
        va_end (Args);
        m_Body += Buf;
        m_Body += "\n";
    }
    static VOID Subst (std::string &Str, CHAR8 CONST *pKey, std::string CONST &Val) {
        size_t Pos = Str.find (pKey);
        if (Pos != std::string::npos) { Str.replace (Pos, std::strlen (pKey), Val); }
    }
    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) {
        *ppValue = new CmakeValue (Id, Bits);
        return S_OK;
    }
    std::string m_Body;
    UINT32      m_Next = 0;
};

class CmakeBackend final : public ComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "cmake"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new CmakeEmitter ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<CmakeEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateCmakeBackend (VOID)
{
    return new CmakeBackend ();
}

} // namespace LibCPU
