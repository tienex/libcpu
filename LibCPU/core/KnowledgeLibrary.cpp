/** @file
  Knowledge-library loader and runtime dispatcher. See KnowledgeLibrary.h.
**/

#include "KnowledgeLibrary.h"
#include "../aot/AotGenerator.h"
#include "LibCPU/PCom.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace LibCPU {

// ===========================================================================
//  Minimal XML reader
//
//  Supports the subset the schema needs: elements with attributes, self-closing
//  tags, nested children, comments, and the <?xml?> prolog. Text content is kept
//  but unused. No entities/CDATA/namespaces -- a knowledge library does not need
//  them, and pulling in a full XML library would violate the no-dependency rule.
// ===========================================================================

namespace {

struct XmlNode {
    std::string                     Name;
    std::map<std::string, std::string> Attrs;
    std::vector<XmlNode>            Children;
};

class XmlParser {
public:
    explicit XmlParser (CHAR8 CONST *pText) : m_p (pText) {}

    bool Parse (XmlNode *pRoot) {
        SkipMisc ();
        if (*m_p != '<') {
            return false;
        }
        return ParseElement (pRoot);
    }

private:
    CHAR8 CONST *m_p;

    void SkipSpace () {
        while (*m_p == ' ' || *m_p == '\t' || *m_p == '\r' || *m_p == '\n') {
            m_p++;
        }
    }

    // Skip whitespace, comments, and the <?xml ...?> prolog between elements.
    void SkipMisc () {
        for (;;) {
            SkipSpace ();
            if (m_p[0] == '<' && m_p[1] == '!' && m_p[2] == '-' && m_p[3] == '-') {
                m_p += 4;
                while (*m_p && !(m_p[0] == '-' && m_p[1] == '-' && m_p[2] == '>')) {
                    m_p++;
                }
                if (*m_p) { m_p += 3; }
                continue;
            }
            if (m_p[0] == '<' && m_p[1] == '?') {
                while (*m_p && !(m_p[0] == '?' && m_p[1] == '>')) {
                    m_p++;
                }
                if (*m_p) { m_p += 2; }
                continue;
            }
            break;
        }
    }

    static bool NameChar (CHAR8 c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ':' || c == '.';
    }

    std::string ParseName () {
        std::string Name;
        while (NameChar (*m_p)) {
            Name.push_back (*m_p++);
        }
        return Name;
    }

    // At '<'. Parse one element (and its subtree) into pNode.
    bool ParseElement (XmlNode *pNode) {
        if (*m_p != '<') {
            return false;
        }
        m_p++;                                  // consume '<'
        pNode->Name = ParseName ();
        if (pNode->Name.empty ()) {
            return false;
        }
        // Attributes.
        for (;;) {
            SkipSpace ();
            if (*m_p == '/' || *m_p == '>') {
                break;
            }
            std::string Key = ParseName ();
            if (Key.empty ()) {
                return false;
            }
            SkipSpace ();
            if (*m_p != '=') {
                return false;
            }
            m_p++;
            SkipSpace ();
            CHAR8 Quote = *m_p;
            if (Quote != '"' && Quote != '\'') {
                return false;
            }
            m_p++;
            std::string Val;
            while (*m_p && *m_p != Quote) {
                Val.push_back (*m_p++);
            }
            if (*m_p != Quote) {
                return false;
            }
            m_p++;
            pNode->Attrs[Key] = Val;
        }
        if (*m_p == '/') {                       // self-closing <foo/>
            m_p++;
            if (*m_p != '>') {
                return false;
            }
            m_p++;
            return true;
        }
        if (*m_p != '>') {
            return false;
        }
        m_p++;                                   // consume '>'
        // Children until the matching close tag.
        for (;;) {
            // Text up to the next '<' is ignored (the schema is element-only).
            while (*m_p && *m_p != '<') {
                m_p++;
            }
            if (!*m_p) {
                return false;                    // unterminated
            }
            if (m_p[1] == '/') {                 // close tag
                m_p += 2;
                std::string Close = ParseName ();
                SkipSpace ();
                if (*m_p != '>' || Close != pNode->Name) {
                    return false;
                }
                m_p++;
                return true;
            }
            if (m_p[1] == '!' && m_p[2] == '-') {    // comment between children
                SkipMisc ();
                continue;
            }
            XmlNode Child;
            if (!ParseElement (&Child)) {
                return false;
            }
            pNode->Children.push_back (std::move (Child));
        }
    }
};

// Parse an integer attribute that may be hex (0x..) or decimal.
static UINT32
ParseNum (std::string CONST &S)
{
    if (S.empty ()) {
        return 0;
    }
    return (UINT32) std::strtoul (S.c_str (), nullptr, 0);
}

static std::string
Attr (XmlNode CONST &N, CHAR8 CONST *pKey)
{
    auto It = N.Attrs.find (pKey);
    return (It == N.Attrs.end ()) ? std::string () : It->second;
}

} // anonymous namespace

// ===========================================================================
//  Loader
// ===========================================================================

bool
LcKnowledgeLibrary::Load (CHAR8 CONST *pPath)
{
    std::FILE *pf = std::fopen (pPath, "rb");
    if (pf == nullptr) {
        return false;
    }
    std::fseek (pf, 0, SEEK_END);
    long Size = std::ftell (pf);
    std::fseek (pf, 0, SEEK_SET);
    if (Size < 0) {
        std::fclose (pf);
        return false;
    }
    std::string Text;
    Text.resize ((size_t) Size);
    size_t Got = std::fread (&Text[0], 1, (size_t) Size, pf);
    std::fclose (pf);
    Text.resize (Got);

    XmlNode Root;
    XmlParser Parser (Text.c_str ());
    if (!Parser.Parse (&Root) || Root.Name != "knowledge") {
        return false;
    }
    m_Target = Attr (Root, "target");
    m_Host   = Attr (Root, "host");

    for (XmlNode CONST &Sc : Root.Children) {
        if (Sc.Name != "syscall") {
            continue;
        }
        KN_SYSCALL Entry;
        Entry.Vector = ParseNum (Attr (Sc, "vector"));
        Entry.Select = Attr (Sc, "select");
        Entry.Value  = ParseNum (Attr (Sc, "value"));
        Entry.Name   = Attr (Sc, "name");
        for (XmlNode CONST &H : Sc.Children) {
            if (H.Name != "host") {
                continue;
            }
            Entry.Host.Call = Attr (H, "call");
            Entry.Host.Lib  = Attr (H, "lib");
            for (XmlNode CONST &A : H.Children) {
                if (A.Name != "arg") {
                    continue;
                }
                KN_HOST_ARG Arg;
                std::string Cst = Attr (A, "const");
                if (!Cst.empty ()) {
                    Arg.Kind  = KN_HOST_ARG::ConstName;
                    Arg.Const = Cst;
                } else {
                    Arg.Kind = KN_HOST_ARG::FromValue;
                    Arg.From = Attr (A, "from");
                    Arg.Conv = Attr (A, "conv");
                }
                Entry.Host.Args.push_back (Arg);
            }
        }
        m_Syscalls.push_back (std::move (Entry));
    }
    return true;
}

KN_SYSCALL CONST *
LcKnowledgeLibrary::Find (UINT32 Vector, UINT32 Selector) CONST
{
    for (KN_SYSCALL CONST &S : m_Syscalls) {
        if (S.Vector != Vector) {
            continue;
        }
        if (S.Select.empty () || S.Value == Selector) {
            return &S;
        }
    }
    return nullptr;
}

// ===========================================================================
//  Runtime: 8086/DOS operand model + host-call dispatch
// ===========================================================================

namespace {

// 8086 register file indices (must match the V20 frontend's RegV20* layout).
enum {
    R_AX = 0, R_CX = 1, R_DX = 2, R_BX = 3,
    R_SP = 4, R_BP = 5, R_SI = 6, R_DI = 7,
    R_ES = 8, R_CS = 9, R_SS = 10, R_DS = 11
};

// Resolve a single 16-bit register or 8-bit half by 8086 name.
static UINT64
Reg16 (CPU_STATE CONST *pState, std::string CONST &Name, bool *pOk)
{
    *pOk = true;
    static struct { CHAR8 CONST *p; int idx; } const Word[] = {
        { "ax", R_AX }, { "cx", R_CX }, { "dx", R_DX }, { "bx", R_BX },
        { "sp", R_SP }, { "bp", R_BP }, { "si", R_SI }, { "di", R_DI },
        { "es", R_ES }, { "cs", R_CS }, { "ss", R_SS }, { "ds", R_DS }
    };
    for (auto CONST &W : Word) {
        if (Name == W.p) {
            return pState->Reg[W.idx] & 0xFFFF;
        }
    }
    static struct { CHAR8 CONST *p; int idx; bool hi; } const Byte[] = {
        { "al", R_AX, false }, { "ah", R_AX, true },
        { "cl", R_CX, false }, { "ch", R_CX, true },
        { "dl", R_DX, false }, { "dh", R_DX, true },
        { "bl", R_BX, false }, { "bh", R_BX, true }
    };
    for (auto CONST &B : Byte) {
        if (Name == B.p) {
            UINT64 V = pState->Reg[B.idx];
            return B.hi ? ((V >> 8) & 0xFF) : (V & 0xFF);
        }
    }
    *pOk = false;
    return 0;
}

// Resolve an operand name: a register ("al"), or a far pointer "seg:off" (e.g.
// "ds:dx") yielding the LINEAR address seg*16 + off.
static UINT64
Resolve (CPU_STATE CONST *pState, std::string CONST &Name, bool *pOk)
{
    size_t Colon = Name.find (':');
    if (Colon == std::string::npos) {
        return Reg16 (pState, Name, pOk);
    }
    std::string Seg = Name.substr (0, Colon);
    std::string Off = Name.substr (Colon + 1);
    bool OkSeg = false, OkOff = false;
    UINT64 S = Reg16 (pState, Seg, &OkSeg);
    UINT64 O = Reg16 (pState, Off, &OkOff);
    *pOk = OkSeg && OkOff;
    return (S << 4) + O;
}

// A host-call argument once materialised from the guest.
struct HostVal {
    enum { Int, Str, Stream } Kind;
    long long   I;
    std::string S;
    std::FILE  *F;
};

// Copy a '$'-terminated DOS string out of guest RAM into a host C string.
static std::string
DollarString (UINT8 CONST *pRAM, UINT64 RamSize, UINT64 Linear)
{
    std::string Out;
    for (UINT64 A = Linear; A < RamSize; A++) {
        CHAR8 c = (CHAR8) pRAM[A];
        if (c == '$') {
            break;
        }
        Out.push_back (c);
    }
    return Out;
}

static HostVal
Materialise (KN_HOST_ARG CONST &Arg, CPU_STATE CONST *pState, UINT8 CONST *pRAM, UINT64 RamSize)
{
    HostVal V;
    if (Arg.Kind == KN_HOST_ARG::ConstName) {
        if (Arg.Const == "stdout") { V.Kind = HostVal::Stream; V.F = stdout; return V; }
        if (Arg.Const == "stderr") { V.Kind = HostVal::Stream; V.F = stderr; return V; }
        V.Kind = HostVal::Int; V.I = 0;
        return V;
    }
    bool Ok = false;
    UINT64 Raw = Resolve (pState, Arg.From, &Ok);
    if (Arg.Conv == "dollar_to_nul") {
        V.Kind = HostVal::Str;
        V.S    = DollarString (pRAM, RamSize, Raw);
        return V;
    }
    V.Kind = HostVal::Int;
    V.I    = (long long) Raw;
    return V;
}

// Execute one host call. Sets *pExited / *pCode for an "exit"-class call.
static VOID
DoHostCall (KN_HOST_CALL CONST &Call, std::vector<HostVal> CONST &Args, bool *pExited, INT32 *pCode)
{
    if (Call.Call == "fputs") {
        if (Args.size () >= 2 && Args[0].Kind == HostVal::Str && Args[1].Kind == HostVal::Stream) {
            std::fputs (Args[0].S.c_str (), Args[1].F);
            std::fflush (Args[1].F);
        }
    } else if (Call.Call == "fputc" || Call.Call == "putchar") {
        std::FILE *F = (Args.size () >= 2 && Args[1].Kind == HostVal::Stream) ? Args[1].F : stdout;
        if (!Args.empty ()) {
            std::fputc ((int) (Args[0].I & 0xFF), F);
            std::fflush (F);
        }
    } else if (Call.Call == "exit") {
        *pExited = true;
        *pCode   = Args.empty () ? 0 : (INT32) (Args[0].I & 0xFF);
    }
    // An unknown call name is a no-op: the library named a host function the runtime
    // does not yet provide. (The loader keeps it so a future runtime can bind it.)
}

} // anonymous namespace

KN_RUN_RESULT
LcRunWithSyscalls (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                   CPU_ADDR Entry, CPU_ADDR End,
                   VOID *pRAM, CPU_STATE *pState,
                   LcKnowledgeLibrary CONST &Library)
{
    KN_RUN_RESULT R;
    R.Exited       = false;
    R.ExitCode     = 0;
    R.Syscalls     = 0;
    R.Translations = 0;
    R.Unhandled    = false;

    UINT8 *pBytes  = (UINT8 *) pRAM;
    UINT64 RamSize = pState->RamSize ? pState->RamSize : CPU_RAM_DEFAULT;

    CPU_ADDR Pc = Entry;
    for (UINT32 Iter = 0; Iter < 1000000u; Iter++) {
        ComPtr<ICpuCode> Code;
        UINT32 N = 0;
        if (FAILED (GenerateAotCfg (pArch, pBackend, Pc, End, &Code, &N)) || Code == nullptr) {
            break;
        }
        R.Translations++;
        pState->TrapPc        = CPU_SMC_NO_TRAP;
        pState->SyscallVector = CPU_NO_SYSCALL;
        Code->Execute (pRAM, pState, nullptr);

        if (pState->TrapPc == CPU_SMC_NO_TRAP) {
            break;                                // ran off the end of the region
        }
        if (pState->SyscallVector == CPU_NO_SYSCALL) {
            Pc = (CPU_ADDR) pState->TrapPc;       // a non-syscall trap (e.g. far jump)
            continue;
        }

        // A guest system call: find its library entry and dispatch it.
        UINT32 Vector = (UINT32) pState->SyscallVector;
        std::string Select;
        for (UINT32 I = 0; I < Library.Count (); I++) {
            if (Library.At (I).Vector == Vector) {
                Select = Library.At (I).Select;
                break;
            }
        }
        UINT32 Selector = 0;
        if (!Select.empty ()) {
            bool Ok = false;
            Selector = (UINT32) Resolve (pState, Select, &Ok);
        }
        KN_SYSCALL CONST *pEntry = Library.Find (Vector, Selector);
        if (pEntry == nullptr) {
            R.Unhandled = true;
            break;
        }
        R.Syscalls++;

        std::vector<HostVal> Args;
        Args.reserve (pEntry->Host.Args.size ());
        for (KN_HOST_ARG CONST &A : pEntry->Host.Args) {
            Args.push_back (Materialise (A, pState, pBytes, RamSize));
        }
        bool  Exited = false;
        INT32 Code32 = 0;
        DoHostCall (pEntry->Host, Args, &Exited, &Code32);
        if (Exited) {
            R.Exited   = true;
            R.ExitCode = Code32;
            break;
        }
        Pc = (CPU_ADDR) pState->TrapPc;           // resume after the INT
    }
    return R;
}

} // namespace LibCPU
