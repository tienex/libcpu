/** @file  HeaderParser -- libclang (loaded dynamically) with a built-in fallback. */

#include "HeaderParser.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace LibCPU {

// ===========================================================================
//  libclang C ABI, declared locally so we can dlopen without the clang-c headers.
//  Struct layouts MUST match <clang-c/Index.h>; they are passed/returned by value.
// ===========================================================================

typedef void *CXIndex;
typedef void *CXTranslationUnit;
typedef void *CXClientData;

typedef struct { CONST void *data; unsigned private_flags; } CXString;
typedef struct { int kind; int xdata; CONST void *data[3]; } CXCursor;
typedef struct { int kind; void *data[2]; } CXType;
typedef struct { CONST void *ptr_data[2]; unsigned int_data; } CXSourceLocation;

// CXCursorKind values we care about (stable across libclang versions).
enum {
    CXCursor_StructDecl   = 2,
    CXCursor_UnionDecl    = 3,
    CXCursor_ClassDecl    = 4,     // C++ class (data members captured like a struct)
    CXCursor_FieldDecl    = 6,
    CXCursor_FunctionDecl = 8,
    CXCursor_ParmDecl     = 10,
    CXCursor_Namespace    = 22,
    CXCursor_LinkageSpec  = 23     // extern "C" { ... }
};
// CXChildVisitResult
enum { CXChildVisit_Break = 0, CXChildVisit_Continue = 1, CXChildVisit_Recurse = 2 };

typedef int (*CXCursorVisitor) (CXCursor, CXCursor, CXClientData);

// The subset of the C API the parser uses.
typedef struct _CLANG_API {
    CXIndex           (*createIndex) (int, int);
    void              (*disposeIndex) (CXIndex);
    CXTranslationUnit (*parseTU) (CXIndex, CONST char *, CONST char *CONST *, int, void *, unsigned, unsigned);
    void              (*disposeTU) (CXTranslationUnit);
    CXCursor          (*getTUCursor) (CXTranslationUnit);
    unsigned          (*visitChildren) (CXCursor, CXCursorVisitor, CXClientData);
    int               (*getCursorKind) (CXCursor);
    CXString          (*getCursorSpelling) (CXCursor);
    CONST char       *(*getCString) (CXString);
    void              (*disposeString) (CXString);
    CXType            (*getCursorType) (CXCursor);
    CXType            (*getResultType) (CXType);
    CXString          (*getTypeSpelling) (CXType);
    int               (*cursorGetNumArguments) (CXCursor);
    CXCursor          (*cursorGetArgument) (CXCursor, unsigned);
    unsigned          (*isFunctionTypeVariadic) (CXType);
    long long         (*typeGetSizeOf) (CXType);
    long long         (*cursorGetOffsetOfField) (CXCursor);
    CXSourceLocation  (*getCursorLocation) (CXCursor);
    unsigned          (*locationIsFromMainFile) (CXSourceLocation);
    CXCursor          (*getCursorSemanticParent) (CXCursor);
    void             *Handle;       // dlopen handle
} CLANG_API;

#ifndef _WIN32
// Resolve one symbol; on any miss the whole load is treated as a failure.
static bool
Bind (void *pHandle, CONST char *pName, void **ppOut)
{
    *ppOut = dlsym (pHandle, pName);
    return *ppOut != nullptr;
}

static bool
LoadClang (CLANG_API *pApi)
{
    std::memset (pApi, 0, sizeof (*pApi));
    // Candidate libclang locations: the loader's default search, then common Homebrew /
    // LLVM / Xcode toolchain paths. First one that opens wins.
    static CONST char *CONST Paths[] = {
        "libclang.dylib",
        "/opt/homebrew/opt/llvm/lib/libclang.dylib",
        "/usr/local/opt/llvm/lib/libclang.dylib",
        "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/libclang.dylib",
        "libclang.so",
        "libclang.so.1"
    };
    void *pH = nullptr;
    for (CONST char *pPath : Paths) {
        pH = dlopen (pPath, RTLD_NOW | RTLD_LOCAL);
        if (pH != nullptr) { break; }
    }
    if (pH == nullptr) {
        return false;
    }
    bool Ok = true;
    Ok &= Bind (pH, "clang_createIndex", (void **) &pApi->createIndex);
    Ok &= Bind (pH, "clang_disposeIndex", (void **) &pApi->disposeIndex);
    Ok &= Bind (pH, "clang_parseTranslationUnit", (void **) &pApi->parseTU);
    Ok &= Bind (pH, "clang_disposeTranslationUnit", (void **) &pApi->disposeTU);
    Ok &= Bind (pH, "clang_getTranslationUnitCursor", (void **) &pApi->getTUCursor);
    Ok &= Bind (pH, "clang_visitChildren", (void **) &pApi->visitChildren);
    Ok &= Bind (pH, "clang_getCursorKind", (void **) &pApi->getCursorKind);
    Ok &= Bind (pH, "clang_getCursorSpelling", (void **) &pApi->getCursorSpelling);
    Ok &= Bind (pH, "clang_getCString", (void **) &pApi->getCString);
    Ok &= Bind (pH, "clang_disposeString", (void **) &pApi->disposeString);
    Ok &= Bind (pH, "clang_getCursorType", (void **) &pApi->getCursorType);
    Ok &= Bind (pH, "clang_getResultType", (void **) &pApi->getResultType);
    Ok &= Bind (pH, "clang_getTypeSpelling", (void **) &pApi->getTypeSpelling);
    Ok &= Bind (pH, "clang_Cursor_getNumArguments", (void **) &pApi->cursorGetNumArguments);
    Ok &= Bind (pH, "clang_Cursor_getArgument", (void **) &pApi->cursorGetArgument);
    Ok &= Bind (pH, "clang_isFunctionTypeVariadic", (void **) &pApi->isFunctionTypeVariadic);
    Ok &= Bind (pH, "clang_Type_getSizeOf", (void **) &pApi->typeGetSizeOf);
    Ok &= Bind (pH, "clang_Cursor_getOffsetOfField", (void **) &pApi->cursorGetOffsetOfField);
    Ok &= Bind (pH, "clang_getCursorLocation", (void **) &pApi->getCursorLocation);
    Ok &= Bind (pH, "clang_Location_isFromMainFile", (void **) &pApi->locationIsFromMainFile);
    Ok &= Bind (pH, "clang_getCursorSemanticParent", (void **) &pApi->getCursorSemanticParent);
    if (!Ok) {
        dlclose (pH);
        return false;
    }
    pApi->Handle = pH;
    return true;
}

// Convert a CXString to std::string and dispose it.
static std::string
TakeString (CLANG_API *pApi, CXString S)
{
    CONST char *p = pApi->getCString (S);
    std::string Out (p ? p : "");
    pApi->disposeString (S);
    return Out;
}

// Visit the fields of a struct cursor.
typedef struct _FIELD_CTX { CLANG_API *Api; HEADER_STRUCT *Struct; } FIELD_CTX;

static int
FieldVisitor (CXCursor C, CXCursor /*Parent*/, CXClientData pData)
{
    FIELD_CTX *pCtx = (FIELD_CTX *) pData;
    if (pCtx->Api->getCursorKind (C) == CXCursor_FieldDecl) {
        CXType Ty = pCtx->Api->getCursorType (C);
        HEADER_FIELD F;
        F.Name   = TakeString (pCtx->Api, pCtx->Api->getCursorSpelling (C));
        F.Type   = TakeString (pCtx->Api, pCtx->Api->getTypeSpelling (Ty));
        long long Bits = pCtx->Api->cursorGetOffsetOfField (C);
        long long Size = pCtx->Api->typeGetSizeOf (Ty);
        F.Offset = Bits >= 0 ? (UINT64) (Bits / 8) : 0;          // negative => error/dependent
        F.Size   = Size >= 0 ? (UINT64) Size : 0;
        pCtx->Struct->Fields.push_back (std::move (F));
    }
    return CXChildVisit_Continue;
}

typedef struct _TOP_CTX { CLANG_API *Api; HeaderParser *Self; std::vector<HEADER_FUNCTION> *Funcs; std::vector<HEADER_STRUCT> *Structs; } TOP_CTX;

// The fully-qualified name of a declaration: its own spelling with each enclosing namespace
// prepended ("gfx::draw"). extern "C" blocks contribute no scope (the function keeps C
// linkage and an unqualified name), so only Namespace parents are walked. This is what the
// catalog matches a C++ export's demangled name against.
static std::string
QualifiedName (CLANG_API *Api, CXCursor C)
{
    std::string Name = TakeString (Api, Api->getCursorSpelling (C));
    for (CXCursor P = Api->getCursorSemanticParent (C);
         Api->getCursorKind (P) == CXCursor_Namespace;
         P = Api->getCursorSemanticParent (P)) {
        std::string Ns = TakeString (Api, Api->getCursorSpelling (P));
        if (!Ns.empty ()) { Name = Ns + "::" + Name; }
    }
    return Name;
}

static int
TopVisitor (CXCursor C, CXCursor /*Parent*/, CXClientData pData)
{
    TOP_CTX *pCtx = (TOP_CTX *) pData;
    CLANG_API *Api = pCtx->Api;

    // Only decls written in the header itself, not in anything it #includes.
    if (!Api->locationIsFromMainFile (Api->getCursorLocation (C))) {
        return CXChildVisit_Continue;
    }
    int Kind = Api->getCursorKind (C);
    if (Kind == CXCursor_Namespace || Kind == CXCursor_LinkageSpec) {
        Api->visitChildren (C, TopVisitor, pCtx);            // descend into namespace / extern "C"
        return CXChildVisit_Continue;
    }
    if (Kind == CXCursor_FunctionDecl) {
        HEADER_FUNCTION Fn;
        CXType FnTy = Api->getCursorType (C);
        Fn.Name       = QualifiedName (Api, C);
        Fn.ReturnType = TakeString (Api, Api->getTypeSpelling (Api->getResultType (FnTy)));
        Fn.Variadic   = Api->isFunctionTypeVariadic (FnTy) != 0;
        int N = Api->cursorGetNumArguments (C);
        for (int I = 0; I < N; I++) {
            CXCursor Arg = Api->cursorGetArgument (C, (unsigned) I);
            HEADER_PARAM P;
            P.Name = TakeString (Api, Api->getCursorSpelling (Arg));
            P.Type = TakeString (Api, Api->getTypeSpelling (Api->getCursorType (Arg)));
            Fn.Params.push_back (std::move (P));
        }
        pCtx->Funcs->push_back (std::move (Fn));
    } else if (Kind == CXCursor_StructDecl || Kind == CXCursor_ClassDecl || Kind == CXCursor_UnionDecl) {
        HEADER_STRUCT St;
        St.Name = QualifiedName (Api, C);                    // qualified so it matches type spellings

        long long Size = Api->typeGetSizeOf (Api->getCursorType (C));
        St.Size = Size >= 0 ? (UINT64) Size : 0;
        FIELD_CTX FCtx{ Api, &St };
        Api->visitChildren (C, FieldVisitor, &FCtx);
        if (!St.Name.empty () || !St.Fields.empty ()) {
            pCtx->Structs->push_back (std::move (St));
        }
    }
    return CXChildVisit_Continue;
}
#endif // !_WIN32

bool
HeaderParser::ParseWithClang (CHAR8 CONST *pPath, CHAR8 CONST *CONST *ppArgs, UINT32 ArgCount, std::string * /*pError*/)
{
#ifndef _WIN32
    // Escape hatch: force the built-in scanner (tests both paths; lets a user opt out of
    // libclang if the host's copy is broken or undesired).
    if (std::getenv ("LIBCPU_NO_CLANG") != nullptr) {
        return false;
    }
    CLANG_API Api;
    if (!LoadClang (&Api)) {
        return false;                                           // libclang absent -> caller falls back
    }
    CXIndex Index = Api.createIndex (0, 0);

    // Unlike the clang driver, clang_parseTranslationUnit defaults an unknown/.hpp file to C,
    // which mis-parses namespaces and extern "C". If the caller did not force a language with
    // -x, parse as C++ -- a superset that handles C headers too -- so those decls are seen.
    std::vector<CHAR8 CONST *> Args (ppArgs, ppArgs + ArgCount);
    bool HasLang = false;
    for (UINT32 I = 0; I < ArgCount; I++) {
        if (std::strcmp (ppArgs[I], "-x") == 0) { HasLang = true; break; }
    }
    if (!HasLang) { Args.push_back ("-x"); Args.push_back ("c++"); }

    // CXTranslationUnit_DetailedPreprocessingRecord(0x01)|SkipFunctionBodies(0x40) keep it light.
    unsigned Options = 0x40;
    CXTranslationUnit Tu = Api.parseTU (Index, pPath, (CONST char *CONST *) Args.data (), (int) Args.size (),
                                        nullptr, 0, Options);
    if (Tu == nullptr) {
        Api.disposeIndex (Index);
        dlclose (Api.Handle);
        return false;
    }
    TOP_CTX Ctx{ &Api, this, &m_Functions, &m_Structs };
    Api.visitChildren (Api.getTUCursor (Tu), TopVisitor, &Ctx);
    Api.disposeTU (Tu);
    Api.disposeIndex (Index);
    dlclose (Api.Handle);
    m_UsedClang = true;
    return true;
#else
    (void) pPath; (void) ppArgs; (void) ArgCount;
    return false;
#endif
}

// ===========================================================================
//  Built-in fallback: a deliberately small scanner for when libclang is absent.
//  It handles one-line prototypes and simple `struct N { type field; ... };` blocks
//  after stripping comments and preprocessor lines. No macro expansion, no offsets.
// ===========================================================================

static bool
SlurpText (CHAR8 CONST *pPath, std::string *pOut)
{
    std::FILE *pf = std::fopen (pPath, "rb");
    if (pf == nullptr) {
        return false;
    }
    std::fseek (pf, 0, SEEK_END);
    long Len = std::ftell (pf);
    std::fseek (pf, 0, SEEK_SET);
    pOut->resize (Len > 0 ? (size_t) Len : 0);
    if (Len > 0) {
        size_t Got = std::fread (&(*pOut)[0], 1, (size_t) Len, pf);
        pOut->resize (Got);
    }
    std::fclose (pf);
    return true;
}

// Remove /* */ and // comments and any line whose first non-space char is '#'.
static std::string
StripNoise (std::string CONST &In)
{
    std::string Out;
    Out.reserve (In.size ());
    for (size_t I = 0; I < In.size (); I++) {
        if (I + 1 < In.size () && In[I] == '/' && In[I + 1] == '*') {
            I += 2;
            while (I + 1 < In.size () && !(In[I] == '*' && In[I + 1] == '/')) { I++; }
            I++;                                                // land on '/'
            continue;
        }
        if (I + 1 < In.size () && In[I] == '/' && In[I + 1] == '/') {
            while (I < In.size () && In[I] != '\n') { I++; }
        }
        Out.push_back (I < In.size () ? In[I] : '\n');
    }
    // Drop preprocessor lines.
    std::string Clean;
    size_t Start = 0;
    while (Start <= Out.size ()) {
        size_t Eol = Out.find ('\n', Start);
        std::string Line = Out.substr (Start, (Eol == std::string::npos ? Out.size () : Eol) - Start);
        size_t First = Line.find_first_not_of (" \t");
        if (First == std::string::npos || Line[First] != '#') {
            Clean += Line;
            Clean.push_back ('\n');
        }
        if (Eol == std::string::npos) { break; }
        Start = Eol + 1;
    }
    return Clean;
}

// Split "const char * name" into (type, name): the last identifier token is the name.
static void
SplitDecl (std::string CONST &Decl, std::string *pType, std::string *pName)
{
    size_t E = Decl.find_last_not_of (" \t\r\n");
    if (E == std::string::npos) { pType->clear (); pName->clear (); return; }
    size_t B = E;
    while (B > 0) {
        char c = Decl[B - 1];
        if (std::isalnum ((unsigned char) c) || c == '_') { B--; } else { break; }
    }
    *pName = Decl.substr (B, E - B + 1);
    std::string Ty = Decl.substr (0, B);
    size_t TB = Ty.find_first_not_of (" \t\r\n");
    size_t TE = Ty.find_last_not_of (" \t\r\n");
    *pType = (TB == std::string::npos) ? "" : Ty.substr (TB, TE - TB + 1);
}

bool
HeaderParser::ParseFallback (CHAR8 CONST *pPath, std::string *pError)
{
    std::string Raw;
    if (!SlurpText (pPath, &Raw)) {
        if (pError) { *pError = std::string ("cannot read '") + pPath + "'"; }
        return false;
    }
    std::string Text = StripNoise (Raw);

    // Structs: `struct NAME { ... };`
    size_t Pos = 0;
    while ((Pos = Text.find ("struct", Pos)) != std::string::npos) {
        size_t Brace = Text.find ('{', Pos);
        size_t Semi  = Text.find (';', Pos);
        if (Brace == std::string::npos || (Semi != std::string::npos && Semi < Brace)) {
            Pos += 6;
            continue;                                           // forward decl / variable, not a definition
        }
        std::string Tag = Text.substr (Pos + 6, Brace - (Pos + 6));
        size_t TB = Tag.find_first_not_of (" \t\r\n");
        size_t TE = Tag.find_last_not_of (" \t\r\n");
        HEADER_STRUCT St;
        St.Name = (TB == std::string::npos) ? "" : Tag.substr (TB, TE - TB + 1);
        St.Size = 0;
        size_t Close = Text.find ('}', Brace);
        std::string Body = Text.substr (Brace + 1, (Close == std::string::npos ? Text.size () : Close) - Brace - 1);
        size_t FS = 0;
        while (FS < Body.size ()) {
            size_t FE = Body.find (';', FS);
            if (FE == std::string::npos) { break; }
            std::string Member = Body.substr (FS, FE - FS);
            if (Member.find_first_not_of (" \t\r\n") != std::string::npos) {
                HEADER_FIELD F;
                F.Offset = 0;
                F.Size = 0;
                SplitDecl (Member, &F.Type, &F.Name);
                if (!F.Name.empty ()) { St.Fields.push_back (std::move (F)); }
            }
            FS = FE + 1;
        }
        m_Structs.push_back (std::move (St));
        Pos = (Close == std::string::npos) ? Text.size () : Close + 1;
    }

    // Functions: `rettype name ( params ) ;` at the top level (no '{' before the ';').
    Pos = 0;
    while (Pos < Text.size ()) {
        size_t Open = Text.find ('(', Pos);
        if (Open == std::string::npos) { break; }
        size_t Close = Text.find (')', Open);
        size_t Semi  = Text.find (';', Close == std::string::npos ? Open : Close);
        size_t Brace = Text.find ('{', Close == std::string::npos ? Open : Close);
        if (Close == std::string::npos || Semi == std::string::npos ||
            (Brace != std::string::npos && Brace < Semi)) {
            Pos = Open + 1;
            continue;                                           // a definition or malformed: skip
        }
        // The declarator is everything from the previous ';' or '}' up to '('.
        size_t Prev = Text.find_last_of (";}", Open);
        size_t Head = (Prev == std::string::npos) ? 0 : Prev + 1;
        std::string Decl = Text.substr (Head, Open - Head);
        std::string RetType, Name;
        SplitDecl (Decl, &RetType, &Name);
        // Reject control keywords / empty names that are not real prototypes.
        if (!Name.empty () && RetType.find_first_not_of (" \t\r\n") != std::string::npos &&
            Name != "if" && Name != "for" && Name != "while" && Name != "switch") {
            HEADER_FUNCTION Fn;
            Fn.Name = Name;
            Fn.ReturnType = RetType;
            Fn.Variadic = false;
            std::string Params = Text.substr (Open + 1, Close - Open - 1);
            size_t PS = 0;
            while (PS < Params.size ()) {
                size_t PC = Params.find (',', PS);
                std::string One = Params.substr (PS, (PC == std::string::npos ? Params.size () : PC) - PS);
                size_t NB = One.find_first_not_of (" \t\r\n");
                if (NB != std::string::npos) {
                    std::string Trim = One.substr (NB);
                    if (Trim == "...") {
                        Fn.Variadic = true;
                    } else if (Trim != "void" && Trim != "") {
                        HEADER_PARAM P;
                        SplitDecl (One, &P.Type, &P.Name);
                        if (P.Type.empty () && !P.Name.empty ()) { P.Type = P.Name; P.Name.clear (); }
                        Fn.Params.push_back (std::move (P));
                    }
                }
                if (PC == std::string::npos) { break; }
                PS = PC + 1;
            }
            m_Functions.push_back (std::move (Fn));
        }
        Pos = Semi + 1;
    }
    return true;
}

bool
HeaderParser::Parse (CHAR8 CONST *pPath, CHAR8 CONST *CONST *ppArgs, UINT32 ArgCount, std::string *pError)
{
    m_UsedClang = false;
    m_Functions.clear ();
    m_Structs.clear ();
    if (ParseWithClang (pPath, ppArgs, ArgCount, pError)) {
        return true;
    }
    return ParseFallback (pPath, pError);
}

} // namespace LibCPU
