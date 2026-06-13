/** @file  DerivationEngine -- join signatures + symbols; (de)serialise to a ZOO archive. */

#include "DerivationEngine.h"
#include "ZooArchive.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace LibCPU {

void
KnowledgeCatalog::Derive (HeaderParser CONST &Headers, SymbolReader CONST &Symbols)
{
    m_Library = Symbols.InstallName ();
    m_Structs = Headers.Structs ();
    m_Functions.clear ();
    for (HEADER_FUNCTION CONST &Fn : Headers.Functions ()) {
        HOST_ENTITY E;
        E.Name       = Fn.Name;
        E.ReturnType = Fn.ReturnType;
        E.Params     = Fn.Params;
        E.Variadic   = Fn.Variadic;
        std::string Underscored = std::string ("_") + Fn.Name;
        E.Exported   = Symbols.Has (Fn.Name) || Symbols.Has (Underscored);
        m_Functions.push_back (std::move (E));
    }
}

UINT32
KnowledgeCatalog::ExportedCount () CONST
{
    UINT32 N = 0;
    for (HOST_ENTITY CONST &E : m_Functions) {
        if (E.Exported) { N++; }
    }
    return N;
}

// --- serialisation: a small tab-delimited text form, one record per line ---------------

// Split Line into fields on '\t'. Trailing empty fields are preserved up to the last tab.
static std::vector<std::string>
SplitTabs (std::string CONST &Line)
{
    std::vector<std::string> Out;
    size_t Start = 0;
    for (;;) {
        size_t Tab = Line.find ('\t', Start);
        Out.push_back (Line.substr (Start, (Tab == std::string::npos ? Line.size () : Tab) - Start));
        if (Tab == std::string::npos) { break; }
        Start = Tab + 1;
    }
    return Out;
}

static void
ForEachLine (std::string CONST &Text, void (*Fn)(std::string CONST &, void *), void *pCtx)
{
    size_t Start = 0;
    while (Start < Text.size ()) {
        size_t Eol = Text.find ('\n', Start);
        size_t End = (Eol == std::string::npos) ? Text.size () : Eol;
        if (End > Start) {
            std::string Line = Text.substr (Start, End - Start);
            Fn (Line, pCtx);
        }
        if (Eol == std::string::npos) { break; }
        Start = Eol + 1;
    }
}

bool
KnowledgeCatalog::Save (CHAR8 CONST *pPath, std::string *pError) CONST
{
    std::string Manifest = "host-catalog\t1\n";
    Manifest += "library\t" + m_Library + "\n";
    Manifest += "functions\t" + std::to_string (m_Functions.size ()) + "\n";
    Manifest += "structs\t" + std::to_string (m_Structs.size ()) + "\n";

    // functions: <exported>\t<variadic>\t<returnType>\t<name>[\t<ptype>=<pname>]...
    std::string Funcs;
    for (HOST_ENTITY CONST &E : m_Functions) {
        Funcs += (E.Exported ? "1\t" : "0\t");
        Funcs += (E.Variadic ? "1\t" : "0\t");
        Funcs += E.ReturnType + "\t" + E.Name;
        for (HEADER_PARAM CONST &P : E.Params) {
            Funcs += "\t" + P.Type + "=" + P.Name;
        }
        Funcs += "\n";
    }

    // structs: a "struct" line then one "field" line per member.
    std::string Structs;
    for (HEADER_STRUCT CONST &S : m_Structs) {
        Structs += "struct\t" + S.Name + "\t" + std::to_string (S.Size) + "\n";
        for (HEADER_FIELD CONST &F : S.Fields) {
            Structs += "field\t" + std::to_string (F.Offset) + "\t" + std::to_string (F.Size) +
                       "\t" + F.Type + "\t" + F.Name + "\n";
        }
    }

    ZooWriter Writer;
    Writer.Add ("manifest", Manifest);
    Writer.Add ("functions", Funcs);
    Writer.Add ("structs", Structs);
    return Writer.Save (pPath, 19, pError);
}

namespace {
// Parse context for the function / struct members.
struct LoadCtx {
    KnowledgeCatalog        *pSelf;
    std::vector<HOST_ENTITY>  *pFuncs;
    std::vector<HEADER_STRUCT> *pStructs;
};
}

static void
ParseFuncLine (std::string CONST &Line, void *pCtx)
{
    std::vector<HOST_ENTITY> *pFuncs = ((LoadCtx *) pCtx)->pFuncs;
    std::vector<std::string> F = SplitTabs (Line);
    if (F.size () < 4) { return; }
    HOST_ENTITY E;
    E.Exported   = F[0] == "1";
    E.Variadic   = F[1] == "1";
    E.ReturnType = F[2];
    E.Name       = F[3];
    for (size_t I = 4; I < F.size (); I++) {
        size_t Eq = F[I].find ('=');
        HEADER_PARAM P;
        P.Type = (Eq == std::string::npos) ? F[I] : F[I].substr (0, Eq);
        P.Name = (Eq == std::string::npos) ? "" : F[I].substr (Eq + 1);
        E.Params.push_back (std::move (P));
    }
    pFuncs->push_back (std::move (E));
}

static void
ParseStructLine (std::string CONST &Line, void *pCtx)
{
    std::vector<HEADER_STRUCT> *pStructs = ((LoadCtx *) pCtx)->pStructs;
    std::vector<std::string> F = SplitTabs (Line);
    if (F.empty ()) { return; }
    if (F[0] == "struct" && F.size () >= 3) {
        HEADER_STRUCT S;
        S.Name = F[1];
        S.Size = std::strtoull (F[2].c_str (), nullptr, 10);
        pStructs->push_back (std::move (S));
    } else if (F[0] == "field" && F.size () >= 5 && !pStructs->empty ()) {
        HEADER_FIELD Fld;
        Fld.Offset = std::strtoull (F[1].c_str (), nullptr, 10);
        Fld.Size   = std::strtoull (F[2].c_str (), nullptr, 10);
        Fld.Type   = F[3];
        Fld.Name   = F[4];
        pStructs->back ().Fields.push_back (std::move (Fld));
    }
}

bool
KnowledgeCatalog::Load (CHAR8 CONST *pPath, std::string *pError)
{
    ZooArchive Archive;
    if (!Archive.Load (pPath, pError)) {
        return false;
    }
    m_Library.clear ();
    m_Functions.clear ();
    m_Structs.clear ();

    std::vector<UINT8> Buf;
    if (Archive.Extract (std::string ("manifest"), &Buf)) {
        std::string Manifest ((CHAR8 CONST *) Buf.data (), Buf.size ());
        size_t Lib = Manifest.find ("library\t");
        if (Lib != std::string::npos) {
            size_t Eol = Manifest.find ('\n', Lib);
            m_Library = Manifest.substr (Lib + 8, (Eol == std::string::npos ? Manifest.size () : Eol) - (Lib + 8));
        }
    }
    LoadCtx Ctx{ this, &m_Functions, &m_Structs };
    if (Archive.Extract (std::string ("functions"), &Buf)) {
        std::string Text ((CHAR8 CONST *) Buf.data (), Buf.size ());
        ForEachLine (Text, ParseFuncLine, &Ctx);
    }
    if (Archive.Extract (std::string ("structs"), &Buf)) {
        std::string Text ((CHAR8 CONST *) Buf.data (), Buf.size ());
        ForEachLine (Text, ParseStructLine, &Ctx);
    }
    return true;
}

VOID *
CatalogBindHost (VOID *pCtx, CHAR8 CONST *pName)
{
#ifndef _WIN32
    KnowledgeCatalog CONST *pCat = (KnowledgeCatalog CONST *) pCtx;
    // Bind only what the catalog knows AND the library actually exports -- the catalog is
    // the authority on what is callable.
    bool Exported = false;
    bool Known = false;
    for (HOST_ENTITY CONST &E : pCat->Functions ()) {
        if (E.Name == pName) { Known = true; Exported = E.Exported; break; }
    }
    if (!Known || !Exported) {
        return nullptr;
    }
    // Prefer an already-loaded definition (libc / libSystem live in the process image);
    // otherwise dlopen the library the catalog was derived against.
    if (VOID *p = dlsym (RTLD_DEFAULT, pName)) {
        return p;
    }
    if (!pCat->Library ().empty ()) {
        if (void *h = dlopen (pCat->Library ().c_str (), RTLD_NOW | RTLD_GLOBAL)) {
            return dlsym (h, pName);
        }
    }
    return nullptr;
#else
    (void) pCtx; (void) pName;
    return nullptr;
#endif
}

UINT32
CatalogStructLayout (VOID *pCtx, CHAR8 CONST *pType, KN_FIELD_MAP *pFields, UINT32 Max,
                     UINT32 *pHostSize, UINT32 *pGuestSize)
{
    KnowledgeCatalog CONST *pCat = (KnowledgeCatalog CONST *) pCtx;
    for (HEADER_STRUCT CONST &S : pCat->Structs ()) {
        if (S.Name != pType) {
            continue;
        }
        // Guest layout = the DOS convention: fields PACKED in declaration order (host
        // alignment padding removed) with each field capped to the 16-bit target word.
        UINT32 CONST GuestWord = 2;
        UINT32 N = 0;
        UINT32 GuestOff = 0;
        for (HEADER_FIELD CONST &F : S.Fields) {
            if (N >= Max) {
                break;
            }
            UINT32 HostSize = (UINT32) (F.Size ? F.Size : GuestWord);
            UINT32 GuestSize = HostSize < GuestWord ? HostSize : GuestWord;   // min(host, word)
            pFields[N].HostOffset  = (UINT32) F.Offset;
            pFields[N].HostSize    = HostSize;
            pFields[N].GuestOffset = GuestOff;
            pFields[N].GuestSize   = GuestSize;
            GuestOff += GuestSize;
            N++;
        }
        *pHostSize  = (UINT32) S.Size;
        *pGuestSize = GuestOff;
        return N;
    }
    *pHostSize = 0;
    *pGuestSize = 0;
    return 0;
}

HOST_BINDER
MakeCatalogBinder (KnowledgeCatalog CONST *pCatalog)
{
    HOST_BINDER B;
    B.pCtx   = (VOID *) pCatalog;
    B.Bind   = CatalogBindHost;
    B.Layout = CatalogStructLayout;
    return B;
}

// --- target ABI table -> mapping derivation --------------------------------

// Trim leading/trailing whitespace.
static std::string
Trim (std::string CONST &S)
{
    size_t B = S.find_first_not_of (" \t\r\n");
    size_t E = S.find_last_not_of (" \t\r\n");
    return (B == std::string::npos) ? std::string () : S.substr (B, E - B + 1);
}

// Split on a delimiter char.
static std::vector<std::string>
Split (std::string CONST &S, char Delim)
{
    std::vector<std::string> Out;
    size_t Start = 0;
    for (;;) {
        size_t P = S.find (Delim, Start);
        Out.push_back (S.substr (Start, (P == std::string::npos ? S.size () : P) - Start));
        if (P == std::string::npos) { break; }
        Start = P + 1;
    }
    return Out;
}

bool
LoadTargetAbi (CHAR8 CONST *pPath, std::vector<TARGET_SYSCALL> *pOut, std::string *pError)
{
    std::FILE *pf = std::fopen (pPath, "rb");
    if (pf == nullptr) {
        if (pError) { *pError = std::string ("cannot open '") + pPath + "'"; }
        return false;
    }
    std::fseek (pf, 0, SEEK_END);
    long Len = std::ftell (pf);
    std::fseek (pf, 0, SEEK_SET);
    std::string Text (Len > 0 ? (size_t) Len : 0, '\0');
    if (Len > 0) { Text.resize (std::fread (&Text[0], 1, (size_t) Len, pf)); }
    std::fclose (pf);

    // Line format (see test/dos.abi):
    //   vector select value name | hostcall | result | operand:conv operand:conv ...
    // '#' comments and blank lines are ignored; result "-" means none.
    for (std::string CONST &Raw : Split (Text, '\n')) {
        std::string Line = Trim (Raw);
        if (Line.empty () || Line[0] == '#') {
            continue;
        }
        std::vector<std::string> Cols = Split (Line, '|');
        if (Cols.size () < 2) {
            continue;
        }
        std::vector<std::string> Head = Split (Trim (Cols[0]), ' ');
        std::vector<std::string> Head2;
        for (std::string CONST &H : Head) {
            if (!Trim (H).empty ()) { Head2.push_back (Trim (H)); }
        }
        if (Head2.size () < 4) {
            continue;
        }
        TARGET_SYSCALL T;
        T.Vector   = (UINT32) std::strtoul (Head2[0].c_str (), nullptr, 0);
        T.Select   = Head2[1];
        T.Value    = (UINT32) std::strtoul (Head2[2].c_str (), nullptr, 0);
        T.Name     = Head2[3];
        T.HostCall = Trim (Cols[1]);
        std::string Res = (Cols.size () > 2) ? Trim (Cols[2]) : std::string ();
        T.Result   = (Res == "-" || Res == "") ? std::string () : Res;
        if (Cols.size () > 3) {
            for (std::string CONST &A : Split (Trim (Cols[3]), ' ')) {
                std::string Tok = Trim (A);
                if (Tok.empty ()) { continue; }
                size_t Colon = Tok.rfind (':');                 // operand may itself contain ':'
                TARGET_ARG Arg;
                Arg.Operand = (Colon == std::string::npos) ? Tok : Tok.substr (0, Colon);
                Arg.Conv    = (Colon == std::string::npos) ? "int" : Tok.substr (Colon + 1);
                T.Args.push_back (Arg);
            }
        }
        pOut->push_back (std::move (T));
    }
    return true;
}

DERIVE_REPORT
DeriveMapping (KnowledgeCatalog CONST &Catalog,
               std::vector<TARGET_SYSCALL> CONST &Target,
               KnowledgeLibrary *pLibrary)
{
    DERIVE_REPORT R;
    R.Total = (UINT32) Target.size ();
    R.Resolved = 0;
    R.Unresolved = 0;

    for (TARGET_SYSCALL CONST &T : Target) {
        // Auto-binding: when the table leaves the host call as "auto" (or blank), infer it
        // by matching the syscall's semantic name to an exported catalog function -- so the
        // table need not name the host function where the names already align.
        std::string HostCall = T.HostCall;
        if (HostCall.empty () || HostCall == "auto") {
            for (HOST_ENTITY CONST &E : Catalog.Functions ()) {
                if (E.Exported && E.Name == T.Name) { HostCall = E.Name; break; }
            }
        }
        // Validate the chosen host call against the catalog: it must be a known, exported
        // entity for us to bind it.
        HOST_ENTITY CONST *pHost = nullptr;
        for (HOST_ENTITY CONST &E : Catalog.Functions ()) {
            if (E.Name == HostCall && E.Exported) { pHost = &E; break; }
        }
        if (pHost == nullptr) {
            R.Unresolved++;
            continue;
        }
        R.Resolved++;

        KN_SYSCALL Entry;
        Entry.Vector      = T.Vector;
        Entry.Select      = T.Select;
        Entry.Value       = T.Value;
        Entry.Name        = T.Name;
        Entry.Host.Call   = HostCall;
        Entry.Host.Lib    = "";
        Entry.Host.Result = T.Result;

        // Generate the argument recipe from the host SIGNATURE: walk the host parameters
        // and, for each, either auto-fill a known host-side value (a FILE* stream) or
        // consume the next target operand, mapping it by its declared convention.
        size_t TargetIdx = 0;
        for (HEADER_PARAM CONST &P : pHost->Params) {
            KN_HOST_ARG Arg;
            Arg.Dir = KnDirNone;
            if (P.Type.find ("FILE") != std::string::npos) {
                Arg.Kind  = KN_HOST_ARG::ConstName;
                Arg.Const = "stdout";                           // signature-driven auto-fill
                Entry.Host.Args.push_back (Arg);
                continue;
            }
            if (TargetIdx >= T.Args.size ()) {
                break;                                          // host wants more than the target supplies
            }
            TARGET_ARG CONST &TA = T.Args[TargetIdx++];
            if (TA.Conv == "string") {
                Arg.Kind = KN_HOST_ARG::FromValue;
                Arg.From = TA.Operand;
                Arg.Conv = "dollar_to_nul";
            } else if (TA.Conv.rfind ("struct:", 0) == 0 || TA.Conv.rfind ("outstruct:", 0) == 0) {
                bool Out = TA.Conv.rfind ("outstruct:", 0) == 0;
                Arg.Kind   = KN_HOST_ARG::StructPtr;
                Arg.From   = TA.Operand;
                Arg.Struct = TA.Conv.substr (TA.Conv.find (':') + 1);
                Arg.Dir    = Out ? KnDirOut : KnDirIn;
            } else {
                Arg.Kind = KN_HOST_ARG::FromValue;              // "int" and anything else
                Arg.From = TA.Operand;
            }
            Entry.Host.Args.push_back (Arg);
        }
        pLibrary->AddEntry (Entry);
    }
    return R;
}

} // namespace LibCPU
