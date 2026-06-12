/** @file  Native AOT: emit + compile a standalone host executable. See NativeAot.h. */

#include "NativeAot.h"
#include "LibCPU/PCom.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <map>
#include <set>
#include <vector>
#include <unistd.h>

namespace LibCPU {
namespace {

static CHAR8 CONST *
CType (UINT32 Bits)
{
    if (Bits <= 8)  { return "uint8_t"; }
    if (Bits <= 16) { return "uint16_t"; }
    if (Bits <= 32) { return "uint32_t"; }
    return "uint64_t";
}

static UINT64
Mask (UINT32 Bits)
{
    return (Bits >= 64) ? ~UINT64_C (0) : ((UINT64_C (1) << Bits) - 1);
}

// A translated value: a C temp t<Id> of a given width.
class NativeValue final : public LcComObject<ICpuValue> {
public:
    NativeValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id;
    UINT32 m_Bits;
};

//
// The native emitter: like the cc backend's, but registers/flags are C LOCALS
// (r<i>/f<i>) instead of CPU_STATE offsets, and there is no SMC write-barrier. It
// QIs ICpuSmcEmitter so a frontend's RET can record its dispatch target (-> "disp").
// The generator owns block structure; the emitter only produces per-instruction C.
//
class NativeEmitter final : public LcComObject<ICpuEmitter>, public ICpuSmcEmitter {
public:
    std::string *m_pCur;        // current instruction's statement buffer
    UINT32       m_Next = 0;    // next temp id

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        if (ppvObject != nullptr && LcIsEqualGUID (&riid, &IID_ICpuSmcEmitter)) {
            *ppvObject = static_cast<ICpuSmcEmitter *> (this);
            AddRef ();
            return S_OK;
        }
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }
    UINT32 STDMETHODCALLTYPE AddRef () override { return LcComObject<ICpuEmitter>::AddRef (); }
    UINT32 STDMETHODCALLTYPE Release () override { return LcComObject<ICpuEmitter>::Release (); }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 D = Decl ();
        Line ("t%u = 0x%llxULL;", D, (unsigned long long) (Value & Mask (Bits)));
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Decl ();
        Line ("t%u = r%u & 0x%llxULL;", D, Index, (unsigned long long) Mask (Bits));
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 /*RegBits*/, BOOLEAN /*Sext*/) override {
        Line ("r%u = t%u;", Index, Id (pValue));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Decl ();
        Line ("t%u = *(%s*)(RAM + t%u);", D, CType (Bits), Id (pAddr));
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Line ("*(%s*)(RAM + t%u) = (%s)t%u;", CType (Bits), Id (pAddr), CType (Bits), Id (pValue));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = Bitsof (pA);
        CHAR8 CONST *pOp;
        switch (Op) {
        case BinAdd: pOp = "+";  break;
        case BinSub: pOp = "-";  break;
        case BinMul: pOp = "*";  break;
        case BinUDiv:pOp = "/";  break;
        case BinURem:pOp = "%";  break;
        case BinAnd: pOp = "&";  break;
        case BinOr:  pOp = "|";  break;
        case BinXor: pOp = "^";  break;
        case BinShl: pOp = "<<"; break;
        case BinLShr:pOp = ">>"; break;
        default:     pOp = "+";  break;
        }
        UINT32 D = Decl ();
        Line ("t%u = (t%u %s t%u) & 0x%llxULL;", D, Id (pA), pOp, Id (pB), (unsigned long long) Mask (Bits));
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = Bitsof (pA), D = Decl ();
        switch (Op) {
        case UnNeg: Line ("t%u = (-t%u) & 0x%llxULL;", D, Id (pA), (unsigned long long) Mask (Bits)); return Make (D, Bits, ppValue);
        case UnCom: Line ("t%u = (~t%u) & 0x%llxULL;", D, Id (pA), (unsigned long long) Mask (Bits)); return Make (D, Bits, ppValue);
        case UnNot: Line ("t%u = (t%u == 0) ? 1 : 0;", D, Id (pA)); return Make (D, 1, ppValue);
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        CHAR8 CONST *pOp; bool Signed = false;
        switch (Pred) {
        case CmpEq:  pOp = "=="; break;
        case CmpNe:  pOp = "!="; break;
        case CmpULt: pOp = "<";  break;
        case CmpULe: pOp = "<="; break;
        case CmpUGt: pOp = ">";  break;
        case CmpUGe: pOp = ">="; break;
        case CmpSLt: pOp = "<";  Signed = true; break;
        case CmpSLe: pOp = "<="; Signed = true; break;
        case CmpSGt: pOp = ">";  Signed = true; break;
        case CmpSGe: pOp = ">="; Signed = true; break;
        default:     pOp = "=="; break;
        }
        UINT32 D = Decl ();
        if (Signed) {
            Line ("t%u = ((int64_t)t%u %s (int64_t)t%u) ? 1 : 0;", D, Id (pA), pOp, Id (pB));
        } else {
            Line ("t%u = (t%u %s t%u) ? 1 : 0;", D, Id (pA), pOp, Id (pB));
        }
        return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = Bitsof (pA), D = Decl ();
        switch (Op) {
        case CastTrunc: Line ("t%u = t%u & 0x%llxULL;", D, Id (pA), (unsigned long long) Mask (Bits)); break;
        case CastZExt:  Line ("t%u = t%u;", D, Id (pA)); break;
        case CastSExt:  Line ("t%u = (uint64_t)(((int64_t)(t%u << %u)) >> %u) & 0x%llxULL;",
                              D, Id (pA), 64 - SrcBits, 64 - SrcBits, (unsigned long long) Mask (Bits)); break;
        }
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *pCond, ICpuValue *pTrue, ICpuValue *pFalse, ICpuValue **ppValue) override {
        UINT32 D = Decl ();
        Line ("t%u = t%u ? t%u : t%u;", D, Id (pCond), Id (pTrue), Id (pFalse));
        return Make (D, Bitsof (pTrue), ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 D = Decl ();
        Line ("t%u = f%u & 1;", D, (UINT32) Flag);
        return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        Line ("f%u = (uint8_t)(t%u & 1);", (UINT32) Flag, Id (pValue));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR /*Pc*/) override { return S_OK; }

    // Block ops are unused: the generator gives each instruction a label and wires
    // the gotos itself, so these are stubs that keep the CFG walk from needing them.
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    // ICpuSmcEmitter: RET records its run-time target; the generator turns the block
    // into "goto DISPATCH". Far jumps / SMC are not representable standalone.
    HRESULT STDMETHODCALLTYPE EmitCodeGuard (CPU_ADDR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE IndirectBranch (ICpuValue *) override { Line ("goto LCX_END;"); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetDispatchTarget (ICpuValue *pTargetPc) override {
        Line ("disp = t%u;", Id (pTargetPc));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDispatchTarget (ICpuValue **ppValue) override {
        UINT32 D = Decl ();
        Line ("t%u = disp;", D);
        return Make (D, 64, ppValue);
    }

private:
    UINT32 Decl () { return m_Next++; }
    static UINT32 Id (ICpuValue *p) { return static_cast<NativeValue *> (p)->m_Id; }
    static UINT32 Bitsof (ICpuValue *p) { return static_cast<NativeValue *> (p)->m_Bits; }
    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) { *ppValue = new NativeValue (Id, Bits); return S_OK; }

    void Line (CHAR8 CONST *pFmt, ...) {
        char Buf[256];
        va_list Args;
        va_start (Args, pFmt);
        std::vsnprintf (Buf, sizeof (Buf), pFmt, Args);
        va_end (Args);
        *m_pCur += "    ";
        *m_pCur += Buf;
        *m_pCur += "\n";
    }
};

} // anonymous namespace

std::string
LcGenerateNativeC (ICpuArchitecture *pArch, UINT8 CONST *pImage, UINT32 ImageLen,
                   CPU_ADDR Entry, CPU_ADDR End, LC_NATIVE_OPTIONS CONST &Opt)
{
    // 1. Discover every reachable instruction and how each terminates.
    struct Insn { UINT32 Tag; CPU_ADDR NewPc; CPU_ADDR NextPc; };
    std::map<CPU_ADDR, Insn> Reached;
    std::vector<CPU_ADDR> Work = { Entry };
    while (!Work.empty ()) {
        CPU_ADDR Pc = Work.back ();
        Work.pop_back ();
        if (Pc >= End || Reached.count (Pc)) {
            continue;
        }
        UINT32 Tag = 0; CPU_ADDR NewPc = 0, NextPc = 0;
        if (FAILED (pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc)) || NextPc <= Pc) {
            continue;
        }
        Reached[Pc] = { Tag, NewPc, NextPc };
        if (Tag & (TagContinue | TagConditional | TagCall)) { Work.push_back (NextPc); }
        if (Tag & (TagBranch | TagConditional | TagCall))   { Work.push_back (NewPc); }
    }
    if (Reached.empty ()) {
        return std::string ();
    }

    // A goto target: a reachable instruction's label, else the program end (a target
    // off the translated region cannot be a host-free standalone transfer).
    auto Label = [&Reached] (CPU_ADDR Target) -> std::string {
        if (Reached.count (Target)) {
            char L[24];
            std::snprintf (L, sizeof (L), "L_%04llx", (unsigned long long) Target);
            return std::string (L);
        }
        return std::string ("LCX_END");
    };

    // 2. Emit each instruction's body + terminator into its own buffer.
    NativeEmitter Emitter;
    std::map<CPU_ADDR, std::string> Bodies;
    for (auto CONST &Pair : Reached) {
        CPU_ADDR Pc = Pair.first;
        Insn CONST &In = Pair.second;
        std::string Body;
        Emitter.m_pCur = &Body;
        pArch->TranslateInstr (Pc, &Emitter);
        if (In.Tag & TagReturn) {
            Body += "    goto LCX_DISPATCH;\n";
        } else if (In.Tag & TagTrap) {
            Body += "    goto LCX_END;            /* needs a host (far/INT/port) */\n";
        } else if (In.Tag & TagConditional) {
            ComPtr<ICpuValue> Cond;
            if (SUCCEEDED (pArch->TranslateCond (Pc, &Emitter, &Cond)) && Cond != nullptr) {
                Body += "    if (t" + std::to_string (static_cast<NativeValue *> (Cond.Get ())->m_Id) +
                        ") goto " + Label (In.NewPc) + "; else goto " + Label (In.NextPc) + ";\n";
            } else {
                Body += "    goto " + Label (In.NextPc) + ";\n";
            }
        } else {
            CPU_ADDR Target = (In.Tag & (TagBranch | TagCall)) ? In.NewPc : In.NextPc;
            Body += "    goto " + Label (Target) + ";\n";
        }
        Bodies[Pc] = std::move (Body);
    }

    // 3. Assemble the standalone program.
    std::string Out;
    Out += "/* Generated by LibCPU native AOT -- standalone, no LibCPU dependency. */\n";
    Out += "#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n#include <stdlib.h>\n\n";
    Out += "#define LCX_RAM_SIZE 0x10000u\n\n";
    // The program's initialised memory (read-only data segment). It is loaded into
    // the guest RAM at start-up -- RAM itself is allocated normally, not a static blob.
    Out += "static const unsigned char LCX_INIT[] = {";
    char Hex[16];
    for (UINT32 I = 0; I < ImageLen; I++) {
        std::snprintf (Hex, sizeof (Hex), "%s0x%02x", (I % 16 == 0) ? "\n    " : ",", pImage[I]);
        Out += Hex;
    }
    Out += "\n};\n\n";

    Out += "int main(void) {\n";
    Out += "    uint8_t *RAM = (uint8_t *) calloc(LCX_RAM_SIZE, 1);\n";
    Out += "    if (RAM == NULL) { return 1; }\n";
    Out += "    memcpy(RAM, LCX_INIT, sizeof(LCX_INIT));\n";
    // Guest registers and flags as locals -> the host compiler maps them to host
    // registers. Temps too (declared before any label, so goto never skips a decl).
    Out += "    uint64_t r0=0,r1=0,r2=0,r3=0,r4=0,r5=0,r6=0,r7=0,r8=0,r9=0,r10=0,r11=0,\n"
           "             r12=0,r13=0,r14=0,r15=0,r16=0,r17=0,r18=0,r19=0,r20=0,r21=0,\n"
           "             r22=0,r23=0,r24=0,r25=0,r26=0,r27=0,r28=0,r29=0,r30=0,r31=0;\n";
    Out += "    uint8_t f0=0,f1=0,f2=0,f3=0,f4=0,f5=0,f6=0,f7=0;\n";
    Out += "    uint64_t disp=0; (void)disp;\n";
    if (Emitter.m_Next > 0) {
        Out += "    uint64_t ";
        for (UINT32 I = 0; I < Emitter.m_Next; I++) {
            char T[16];
            std::snprintf (T, sizeof (T), "%st%u", I ? "," : "", I);
            Out += T;
        }
        Out += ";\n";
    }
    Out += "    (void)f0;(void)f1;(void)f2;(void)f3;(void)f4;(void)f5;(void)f6;(void)f7;\n";

    char Goto[32];
    std::snprintf (Goto, sizeof (Goto), "    goto L_%04llx;\n", (unsigned long long) Entry);
    Out += Goto;

    for (auto CONST &Pair : Bodies) {
        char Label[24];
        std::snprintf (Label, sizeof (Label), "L_%04llx: ;\n", (unsigned long long) Pair.first);
        Out += Label;
        Out += Pair.second;
    }

    // RET dispatch: route the run-time return target to the matching label.
    Out += "LCX_DISPATCH: ;\n";
    for (auto CONST &Pair : Reached) {
        char Case[64];
        std::snprintf (Case, sizeof (Case), "    if (disp == 0x%04llxULL) goto L_%04llx;\n",
                       (unsigned long long) Pair.first, (unsigned long long) Pair.first);
        Out += Case;
    }
    Out += "    goto LCX_END;\n";

    Out += "LCX_END: ;\n";
    if (Opt.DumpResult) {
        char Dump[224];
        std::snprintf (Dump, sizeof (Dump),
                       "    {\n"
                       "        unsigned res = (unsigned)(RAM[0x%llx] | (RAM[0x%llx]<<8));\n"
                       "        printf(\"0x%%04x\\n\", res);\n"
                       "        free(RAM);\n"
                       "        return (int)(res & 0xff);\n"
                       "    }\n",
                       (unsigned long long) Opt.ResultAddr, (unsigned long long) (Opt.ResultAddr + 1));
        Out += Dump;
    } else {
        Out += "    free(RAM);\n    return 0;\n";
    }
    Out += "}\n";
    return Out;
}

bool
LcCompileNative (std::string CONST &Source, CHAR8 CONST *pOutExe, std::string *pError)
{
    char Tmpl[] = "/tmp/libcpu_native_XXXXXX";
    if (mkdtemp (Tmpl) == nullptr) {
        if (pError) { *pError = "mkdtemp failed"; }
        return false;
    }
    std::string Dir = Tmpl;
    std::string Src = Dir + "/out.c";
    std::FILE *pf = std::fopen (Src.c_str (), "w");
    if (pf == nullptr) {
        if (pError) { *pError = "cannot open source"; }
        return false;
    }
    std::fwrite (Source.data (), 1, Source.size (), pf);
    std::fclose (pf);

    CHAR8 CONST *pCc = std::getenv ("CC");
    std::string Cmd = std::string ("\"") + (pCc ? pCc : "cc") + "\" -O2 -o '" + pOutExe + "' '" + Src + "' 2>'" + Dir + "/err'";
    int Rc = std::system (Cmd.c_str ());
    if (Rc != 0 && pError) {
        std::FILE *pe = std::fopen ((Dir + "/err").c_str (), "rb");
        if (pe) {
            char Buf[512];
            size_t N = std::fread (Buf, 1, sizeof (Buf) - 1, pe);
            Buf[N] = '\0';
            *pError = Buf;
            std::fclose (pe);
        }
    }
    std::remove (Src.c_str ());
    std::remove ((Dir + "/err").c_str ());
    std::remove (Dir.c_str ());
    return Rc == 0;
}

} // namespace LibCPU
