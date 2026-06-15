/** @file
  C++ member-call demo: the capstone of the C++ catalog pipeline (parse -> match -> bind ->
  INVOKE). Derives a catalog from a C++ class header plus its compiled library, then actually
  CALLS the constructor and member functions through their bound symbols, supplying a real
  "this" pointer as the implicit first argument (Itanium ABI). This proves three things that
  symbol-string matching alone cannot:

    1. the this-as-first-parameter model HeaderParser builds is ABI-correct (a non-static
       method really is callable as Ret(*)(ThisPtr, args...));
    2. the catalog's recorded struct layout gives the right storage size for the object;
    3. the two add() overloads were kept as DISTINCT symbols, so add(int) and add(double)
       reach DIFFERENT code -- a wrong/collapsed binding yields a wrong final value.

  Counter(10); add(5) [int] -> 15; add(2.5) [double] -> +25 -> 40; get() [const] -> 40.
**/
#ifndef LIBCPU_RUNMETHODCALL_H
#define LIBCPU_RUNMETHODCALL_H

#include "../core/SymbolReader.h"
#include "../core/HeaderParser.h"
#include "../core/DerivationEngine.h"
#include <cstdio>
#include <string>
#include <vector>
#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace LibCPU {

#ifndef _WIN32
// Resolve a catalog entity's recorded export symbol in an already-open library. The candidates
// mirror CatalogBindHost: the mangled symbol as recorded, then with a leading underscore
// stripped (dlsym omits the Mach-O '_'). Binding by the entity's own Symbol -- not its name --
// is what lets overloads that share a name reach their own distinct entry points.
static VOID *
ResolveEntity (VOID *pHandle, HOST_ENTITY CONST &E)
{
    std::string CONST &Sym = E.Symbol.empty () ? E.Name : E.Symbol;
    std::string Cands[2] = { Sym, (!Sym.empty () && Sym[0] == '_') ? Sym.substr (1) : Sym };
    for (std::string CONST &C : Cands) {
        if (C.empty ()) { continue; }
        if (VOID *p = dlsym (pHandle, C.c_str ())) { return p; }
    }
    return nullptr;
}

// Find an entity by qualified name and, when pParamType is non-null, by the type of its first
// explicit parameter (the one that is not the implicit "this") -- this is how an overload is
// selected. Returns nullptr when no entity matches.
static HOST_ENTITY CONST *
FindEntity (KnowledgeCatalog CONST &Catalog, CHAR8 CONST *pName, CHAR8 CONST *pParamType)
{
    for (HOST_ENTITY CONST &E : Catalog.Functions ()) {
        if (E.Name != pName) { continue; }
        if (pParamType == nullptr) { return &E; }
        for (HEADER_PARAM CONST &P : E.Params) {
            if (P.Name == "this") { continue; }
            if (P.Type == pParamType) { return &E; }
            break;                                       // only the first explicit parameter matters here
        }
    }
    return nullptr;
}
#endif

static inline int
RunMethodCallDemo (CHAR8 CONST *pHeader, CHAR8 CONST *pDylib)
{
    std::printf ("== C++ member call: %s x %s\n", pHeader, pDylib);
#ifdef _WIN32
    std::printf ("  dynamic binding unavailable on this platform\n");
    return 0;
#else
    HeaderParser Headers;
    SymbolReader Symbols;
    std::string Error;
    CHAR8 CONST *Args[] = { "-x", "c++" };
    if (!Headers.Parse (pHeader, Args, 2, &Error) || !Symbols.Read (pDylib, &Error)) {
        std::printf ("  derive inputs failed: %s\n", Error.c_str ());
        return 1;
    }
    KnowledgeCatalog Catalog;
    Catalog.Derive (Headers, Symbols);

    // The catalog records the object's storage size; allocate "this" from it.
    UINT64 Size = 0;
    for (HEADER_STRUCT CONST &S : Catalog.Structs ()) {
        if (S.Name == "ct::Counter") { Size = S.Size; break; }
    }
    if (Size == 0) {
        // No layout means the C++ class was not parsed (e.g. libclang absent) -- nothing to
        // invoke. Report without failing the suite; the symbol-level tests cover the rest.
        std::printf ("  ct::Counter layout not available (no libclang?); skipping invocation\n");
        return 0;
    }

    VOID *pHandle = dlopen (pDylib, RTLD_NOW | RTLD_LOCAL);
    if (pHandle == nullptr) {
        std::printf ("  cannot dlopen '%s'\n", pDylib);
        return 1;
    }

    HOST_ENTITY CONST *pCtor = FindEntity (Catalog, "ct::Counter::Counter", nullptr);
    HOST_ENTITY CONST *pAddI = FindEntity (Catalog, "ct::Counter::add", "int");
    HOST_ENTITY CONST *pAddD = FindEntity (Catalog, "ct::Counter::add", "double");
    HOST_ENTITY CONST *pGet  = FindEntity (Catalog, "ct::Counter::get", nullptr);
    if (pCtor == nullptr || pAddI == nullptr || pAddD == nullptr || pGet == nullptr) {
        std::printf ("  RESULT: FAIL  (catalog missing a Counter entity)\n");
        dlclose (pHandle);
        return 1;
    }

    VOID *pCtorFn = ResolveEntity (pHandle, *pCtor);
    VOID *pAddIFn = ResolveEntity (pHandle, *pAddI);
    VOID *pAddDFn = ResolveEntity (pHandle, *pAddD);
    VOID *pGetFn  = ResolveEntity (pHandle, *pGet);
    std::printf ("  bound Counter(int)=%s add(int)=%s add(double)=%s get()const=%s\n",
                 pCtor->Symbol.c_str (), pAddI->Symbol.c_str (), pAddD->Symbol.c_str (), pGet->Symbol.c_str ());
    if (pCtorFn == nullptr || pAddIFn == nullptr || pAddDFn == nullptr || pGetFn == nullptr) {
        std::printf ("  RESULT: FAIL  (a method symbol did not resolve)\n");
        dlclose (pHandle);
        return 1;
    }

    // Non-static member functions take "this" as an explicit leading pointer (Itanium ABI);
    // the const accessor takes a const this, which is the same pointer at the call boundary.
    typedef void (*CtorFn) (VOID *, int);
    typedef void (*AddIFn) (VOID *, int);
    typedef void (*AddDFn) (VOID *, double);
    typedef int  (*GetFn)  (VOID *);

    std::vector<UINT8> Obj ((size_t) Size, 0);
    VOID *pThis = Obj.data ();
    ((CtorFn) pCtorFn) (pThis, 10);          // Counter(10)
    ((AddIFn) pAddIFn) (pThis, 5);           // add(5)    -> 15  (int overload)
    ((AddDFn) pAddDFn) (pThis, 2.5);         // add(2.5)  -> +25 -> 40 (double overload)
    int Result = ((GetFn) pGetFn) (pThis);   // get()     -> 40

    bool Ok = (Result == 40);
    std::printf ("  Counter(10); add(5); add(2.5); get() = %d (expected 40)\n", Result);
    std::printf ("  RESULT: %s  (this-pointer member calls + per-overload symbol selection)\n",
                 Ok ? "PASS" : "FAIL");
    dlclose (pHandle);
    return Ok ? 0 : 1;
#endif
}

} // namespace LibCPU

#endif // LIBCPU_RUNMETHODCALL_H
