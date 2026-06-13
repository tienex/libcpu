/** @file
  SymbolReader -- a facade over a registry of per-format FormatReader objects. Each format
  (Mach-O, ELF, a.out, PE/COFF, WinCOFF, bigobj, OMF, NE, plus detection-only legacy
  formats) is one reader; Read() asks each in priority order and the first match extracts.
**/

#include "SymbolReader.h"
#include <cstdio>
#include <cstring>

namespace LibCPU {

// --- little/big-endian field reads -----------------------------------------

static UINT16 Be16 (UINT8 CONST *p) { return (UINT16) ((p[0] << 8) | p[1]); }
static UINT32 Be32 (UINT8 CONST *p) { return ((UINT32) p[0] << 24) | ((UINT32) p[1] << 16) | ((UINT32) p[2] << 8) | p[3]; }
static UINT16 Le16 (UINT8 CONST *p) { return (UINT16) (p[0] | (p[1] << 8)); }
static UINT32 Le32 (UINT8 CONST *p) { return (UINT32) (p[0] | (p[1] << 8) | (p[2] << 16) | ((UINT32) p[3] << 24)); }

static UINT32
Swap32 (UINT32 V)
{
    return ((V & 0x000000FFu) << 24) | ((V & 0x0000FF00u) << 8) |
           ((V & 0x00FF0000u) >> 8)  | ((V & 0xFF000000u) >> 24);
}

// --- SymbolSink -------------------------------------------------------------

void
SymbolSink::Add (std::string Name)
{
    if (Name.empty ()) {
        return;
    }
    for (std::string CONST &S : m_Symbols) {
        if (S == Name) { return; }              // de-duplicating append
    }
    m_Symbols.push_back (std::move (Name));
}

bool
SymbolSink::Has (std::string CONST &Name) CONST
{
    for (std::string CONST &S : m_Symbols) {
        if (S == Name) { return true; }
    }
    return false;
}

// --- format names / capability ---------------------------------------------

CHAR8 CONST *
SymbolFormatName (SYMBOL_FORMAT Format)
{
    switch (Format) {
        case SymbolFormatTbd:        return "tbd";
        case SymbolFormatMachO:      return "mach-o";
        case SymbolFormatElf:        return "elf";
        case SymbolFormatAOut:       return "a.out";
        case SymbolFormatPeCoff:     return "pe/coff";
        case SymbolFormatWinCoff:    return "wincoff";
        case SymbolFormatBigObjCoff: return "bigobj-coff";
        case SymbolFormatOmf:        return "omf";
        case SymbolFormatNe:         return "ne";
        case SymbolFormatXcoff:      return "xcoff";
        case SymbolFormatEcoff:      return "ecoff";
        case SymbolFormatMz:         return "mz";
        case SymbolFormatLe:         return "le";
        case SymbolFormatLx:         return "lx";
        case SymbolFormatMinixAOut:  return "minix-a.out";
        case SymbolFormatXenixXOut:  return "xenix-x.out";
        case SymbolFormatPef:        return "pef";
        case SymbolFormatCfm68k:     return "cfm-68k";
        case SymbolFormatCfmPpc:     return "cfm-ppc";
        case SymbolFormatSom:        return "som";
        case SymbolFormatAmigaHunk:  return "amiga-hunk";
        case SymbolFormatNlm:        return "nlm";
        case SymbolFormatVms:        return "vms";
        default:                     return "unknown";
    }
}

// ===========================================================================
//  Mach-O
// ===========================================================================

static UINT32 CONST MH_MAGIC_64  = 0xFEEDFACFu;
static UINT32 CONST MH_CIGAM_64  = 0xCFFAEDFEu;
static UINT32 CONST MH_MAGIC_32  = 0xFEEDFACEu;
static UINT32 CONST MH_CIGAM_32  = 0xCEFAEDFEu;
static UINT32 CONST FAT_MAGIC    = 0xCAFEBABEu;
static UINT32 CONST FAT_CIGAM    = 0xBEBAFECAu;
static UINT32 CONST FAT_MAGIC_64 = 0xCAFEBABFu;
static UINT32 CONST FAT_CIGAM_64 = 0xBFBAFECAu;
static UINT32 CONST LC_SYMTAB    = 0x2u;
static UINT32 CONST LC_ID_DYLIB  = 0xDu;
static UINT8  CONST N_STAB       = 0xE0u;
static UINT8  CONST N_EXT        = 0x01u;
static UINT8  CONST N_TYPE_MASK  = 0x0Eu;
static UINT8  CONST N_SECT       = 0x0Eu;

typedef struct _MACHO_HEADER_64 {
    UINT32 Magic, CpuType, CpuSubtype, FileType, NCmds, SizeOfCmds, Flags, Reserved;
} MACHO_HEADER_64;
typedef struct _MACHO_LOAD_COMMAND { UINT32 Cmd, CmdSize; } MACHO_LOAD_COMMAND;
typedef struct _MACHO_SYMTAB_COMMAND { UINT32 Cmd, CmdSize, SymOff, NSyms, StrOff, StrSize; } MACHO_SYMTAB_COMMAND;
typedef struct _MACHO_NLIST_64 { UINT32 StrX; UINT8 Type, Sect; UINT16 Desc; UINT64 Value; } MACHO_NLIST_64;

// Parse one thin, host-endian 64-bit Mach-O image and harvest its defined external symbols
// (and the install name from LC_ID_DYLIB). Offsets are image-relative, bounded by Len.
static void
HarvestMachO64 (UINT8 CONST *pImage, UINT64 Len, SymbolSink *pSink)
{
    if (Len < sizeof (MACHO_HEADER_64)) {
        return;
    }
    MACHO_HEADER_64 Hdr;
    std::memcpy (&Hdr, pImage, sizeof (Hdr));
    if (Hdr.Magic != MH_MAGIC_64) {
        return;                                              // byte-swapped image: unsupported host pairing
    }
    UINT64 Off = sizeof (MACHO_HEADER_64);
    for (UINT32 I = 0; I < Hdr.NCmds; I++) {
        if (Off + sizeof (MACHO_LOAD_COMMAND) > Len) {
            return;
        }
        MACHO_LOAD_COMMAND Lc;
        std::memcpy (&Lc, pImage + Off, sizeof (Lc));
        if (Lc.CmdSize < sizeof (MACHO_LOAD_COMMAND) || Off + Lc.CmdSize > Len) {
            return;
        }
        if (Lc.Cmd == LC_ID_DYLIB && Lc.CmdSize >= 16) {
            UINT32 NameOff;
            std::memcpy (&NameOff, pImage + Off + 8, 4);
            if (NameOff < Lc.CmdSize && Off + NameOff < Len) {
                CHAR8 CONST *pName = (CHAR8 CONST *) (pImage + Off + NameOff);
                pSink->SetInstallName (std::string (pName, strnlen (pName, (size_t) (Lc.CmdSize - NameOff))));
            }
        }
        if (Lc.Cmd == LC_SYMTAB && Lc.CmdSize >= sizeof (MACHO_SYMTAB_COMMAND)) {
            MACHO_SYMTAB_COMMAND St;
            std::memcpy (&St, pImage + Off, sizeof (St));
            UINT64 SymBytes = (UINT64) St.NSyms * sizeof (MACHO_NLIST_64);
            bool SymOk = St.SymOff <= Len && SymBytes <= Len - St.SymOff;
            bool StrOk = St.StrOff <= Len && St.StrSize <= Len - St.StrOff;
            if (SymOk && StrOk) {
                for (UINT32 N = 0; N < St.NSyms; N++) {
                    MACHO_NLIST_64 Sym;
                    std::memcpy (&Sym, pImage + St.SymOff + (UINT64) N * sizeof (Sym), sizeof (Sym));
                    bool Exported = (Sym.Type & N_STAB) == 0 && (Sym.Type & N_EXT) != 0 &&
                                    (Sym.Type & N_TYPE_MASK) == N_SECT;
                    if (Exported && Sym.StrX < St.StrSize) {
                        CHAR8 CONST *pName = (CHAR8 CONST *) (pImage + St.StrOff + Sym.StrX);
                        size_t MaxLen = (size_t) (St.StrSize - Sym.StrX);
                        size_t NameLen = strnlen (pName, MaxLen);
                        if (NameLen > 0 && NameLen < MaxLen) {
                            pSink->Add (std::string (pName, NameLen));
                        }
                    }
                }
            }
        }
        Off += Lc.CmdSize;
    }
}

namespace {

class MachOReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatMachO; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        if (Len < 4) { return false; }
        UINT32 M = Le32 (p);
        return M == MH_MAGIC_64 || M == MH_CIGAM_64 || M == MH_MAGIC_32 || M == MH_CIGAM_32 ||
               M == FAT_MAGIC || M == FAT_CIGAM || M == FAT_MAGIC_64 || M == FAT_CIGAM_64;
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT32 Magic = Le32 (p);
        if (Magic == MH_MAGIC_64) {
            HarvestMachO64 (p, Len, pSink);
            return;
        }
        if (Magic == FAT_MAGIC || Magic == FAT_CIGAM || Magic == FAT_MAGIC_64 || Magic == FAT_CIGAM_64) {
            bool Is64 = (Magic == FAT_MAGIC_64 || Magic == FAT_CIGAM_64);
            if (Len < 8) { return; }
            UINT32 NFat = Swap32 (Le32 (p + 4));             // fat header is big-endian
            UINT64 Off = 8;
            for (UINT32 I = 0; I < NFat; I++) {
                UINT64 SliceOff, SliceSize;
                if (!Is64) {
                    if (Off + 20 > Len) { break; }
                    SliceOff  = Swap32 (Le32 (p + Off + 8));
                    SliceSize = Swap32 (Le32 (p + Off + 12));
                    Off += 20;
                } else {
                    if (Off + 32 > Len) { break; }
                    SliceOff  = ((UINT64) Swap32 (Le32 (p + Off + 8)) << 32) | Swap32 (Le32 (p + Off + 12));
                    SliceSize = ((UINT64) Swap32 (Le32 (p + Off + 16)) << 32) | Swap32 (Le32 (p + Off + 20));
                    Off += 32;
                }
                if (SliceOff <= Len && SliceSize <= Len - SliceOff && SliceSize >= 4 &&
                    Le32 (p + SliceOff) == MH_MAGIC_64) {
                    HarvestMachO64 (p + SliceOff, SliceSize, pSink);
                }
            }
        }
    }
};

// ===========================================================================
//  ELF (section-header symbol table; 32/64, little/big endian)
// ===========================================================================

class ElfReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatElf; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        return Len >= 4 && p[0] == 0x7F && p[1] == 'E' && p[2] == 'L' && p[3] == 'F';
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        if (Len < 24) { return; }
        bool Is64 = p[4] == 2;
        bool Be   = p[5] == 2;
        auto U16 = [&] (UINT64 o) -> UINT32 { return (o + 2 > Len) ? 0 : (Be ? Be16 (p + o) : Le16 (p + o)); };
        auto U32 = [&] (UINT64 o) -> UINT32 { return (o + 4 > Len) ? 0 : (Be ? Be32 (p + o) : Le32 (p + o)); };
        auto U64 = [&] (UINT64 o) -> UINT64 {
            if (o + 8 > Len) { return 0; }
            return Be ? (((UINT64) Be32 (p + o) << 32) | Be32 (p + o + 4))
                      : ((UINT64) Le32 (p + o) | ((UINT64) Le32 (p + o + 4) << 32));
        };
        UINT64 ShOff = Is64 ? U64 (40) : U32 (32);
        UINT32 ShEnt = Is64 ? U16 (58) : U16 (46);
        UINT32 ShNum = Is64 ? U16 (60) : U16 (48);

        UINT64 SymOff = 0, SymSize = 0, SymEnt = 0, StrOff = 0, StrSize = 0;
        int Best = -1;
        for (UINT32 I = 0; I < ShNum; I++) {
            UINT64 Sh = ShOff + (UINT64) I * ShEnt;
            if (ShEnt == 0 || Sh + ShEnt > Len) { break; }
            UINT32 Type = U32 (Sh + 4);
            if (Type != 2 && Type != 11) { continue; }       // SHT_SYMTAB / SHT_DYNSYM
            UINT32 Link = Is64 ? U32 (Sh + 40) : U32 (Sh + 24);
            UINT64 Lsh  = ShOff + (UINT64) Link * ShEnt;
            if (Lsh + ShEnt > Len) { continue; }
            bool Prefer = (Type == 11);
            if (Best == -1 || Prefer) {
                SymOff  = Is64 ? U64 (Sh + 24) : U32 (Sh + 16);
                SymSize = Is64 ? U64 (Sh + 32) : U32 (Sh + 20);
                SymEnt  = Is64 ? U64 (Sh + 56) : U32 (Sh + 36);
                StrOff  = Is64 ? U64 (Lsh + 24) : U32 (Lsh + 16);
                StrSize = Is64 ? U64 (Lsh + 32) : U32 (Lsh + 20);
                Best = Prefer ? 1 : 0;
            }
        }
        if (SymEnt == 0 || SymOff > Len || SymSize > Len - SymOff || StrOff > Len || StrSize > Len - StrOff) {
            return;
        }
        for (UINT64 O = 0; O + SymEnt <= SymSize; O += SymEnt) {
            UINT64 E = SymOff + O;
            UINT32 NameX = U32 (E + 0);
            UINT8  Info  = Is64 ? p[E + 4] : p[E + 12];
            UINT16 Shndx = Is64 ? U16 (E + 6) : U16 (E + 14);
            UINT32 Bind  = Info >> 4;                         // STB_GLOBAL=1, STB_WEAK=2
            bool   Defined = Shndx != 0 && Shndx < 0xFF00;
            if ((Bind == 1 || Bind == 2) && Defined && NameX != 0 && StrOff + NameX < Len) {
                CHAR8 CONST *pName = (CHAR8 CONST *) (p + StrOff + NameX);
                size_t Max = (size_t) (StrSize - NameX);
                size_t NameLen = strnlen (pName, Max);
                if (NameLen > 0 && NameLen < Max) { pSink->Add (std::string (pName, NameLen)); }
            }
        }
    }
};

// ===========================================================================
//  classic a.out (32-byte exec header + nlist symbol table)
// ===========================================================================

class AOutReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatAOut; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        if (Len < 4) { return false; }
        UINT32 Lo = Le32 (p), Be = Be32 (p);
        UINT32 Info = (Lo & 0xFFFF) ? Lo : Be;
        UINT16 AMagic = (UINT16) (Info & 0xFFFF);
        return AMagic == 0x0107 || AMagic == 0x0108 || AMagic == 0x010B || AMagic == 0xCC;
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        if (Len < 32) { return; }
        UINT16 M = (UINT16) (Le32 (p) & 0xFFFF);
        bool Be = !(M == 0x0107 || M == 0x0108 || M == 0x010B || M == 0xCC);
        auto U32 = [&] (UINT64 o) -> UINT32 { return Be ? Be32 (p + o) : Le32 (p + o); };
        UINT32 ATextSize = U32 (4), ADataSize = U32 (8), ASyms = U32 (16);
        UINT32 ATrSize = U32 (24), ADrSize = U32 (28);
        UINT64 SymOff = 32 + (UINT64) ATextSize + ADataSize + ATrSize + ADrSize;
        UINT64 StrOff = SymOff + ASyms;
        if (SymOff > Len || ASyms > Len - SymOff || StrOff > Len) { return; }
        for (UINT64 O = 0; O + 12 <= ASyms; O += 12) {       // struct nlist = 12 bytes
            UINT64 E = SymOff + O;
            UINT32 NameX = U32 (E + 0);
            UINT8  Type  = p[E + 4];
            bool   Ext   = (Type & 0x01) != 0;               // N_EXT
            UINT8  Ty    = Type & 0x1E;                      // N_TYPE
            bool   Defined = (Ty == 0x04 || Ty == 0x06 || Ty == 0x08);   // TEXT/DATA/BSS
            if (Ext && Defined && NameX >= 4 && StrOff + NameX < Len) {
                CHAR8 CONST *pName = (CHAR8 CONST *) (p + StrOff + NameX);
                size_t NameLen = strnlen (pName, (size_t) (Len - (StrOff + NameX)));
                if (NameLen > 0) { pSink->Add (std::string (pName, NameLen)); }
            }
        }
    }
};

// ===========================================================================
//  COFF symbol table -- shared by PE, bare WinCOFF, and /bigobj
// ===========================================================================

// Harvest external defined symbols from a COFF symbol table. BigObj selects the 20-byte
// record layout (32-bit section number) used by Microsoft /bigobj vs the normal 18-byte;
// Be selects big-endian field reads (XCOFF). The same C_EXT/defined-section test applies.
static void
HarvestCoff (UINT8 CONST *p, UINT64 Len, UINT64 SymOff, UINT32 NSym, bool BigObj, bool Be, SymbolSink *pSink)
{
    auto W16 = [&] (UINT64 o) -> UINT16 { return Be ? Be16 (p + o) : Le16 (p + o); };
    auto W32 = [&] (UINT64 o) -> UINT32 { return Be ? Be32 (p + o) : Le32 (p + o); };
    UINT32 RecSize = BigObj ? 20 : 18;
    if (SymOff > Len || (UINT64) NSym * RecSize > Len - SymOff) { return; }
    UINT64 StrOff = SymOff + (UINT64) NSym * RecSize;
    for (UINT32 I = 0; I < NSym; ) {
        UINT64 E = SymOff + (UINT64) I * RecSize;
        INT32 Section;
        UINT8 Class, Aux;
        if (BigObj) {
            Section = (INT32) Le32 (p + E + 12);             // /bigobj is little-endian
            Class   = p[E + 18];
            Aux     = p[E + 19];
        } else {
            Section = (INT16) W16 (E + 12);
            Class   = p[E + 16];
            Aux     = p[E + 17];
        }
        if (Class == 2 && Section > 0) {                     // C_EXT / IMAGE_SYM_CLASS_EXTERNAL, defined
            std::string Name;
            if (W32 (E) == 0) {                              // long name: string-table offset
                UINT32 So = W32 (E + 4);
                if (StrOff + So < Len) {
                    CHAR8 CONST *pName = (CHAR8 CONST *) (p + StrOff + So);
                    Name.assign (pName, strnlen (pName, (size_t) (Len - (StrOff + So))));
                }
            } else {                                         // inline 8-byte name (NUL-padded)
                char Buf[9];
                std::memcpy (Buf, p + E, 8);
                Buf[8] = '\0';
                Name = Buf;
            }
            if (!Name.empty ()) { pSink->Add (std::move (Name)); }
        }
        I += 1 + Aux;                                        // skip auxiliary records
    }
}

class PeReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatPeCoff; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override { return MzSig (p, Len, 'P', 'E'); }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        if (Len < 0x40) { return; }
        UINT32 Pe = Le32 (p + 0x3C);
        if ((UINT64) Pe + 24 > Len) { return; }
        UINT64 Coff = (UINT64) Pe + 4;
        UINT16 NumSec = Le16 (p + Coff + 2);
        UINT32 SymPtr = Le32 (p + Coff + 8);
        UINT32 NSym   = Le32 (p + Coff + 12);
        UINT16 OptSize = Le16 (p + Coff + 16);

        if (SymPtr != 0 && NSym != 0) {                      // (1) COFF symbol table
            HarvestCoff (p, Len, SymPtr, NSym, /*BigObj=*/ false, /*Be=*/ false, pSink);
        }

        // (2) export directory -- stripped images / DLLs. Its fields are RVAs, translated
        // to file offsets through the section table.
        UINT64 Opt = Coff + 20;
        if (Opt + 2 > Len || OptSize < 96) { return; }
        UINT16 Magic = Le16 (p + Opt);
        UINT64 DDOff = 0;
        UINT32 NRva = 0;
        if (Magic == 0x10B) { NRva = Le32 (p + Opt + 92); DDOff = Opt + 96; }
        else if (Magic == 0x20B) { NRva = (Opt + 112 <= Len) ? Le32 (p + Opt + 108) : 0; DDOff = Opt + 112; }
        else { return; }
        if (NRva < 1 || DDOff + 8 > Len) { return; }
        UINT32 ExpRva = Le32 (p + DDOff + 0);
        if (ExpRva == 0) { return; }
        UINT64 SecOff = Opt + OptSize;
        auto Rva2Off = [&] (UINT32 Rva) -> UINT64 {
            for (UINT32 I = 0; I < NumSec; I++) {
                UINT64 Sh = SecOff + (UINT64) I * 40;
                if (Sh + 40 > Len) { break; }
                UINT32 VSize = Le32 (p + Sh + 8), VAddr = Le32 (p + Sh + 12);
                UINT32 RawSz = Le32 (p + Sh + 16), PRaw = Le32 (p + Sh + 20);
                UINT32 Span = VSize > RawSz ? VSize : RawSz;
                if (Rva >= VAddr && (UINT64) Rva < (UINT64) VAddr + Span) { return (UINT64) PRaw + (Rva - VAddr); }
            }
            return (UINT64) -1;
        };
        UINT64 ExpOff = Rva2Off (ExpRva);
        if (ExpOff == (UINT64) -1 || ExpOff + 40 > Len) { return; }
        UINT32 NNames   = Le32 (p + ExpOff + 24);
        UINT64 NamesOff = Rva2Off (Le32 (p + ExpOff + 32));
        if (NamesOff == (UINT64) -1 || NNames > (1u << 20)) { return; }
        for (UINT32 I = 0; I < NNames; I++) {
            UINT64 E = NamesOff + (UINT64) I * 4;
            if (E + 4 > Len) { break; }
            UINT64 NmOff = Rva2Off (Le32 (p + E));
            if (NmOff != (UINT64) -1 && NmOff < Len) {
                CHAR8 CONST *pName = (CHAR8 CONST *) (p + NmOff);
                size_t Max = (size_t) (Len - NmOff);
                size_t NameLen = strnlen (pName, Max);
                if (NameLen > 0 && NameLen < Max) { pSink->Add (std::string (pName, NameLen)); }
            }
        }
    }
    // Shared by the MZ-prefixed readers: does p look like MZ with a new-exe sig <c0><c1>?
    static bool MzSig (UINT8 CONST *p, UINT64 Len, char C0, char C1) {
        if (Len < 0x40 || p[0] != 'M' || p[1] != 'Z') { return false; }
        UINT32 e = Le32 (p + 0x3C);
        return e >= 0x40 && (UINT64) e + 2 <= Len && p[e] == (UINT8) C0 && p[e + 1] == (UINT8) C1;
    }
};

class WinCoffReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatWinCoff; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        if (Len < 2) { return false; }
        UINT16 M = Le16 (p);
        return M == 0x014C || M == 0x8664 || M == 0x01C0 || M == 0x01C4 || M == 0x0200;
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        if (Len < 20) { return; }
        UINT32 SymPtr = Le32 (p + 8), NSym = Le32 (p + 12);
        if (SymPtr != 0 && NSym != 0) { HarvestCoff (p, Len, SymPtr, NSym, /*BigObj=*/ false, /*Be=*/ false, pSink); }
    }
};

static UINT8 CONST BigObjMagic[16] = {
    0xC7, 0xA1, 0xBA, 0xD1, 0xEE, 0xBA, 0xA9, 0x4B,
    0xAF, 0x20, 0xFA, 0xF6, 0x6A, 0xA4, 0xDC, 0xB8
};

class BigObjReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatBigObjCoff; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        return Len >= 28 && Le16 (p) == 0x0000 && Le16 (p + 2) == 0xFFFF &&
               std::memcmp (p + 12, BigObjMagic, 16) == 0;
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        if (Len < 56) { return; }
        UINT32 SymPtr = Le32 (p + 48), NSym = Le32 (p + 52);
        if (SymPtr != 0 && NSym != 0) { HarvestCoff (p, Len, SymPtr, NSym, /*BigObj=*/ true, /*Be=*/ false, pSink); }
    }
};

// XCOFF (AIX): COFF layout but big-endian. f_symptr@8, f_nsyms@12; C_EXT symbols, 18-byte
// records, names inline or via the trailing string table.
class XcoffReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatXcoff; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        return Len >= 2 && (Be16 (p) == 0x01DF || Be16 (p) == 0x01F7);
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        if (Be16 (p) != 0x01DF || Len < 20) {               // 32-bit XCOFF; 64-bit (0x01F7) TODO
            return;
        }
        UINT32 SymPtr = Be32 (p + 8), NSym = Be32 (p + 12);
        if (SymPtr != 0 && NSym != 0) { HarvestCoff (p, Len, SymPtr, NSym, /*BigObj=*/ false, /*Be=*/ true, pSink); }
    }
};

// ===========================================================================
//  AmigaOS Hunk: HUNK_EXT blocks carry EXT_DEF exported definitions
// ===========================================================================

class AmigaHunkReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatAmigaHunk; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override { return Len >= 4 && Be32 (p) == 0x000003F3u; }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        auto R = [&] (UINT64 o) -> UINT32 { return o + 4 <= Len ? Be32 (p + o) : 0; };
        UINT64 o = 0;
        if (R (o) != 0x3F3) { return; }                      // HUNK_HEADER
        o += 4;
        for (;;) {                                           // resident library name list
            UINT32 n = R (o); o += 4;
            if (n == 0) { break; }
            o += (UINT64) n * 4;
            if (o > Len) { return; }
        }
        o += 4;                                              // table_size
        UINT32 First = R (o); o += 4;
        UINT32 Last  = R (o); o += 4;
        UINT32 NHunks = (Last >= First) ? (Last - First + 1) : 0;
        o += (UINT64) NHunks * 4;                            // hunk size table

        while (o + 4 <= Len) {
            UINT32 T = R (o) & 0x3FFFFFFFu; o += 4;          // strip memory-flag bits 30/31
            if (T == 0x3E9 || T == 0x3EA) {                  // HUNK_CODE / HUNK_DATA
                UINT32 N = R (o); o += 4; o += (UINT64) N * 4;
            } else if (T == 0x3EB) {                         // HUNK_BSS
                o += 4;
            } else if (T == 0x3EC) {                         // HUNK_RELOC32
                for (;;) {
                    UINT32 C = R (o); o += 4;
                    if (C == 0) { break; }
                    o += 4 + (UINT64) C * 4;                 // hunk number + offsets
                    if (o > Len) { return; }
                }
            } else if (T == 0x3EF) {                         // HUNK_EXT
                for (;;) {
                    UINT32 W = R (o); o += 4;
                    if (W == 0) { break; }
                    UINT32 Type = W >> 24;
                    UINT64 NameBytes = (UINT64) (W & 0xFFFFFFu) * 4;   // name length in longwords
                    if (o + NameBytes > Len) { return; }
                    if (Type < 128) {                        // a definition (value longword follows)
                        if (Type == 1) {                     // EXT_DEF -> exported
                            CHAR8 CONST *pName = (CHAR8 CONST *) (p + o);
                            pSink->Add (std::string (pName, strnlen (pName, (size_t) NameBytes)));
                        }
                        o += NameBytes + 4;
                    } else {                                 // a reference
                        o += NameBytes;
                        if (Type == 130) { o += 4; }         // EXT_COMMON: leading size longword
                        UINT32 C = R (o); o += 4;
                        o += (UINT64) C * 4;
                    }
                    if (o > Len) { return; }
                }
            } else if (T == 0x3F0) {                         // HUNK_SYMBOL (debug names)
                for (;;) {
                    UINT32 N = R (o); o += 4;
                    if (N == 0) { break; }
                    o += (UINT64) N * 4 + 4;                 // name + value
                    if (o > Len) { return; }
                }
            } else if (T == 0x3F2) {                         // HUNK_END
                continue;
            } else {
                break;                                       // unknown hunk -> stop
            }
        }
    }
};

// ===========================================================================
//  PEF (classic Mac OS) -- exports live in the loader section's export tables.
//  Shared by plain PEF and the CFM-68k / CFM-PPC architecture variants.
// ===========================================================================

class PefReader : public FormatReader {
public:
    PefReader (SYMBOL_FORMAT Fmt, CHAR8 CONST *pArch) : m_Fmt (Fmt), m_pArch (pArch) {}
    SYMBOL_FORMAT Format () CONST override { return m_Fmt; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        if (Len < 12 || std::memcmp (p, "Joy!", 4) != 0 || std::memcmp (p + 4, "peff", 4) != 0) { return false; }
        return m_pArch == nullptr || std::memcmp (p + 8, m_pArch, 4) == 0;
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        if (Len < 40) { return; }
        UINT16 SectionCount = Be16 (p + 32);
        for (UINT16 I = 0; I < SectionCount; I++) {
            UINT64 Sh = 40 + (UINT64) I * 28;
            if (Sh + 28 > Len) { return; }
            if (p[Sh + 24] != 4) { continue; }               // PEF loader section kind
            UINT64 L = Be32 (p + Sh + 20);                   // container offset of the loader section
            if (L + 56 > Len) { return; }
            UINT32 StringsOff  = Be32 (p + L + 40);
            UINT32 HashOff     = Be32 (p + L + 44);
            UINT32 HashPower   = Be32 (p + L + 48);
            UINT32 ExportCount = Be32 (p + L + 52);
            if (HashPower > 31 || ExportCount > (1u << 20)) { return; }
            // Loader layout: hash slot table (2^power * 4) | key table (count*4) | symbol table (count*10).
            UINT64 KeyOff = L + HashOff + (((UINT64) 1 << HashPower) * 4);
            UINT64 SymOff = KeyOff + (UINT64) ExportCount * 4;
            UINT64 StrBase = L + StringsOff;
            for (UINT32 S = 0; S < ExportCount; S++) {
                UINT64 Ke = KeyOff + (UINT64) S * 4;
                UINT64 Se = SymOff + (UINT64) S * 10;
                if (Ke + 4 > Len || Se + 4 > Len) { break; }
                UINT32 NameLen = Be32 (p + Ke) >> 16;        // PEF hash word: length<<16 | hash
                UINT32 NameOff = Be32 (p + Se) & 0xFFFFFFu;  // classAndName: class<<24 | name offset
                UINT64 Na = StrBase + NameOff;
                if (NameLen > 0 && Na + NameLen <= Len) {
                    pSink->Add (std::string ((CHAR8 CONST *) (p + Na), NameLen));
                }
            }
            return;
        }
    }
private:
    SYMBOL_FORMAT m_Fmt;
    CHAR8 CONST  *m_pArch;
};

// ===========================================================================
//  OMF (object/library): PUBDEF records list public (exported) names
// ===========================================================================

class OmfReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatOmf; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        if (Len < 3 || !(p[0] == 0x80 || p[0] == 0x82 || p[0] == 0xF0)) { return false; }
        UINT16 RecLen = Le16 (p + 1);
        return (UINT64) RecLen + 3 <= Len + 8;               // plausible record length
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        auto Index = [&] (UINT64 *pOff) -> UINT32 {          // OMF index: 1 or 2 bytes
            if (*pOff >= Len) { return 0; }
            UINT8 B = p[*pOff];
            if (B < 0x80) { (*pOff)++; return B; }
            if (*pOff + 1 >= Len) { (*pOff) += 1; return 0; }
            UINT32 V = ((UINT32) (B & 0x7F) << 8) | p[*pOff + 1];
            (*pOff) += 2;
            return V;
        };
        UINT64 Off = 0;
        while (Off + 3 <= Len) {
            UINT8  Type = p[Off];
            UINT16 RecLen = Le16 (p + Off + 1);
            UINT64 Data = Off + 3;
            UINT64 End  = Data + RecLen;
            if (RecLen < 1 || End > Len) { break; }
            UINT64 DataEnd = End - 1;                         // exclude the checksum byte
            if (Type == 0x90 || Type == 0x91) {              // PUBDEF (16-bit / 32-bit)
                bool Wide = (Type == 0x91);
                UINT64 Q = Data;
                Index (&Q);                                   // base group index
                UINT32 Seg = Index (&Q);                      // base segment index
                if (Seg == 0) { Q += 2; }                     // base frame present when seg == 0
                while (Q < DataEnd) {
                    UINT8 NameLen = p[Q++];
                    if (Q + NameLen > DataEnd) { break; }
                    if (NameLen > 0) { pSink->Add (std::string ((CHAR8 CONST *) (p + Q), NameLen)); }
                    Q += NameLen;
                    Q += Wide ? 4 : 2;                        // public offset
                    Index (&Q);                               // type index
                }
            }
            Off = End;
        }
    }
};

// ===========================================================================
//  NE (16-bit Windows/OS2): resident + non-resident name tables
// ===========================================================================

class NeReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatNe; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override { return PeReader::MzSig (p, Len, 'N', 'E'); }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT32 NeOff = Le32 (p + 0x3C);
        if ((UINT64) NeOff + 0x40 > Len || p[NeOff] != 'N' || p[NeOff + 1] != 'E') { return; }
        UINT16 ResRva = Le16 (p + NeOff + 0x26);              // relative to the NE header
        if (ResRva != 0) { NameTable (p, Len, (UINT64) NeOff + ResRva, pSink); }
        UINT32 NonResOff = Le32 (p + NeOff + 0x2C);           // a file offset
        if (NonResOff != 0 && NonResOff < Len) { NameTable (p, Len, NonResOff, pSink); }
    }
private:
    // { len(1), name[len], ordinal(2) } until len==0; entry 0 is the module name, not export.
    static void NameTable (UINT8 CONST *p, UINT64 Len, UINT64 Off, SymbolSink *pSink) {
        bool First = true;
        while (Off < Len) {
            UINT8 NameLen = p[Off++];
            if (NameLen == 0) { break; }
            if (Off + NameLen + 2 > Len) { break; }
            if (!First) { pSink->Add (std::string ((CHAR8 CONST *) (p + Off), NameLen)); }
            First = false;
            Off += NameLen + 2;
        }
    }
};

// ===========================================================================
//  .tbd (Apple text-based dylib stub)
// ===========================================================================

// Strip surrounding whitespace and a single layer of '...' or "..." quotes.
static std::string
Unquote (std::string S)
{
    size_t B = S.find_first_not_of (" \t\r\n");
    size_t E = S.find_last_not_of (" \t\r\n");
    if (B == std::string::npos) { return std::string (); }
    S = S.substr (B, E - B + 1);
    if (S.size () >= 2 && (S.front () == '\'' || S.front () == '"') && S.back () == S.front ()) {
        S = S.substr (1, S.size () - 2);
    }
    return S;
}

class TbdReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatTbd; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        std::string Text ((CHAR8 CONST *) p, (size_t) Len);
        return Text.find ("!tapi") != std::string::npos || Text.find ("tbd-version") != std::string::npos;
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        std::string Text ((CHAR8 CONST *) p, (size_t) Len);
        size_t In = Text.find ("install-name:");
        if (In != std::string::npos) {
            size_t Eol = Text.find ('\n', In);
            pSink->SetInstallName (Unquote (Text.substr (In + 13, (Eol == std::string::npos ? Text.size () : Eol) - (In + 13))));
        }
        size_t Pos = 0;
        while ((Pos = Text.find ("symbols:", Pos)) != std::string::npos) {
            size_t Open = Text.find ('[', Pos);
            if (Open == std::string::npos) { Pos += 8; continue; }
            size_t Close = Text.find (']', Open);
            if (Close == std::string::npos) { break; }
            std::string Inner = Text.substr (Open + 1, Close - Open - 1);
            size_t Start = 0;
            while (Start <= Inner.size ()) {
                size_t Comma = Inner.find (',', Start);
                pSink->Add (Unquote (Inner.substr (Start, (Comma == std::string::npos ? Inner.size () : Comma) - Start)));
                if (Comma == std::string::npos) { break; }
                Start = Comma + 1;
            }
            Pos = Close + 1;
        }
    }
};

// ===========================================================================
//  detection-only formats -- one reader instance per format, sharing a predicate
// ===========================================================================

class SignatureReader : public FormatReader {
public:
    typedef bool (*PredicateFn) (UINT8 CONST *p, UINT64 Len);
    SignatureReader (SYMBOL_FORMAT Fmt, PredicateFn Pred) : m_Fmt (Fmt), m_Pred (Pred) {}
    SYMBOL_FORMAT Format () CONST override { return m_Fmt; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override { return m_Pred (p, Len); }
private:
    SYMBOL_FORMAT m_Fmt;
    PredicateFn   m_Pred;
};

static bool DetectNlm (UINT8 CONST *p, UINT64 Len) { return Len >= 24 && std::memcmp (p, "NetWare Loadable Module", 23) == 0; }
static bool DetectVms (UINT8 CONST *p, UINT64 Len) {
    if (Len < 20 || Le32 (p + 8) != 3 || Le32 (p + 12) != 0) { return false; }
    UINT32 Size = Le32 (p);
    return Size >= 0x20 && Size <= 0x4000;
}
static bool DetectMinixAOut (UINT8 CONST *p, UINT64 Len) { return Len >= 4 && (Be16 (p) == 0x0301 || Le16 (p) == 0x0301); }
static bool DetectXenixXOut (UINT8 CONST *p, UINT64 Len) { return Len >= 4 && (Le16 (p) == 0x0206 || Be16 (p) == 0x0206); }
static bool DetectEcoff (UINT8 CONST *p, UINT64 Len) {
    if (Len < 2) { return false; }
    UINT16 M = Le16 (p);
    return M == 0x0162 || M == 0x0166 || M == 0x0140 || M == 0x0184;
}
static bool DetectSom (UINT8 CONST *p, UINT64 Len) {
    if (Len < 2) { return false; }
    UINT16 M = Be16 (p);
    return M == 0x0210 || M == 0x020B || M == 0x0211;
}
static bool DetectLe (UINT8 CONST *p, UINT64 Len) { return PeReader::MzSig (p, Len, 'L', 'E'); }
static bool DetectLx (UINT8 CONST *p, UINT64 Len) { return PeReader::MzSig (p, Len, 'L', 'X'); }
static bool DetectMz (UINT8 CONST *p, UINT64 Len) { return Len >= 2 && p[0] == 'M' && p[1] == 'Z'; }

} // anonymous namespace

// --- registry ---------------------------------------------------------------

// All readers, in priority order: specific magics before the catch-alls they could shadow
// (bigobj before WinCOFF; PE/NE/LE/LX before the bare MZ fallback; OMF's heuristic last).
static std::vector<FormatReader CONST *> CONST &
Registry ()
{
    static MachOReader     S_MachO;
    static ElfReader       S_Elf;
    static AmigaHunkReader S_AmigaHunk;
    static PefReader       S_CfmPpc (SymbolFormatCfmPpc, "pwpc");
    static PefReader       S_Cfm68k (SymbolFormatCfm68k, "m68k");
    static PefReader       S_Pef (SymbolFormatPef, nullptr);
    static SignatureReader S_Nlm (SymbolFormatNlm, DetectNlm);
    static SignatureReader S_Vms (SymbolFormatVms, DetectVms);
    static AOutReader      S_AOut;
    static SignatureReader S_MinixAOut (SymbolFormatMinixAOut, DetectMinixAOut);
    static SignatureReader S_XenixXOut (SymbolFormatXenixXOut, DetectXenixXOut);
    static BigObjReader    S_BigObj;
    static WinCoffReader   S_WinCoff;
    static SignatureReader S_Ecoff (SymbolFormatEcoff, DetectEcoff);
    static XcoffReader     S_Xcoff;
    static SignatureReader S_Som (SymbolFormatSom, DetectSom);
    static PeReader        S_Pe;
    static NeReader        S_Ne;
    static SignatureReader S_Le (SymbolFormatLe, DetectLe);
    static SignatureReader S_Lx (SymbolFormatLx, DetectLx);
    static SignatureReader S_Mz (SymbolFormatMz, DetectMz);
    static OmfReader       S_Omf;
    static TbdReader       S_Tbd;

    static std::vector<FormatReader CONST *> List = {
        &S_Tbd,                                              // text stub, tried first
        &S_MachO, &S_Elf, &S_AmigaHunk, &S_CfmPpc, &S_Cfm68k, &S_Pef, &S_Nlm, &S_Vms,
        &S_AOut, &S_MinixAOut, &S_XenixXOut,
        &S_BigObj, &S_WinCoff, &S_Ecoff, &S_Xcoff, &S_Som,
        &S_Pe, &S_Ne, &S_Le, &S_Lx, &S_Mz,
        &S_Omf                                               // record-type heuristic, tried last
    };
    return List;
}

bool
SymbolFormatHasExtractor (SYMBOL_FORMAT Format)
{
    // Exactly the formats whose reader class overrides Extract().
    switch (Format) {
        case SymbolFormatTbd:
        case SymbolFormatMachO:
        case SymbolFormatElf:
        case SymbolFormatAOut:
        case SymbolFormatPeCoff:
        case SymbolFormatWinCoff:
        case SymbolFormatBigObjCoff:
        case SymbolFormatOmf:
        case SymbolFormatNe:
        case SymbolFormatXcoff:
        case SymbolFormatAmigaHunk:
        case SymbolFormatPef:
        case SymbolFormatCfm68k:
        case SymbolFormatCfmPpc:
            return true;
        default:
            return false;
    }
}

// --- facade -----------------------------------------------------------------

static bool
SlurpFile (CHAR8 CONST *pPath, std::vector<UINT8> *pOut)
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
        size_t Got = std::fread (pOut->data (), 1, (size_t) Len, pf);
        pOut->resize (Got);
    }
    std::fclose (pf);
    return true;
}

bool
SymbolReader::Read (CHAR8 CONST *pPath, std::string *pError)
{
    m_Format = SymbolFormatUnknown;
    m_Sink = SymbolSink ();

    std::vector<UINT8> Data;
    if (!SlurpFile (pPath, &Data)) {
        if (pError) { *pError = std::string ("cannot read '") + pPath + "'"; }
        return false;
    }
    UINT8 CONST *p = Data.data ();
    UINT64 Len = Data.size ();

    for (FormatReader CONST *pReader : Registry ()) {
        if (pReader->Detect (p, Len)) {
            m_Format = pReader->Format ();
            pReader->Extract (p, Len, &m_Sink);              // no-op for detection-only readers
            return true;
        }
    }
    if (pError) { *pError = std::string ("'") + pPath + "': unrecognised object/executable format"; }
    return false;
}

} // namespace LibCPU
