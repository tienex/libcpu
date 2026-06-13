/** @file
  SymbolReader -- derive a library's exported-symbol set from a real toolchain artifact.

  Knowledge libraries (v2) are auto-derived from what the host and target toolchains
  actually ship. This reader supplies one half of that: the set of symbols a library
  exports, read from a .tbd text stub or any of a range of object/executable formats.

  The reader is a thin facade over a registry of per-format FormatReader objects: Read()
  slurps the file and asks each reader, in priority order, whether the bytes are its format;
  the first match extracts symbols into a SymbolSink. Adding a format is adding one reader
  to the registry -- the facade does not change.

  Binary-format structures are parsed inline (no Apple/ELF/PE headers) so the reader builds
  on toolchains without them (MSVC, OpenWatcom); it reports no symbols for formats it does
  not understand on those hosts.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_SYMBOLREADER_H
#define LIBCPU_SYMBOLREADER_H

#include "LibCPU/Base.h"
#include <string>
#include <vector>

namespace LibCPU {

//
// The artifact format a SymbolReader recognised. Formats whose reader implements Extract()
// yield symbols; the rest are detected (by magic) and reported with extraction not yet
// implemented -- the reader names the format rather than failing or guessing.
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
    SymbolFormatVms,            // OpenVMS image (Alpha/Itanium EIHD)
    SymbolFormatPlan9,          // Plan 9 a.out (32/64-bit, big-endian)
    SymbolFormatPdp10Sav,       // PDP-10 .SAV core image (TOPS-10/20)
    SymbolFormatGemdos,         // GEMDOS/DRI m68k (Atari ST/TT/Falcon, CP/M-68K) -- 0x601A
    SymbolFormatRdoff,          // NASM RDOFF2 ("RDOFF2") -- export records
    SymbolFormatBout,           // Intel i960 b.out (a.out variant, BMAGIC 0415)
    SymbolFormatOsfRose,        // OSF/ROSE (OSF/1 "Mach-O", MOH_MAGIC 0xbeefface)
    SymbolFormatCpmZ8000,       // CP/M-8000 (Zilog Z8000) command file (DRI, magic EE00..EE0B)
    // --- detection only ---
    SymbolFormatIeee695,        // IEEE-695 object (record stream, MB record 0xE0)
    SymbolFormatSrec,           // Motorola S-records (ASCII)
    SymbolFormatIntelHex,       // Intel HEX (ASCII)
    SymbolFormatTekHex,         // Tektronix Hex (ASCII)
    SymbolFormatVerilogHex,     // Verilog $readmemh (ASCII)
    SymbolFormatUefiTe,         // UEFI Terse Executable ("VZ")
    SymbolFormatPharLap,        // Phar-Lap DOS-extender ("MP"/"P2"/"P3")
    SymbolFormatX68000,         // Sharp X68000 .X ("HU")
    SymbolFormatAif,            // ARM Image Format (AIF)
    SymbolFormatOs360,          // IBM OS/360 object deck (EBCDIC ESD/TXT/RLD/END)
    SymbolFormatGoff,           // IBM GOFF object (X'03' PTV records)
    SymbolFormatDriCmd,         // DRI CMD command file (CP/M-86 / FlexOS 186/286 group descriptors)
    SymbolFormatGeos,           // GEOS geode (PC/GEOS & handheld, BE signature 0xC745C153)
    SymbolFormatGeosC64         // Commodore 64 GEOS file ("PRG/SEQ formatted GEOS file")
} SYMBOL_FORMAT;

// Human-readable name of a format (for listings / diagnostics).
CHAR8 CONST *SymbolFormatName (SYMBOL_FORMAT Format);

// Whether any registered reader extracts symbols for a format (vs. detection only).
bool SymbolFormatHasExtractor (SYMBOL_FORMAT Format);

//
// Accumulates the parsed result; handed to each format reader's Extract().
//
class SymbolSink {
public:
    void Add (std::string Name);                                       // de-duplicating append
    void SetInstallName (std::string Name) { if (m_InstallName.empty ()) { m_InstallName = std::move (Name); } }
    bool Has (std::string CONST &Name) CONST;

    std::string CONST              &InstallName () CONST { return m_InstallName; }
    std::vector<std::string> CONST &Symbols () CONST { return m_Symbols; }

private:
    std::string              m_InstallName;
    std::vector<std::string> m_Symbols;
};

//
// One object/executable format. Detect() recognises it by content; Extract() (optional --
// detection-only readers do not override it) harvests exported symbols into the sink.
//
class FormatReader {
public:
    virtual ~FormatReader () = default;
    virtual SYMBOL_FORMAT Format () CONST = 0;
    virtual bool          Detect (UINT8 CONST *pData, UINT64 Len) CONST = 0;
    virtual void          Extract (UINT8 CONST *pData, UINT64 Len, SymbolSink *pSink) CONST { (void) pData; (void) Len; (void) pSink; }
};

//
// Reads the exported-symbol set (and install name, when present) of one library artifact.
//
class SymbolReader {
public:
    // Read exported symbols from a file. Returns false (and fills *pError) when the file
    // cannot be read or no registered reader recognises it.
    bool Read (CHAR8 CONST *pPath, std::string *pError);

    SYMBOL_FORMAT                   Format () CONST { return m_Format; }
    std::string CONST              &InstallName () CONST { return m_Sink.InstallName (); }
    std::vector<std::string> CONST &Symbols () CONST { return m_Sink.Symbols (); }
    bool                            Has (std::string CONST &Name) CONST { return m_Sink.Has (Name); }

private:
    SYMBOL_FORMAT m_Format = SymbolFormatUnknown;
    SymbolSink    m_Sink;
};

} // namespace LibCPU

#endif // LIBCPU_SYMBOLREADER_H
