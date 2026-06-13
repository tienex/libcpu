/** @file
  JavaScript backend: emit JS for the instruction's semantics and run it on any
  available JS engine (node, qjs/QuickJS, js/SpiderMonkey, ...). The guest RAM and
  register file are marshalled to a temp file; the engine reads it as typed
  arrays, runs the emitted code (mutating them), and writes it back.

  Note: ICpuCode::Execute does not carry a RAM size, so this backend assumes a
  64 KiB guest RAM (true for the 6502). A future Execute contract should pass the
  RAM length.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/
#include "JsBackend.h"
#include "JsEngines.h"
#include "LibCPU/CpuState.h"

#include <vector>

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

// One JS engine: how to read the state file into RAM/ST typed arrays and write
// it back. %S% is replaced by sizeof(CPU_STATE), and the state file is passed as
// the script's first argument.
// The emitted output is one PORTABLE function `insn(RAM, ST)` that runs on any
// engine. Per engine we only need a tiny I/O harness: read the state file into
// RAM/ST, call insn(RAM,ST), then persist (write the file back, or -- for engines
// that cannot write files, d8/jsc -- print the state as hex on stdout).
typedef struct _JS_ENGINE {
    CHAR8 CONST *Exe;       // executable name (looked up on PATH)
    CHAR8 CONST *Family;    // node-like, deno, quickjs, spidermonkey, v8, javascriptcore
    CHAR8 CONST *RunFmt;    // printf: (exePath, scriptPath, stateFilePath)
    CHAR8 CONST *ReadPre;   // defines RAM and ST from the state file
    CHAR8 CONST *WriteEpi;  // persists RAM and ST after insn() runs
    bool         Stdout;    // true: state returned as hex on stdout (no file write)
} JS_ENGINE;

// node and bun share the CommonJS fs + process.argv interface.
#define JS_NODE_READ \
    "var fs=require('fs');var __p=process.argv[2];var __b=fs.readFileSync(__p);" \
    "var RAM=new Uint8Array(__b.buffer,__b.byteOffset,65536);" \
    "var ST=new Uint8Array(__b.buffer,__b.byteOffset+65536,%S%);"
#define JS_NODE_WRITE "fs.writeFileSync(__p,__b);"
// Shared stdout writer for engines without file write (d8, jsc).
#define JS_HEX_WRITE \
    "var __o='';for(var __i=0;__i<RAM.length;__i++)__o+=(256+RAM[__i]).toString(16).slice(1);" \
    "for(var __i=0;__i<ST.length;__i++)__o+=(256+ST[__i]).toString(16).slice(1);print(__o);"

static CONST JS_ENGINE kEngines[] = {
    { "node", "node-like", "'%s' '%s' '%s'", JS_NODE_READ, JS_NODE_WRITE, false },
    { "bun",  "node-like", "'%s' '%s' '%s'", JS_NODE_READ, JS_NODE_WRITE, false },
    { "deno", "deno", "'%s' run --allow-read --allow-write '%s' '%s'",
      "var __p=Deno.args[0];var __b=Deno.readFileSync(__p);"
      "var RAM=new Uint8Array(__b.buffer,0,65536);var ST=new Uint8Array(__b.buffer,65536,%S%);",
      "Deno.writeFileSync(__p,__b);", false },
    { "qjs", "quickjs", "'%s' '%s' '%s'",
      "import * as std from 'std';"
      "var __p=scriptArgs[1];var __f=std.open(__p,'rb+');__f.seek(0,std.SEEK_END);"
      "var __n=__f.tell();__f.seek(0,std.SEEK_SET);var __ab=new ArrayBuffer(__n);"
      "__f.read(__ab,0,__n);var RAM=new Uint8Array(__ab,0,65536);var ST=new Uint8Array(__ab,65536,%S%);",
      "__f.seek(0,std.SEEK_SET);__f.write(__ab,0,__ab.byteLength);__f.close();", false },
    { "js", "spidermonkey", "'%s' '%s' '%s'",
      "var __p=scriptArgs[0];var __b=os.file.readFile(__p,'binary');"
      "var RAM=new Uint8Array(__b.buffer,0,65536);var ST=new Uint8Array(__b.buffer,65536,%S%);",
      "os.file.writeTypedArrayToFile(__p,__b);", false },
    { "d8", "v8", "'%s' '%s' -- '%s'",
      "var __p=arguments[0];var __ab=readbuffer(__p);"
      "var RAM=new Uint8Array(__ab,0,65536);var ST=new Uint8Array(__ab,65536,%S%);",
      JS_HEX_WRITE, true },
    { "jsc", "javascriptcore", "'%s' '%s' -- '%s'",
      "var __p=arguments[0];var __u=read(__p,'binary');"
      "var RAM=new Uint8Array(__u.buffer,0,65536);var ST=new Uint8Array(__u.buffer,65536,%S%);",
      JS_HEX_WRITE, true },
    // Windows Script Host (built into Windows; no install). Classic JScript is
    // ES3 with no typed arrays, but the emitted insn() only indexes + does integer
    // math, so plain Arrays work. Binary I/O uses ADODB.Stream with the latin1
    // charset (a 1:1 byte<->char map). Discovered only once the host port uses
    // `where` instead of `command -v` (see WhichExe).
    { "cscript", "wsh-jscript", "%s //Nologo //E:JScript \"%s\" \"%s\"",
      "var __p=WScript.Arguments(0);"
      "function __rd(p){var s=new ActiveXObject('ADODB.Stream');s.Type=2;s.CharSet='iso-8859-1';"
      "s.Open();s.LoadFromFile(p);var t=s.ReadText();s.Close();var a=[];"
      "for(var i=0;i<t.length;i++)a[i]=t.charCodeAt(i)&255;return a;}"
      "var __all=__rd(__p);var RAM=__all.slice(0,65536);var ST=__all.slice(65536,65536+%S%);",
      "function __wr(p,a){var s=new ActiveXObject('ADODB.Stream');s.Type=2;s.CharSet='iso-8859-1';"
      "s.Open();var t='';for(var i=0;i<a.length;i++)t+=String.fromCharCode(a[i]&255);"
      "s.WriteText(t);s.SaveToFile(p,2);s.Close();}__wr(__p,RAM.concat(ST));", false },
    { nullptr, nullptr, nullptr, nullptr, nullptr, false }
};

// Shared, engine-independent helper functions + the per-instruction body go
// between ReadPre and WriteEpi.
static CHAR8 CONST *kHelpers =
    "function getReg(i,b){var v=0;for(var k=0;k<b/8;k++)v=(v|(ST[i*8+k]<<(8*k)))>>>0;return v>>>0;}"
    "function putReg(i,v,b){for(var k=0;k<8;k++)ST[i*8+k]=(k<b/8)?((v>>>(8*k))&0xff):0;}"
    "function rdMem(a,b){var v=0;for(var k=0;k<b/8;k++)v=(v|(RAM[a+k]<<(8*k)))>>>0;return v>>>0;}"
    "function wrMem(a,v,b){for(var k=0;k<b/8;k++)RAM[a+k]=(v>>>(8*k))&0xff;}"
    "function getFlag(f){return ST[256+f]&1;}"
    "function setFlag(f,v){ST[256+f]=v&1;}\n";

static std::string
WhichExe (CHAR8 CONST *Name)
{
    std::string Cmd = std::string ("command -v ") + Name + " 2>/dev/null";
    FILE *pP = popen (Cmd.c_str (), "r");
    if (!pP) return "";
    char Buf[512]; std::string Out;
    if (std::fgets (Buf, sizeof (Buf), pP)) Out = Buf;
    pclose (pP);
    while (!Out.empty () && (Out.back () == '\n' || Out.back () == '\r')) Out.pop_back ();
    return Out;
}

static UINT64 Mask (UINT64 V, UINT32 Bits) { return (Bits >= 64) ? V : (V & (((UINT64) 1 << Bits) - 1)); }

class JsValue final : public ComObject<ICpuValue> {
public:
    JsValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id, m_Bits;
};

class JsBlock final : public ComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
};

static UINT32 IdOf   (ICpuValue *pV) { return static_cast<JsValue *> (pV)->m_Id; }
static UINT32 BitsOf (ICpuValue *pV) { return static_cast<JsValue *> (pV)->m_Bits; }

class JsCode final : public ComObject<ICpuCode> {
public:
    JsCode (std::string Dir, std::string Func, CONST JS_ENGINE *pEngine, std::string ExePath)
        : m_Dir (std::move (Dir)), m_Func (std::move (Func)), m_pEngine (pEngine), m_ExePath (std::move (ExePath)) {}
    ~JsCode () override {
        if (!m_Dir.empty ()) {
            unlink ((m_Dir + "/insn.js").c_str ());
            unlink ((m_Dir + "/state.bin").c_str ());
            rmdir (m_Dir.c_str ());
        }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }
    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        std::string Script = m_Dir + "/insn.js", State = m_Dir + "/state.bin";
        // Wrap the PORTABLE insn() with the selected engine's I/O harness.
        std::string Full = SubstS (m_pEngine->ReadPre) + "\n" + m_Func + "insn(RAM,ST);\n" + m_pEngine->WriteEpi + "\n";
        FILE *pS = std::fopen (Script.c_str (), "wb");
        if (!pS) return ExecTrap;
        std::fwrite (Full.data (), 1, Full.size (), pS);
        std::fclose (pS);

        // Marshal state out.
        FILE *pF = std::fopen (State.c_str (), "wb");
        if (!pF) return ExecTrap;
        std::fwrite (pRAM, 1, RAM_SIZE, pF);
        std::fwrite (pGRF, 1, sizeof (CPU_STATE), pF);
        std::fclose (pF);

        char Run[2048];
        std::snprintf (Run, sizeof (Run), m_pEngine->RunFmt, m_ExePath.c_str (), Script.c_str (), State.c_str ());
        std::string Cmd = std::string (Run) + " 2>/dev/null";

        if (m_pEngine->Stdout) {
            // Engine cannot write files: it prints the resulting state as hex.
            FILE *pP = popen (Cmd.c_str (), "r");
            if (!pP) return ExecTrap;
            std::string Hex; char Buf[8192]; size_t N;
            while ((N = std::fread (Buf, 1, sizeof (Buf), pP)) > 0) Hex.append (Buf, N);
            pclose (pP);
            ParseHex (Hex, pRAM, pGRF);
        } else {
            if (std::system (Cmd.c_str ()) != 0) return ExecTrap;
            pF = std::fopen (State.c_str (), "rb");
            if (!pF) return ExecOk;
            if (std::fread (pRAM, 1, RAM_SIZE, pF) != RAM_SIZE) { std::fclose (pF); return ExecOk; }
            (void) !std::fread (pGRF, 1, sizeof (CPU_STATE), pF);
            std::fclose (pF);
        }
        return ExecOk;
    }
private:
    static int HexVal (char C) {
        if (C >= '0' && C <= '9') return C - '0';
        if (C >= 'a' && C <= 'f') return C - 'a' + 10;
        if (C >= 'A' && C <= 'F') return C - 'A' + 10;
        return -1;
    }
    static VOID ParseHex (std::string CONST &Hex, VOID *pRAM, VOID *pGRF) {
        UINT8 *pR = (UINT8 *) pRAM, *pG = (UINT8 *) pGRF;
        UINTN Byte = 0; int Hi = -1;
        for (char C : Hex) {
            int V = HexVal (C);
            if (V < 0) continue;
            if (Hi < 0) { Hi = V; continue; }
            UINT8 B = (UINT8) ((Hi << 4) | V); Hi = -1;
            if (Byte < RAM_SIZE) pR[Byte] = B;
            else if (Byte < RAM_SIZE + sizeof (CPU_STATE)) pG[Byte - RAM_SIZE] = B;
            Byte++;
        }
    }
    std::string SubstS (CHAR8 CONST *pTemplate) {
        std::string S = pTemplate, Key = "%S%", Val = std::to_string (sizeof (CPU_STATE));
        size_t Pos;
        while ((Pos = S.find (Key)) != std::string::npos) S.replace (Pos, Key.size (), Val);
        return S;
    }
    std::string      m_Dir, m_Func;
    CONST JS_ENGINE *m_pEngine;
    std::string      m_ExePath;
};

class JsEmitter final : public ComObject<ICpuEmitter> {
public:
    explicit JsEmitter (CONST JS_ENGINE *pEngine, std::string ExePath)
        : m_pEngine (pEngine), m_ExePath (std::move (ExePath)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

    // ---- values -----------------------------------------------------------
    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("var t%u=%llu;", D, (unsigned long long) Mask (Value, Bits)); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("var t%u=getReg(%u,%u);", D, Index, Bits); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN /*Sext*/) override {
        Line ("putReg(%u,t%u,%u);", Index, IdOf (pValue), Bits ? Bits : BitsOf (pValue)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("var t%u=rdMem(t%u,%u);", D, IdOf (pAddr), Bits); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Line ("wrMem(t%u,t%u,%u);", IdOf (pAddr), IdOf (pValue), Bits); return S_OK;
    }

    // ---- arithmetic / compare / cast --------------------------------------
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
        Line ("var t%u=((t%u%st%u)&%llu)>>>0;", D, IdOf (pA), pOp, IdOf (pB), (unsigned long long) Mask (~0ull, Bits));
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA), D = Fresh ();
        switch (Op) {
        case UnNeg: Line ("var t%u=((-t%u)&%llu)>>>0;", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); return Make (D, Bits, ppValue);
        case UnCom: Line ("var t%u=((~t%u)&%llu)>>>0;", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); return Make (D, Bits, ppValue);
        case UnNot: Line ("var t%u=(t%u==0)?1:0;", D, IdOf (pA)); return Make (D, 1, ppValue);
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        CONST CHAR8 *pOp; bool Signed = false;
        switch (Pred) {
        case CmpEq: pOp = "=="; break; case CmpNe: pOp = "!="; break;
        case CmpULt: pOp = "<"; break; case CmpULe: pOp = "<="; break;
        case CmpUGt: pOp = ">"; break; case CmpUGe: pOp = ">="; break;
        case CmpSLt: pOp = "<"; Signed = true; break; case CmpSLe: pOp = "<="; Signed = true; break;
        case CmpSGt: pOp = ">"; Signed = true; break; case CmpSGe: pOp = ">="; Signed = true; break;
        default: pOp = "=="; break;
        }
        UINT32 D = Fresh ();
        if (Signed) {
            Line ("var t%u=((t%u|0)%s(t%u|0))?1:0;", D, IdOf (pA), pOp, IdOf (pB));
        } else {
            Line ("var t%u=(t%u%st%u)?1:0;", D, IdOf (pA), pOp, IdOf (pB));
        }
        return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA), D = Fresh ();
        switch (Op) {
        case CastTrunc: Line ("var t%u=t%u&%llu;", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); break;
        case CastZExt:  Line ("var t%u=t%u;", D, IdOf (pA)); break;
        case CastSExt:  Line ("var t%u=(((t%u<<%u)>>%u)&%llu)>>>0;", D, IdOf (pA), 32 - SrcBits, 32 - SrcBits, (unsigned long long) Mask (~0ull, Bits)); break;
        }
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override {
        *ppValue = nullptr; return E_NOTIMPL;
    }

    // ---- flags ------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("var t%u=getFlag(%u);", D, (UINT32) Flag); return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        Line ("setFlag(%u,t%u);", (UINT32) Flag, IdOf (pValue)); return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        Line ("for(var __k=0;__k<8;__k++)ST[264+__k]=(Math.floor(%llu/Math.pow(2,8*__k)))&0xff;", (unsigned long long) Pc); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = new JsBlock (); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        char DirTmpl[] = "/tmp/libcpu_js_XXXXXX";
        if (mkdtemp (DirTmpl) == nullptr) return nullptr;
        // The output is one PORTABLE function: helpers + body, closed over RAM/ST.
        // Any engine can run it; JsCode wraps it with the selected engine's harness.
        std::string Func = "function insn(RAM,ST){\n" + std::string (kHelpers) + m_Body + "}\n";
        return new JsCode (std::string (DirTmpl), Func, m_pEngine, m_ExePath);
    }

private:
    UINT32 Fresh () { return m_Next++; }
    void Line (CONST CHAR8 *pFmt, ...) {
        char Buf[256];
        va_list Args; va_start (Args, pFmt);
        std::vsnprintf (Buf, sizeof (Buf), pFmt, Args);
        va_end (Args);
        m_Body += Buf; m_Body += "\n";
    }
    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) { *ppValue = new JsValue (Id, Bits); return S_OK; }

    CONST JS_ENGINE *m_pEngine;
    std::string      m_ExePath;
    std::string      m_Body;
    UINT32           m_Next = 0;
};

// One discovered engine: the static template + its resolved path.
struct JsRec {
    CONST JS_ENGINE *pEngine;
    std::string      Path;
};

//
// The js backend exposes both ICpuBackend and the ICpuJsEngines query/select
// interface, so IUnknown is implemented manually.
//
class JsBackend final : public ICpuBackend, public ICpuJsEngines {
public:
    JsBackend () : m_Ref (1) {
        for (UINTN I = 0; kEngines[I].Exe != nullptr; I++) {
            std::string Path = WhichExe (kEngines[I].Exe);
            if (Path.empty () && std::strcmp (kEngines[I].Exe, "jsc") == 0) {
                // jsc ships inside the JavaScriptCore framework, not on PATH.
                CHAR8 CONST *Cand[] = {
                    "/System/Library/Frameworks/JavaScriptCore.framework/Versions/A/Helpers/jsc",
                    "/System/Library/Frameworks/JavaScriptCore.framework/Helpers/jsc", nullptr };
                for (UINTN J = 0; Cand[J] != nullptr; J++) {
                    if (access (Cand[J], X_OK) == 0) { Path = Cand[J]; break; }
                }
            }
            if (!Path.empty ()) m_Avail.push_back ({ &kEngines[I], Path });
        }
    }
    virtual ~JsBackend () = default;

    // ---- IUnknown (shared by both interface vtables) ----
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        if (ppvObject == nullptr) return E_POINTER;
        if (CompareGuid (&riid, &IID_IUnknown) || CompareGuid (&riid, &IID_ICpuBackend)) {
            *ppvObject = static_cast<ICpuBackend *> (this);
        } else if (CompareGuid (&riid, &IID_ICpuJsEngines)) {
            *ppvObject = static_cast<ICpuJsEngines *> (this);
        } else {
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }
        AddRef ();
        return S_OK;
    }
    UINT32 STDMETHODCALLTYPE AddRef () override { return (UINT32) ++m_Ref; }
    UINT32 STDMETHODCALLTYPE Release () override {
        UINT32 C = (UINT32) --m_Ref;
        if (C == 0) delete this;
        return C;
    }

    // ---- ICpuBackend ----
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "js"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        if (m_Selected >= m_Avail.size ()) { *ppEmitter = nullptr; return E_FAIL; }
        *ppEmitter = new JsEmitter (m_Avail[m_Selected].pEngine, m_Avail[m_Selected].Path);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<JsEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }

    // ---- ICpuJsEngines ----
    UINT32 STDMETHODCALLTYPE GetEngineCount () override { return (UINT32) m_Avail.size (); }
    HRESULT STDMETHODCALLTYPE GetEngineInfo (UINT32 Index, JS_ENGINE_INFO *pInfo) override {
        if (Index >= m_Avail.size () || pInfo == nullptr) return E_INVALIDARG;
        pInfo->Name   = m_Avail[Index].pEngine->Exe;       // non-owning; backend lifetime
        pInfo->Path   = m_Avail[Index].Path.c_str ();
        pInfo->Family = m_Avail[Index].pEngine->Family;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SelectEngine (UINT32 Index) override {
        if (Index >= m_Avail.size ()) return E_INVALIDARG;
        m_Selected = Index;
        return S_OK;
    }
    UINT32 STDMETHODCALLTYPE GetSelectedEngine () override { return m_Selected; }

private:
    std::atomic<INT32>  m_Ref;
    std::vector<JsRec>  m_Avail;
    UINT32              m_Selected = 0;
};

} // anonymous namespace

ICpuBackend *
CreateJsBackend (VOID)
{
    return new JsBackend ();
}

} // namespace LibCPU
