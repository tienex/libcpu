/** @file
  DerivationEngine -- join header signatures with a library's exported symbols into a
  knowledge catalog, packaged as a ZOO/zstd archive.

  This is the step that ties the v2 pieces together: [HeaderParser] supplies the
  signatures (what a call looks like), [SymbolReader] supplies the ground truth (what the
  library actually ships), and the join yields a catalog of host entities -- each a real
  prototype tagged with whether the library exports it. Mismatches are themselves
  knowledge: a prototype with no export is a header-only declaration; the catalog records
  only what is callable. Struct layouts are carried through for argument marshalling.

  The catalog serialises to a [ZooArchive] (members: manifest / functions / structs), so a
  derived knowledge library is a real ZOO/zstd solid archive on disk.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_DERIVATIONENGINE_H
#define LIBCPU_DERIVATIONENGINE_H

#include "HeaderParser.h"
#include "SymbolReader.h"
#include "KnowledgeLibrary.h"        // HOST_BINDER / KN_FIELD_MAP ABI the dispatcher expects
#include <string>
#include <vector>

namespace LibCPU {

//
// One host call the catalog knows how to issue: a real prototype plus whether the backing
// library exports it (so the dispatcher can bind to it).
//
typedef struct _HOST_ENTITY {
    std::string               Name;
    std::string               ReturnType;
    std::vector<HEADER_PARAM>  Params;
    bool                       Variadic;
    bool                       Exported;     // a matching symbol is present in the library
    std::string               Symbol;       // the actual export symbol to bind (mangled, for C++)
} HOST_ENTITY;

//
// A derived knowledge catalog: the host entities and struct layouts, plus the library it
// was derived against. Serialises to / from a ZOO/zstd archive.
//
class KnowledgeCatalog {
public:
    // Join header prototypes with the library's exported symbols. A C name matches either
    // "name" (ELF) or "_name" (Mach-O underscore convention); both are tried.
    void Derive (HeaderParser CONST &Headers, SymbolReader CONST &Symbols);

    // Serialise the catalog to / parse it from a ZOO/zstd archive.
    bool Save (CHAR8 CONST *pPath, std::string *pError) CONST;
    bool Load (CHAR8 CONST *pPath, std::string *pError);

    std::string CONST                &Library () CONST { return m_Library; }
    std::vector<HOST_ENTITY> CONST   &Functions () CONST { return m_Functions; }
    std::vector<HEADER_STRUCT> CONST &Structs () CONST { return m_Structs; }
    UINT32                            ExportedCount () CONST;

private:
    std::string                m_Library;
    std::vector<HOST_ENTITY>    m_Functions;
    std::vector<HEADER_STRUCT>   m_Structs;
};

//
// A host-call binder backed by a KnowledgeCatalog, with the signature KnowledgeLibrary's
// RunWithSyscalls expects (HOST_BIND_FN). pCtx is a `KnowledgeCatalog CONST *`. Binds a
// name only if the catalog knows it AND marks it exported, resolving the native function
// pointer from the already-loaded image first (RTLD_DEFAULT, e.g. libc/libSystem) and then
// by dlopen'ing the catalog's recorded library. Returns nullptr if it cannot bind.
//
VOID *CatalogBindHost (VOID *pCtx, CHAR8 CONST *pName);

//
// The Layout hook of a catalog-backed HOST_BINDER: fills *pFields with the field map of
// struct pType, the host side from the catalog's derived layout and the guest side as the
// DOS convention (16-bit packed words). pCtx is a `KnowledgeCatalog CONST *`. Returns the
// field count (0 if the type is unknown).
//
UINT32 CatalogStructLayout (VOID *pCtx, CHAR8 CONST *pType, KN_FIELD_MAP *pFields, UINT32 Max,
                            UINT32 *pHostSize, UINT32 *pGuestSize);

// Build a HOST_BINDER wired to a catalog (Bind = CatalogBindHost, Layout = CatalogStructLayout).
HOST_BINDER MakeCatalogBinder (KnowledgeCatalog CONST *pCatalog);

// ===========================================================================
//  Target ABI table -> mapping derivation
//
//  The host side of a knowledge entity is derivable (the catalog), but the target side --
//  which trap vector/selector does what, and how its arguments sit in registers -- is OS
//  ABI knowledge that must be supplied as a compact "target ABI table". Joining that table
//  with the host catalog DERIVES the full target->host mapping (the <syscall> entries that
//  were hand-authored before): the host call is validated against real exports, and its
//  argument recipe is generated from the catalog SIGNATURE (e.g. a FILE* parameter with no
//  target operand is auto-filled with the stdout stream).
// ===========================================================================

// One target-side argument and how to read it from the guest.
//   Conv: "string" ($-terminated far string), "int", "struct:<type>", "outstruct:<type>".
typedef struct _TARGET_ARG {
    std::string Operand;       // "ds:dx", "al", ...
    std::string Conv;
} TARGET_ARG;

// One target system call, described semantically (no host detail except the chosen call).
typedef struct _TARGET_SYSCALL {
    UINT32                   Vector;
    std::string              Select;     // selector register ("ah"), or "" for vector-only
    UINT32                   Value;       // selector value
    std::string              Name;        // semantic label ("WriteString")
    std::string              HostCall;    // host function to bind to ("fputs")
    std::string              Result;      // target operand to receive the return ("" none)
    std::vector<TARGET_ARG>   Args;
} TARGET_SYSCALL;

// Load a target ABI table (line format, see test/dos.abi). Returns false (and fills *pError)
// on a read error.
bool LoadTargetAbi (CHAR8 CONST *pPath, std::vector<TARGET_SYSCALL> *pOut, std::string *pError);

// Outcome of a derivation, for reporting.
typedef struct _DERIVE_REPORT {
    UINT32 Total;        // target syscalls seen
    UINT32 Resolved;     // host call found AND exported in the catalog
    UINT32 Unresolved;   // host call missing/not exported (entry skipped)
} DERIVE_REPORT;

// Derive the target->host mapping from a target ABI table and the host catalog, appending
// the resolved entries to *pLibrary. The argument recipe for each call is generated from
// the catalog signature (positional alignment + FILE*-as-stdout auto-fill). Returns the
// report; entries whose host call is absent/unexported are counted Unresolved and skipped.
DERIVE_REPORT DeriveMapping (KnowledgeCatalog CONST &Catalog,
                             std::vector<TARGET_SYSCALL> CONST &Target,
                             KnowledgeLibrary *pLibrary);

} // namespace LibCPU

#endif // LIBCPU_DERIVATIONENGINE_H
