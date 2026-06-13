/** @file
  SymbolReader -- derive a library's exported-symbol set from a real toolchain artifact.

  Knowledge libraries (v2) are auto-derived from what the host and target toolchains
  actually ship, rather than hand-written. This reader supplies one half of that: the
  set of symbols a library exports, read from either

    - a **.tbd** text stub (Apple "text-based dylib"/tapi-tbd, what the macOS SDK ships
      in place of the real dylib, which lives in the dyld shared cache), or
    - a **Mach-O** binary (a real .dylib / bundle / executable on disk).

  The exported names, joined later with header signatures (the other half), yield the
  syscall->native and struct-conversion entities a knowledge library holds.

  Mach-O structures are declared inline here (not via the Apple <mach-o/...> headers) so
  the reader still compiles on toolchains without them (MSVC, OpenWatcom); it simply
  reports no symbols for formats it does not understand on those hosts.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_SYMBOLREADER_H
#define LIBCPU_SYMBOLREADER_H

#include "LibCPU/Base.h"
#include <string>
#include <vector>

namespace LibCPU {

//
// The artifact format a SymbolReader recognised. Formats up to and including PeCoff have
// symbol extraction; the rest are detected (by magic) and reported, with extraction not
// yet implemented -- the reader names the format rather than failing or guessing.
//
typedef enum _SYMBOL_FORMAT {
    SymbolFormatUnknown = 0,
    SymbolFormatTbd,            // Apple text-based dylib stub (.tbd, tapi-tbd YAML)
    SymbolFormatMachO,          // Mach-O binary (thin or fat)
    SymbolFormatElf,            // ELF object/shared object (32/64, LE/BE)
    SymbolFormatAOut,           // classic BSD/SysV a.out
    SymbolFormatPeCoff,         // PE (MZ + PE\0\0) -- COFF symbol table / export directory
    SymbolFormatWinCoff,        // bare Windows COFF object (machine magic, no MZ)
    SymbolFormatBigObjCoff,     // Microsoft /bigobj COFF (anon-object bigobj header)
    SymbolFormatOmf,            // OMF object/library (PUBDEF records)
    SymbolFormatNe,             // 16-bit Windows/OS2 New Executable (resident-name table)
    // --- detection only (recognised by magic; symbol extraction TODO) ---
    SymbolFormatXcoff,          // AIX XCOFF (32/64)
    SymbolFormatEcoff,          // MIPS/Alpha ECOFF
    SymbolFormatMz,             // DOS MZ executable (no new-exe header)
    SymbolFormatLe,             // VxD/OS2 Linear Executable
    SymbolFormatLx,             // OS/2 2.x Linear eXecutable
    SymbolFormatMinixAOut,      // MINIX a.out
    SymbolFormatXenixXOut,      // XENIX x.out
    SymbolFormatPef,            // classic Mac OS PEF container
    SymbolFormatCfm68k,         // PEF, m68k architecture
    SymbolFormatCfmPpc,         // PEF, PowerPC architecture
    SymbolFormatSom,            // HP-UX SOM
    SymbolFormatAmigaHunk,      // AmigaOS Hunk executable/library
    SymbolFormatNlm,            // NetWare Loadable Module
    SymbolFormatVms             // OpenVMS image (Alpha/Itanium EIHD)
} SYMBOL_FORMAT;

// Human-readable name of a format (for listings / diagnostics).
CHAR8 CONST *SymbolFormatName (SYMBOL_FORMAT Format);

// Whether the reader extracts symbols for a format (vs. detection only).
bool SymbolFormatHasExtractor (SYMBOL_FORMAT Format);

//
// Reads the exported-symbol set (and install name, when present) of one library artifact.
//
class SymbolReader {
public:
    // Read exported symbols from a file, auto-detecting .tbd (text) vs Mach-O (binary).
    // Returns false (and fills *pError) when the file is neither or cannot be read.
    bool Read (CHAR8 CONST *pPath, std::string *pError);

    SYMBOL_FORMAT                   Format () CONST { return m_Format; }
    std::string CONST              &InstallName () CONST { return m_InstallName; }
    std::vector<std::string> CONST &Symbols () CONST { return m_Symbols; }
    bool                            Has (std::string CONST &Name) CONST;

private:
    bool ReadTbd (std::string CONST &Text);
    bool ReadMachO (UINT8 CONST *pData, UINT64 Len, std::string *pError);
    bool ReadElf (UINT8 CONST *pData, UINT64 Len, std::string *pError);
    bool ReadAOut (UINT8 CONST *pData, UINT64 Len, std::string *pError);
    bool ReadPeCoff (UINT8 CONST *pData, UINT64 Len, std::string *pError);
    bool ReadOmf (UINT8 CONST *pData, UINT64 Len, std::string *pError);
    bool ReadNe (UINT8 CONST *pData, UINT64 Len, std::string *pError);
    bool ReadWinCoff (UINT8 CONST *pData, UINT64 Len, std::string *pError);
    bool ReadBigObjCoff (UINT8 CONST *pData, UINT64 Len, std::string *pError);
    // Harvest external defined symbols from a COFF symbol table. BigObj selects the 20-byte
    // record layout (32-bit section number) used by Microsoft /bigobj, vs the normal 18-byte.
    void HarvestCoff (UINT8 CONST *pData, UINT64 Len, UINT64 SymOff, UINT32 NSym, bool BigObj);
    void AddSymbol (std::string Name);          // de-duplicating append

    SYMBOL_FORMAT            m_Format = SymbolFormatUnknown;
    std::string             m_InstallName;
    std::vector<std::string> m_Symbols;
};

} // namespace LibCPU

#endif // LIBCPU_SYMBOLREADER_H
