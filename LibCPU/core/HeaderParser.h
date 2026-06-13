/** @file
  HeaderParser -- derive function signatures and struct layouts from a C header.

  This is the signature half of a knowledge entity (the symbol half comes from
  [SymbolReader]); joined, they describe how to marshal a guest call to a host one.

  Preferred path: **libclang, loaded dynamically** (dlopen at run time, never linked) so
  the framework keeps its no-hard-dependency stance -- if a libclang is present on the
  host it is used (full preprocessor, real types, exact field offsets); if not, a small
  built-in scanner handles simple prototypes and struct field lists. Call UsedClang() to
  learn which ran.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_HEADERPARSER_H
#define LIBCPU_HEADERPARSER_H

#include "LibCPU/Base.h"
#include <string>
#include <vector>

namespace LibCPU {

typedef struct _HEADER_PARAM {
    std::string Type;           // textual type, e.g. "const char *"
    std::string Name;           // parameter name (empty if the prototype omits it)
} HEADER_PARAM;

typedef struct _HEADER_FUNCTION {
    std::string               Name;
    std::string               ReturnType;
    std::vector<HEADER_PARAM>  Params;
    bool                       Variadic;
} HEADER_FUNCTION;

typedef struct _HEADER_FIELD {
    std::string Type;
    std::string Name;
    UINT64      Offset;         // byte offset within the struct (0 if unknown, e.g. fallback)
    UINT64      Size;           // byte size of the field (0 if unknown)
} HEADER_FIELD;

typedef struct _HEADER_STRUCT {
    std::string                Name;
    std::vector<HEADER_FIELD>   Fields;
    UINT64                      Size;       // sizeof the struct (0 if unknown)
} HEADER_STRUCT;

//
// Parses a C header into the function prototypes and struct layouts it declares.
//
class HeaderParser {
public:
    // Parse a C header. ppArgs/ArgCount are extra compiler arguments forwarded verbatim to
    // libclang (-I, -D, -std=, --target=, -isysroot, ...). Returns false (and fills *pError)
    // only on a hard failure (file unreadable, or libclang reported a fatal parse error and
    // no fallback could run). With libclang absent, the built-in scanner is used.
    bool Parse (CHAR8 CONST *pPath, CHAR8 CONST *CONST *ppArgs, UINT32 ArgCount, std::string *pError);

    bool                                UsedClang () CONST { return m_UsedClang; }
    std::vector<HEADER_FUNCTION> CONST &Functions () CONST { return m_Functions; }
    std::vector<HEADER_STRUCT> CONST   &Structs () CONST { return m_Structs; }

private:
    bool ParseWithClang (CHAR8 CONST *pPath, CHAR8 CONST *CONST *ppArgs, UINT32 ArgCount, std::string *pError);
    bool ParseFallback (CHAR8 CONST *pPath, std::string *pError);

    bool                         m_UsedClang = false;
    std::vector<HEADER_FUNCTION>  m_Functions;
    std::vector<HEADER_STRUCT>    m_Structs;
};

} // namespace LibCPU

#endif // LIBCPU_HEADERPARSER_H
