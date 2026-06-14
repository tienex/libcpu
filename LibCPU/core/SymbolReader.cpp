/** @file
  SymbolReader -- a facade over a registry of per-format FormatReader objects. Each format
  (Mach-O, ELF, a.out, PE/COFF, WinCOFF, bigobj, OMF, NE, plus detection-only legacy
  formats) is one reader; Read() asks each in priority order and the first match extracts.
**/

#include "SymbolReader.h"
#include <cstdio>
#include <cstring>
#include <map>

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
        case SymbolFormatPlan9:      return "plan9-a.out";
        case SymbolFormatPdp10Sav:   return "pdp10-sav";
        case SymbolFormatGemdos:     return "gemdos-68k (atari/cpm68k)";
        case SymbolFormatRdoff:      return "rdoff";
        case SymbolFormatUefiTe:     return "uefi-te";
        case SymbolFormatPharLap:    return "phar-lap";
        case SymbolFormatX68000:     return "x68000";
        case SymbolFormatAif:        return "arm-aif";
        case SymbolFormatOs360:      return "os360-obj";
        case SymbolFormatGoff:       return "goff";
        case SymbolFormatBout:       return "b.out";
        case SymbolFormatOsfRose:    return "osf-rose";
        case SymbolFormatCpmZ8000:   return "cpm-z8000";
        case SymbolFormatCpmVax:     return "cpm-vax";
        case SymbolFormatArchive:    return "ar (static library)";
        case SymbolFormatOmfLib:     return "omf-lib";
        case SymbolFormatCpmLbr:     return "cpm-lbr";
        case SymbolFormatAmigaLib:   return "amiga-lib";
        case SymbolFormatMwob:       return "metrowerks-mwob";
        case SymbolFormatMpw:        return "mpw-object";
        case SymbolFormatAof:        return "arm-aof";
        case SymbolFormatDriCmd:     return "dri-cmd (cp/m-86 / flexos)";
        case SymbolFormatGeos:       return "geos-geode";
        case SymbolFormatGeosC64:    return "geos-c64";
        case SymbolFormatPalmPrc:    return "palm-prc";
        case SymbolFormatPalmPdb:    return "palm-pdb";
        case SymbolFormatIeee695:    return "ieee-695";
        case SymbolFormatSrec:       return "srec";
        case SymbolFormatIntelHex:   return "intel-hex";
        case SymbolFormatTekHex:     return "tektronix-hex";
        case SymbolFormatVerilogHex: return "verilog-hex";
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
static UINT8  CONST N_UNDF       = 0x00u;

// Parse one thin, host-endian Mach-O image (32- or 64-bit) and harvest its defined external
// symbols (and the install name from LC_ID_DYLIB). The header (28 vs 32 bytes) and nlist
// stride (12 vs 16) are the only differences; the fields we read (ncmds@16, nlist n_strx@0,
// n_type@4) sit at the same offsets in both. Offsets are image-relative, bounded by Len.
static void
HarvestMachO (UINT8 CONST *pImage, UINT64 Len, bool Is64, SymbolSink *pSink)
{
    UINT64 HdrSize   = Is64 ? 32 : 28;
    UINT64 NlistSize = Is64 ? 16 : 12;
    if (Len < HdrSize) {
        return;
    }
    UINT32 NCmds = Le32 (pImage + 16);
    UINT64 Off = HdrSize;
    for (UINT32 I = 0; I < NCmds; I++) {
        if (Off + 8 > Len) {
            return;
        }
        UINT32 Cmd     = Le32 (pImage + Off);
        UINT32 CmdSize = Le32 (pImage + Off + 4);
        if (CmdSize < 8 || Off + CmdSize > Len) {
            return;
        }
        if (Cmd == LC_ID_DYLIB && CmdSize >= 16) {
            UINT32 NameOff = Le32 (pImage + Off + 8);
            if (NameOff < CmdSize && Off + NameOff < Len) {
                CHAR8 CONST *pName = (CHAR8 CONST *) (pImage + Off + NameOff);
                pSink->SetInstallName (std::string (pName, strnlen (pName, (size_t) (CmdSize - NameOff))));
            }
        }
        if (Cmd == LC_SYMTAB && CmdSize >= 24) {
            UINT32 SymOff = Le32 (pImage + Off + 8);
            UINT32 NSyms  = Le32 (pImage + Off + 12);
            UINT32 StrOff = Le32 (pImage + Off + 16);
            UINT32 StrSize = Le32 (pImage + Off + 20);
            UINT64 SymBytes = (UINT64) NSyms * NlistSize;
            bool SymOk = SymOff <= Len && SymBytes <= Len - SymOff;
            bool StrOk = StrOff <= Len && StrSize <= Len - StrOff;
            if (SymOk && StrOk) {
                for (UINT32 N = 0; N < NSyms; N++) {
                    UINT64 E = (UINT64) SymOff + (UINT64) N * NlistSize;
                    UINT32 StrX = Le32 (pImage + E);
                    UINT8  Type = pImage[E + 4];
                    // Defined in a section (N_SECT), or a common symbol -- a tentative
                    // definition (N_UNDF with a nonzero n_value, i.e. its size) which the
                    // linker allocates, and which nm reports as defined. n_value is the
                    // 8-byte (64) / 4-byte (32) field at E+8; test it without a Le64 helper.
                    bool Defined = (Type & N_TYPE_MASK) == N_SECT;
                    if (!Defined && (Type & N_TYPE_MASK) == N_UNDF) {
                        UINT32 VLen = Is64 ? 8 : 4;
                        for (UINT32 B = 0; B < VLen; B++) {
                            if (pImage[E + 8 + B] != 0) { Defined = true; break; }
                        }
                    }
                    bool Exported = (Type & N_STAB) == 0 && (Type & N_EXT) != 0 && Defined;
                    if (Exported && StrX < StrSize) {
                        CHAR8 CONST *pName = (CHAR8 CONST *) (pImage + StrOff + StrX);
                        size_t MaxLen = (size_t) (StrSize - StrX);
                        size_t NameLen = strnlen (pName, MaxLen);
                        if (NameLen > 0 && NameLen < MaxLen) {
                            pSink->Add (std::string (pName, NameLen));
                        }
                    }
                }
            }
        }
        Off += CmdSize;
    }
}

// The reader registry (defined after the reader classes). Forward-declared at file scope so
// ArReader, inside the anonymous namespace below, can dispatch each archive member to every
// other reader without an ambiguous second declaration.
static std::vector<FormatReader CONST *> CONST &Registry ();

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
        if (Magic == MH_MAGIC_64) { HarvestMachO (p, Len, /*Is64=*/ true, pSink); return; }
        if (Magic == MH_MAGIC_32) { HarvestMachO (p, Len, /*Is64=*/ false, pSink); return; }
        if (Magic == FAT_MAGIC || Magic == FAT_CIGAM || Magic == FAT_MAGIC_64 || Magic == FAT_CIGAM_64) {
            bool Fat64 = (Magic == FAT_MAGIC_64 || Magic == FAT_CIGAM_64);
            if (Len < 8) { return; }
            UINT32 NFat = Swap32 (Le32 (p + 4));             // fat header is big-endian
            UINT64 Off = 8;
            for (UINT32 I = 0; I < NFat; I++) {
                UINT64 SliceOff, SliceSize;
                if (!Fat64) {
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
                if (SliceOff <= Len && SliceSize <= Len - SliceOff && SliceSize >= 4) {
                    UINT32 SliceMagic = Le32 (p + SliceOff);
                    if (SliceMagic == MH_MAGIC_64) { HarvestMachO (p + SliceOff, SliceSize, true, pSink); }
                    else if (SliceMagic == MH_MAGIC_32) { HarvestMachO (p + SliceOff, SliceSize, false, pSink); }
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
            // Defined in a real section, or a common symbol (SHN_COMMON = 0xFFF2): a tentative
            // definition the linker allocates, which nm reports as defined -- mirroring the
            // Mach-O common-symbol (N_UNDF with nonzero n_value) case. SHN_UNDEF (0) and the
            // other reserved indices (0xFF00..0xFFFF, e.g. SHN_ABS) stay excluded.
            bool   Defined = (Shndx != 0 && Shndx < 0xFF00) || Shndx == 0xFFF2;
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
        if (Len < 20) { return; }
        if (Be16 (p) == 0x01DF) {                            // XCOFF32: standard COFF symtab, big-endian
            UINT32 SymPtr = Be32 (p + 8), NSym = Be32 (p + 12);
            if (SymPtr != 0 && NSym != 0) { HarvestCoff (p, Len, SymPtr, NSym, /*BigObj=*/ false, /*Be=*/ true, pSink); }
            return;
        }
        if (Be16 (p) == 0x01F7 && Len >= 24) {               // XCOFF64: 24-byte header, 18-byte syments
            UINT64 SymPtr = ((UINT64) Be32 (p + 8) << 32) | Be32 (p + 12);
            UINT32 NSym   = Be32 (p + 16);
            if (SymPtr == 0 || NSym == 0 || SymPtr > Len || (UINT64) NSym * 18 > Len - SymPtr) { return; }
            UINT64 StrOff = SymPtr + (UINT64) NSym * 18;     // names are ALWAYS string-table offsets here
            for (UINT32 I = 0; I < NSym; ) {
                UINT64 E = SymPtr + (UINT64) I * 18;
                INT16 Section = (INT16) Be16 (p + E + 12);   // n_value(8)@0, n_offset(4)@8, n_scnum(2)@12
                UINT8 Class   = p[E + 16];
                UINT8 Aux     = p[E + 17];
                if (Class == 2 && Section > 0) {
                    UINT32 So = Be32 (p + E + 8);
                    if (StrOff + So < Len) {
                        CHAR8 CONST *pName = (CHAR8 CONST *) (p + StrOff + So);
                        size_t NameLen = strnlen (pName, (size_t) (Len - (StrOff + So)));
                        if (NameLen > 0) { pSink->Add (std::string (pName, NameLen)); }
                    }
                }
                I += 1 + Aux;
            }
        }
    }
};

// ===========================================================================
//  AmigaOS Hunk: HUNK_EXT blocks carry EXT_DEF exported definitions
// ===========================================================================

// Walk an AmigaOS Hunk stream from offset `o`, harvesting HUNK_EXT EXT_DEF (exported) symbols.
// Handles the full hunk vocabulary so it can traverse the many units of an object file or link
// library, not just a single executable's hunks. Shared by the executable and library readers.
static void HunkWalk (UINT8 CONST *p, UINT64 Len, UINT64 o, SymbolSink *pSink) {
    auto R = [&] (UINT64 q) -> UINT32 { return q + 4 <= Len ? Be32 (p + q) : 0; };
    while (o + 4 <= Len) {
        UINT32 T = R (o) & 0x3FFFFFFFu; o += 4;              // strip memory-flag bits 30/31
        if (T == 0x3E7 || T == 0x3E8 || T == 0x3F1 || T == 0x3F5 || T == 0x3FB) {
            UINT32 N = R (o); o += 4; o += (UINT64) N * 4;   // UNIT/NAME/DEBUG/OVERLAY/INDEX: count + longwords
        } else if (T == 0x3E9 || T == 0x3EA) {              // HUNK_CODE / HUNK_DATA
            UINT32 N = R (o); o += 4; o += (UINT64) N * 4;
        } else if (T == 0x3EB) {                            // HUNK_BSS (size only)
            o += 4;
        } else if (T == 0x3EC || T == 0x3ED || T == 0x3EE ||
                   T == 0x3F7 || T == 0x3F8 || T == 0x3F9 ||
                   T == 0x3FD || T == 0x3FE) {              // RELOC32/16/8, DREL32/16/8, RELRELOC32, ABSRELOC16
            for (;;) {
                UINT32 C = R (o); o += 4;
                if (C == 0) { break; }
                o += 4 + (UINT64) C * 4;                     // hunk number + offset longwords
                if (o > Len) { return; }
            }
        } else if (T == 0x3FC) {                            // HUNK_RELOC32SHORT: word-counted
            UINT64 Start = o;
            for (;;) {
                UINT32 C = (o + 2 <= Len) ? Be16 (p + o) : 0; o += 2;
                if (C == 0) { break; }
                o += 2 + (UINT64) C * 2;                     // hunk number word + offset words
                if (o > Len) { return; }
            }
            if (((o - Start) & 2) != 0) { o += 2; }          // pad the block to a longword
        } else if (T == 0x3EF) {                            // HUNK_EXT
            for (;;) {
                UINT32 W = R (o); o += 4;
                if (W == 0) { break; }
                UINT32 Type = W >> 24;
                UINT64 NameBytes = (UINT64) (W & 0xFFFFFFu) * 4;   // name length in longwords
                if (o + NameBytes > Len) { return; }
                if (Type < 128) {                            // a definition (value longword follows)
                    if (Type == 1) {                         // EXT_DEF -> exported
                        CHAR8 CONST *pName = (CHAR8 CONST *) (p + o);
                        pSink->Add (std::string (pName, strnlen (pName, (size_t) NameBytes)));
                    }
                    o += NameBytes + 4;
                } else {                                     // a reference
                    o += NameBytes;
                    if (Type == 130) { o += 4; }             // EXT_ABSCOMMON: leading size longword
                    UINT32 C = R (o); o += 4;
                    o += (UINT64) C * 4;
                }
                if (o > Len) { return; }
            }
        } else if (T == 0x3F0) {                            // HUNK_SYMBOL (debug names)
            for (;;) {
                UINT32 N = R (o); o += 4;
                if (N == 0) { break; }
                o += (UINT64) N * 4 + 4;                     // name + value
                if (o > Len) { return; }
            }
        } else if (T == 0x3FA) {                            // HUNK_LIB: length longword, then a body of hunks
            o += 4;                                          // consume the length; walk the body inline
        } else if (T == 0x3F2 || T == 0x3F6) {              // HUNK_END / HUNK_BREAK
            continue;
        } else {
            break;                                           // unknown hunk -> stop
        }
    }
}

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
        HunkWalk (p, Len, o, pSink);
    }
};

// AmigaOS object file / link library. Both begin with HUNK_UNIT (0x3E7); an indexed library
// begins with HUNK_LIB (0x3FA). Either way the body is a sequence of hunks whose HUNK_EXT
// records carry the exported (EXT_DEF) symbols, harvested by the shared walk.
class AmigaLibReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatAmigaLib; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        if (Len < 4) { return false; }
        UINT32 M = Be32 (p);
        return M == 0x000003E7u || M == 0x000003FAu;         // HUNK_UNIT / HUNK_LIB
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override { HunkWalk (p, Len, 0, pSink); }
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

// Walk a stream of OMF records (an object module, or a whole .lib of concatenated modules)
// and harvest PUBDEF (public-definition) symbol names. Shared by the object and library readers.
static void HarvestOmf (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) {
    auto Index = [&] (UINT64 *pOff) -> UINT32 {              // OMF index: 1 or 2 bytes
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
        UINT64 DataEnd = End - 1;                             // exclude the checksum byte
        if (Type == 0x90 || Type == 0x91) {                  // PUBDEF (16-bit / 32-bit)
            bool Wide = (Type == 0x91);
            UINT64 Q = Data;
            Index (&Q);                                       // base group index
            UINT32 Seg = Index (&Q);                          // base segment index
            if (Seg == 0) { Q += 2; }                         // base frame present when seg == 0
            while (Q < DataEnd) {
                UINT8 NameLen = p[Q++];
                if (Q + NameLen > DataEnd) { break; }
                if (NameLen > 0) { pSink->Add (std::string ((CHAR8 CONST *) (p + Q), NameLen)); }
                Q += NameLen;
                Q += Wide ? 4 : 2;                            // public offset
                Index (&Q);                                   // type index
            }
        }
        Off = End;
    }
}

class OmfReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatOmf; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        if (Len < 3 || !(p[0] == 0x80 || p[0] == 0x82)) { return false; }   // THEADR / LHEADR
        UINT16 RecLen = Le16 (p + 1);
        return (UINT64) RecLen + 3 <= Len + 8;               // plausible record length
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override { HarvestOmf (p, Len, pSink); }
};

// OMF library (.lib): a LIBRARY-HEADER record (0xF0, giving the page size) followed by the
// member object modules and a dictionary. PUBDEF records appear in each module, so the same
// record walk harvests the library's whole exported-symbol set.
class OmfLibReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatOmfLib; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        if (Len < 3 || p[0] != 0xF0) { return false; }       // LIBHDR
        UINT16 RecLen = Le16 (p + 1);
        return (UINT64) RecLen + 3 <= Len + 8;               // page size - 3
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override { HarvestOmf (p, Len, pSink); }
};

// CP/M LU/LBR archive (.lbr): the library is a sequence of 128-byte sectors; the first member
// is the directory, a list of 32-byte entries (status @0: 00 active / FE deleted / FF unused;
// name[8] @1; ext[3] @9; index @12 = first sector LE; length @14 = sectors LE). The first
// entry describes the directory itself. There is no magic, so detection validates that first
// (control) entry. Members are named CP/M files; their names are reported as the contents.
class CpmLbrReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatCpmLbr; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        if (Len < 32) { return false; }
        if (p[0] != 0x00) { return false; }                  // control entry is active
        for (UINT32 i = 1; i <= 11; ++i) {                   // its name+ext are all spaces
            if (p[i] != 0x20) { return false; }
        }
        return Le16 (p + 12) == 0;                           // the directory starts at sector 0
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT16 DirSectors = Le16 (p + 14);                   // length of the directory member
        UINT64 DirEnd = (UINT64) DirSectors * 128;
        if (DirEnd == 0 || DirEnd > Len) { DirEnd = Len; }
        for (UINT64 O = 32; O + 32 <= DirEnd; O += 32) {     // entry 0 is the directory itself
            UINT8 Status = p[O];
            if (Status == 0xFF) { break; }                   // unused entries end the directory
            if (Status != 0x00) { continue; }                // skip deleted (FE) and others
            std::string Name ((CHAR8 CONST *) (p + O + 1), strnlen ((CHAR8 CONST *) (p + O + 1), 8));
            while (!Name.empty () && Name.back () == ' ') { Name.pop_back (); }
            std::string Ext ((CHAR8 CONST *) (p + O + 9), strnlen ((CHAR8 CONST *) (p + O + 9), 3));
            while (!Ext.empty () && Ext.back () == ' ') { Ext.pop_back (); }
            if (!Ext.empty ()) { Name += '.'; Name += Ext; }
            if (!Name.empty ()) { pSink->Add (std::move (Name)); }
        }
    }
};

// Parse one Metrowerks CodeWarrior object (magic 0xFEEDBEAD) occupying [base, base+olen).
// The 64-byte header gives the name table (nametable_offset @12, count @16) and the hunk
// stream length (obj_size @8); the hunk stream itself starts at object offset 64. Names are a
// table of [2-byte prefix][NUL-terminated string] entries with ids 1.., and the exported
// symbols are the name ids referenced by the Global* code/data hunks and Global entry points.
// Tags and per-hunk sizes follow libmetro's reverse-engineered m68k/PPC hunk format.
static void HarvestMwobObject (UINT8 CONST *p, UINT64 base, UINT64 olen, SymbolSink *pSink) {
    auto B32 = [&] (UINT64 o) -> UINT32 { return o + 4 <= olen ? Be32 (p + base + o) : 0; };
    auto B16 = [&] (UINT64 o) -> UINT16 { return o + 2 <= olen ? Be16 (p + base + o) : 0; };
    if (olen < 64 || B32 (0) != 0xFEEDBEADu) { return; }      // object magic
    UINT32 NameOff = B32 (12), NameCnt = B32 (16), ObjSize = B32 (8);

    // Name table: id 1.., each entry = 2-byte prefix + NUL-terminated string (count-1 names).
    std::map<UINT32, std::string> Names;
    if (NameOff != 0 && NameOff < olen && NameCnt >= 1) {
        UINT64 Q = NameOff;
        UINT32 Id = 1;
        for (UINT32 Remaining = NameCnt - 1; Remaining > 0 && Q + 2 < olen; --Remaining) {
            Q += 2;                                           // skip the 2-byte prefix (a hash)
            UINT64 S = Q;
            while (Q < olen && p[base + Q] != 0) { ++Q; }
            Names[Id++] = std::string ((CHAR8 CONST *) (p + base + S), (size_t) (Q - S));
            if (Q < olen) { ++Q; }                            // skip the NUL terminator
        }
    }

    // Walk the hunk stream (object offset 64) and collect the Global* / Global-entry name ids.
    std::vector<UINT32> Exported;
    UINT64 HunkEnd = (ObjSize != 0 && 64 + (UINT64) ObjSize <= olen) ? 64 + (UINT64) ObjSize : olen;
    UINT64 O = 64;
    while (O + 2 <= HunkEnd) {
        UINT16 Tag = B16 (O); O += 2;
        switch (Tag) {
            case 0x4568: O = HunkEnd; break;                  // HUNK_END
            case 0x4567: case 0x4576: case 0x4579: case 0x457A: case 0x457B:   // START / LIB_BREAK / DIFF*
            case 0x457E: case 0x457F: case 0x4580: case 0x4583:                // DEINIT / MULTIDEF / OVERLOAD / FORCE
            case 0x4588: case 0x4589: case 0x458A: case 0x4592: break;         // ILLEGAL* / CFM_EXPORT / CFM_INTERNAL
            case 0x4569: case 0x456A:                         // LOCAL_CODE / GLOBAL_CODE: name,size,off,off + size
            case 0x456D: case 0x456E: case 0x4571: case 0x4572: {   // (FAR)IDATA: same, + size data bytes
                if (Tag == 0x456A || Tag == 0x456E || Tag == 0x4572) { Exported.push_back (B32 (O)); }
                O += 16 + (UINT64) B32 (O + 4);
                break;
            }
            case 0x456B: case 0x456C: case 0x456F: case 0x4570:     // (FAR)UDATA: header only, no data bytes
                if (Tag == 0x456C || Tag == 0x4570) { Exported.push_back (B32 (O)); }
                O += 16;
                break;
            case 0x4577: case 0x4578:                         // GLOBAL_ENTRY / LOCAL_ENTRY: name + offset
                if (Tag == 0x4577) { Exported.push_back (B32 (O)); }
                O += 8;
                break;
            case 0x4573: case 0x4574: case 0x4575:            // XREF_* (imports): name + npairs + npairs*8
            case 0x4581: case 0x4582: case 0x4587: case 0x4595:
                O += 6 + (UINT64) B16 (O + 4) * 8;
                break;
            case 0x457D: case 0x4591: O += 4 + (UINT64) B32 (O); break;        // INIT_CODE / EXCEPTION_INFO: size + data
            case 0x457C: case 0x458B: O += 4; break;          // SEGMENT / CFM_IMPORT: name id
            case 0x4584: case 0x4585: case 0x4586: case 0x458D:                // GLOBAL data/x ptr/vec / SRC_BREAK
            case 0x458E: case 0x458F: case 0x4590: case 0x4593: O += 8; break; // LOCAL ptrs / METHOD_REF
            case 0x458C: case 0x4596: O += 16; break;         // CFM / WEAK import container
            case 0x4594: O += 8 + (UINT64) B16 (O + 6) * 8; break;             // METHOD_CLASS_DEF: + npairs*8
            default: O = HunkEnd; break;                      // unknown hunk -> stop this object
        }
    }
    for (UINT32 Id : Exported) {
        auto It = Names.find (Id);
        if (It != Names.end () && !It->second.empty ()) { pSink->Add (It->second); }
    }
}

// Metrowerks CodeWarrior object/library. A standalone object opens with magic 0xFEEDBEAD; a
// library opens with "MWOB" + a 4-character architecture ("M68K" / "PPC ") and indexes its
// member objects (num_files @24, then 20-byte file entries with data_start @12 / data_size @16,
// both relative to the library start). Either way the exported symbols are harvested from each
// object's Global* hunks against its name table.
class MwobReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatMwob; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        if (Len < 8) { return false; }
        UINT32 M = Be32 (p);
        if (M == 0xFEEDBEADu) { return true; }                          // standalone object
        if (M != 0x4D574F42u) { return false; }                         // 'MWOB' library
        UINT32 Arch = Be32 (p + 4);
        return Arch == 0x4D36384Bu || Arch == 0x50504320u;              // 'M68K' / 'PPC '
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        if (Be32 (p) == 0xFEEDBEADu) { HarvestMwobObject (p, 0, Len, pSink); return; }
        if (Len < 28) { return; }
        UINT32 NumFiles = Be32 (p + 24);
        UINT64 E = 28;
        for (UINT32 i = 0; i < NumFiles && E + 20 <= Len; ++i, E += 20) {
            UINT32 DataStart = Be32 (p + E + 12);
            UINT32 DataSize  = Be32 (p + E + 16);
            if (DataStart != 0 && (UINT64) DataStart + DataSize <= Len) {
                HarvestMwobObject (p, DataStart, DataSize, pSink);
            }
        }
    }
};

// MPW (Macintosh Programmer's Workshop) object file and library (classic Mac, big-endian).
// A record stream opening with a kFirst record (type 1, flags, 2-byte version 1..3). Names
// live in kDictionary (4) records (string-id -> name); kModule (5) and kEntryPoint (6) carry
// a kExtern (0x08) flag and a name string-id, so the exported symbols are the names of the
// extern modules and entry points, resolved against the dictionary. A library built by the
// MPW Lib tool is just more modules in the same stream, so one walk covers objects and
// libraries. Record layout per Retro68's ConvertObj. Names are resolved after the full walk
// so that forward references (module before its dictionary block) still resolve.
class MpwReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatMpw; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        return Len >= 4 && p[0] == 1 && p[2] == 0 && p[3] >= 1 && p[3] <= 3;   // kFirst + version 1..3
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        std::map<UINT32, std::string> Dict;
        std::vector<UINT32> Exported;
        UINT64 O = 0;
        while (O < Len) {
            UINT8 Rec = p[O];
            if (Rec == 0) { O += 1; }                         // kPad
            else if (Rec == 1) { O += 4; }                    // kFirst (header)
            else if (Rec == 2) { O += 2; }                    // kLast (a stream may concatenate objects)
            else if (Rec == 7) { O += 6; }                    // kSize (flags + long)
            else if (Rec == 11) { O += 8; }                   // kFilename (flags + word + long)
            else if (Rec == 5) {                              // kModule: flags, name, segment
                if (O + 6 > Len) { return; }
                if (p[O + 1] & 0x08) { Exported.push_back (Be16 (p + O + 2)); }   // kExtern
                O += 6;
            } else if (Rec == 6) {                            // kEntryPoint: flags, name, offset
                if (O + 8 > Len) { return; }
                if (p[O + 1] & 0x08) { Exported.push_back (Be16 (p + O + 2)); }
                O += 8;
            } else if (Rec == 4) {                            // kDictionary: flags, size, first-id, names
                if (O + 6 > Len) { return; }
                UINT32 Sz = Be16 (p + O + 2);
                UINT32 Id = Be16 (p + O + 4);
                if (Sz < 6 || O + Sz > Len) { return; }
                UINT64 Q = O + 6, End = O + Sz;
                while (Q < End) {
                    UINT8 N = p[Q++];
                    if (Q + N > End) { break; }
                    Dict[Id++] = std::string ((CHAR8 CONST *) (p + Q), N);
                    Q += N;
                }
                O += Sz;
            } else if (Rec == 3 || Rec == 8 || Rec == 9 || Rec == 10) {
                // kComment / kContent / kReference / kComputedRef: a size word gives the record length.
                if (O + 4 > Len) { return; }
                UINT32 Sz = Be16 (p + O + 2);
                if (Sz < 4 || O + Sz > Len) { return; }
                O += Sz;
            } else {
                return;                                       // unknown record -> stop
            }
        }
        for (UINT32 Id : Exported) {
            auto It = Dict.find (Id);
            if (It != Dict.end () && !It->second.empty ()) { pSink->Add (It->second); }
        }
    }
};

// ===========================================================================
//  NE (16-bit Windows/OS2): resident + non-resident name tables
// ===========================================================================

// A length-prefixed name table { len(1), name[len], ordinal(2) } until len==0; entry 0 is
// the module's own name (not an export). Shared by NE and LE/LX, which use the same format.
static void
ReadNameTable (UINT8 CONST *p, UINT64 Len, UINT64 Off, SymbolSink *pSink)
{
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

class NeReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatNe; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override { return PeReader::MzSig (p, Len, 'N', 'E'); }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT32 NeOff = Le32 (p + 0x3C);
        if ((UINT64) NeOff + 0x40 > Len || p[NeOff] != 'N' || p[NeOff + 1] != 'E') { return; }
        UINT16 ResRva = Le16 (p + NeOff + 0x26);              // resident table, relative to NE header
        if (ResRva != 0) { ReadNameTable (p, Len, (UINT64) NeOff + ResRva, pSink); }
        UINT32 NonResOff = Le32 (p + NeOff + 0x2C);           // non-resident table, a file offset
        if (NonResOff != 0 && NonResOff < Len) { ReadNameTable (p, Len, NonResOff, pSink); }
    }
};

// LE (VxD/OS2 16-bit) and LX (OS/2 32-bit) linear executables share a header whose resident
// name table offset is at +0x58 (relative to the LE/LX header) and non-resident at +0x88 (a
// file offset). Both use the same length-prefixed name-table format as NE.
class LeLxReader : public FormatReader {
public:
    LeLxReader (SYMBOL_FORMAT Fmt, char Sig1) : m_Fmt (Fmt), m_Sig1 (Sig1) {}
    SYMBOL_FORMAT Format () CONST override { return m_Fmt; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override { return PeReader::MzSig (p, Len, 'L', m_Sig1); }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT32 He = Le32 (p + 0x3C);
        if ((UINT64) He + 0x90 > Len || p[He] != 'L' || p[He + 1] != (UINT8) m_Sig1) { return; }
        UINT32 ResOff = Le32 (p + He + 0x58);                 // relative to the LE/LX header
        if (ResOff != 0 && (UINT64) He + ResOff < Len) { ReadNameTable (p, Len, (UINT64) He + ResOff, pSink); }
        UINT32 NonResOff = Le32 (p + He + 0x88);              // a file offset
        if (NonResOff != 0 && NonResOff < Len) { ReadNameTable (p, Len, NonResOff, pSink); }
    }
private:
    SYMBOL_FORMAT m_Fmt;
    char          m_Sig1;
};

// NLM (NetWare Loadable Module): a "publics" table of length-prefixed name + 4-byte address.
class NlmReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatNlm; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        return Len >= 24 && std::memcmp (p, "NetWare Loadable Module\032", 24) == 0;
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        // Fixed header: signature(24) version(4) moduleName(14) then code/data/... offsets;
        // publicsOffset@94, numberOfPublics@98 (little-endian).
        if (Len < 102) { return; }
        UINT32 PubOff = Le32 (p + 94);
        UINT32 NPub   = Le32 (p + 98);
        UINT64 Off = PubOff;
        for (UINT32 I = 0; I < NPub && Off < Len; I++) {
            UINT8 NameLen = p[Off++];
            if (Off + NameLen + 4 > Len) { break; }
            pSink->Add (std::string ((CHAR8 CONST *) (p + Off), NameLen));
            Off += (UINT64) NameLen + 4;                      // name + 4-byte address
        }
    }
};

// ECOFF (MIPS/Alpha): external symbols live behind the symbolic header (HDRR). 32-bit
// little-endian MIPS is implemented; big-endian MIPS and 64-bit Alpha (different HDRR/SYMR
// widths and bit order) are detected but not yet extracted.
class EcoffReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatEcoff; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        if (Len < 2) { return false; }
        UINT16 M = Le16 (p);
        return M == 0x0162 || M == 0x0166 || M == 0x0140 || M == 0x0184;
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        if (Len < 20) { return; }
        UINT16 Magic = Le16 (p);
        if (Magic != 0x0162 && Magic != 0x0166) { return; }  // 32-bit little-endian MIPS only
        UINT64 H = Le32 (p + 8);                              // f_symptr -> HDRR (symbolic header)
        if (H + 96 > Len) { return; }
        UINT32 SsExtOff = Le32 (p + H + 68);                  // external string table
        UINT32 IextMax  = Le32 (p + H + 88);                  // external symbol count
        UINT32 ExtOff   = Le32 (p + H + 92);                  // external symbol table
        for (UINT32 I = 0; I < IextMax; I++) {
            UINT64 B = (UINT64) ExtOff + (UINT64) I * 16;     // EXTR = flags(2) ifd(2) SYMR(12)
            if (B + 16 > Len) { break; }
            UINT32 Iss  = Le32 (p + B + 4);                   // SYMR.iss (into the external strings)
            UINT32 Bits = Le32 (p + B + 12);                  // little-endian: st:6 | sc:5 | ... | index:20
            UINT32 Sc   = (Bits >> 6) & 0x1F;                 // storage class
            // Defined external -- any storage class except scNil (0) and scUndefined (6, an
            // import). This keeps text/data/bss/sdata/sbss/rdata/abs/common, drops references.
            bool Defined = (Sc != 0 && Sc != 6);
            UINT64 Na = (UINT64) SsExtOff + Iss;
            if (Defined && Na < Len) {
                CHAR8 CONST *pName = (CHAR8 CONST *) (p + Na);
                size_t NameLen = strnlen (pName, (size_t) (Len - Na));
                if (NameLen > 0) { pSink->Add (std::string (pName, NameLen)); }
            }
        }
    }
};

// ===========================================================================
//  Plan 9 a.out (big-endian; 32- and 64-bit)
// ===========================================================================

class Plan9Reader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatPlan9; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        return Len >= 32 && IsMagic (Be32 (p) & ~0x00008000u);
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT32 Magic = Be32 (p);
        bool Is64 = (Magic & 0x00008000u) != 0;              // 64-bit images set the high-mag bit
        UINT64 HdrSize = Is64 ? 40 : 32;                     // 64-bit entry widens the header
        UINT32 ValSize = Is64 ? 8 : 4;
        if (Len < HdrSize) { return; }
        UINT64 SymOff = HdrSize + (UINT64) Be32 (p + 4) + Be32 (p + 8);   // after text + data
        UINT32 Syms   = Be32 (p + 16);
        if (SymOff > Len || Syms > Len - SymOff) { return; }
        UINT64 End = SymOff + Syms;
        UINT64 O = SymOff;
        while (O + ValSize + 1 <= End) {
            O += ValSize;                                    // symbol value
            char Type = (char) (p[O++] & 0x7F);              // type byte has the high bit set
            if (Type == 'z' || Type == 'Z') {                // source path: 16-bit numbers, 0-terminated
                while (O + 2 <= End && Be16 (p + O) != 0) { O += 2; }
                O += 2;
                continue;
            }
            UINT64 Ns = O;                                   // other types: NUL-terminated name
            while (O < End && p[O] != 0) { O++; }
            size_t NameLen = (size_t) (O - Ns);
            if (O < End) { O++; }
            // Uppercase types are global (exported): text/data/bss/leaf-text.
            if (NameLen > 0 && (Type == 'T' || Type == 'D' || Type == 'B' || Type == 'L')) {
                pSink->Add (std::string ((CHAR8 CONST *) (p + Ns), NameLen));
            }
        }
    }
private:
    static bool IsMagic (UINT32 B) {
        static UINT32 CONST M[] = { 520, 921, 1386, 2184, 2808, 3784 };   // 68020/386/sparc/mips/mips4k/alpha
        for (UINT32 V : M) { if (B == V) { return true; } }
        return false;
    }
};

// ===========================================================================
//  PDP-10 .SAV core image (TOPS-10/20). No byte magic: a 36-bit core image, here in the
//  "one word per 8 big-endian bytes, value in the low 36 bits" convention. Symbols are
//  reached via JOBSYM (the AOBJN pointer at location 0o116) -> pairs of {RADIX-50 name,
//  value} words. Best-effort for this convention; detection is a conservative structural
//  heuristic (a valid AOBJN symbol pointer at 0o116) since the format carries no magic.
// ===========================================================================

// Decode a 32-bit PDP-10 SQUOZE / RADIX-50 code into up to six characters.
static std::string
Radix50 (UINT32 Code)
{
    static CHAR8 CONST *CS = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.$%";   // 40-char PDP-10 set
    char Out[6];
    for (int I = 5; I >= 0; I--) { Out[I] = CS[Code % 40]; Code /= 40; }
    std::string S (Out, 6);
    while (!S.empty () && S.back () == ' ') { S.pop_back (); }
    return S;
}

class Pdp10SavReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatPdp10Sav; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        if (Len < (0116 + 2) * 8 || (Len % 8) != 0) { return false; }
        UINT64 Jw = Word (p, Len, 0116);
        UINT32 Left = (UINT32) ((Jw >> 18) & 0777777);       // -count (18-bit two's complement)
        UINT32 Right = (UINT32) (Jw & 0777777);              // symbol table word address
        return (Left & 0400000) != 0 && Right > 0 && Right < (Len / 8);
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT64 Jw = Word (p, Len, 0116);
        UINT32 Left = (UINT32) ((Jw >> 18) & 0777777);
        UINT32 Base = (UINT32) (Jw & 0777777);
        UINT32 Count = (0777777 + 1 - Left) & 0777777;       // word count (two words per symbol)
        if (Count == 0 || Count > 200000) { return; }
        for (UINT32 I = 0; I + 1 < Count; I += 2) {
            UINT64 NameW = Word (p, Len, (UINT64) Base + I);
            std::string Nm = Radix50 ((UINT32) (NameW & 0xFFFFFFFFu));   // low 32 bits = squoze code
            if (!Nm.empty ()) { pSink->Add (Nm); }
        }
    }
private:
    static UINT64 Word (UINT8 CONST *p, UINT64 Len, UINT64 Idx) {
        UINT64 O = Idx * 8;
        if (O + 8 > Len) { return 0; }
        UINT64 V = 0;
        for (int I = 0; I < 8; I++) { V = (V << 8) | p[O + I]; }
        return V & 0xFFFFFFFFFull;                           // low 36 bits
    }
};

// ===========================================================================
//  HP-UX SOM (PA-RISC), big-endian. Universal (exported) symbols in the symbol
//  dictionary; names in the symbol string table. (binutils struct header / record.)
// ===========================================================================

class SomReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatSom; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        if (Len < 128) { return false; }
        UINT16 Sid = Be16 (p);                               // system_id (PA-RISC version)
        UINT16 Am  = Be16 (p + 2);                           // a_magic
        bool SidOk = (Sid == 0x020B || Sid == 0x0210 || Sid == 0x0214);
        bool AmOk  = (Am == 0x0104 || Am == 0x0106 || Am == 0x0107 || Am == 0x0108 ||
                      Am == 0x010B || Am == 0x020B || Am == 0x0210 || Am == 0x0211);
        return SidOk && AmOk;
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT32 SymLoc = Be32 (p + 92);                       // symbol_location
        UINT32 SymTot = Be32 (p + 96);                       // symbol_total
        UINT32 StrLoc = Be32 (p + 108);                      // symbol_strings_location
        for (UINT32 I = 0; I < SymTot; I++) {
            UINT64 Rec = (UINT64) SymLoc + (UINT64) I * 20;  // symbol_dictionary_record = 20 bytes
            if (Rec + 20 > Len) { break; }
            UINT32 W = Be32 (p + Rec);                        // symbol_type:8 | symbol_scope:4 | ...
            UINT32 Scope = (W >> 20) & 0xF;
            UINT32 NameOff = Be32 (p + Rec + 4);              // offset into the symbol string table
            if (Scope == 3) {                                 // SS_UNIVERSAL -> exported
                UINT64 Na = (UINT64) StrLoc + NameOff;
                if (Na < Len) {
                    CHAR8 CONST *pName = (CHAR8 CONST *) (p + Na);
                    size_t NameLen = strnlen (pName, (size_t) (Len - Na));
                    if (NameLen > 0) { pSink->Add (std::string (pName, NameLen)); }
                }
            }
        }
    }
};

// ===========================================================================
//  MINIX a.out: 16-byte nlist with an inline 8-char name; external defined symbols
//  (n_sclass class bits == C_EXT, section bits == TEXT/DATA/BSS).
// ===========================================================================

class MinixReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatMinixAOut; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        return Len >= 32 && p[0] == 0x01 && p[1] == 0x03;    // a_magic bytes
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT8  HdrLen = p[4];                                // a_hdrlen
        UINT32 Text = Le32 (p + 8), Data = Le32 (p + 12), Syms = Le32 (p + 28);
        UINT64 SymOff = (UINT64) HdrLen + Text + Data;       // short header: no relocations
        if (SymOff > Len || Syms > Len - SymOff) { return; }
        for (UINT64 O = 0; O + 16 <= Syms; O += 16) {        // struct nlist = 16 bytes
            UINT64 E = SymOff + O;
            UINT8 SClass = p[E + 12];                         // n_name[8], n_value@8, n_sclass@12
            UINT8 Section = SClass & 0007;
            if ((SClass & 0370) == 0020 && Section >= 2 && Section <= 4) {  // C_EXT + TEXT/DATA/BSS
                char Buf[9];
                std::memcpy (Buf, p + E, 8);
                Buf[8] = '\0';
                std::string Nm (Buf, strnlen (Buf, 8));
                if (!Nm.empty ()) { pSink->Add (std::move (Nm)); }
            }
        }
    }
};

// ===========================================================================
//  GEMDOS / DRI m68k executable (Atari ST/TT/Falcon, CP/M-68K). Big-endian; the DRI
//  symbol table is 14-byte entries with an inline 8-char name (extended by a_lname
//  continuations), a 16-bit type, and a 32-bit value.
// ===========================================================================

class GemdosReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatGemdos; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        if (Len < 28) { return false; }
        UINT16 M = Be16 (p);
        return M == 0x601A || M == 0x601B;                   // PRG / CMD (contiguous / not)
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT32 TSize = Be32 (p + 2), DSize = Be32 (p + 6), SSize = Be32 (p + 14);
        UINT64 SymOff = 28 + (UINT64) TSize + DSize;
        if (SymOff > Len || SSize > Len - SymOff) { return; }
        UINT64 End = SymOff + SSize;
        for (UINT64 O = SymOff; O + 14 <= End; ) {
            UINT16 Type = Be16 (p + O + 8);
            char Buf[9];
            std::memcpy (Buf, p + O, 8);
            Buf[8] = '\0';
            std::string Nm (Buf, strnlen (Buf, 8));
            O += 14;
            // GST long names (a_lname, 0x0048): the next entry holds 14 more name chars.
            if ((Type & 0x0048) == 0x0048 && O + 14 <= End) {
                Nm.append ((CHAR8 CONST *) (p + O), strnlen ((CHAR8 CONST *) (p + O), 14));
                O += 14;
            }
            if (!Nm.empty () && (Type & 0x2000) != 0) {       // a_global -> exported
                pSink->Add (std::move (Nm));
            }
        }
    }
};

// ===========================================================================
//  CP/M-8000 (Zilog Z8000) command file (DRI). Big-endian; magic EE00..EE0B. The symbol
//  table is the LAST file section, so it starts at filelen - symtab_len; each entry is
//  12 bytes (seg/type/value + inline 8-char name); type 3 = Global Definition (exported).
// ===========================================================================

class CpmZ8000Reader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatCpmZ8000; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        if (Len < 16) { return false; }
        UINT16 M = Be16 (p);
        return M == 0xEE00 || M == 0xEE01 || M == 0xEE02 || M == 0xEE03 || M == 0xEE07 || M == 0xEE0B;
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT32 SymLen = Be32 (p + 0x0C);                     // length of symbol table; 0 = stripped
        if (SymLen == 0 || SymLen > Len) { return; }
        UINT64 SymOff = Len - SymLen;                        // symbol table is the last section
        for (UINT64 O = 0; O + 12 <= SymLen; O += 12) {      // entry = seg(1) type(1) value(2) name(8)
            UINT64 E = SymOff + O;
            if (p[E + 1] == 3) {                             // type 3 = Global Definition
                char Buf[9];
                std::memcpy (Buf, p + E + 4, 8);
                Buf[8] = '\0';
                std::string Nm (Buf, strnlen (Buf, 8));
                if (!Nm.empty ()) { pSink->Add (std::move (Nm)); }
            }
        }
    }
};

// ===========================================================================
//  GEOS geode (PC/GEOS and the GEOS-derived handheld/embedded OS). The file opens with the
//  big-endian signature 0xC745C153; a file-type byte sits at offset 0x28 (1 = executable,
//  2 = VM file, 3 = binary, 4 = directory label) and the geode's name is a NUL-terminated
//  string at offset 4. Geode entry points are exported by ordinal, not by a symbol-name
//  table, so the only name recovered is the geode name itself, reported as the install name.
// ===========================================================================

class GeosReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatGeos; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        return Len >= 0x29 && Be32 (p) == 0xC745C153u;        // signature + room for the file-type byte
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        (void) Len;
        // The name occupies the bytes between the signature and the file-type byte at 0x28.
        std::string Nm ((CHAR8 CONST *) (p + 4), strnlen ((CHAR8 CONST *) (p + 4), 0x28 - 4));
        if (!Nm.empty ()) { pSink->SetInstallName (std::move (Nm)); }
    }
};

// ===========================================================================
//  Palm OS database/resource file (.pdb / .prc) -- the container Palm "PIM" applications and
//  their data ship in (layout per PumpkinOS src/prcbuild/pdb.c). Big-endian; a 78-byte
//  header: name[32] @0, fileAttributes @32 (bit 0 = dmHdrAttrResDB -> resource DB / .prc),
//  type[4] @60, creator[4] @64, numberOfRecords @76; then the entry list (10-byte resource
//  entries for .prc, 8-byte record entries for .pdb). There is no symbol-name table -- code
//  lives in 'code' resources reached by ordinal -- so the database name is recovered as the
//  install name. The type and creator are always printable 4-char codes, which (with a
//  "the entry list fits the file" check) anchors detection of this otherwise magic-less file.
// ===========================================================================

static bool DetectPalm (UINT8 CONST *p, UINT64 Len, bool WantResDB) {
    if (Len < 78) { return false; }
    if (p[0] < 0x20 || p[0] > 0x7E) { return false; }         // database name begins with a printable char
    for (UINT32 i = 60; i < 68; ++i) {                        // type[4] @60 and creator[4] @64
        if (p[i] < 0x20 || p[i] > 0x7E) { return false; }
    }
    bool ResDB = (Be16 (p + 32) & 0x0001) != 0;               // dmHdrAttrResDB
    if (ResDB != WantResDB) { return false; }
    UINT16 NumRecs = Be16 (p + 76);
    UINT64 EntrySize = ResDB ? 10 : 8;                        // PDB_RESHDR vs PDB_RECHDR
    return 78 + (UINT64) NumRecs * EntrySize <= Len;          // the entry list must fit in the file
}

class PalmReader : public FormatReader {
public:
    PalmReader (SYMBOL_FORMAT Fmt, bool ResDB) : m_Fmt (Fmt), m_ResDB (ResDB) {}
    SYMBOL_FORMAT Format () CONST override { return m_Fmt; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override { return DetectPalm (p, Len, m_ResDB); }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        std::string Nm ((CHAR8 CONST *) p, strnlen ((CHAR8 CONST *) p, 32));   // name[32] @0
        if (!Nm.empty ()) { pSink->SetInstallName (std::move (Nm)); }
        if (!m_ResDB) { return; }                              // record DBs (.pdb) hold no resource table
        // Resource list: each 10-byte entry is type[4], id (UInt16), localChunkID (UInt32),
        // starting at PDB_HEADER (78). Report every resource as "type #id".
        UINT16 NumRecs = Be16 (p + 76);
        for (UINT32 i = 0; i < NumRecs; ++i) {
            UINT64 E = 78 + (UINT64) i * 10;
            if (E + 10 > Len) { break; }
            std::string Line ((CHAR8 CONST *) (p + E), 4);     // 4-char resource type
            Line += " #";
            Line += std::to_string (Be16 (p + E + 4));         // resource id
            pSink->Add (std::move (Line));
        }
    }
private:
    SYMBOL_FORMAT m_Fmt;
    bool          m_ResDB;
};

// ===========================================================================
//  CP/M-VAX command file. CP/M-VAX (Roger Ivie's CP/M-68K port to the VAX) reuses the DRI
//  CP/M-68K command-file layout, but on the little-endian VAX the fields -- including the
//  0x601A branch magic -- are stored little-endian, so the file opens with 1A 60 rather than
//  the 60 1A of big-endian GEMDOS/CP/M-68K, making the two byte-distinct. Header (28 bytes):
//  magic @0, text size @2, data size @6, bss @10, symbol-table size @14. The symbol table
//  follows text+data; each entry is 14 bytes (name[8], type, value), and type & 0x2000 marks
//  a global (exported) symbol -- mirroring CP/M-68K, little-endian throughout.
// ===========================================================================

class CpmVaxReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatCpmVax; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        return Len >= 28 && p[0] == 0x1A && p[1] == 0x60;     // 0x601A little-endian (vs 60 1A big-endian GEMDOS)
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT32 TSize = Le32 (p + 2), DSize = Le32 (p + 6), SSize = Le32 (p + 14);
        UINT64 SymOff = 28 + (UINT64) TSize + DSize;
        if (SymOff > Len || SSize > Len - SymOff) { return; }
        UINT64 End = SymOff + SSize;
        for (UINT64 O = SymOff; O + 14 <= End; ) {
            UINT16 Type = Le16 (p + O + 8);
            char Buf[9];
            std::memcpy (Buf, p + O, 8);
            Buf[8] = '\0';
            std::string Nm (Buf, strnlen (Buf, 8));
            O += 14;
            // GST long names (a_lname, 0x0048): the next entry holds 14 more name chars.
            if ((Type & 0x0048) == 0x0048 && O + 14 <= End) {
                Nm.append ((CHAR8 CONST *) (p + O), strnlen ((CHAR8 CONST *) (p + O), 14));
                O += 14;
            }
            if (!Nm.empty () && (Type & 0x2000) != 0) {       // a_global -> exported
                pSink->Add (std::move (Nm));
            }
        }
    }
};

// ===========================================================================
//  NASM RDOFF2: a record-based header; RDFREC_GLOBAL (type 3) records export symbols.
// ===========================================================================

class RdoffReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatRdoff; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        return Len >= 10 && std::memcmp (p, "RDOFF2", 6) == 0;
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT32 HdrLen = Le32 (p + 6);                        // header records follow the length
        UINT64 O = 10;
        UINT64 HdrEnd = O + HdrLen;
        if (HdrEnd > Len) { HdrEnd = Len; }
        while (O + 2 <= HdrEnd) {
            UINT8 Type = p[O];
            UINT8 RecLen = p[O + 1];
            UINT64 Data = O + 2;
            if (Data + RecLen > HdrEnd) { break; }
            if (Type == 3 && RecLen > 6) {                   // RDFREC_GLOBAL: flags,seg,offset,label
                CHAR8 CONST *pName = (CHAR8 CONST *) (p + Data + 6);
                size_t NameLen = strnlen (pName, (size_t) (RecLen - 6));
                if (NameLen > 0) { pSink->Add (std::string (pName, NameLen)); }
            }
            O = Data + RecLen;
        }
    }
};

// ===========================================================================
//  Intel i960 b.out: an a.out variant (BMAGIC 0415, 44-byte header, both endiannesses).
//  Standard 12-byte nlist with names in the trailing string table.
// ===========================================================================

class BoutReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatBout; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        return Len >= 44 && (Le32 (p) == 0x10D || Be32 (p) == 0x10D);
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        bool Be = (Le32 (p) != 0x10D);                       // big-endian b.out variant
        auto U32 = [&] (UINT64 o) -> UINT32 { return Be ? Be32 (p + o) : Le32 (p + o); };
        UINT32 Text = U32 (4), Data = U32 (8), Syms = U32 (16);
        UINT32 TrSize = U32 (24), DrSize = U32 (28);
        UINT64 SymOff = 44 + (UINT64) Text + Data + TrSize + DrSize;   // EXEC_BYTES_SIZE = 44
        UINT64 StrOff = SymOff + Syms;
        if (SymOff > Len || Syms > Len - SymOff || StrOff > Len) { return; }
        for (UINT64 O = 0; O + 12 <= Syms; O += 12) {        // struct nlist = 12 bytes
            UINT64 E = SymOff + O;
            UINT32 StrX = U32 (E);
            UINT8  Type = p[E + 4];
            bool   Ext  = (Type & 0x01) != 0;                // N_EXT
            UINT8  Ty   = Type & 0x1E;                       // N_TYPE
            bool   Defined = (Ty == 0x04 || Ty == 0x06 || Ty == 0x08);   // TEXT/DATA/BSS
            if (Ext && Defined && StrX != 0 && StrOff + StrX < Len) {
                CHAR8 CONST *pName = (CHAR8 CONST *) (p + StrOff + StrX);
                size_t NameLen = strnlen (pName, (size_t) (Len - (StrOff + StrX)));
                if (NameLen > 0) { pSink->Add (std::string (pName, NameLen)); }
            }
        }
    }
};

// ===========================================================================
//  OSF/ROSE (OSF/1 "Mach-O", Tru64 mach_o_format.h). MOH_MAGIC 0xbeefface; a 56-byte
//  mo_header, contiguous load commands (ldc_header_t = 16 bytes), with LDC_SYMBOLS (7)
//  pointing at symbol_info_t records (20 bytes) and LDC_STRINGS (3) at the name strings.
// ===========================================================================

class OsfRoseReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatOsfRose; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        return Len >= 56 && (Le32 (p) == 0xBEEFFACEu || Be32 (p) == 0xBEEFFACEu);
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        bool Be = (Le32 (p) != 0xBEEFFACEu);
        auto U16 = [&] (UINT64 o) -> UINT32 { return (o + 2 > Len) ? 0 : (Be ? Be16 (p + o) : Le16 (p + o)); };
        auto U32 = [&] (UINT64 o) -> UINT32 { return (o + 4 > Len) ? 0 : (Be ? Be32 (p + o) : Le32 (p + o)); };
        UINT32 FirstCmd = U32 (36);                          // moh_first_cmd_off
        UINT32 NCmds    = U32 (44);                          // moh_n_load_cmds
        UINT64 StrOff = 0, SymOff = 0;
        UINT32 NSyms = 0;
        UINT64 O = FirstCmd;
        for (UINT32 I = 0; I < NCmds && O + 16 <= Len; I++) {
            UINT32 CmdType = U32 (O);                         // ldci_cmd_type
            UINT32 CmdSize = U32 (O + 4);                     // ldci_cmd_size
            UINT32 SecOff  = U32 (O + 8);                     // ldci_section_off (from BOF)
            if (CmdType == 3) { StrOff = SecOff; }            // LDC_STRINGS
            else if (CmdType == 7) { SymOff = SecOff; NSyms = U32 (O + 20); }   // LDC_SYMBOLS: symc_nentries@20
            if (CmdSize < 16) { break; }
            O += CmdSize;
        }
        if (SymOff == 0 || NSyms == 0) { return; }
        for (UINT32 I = 0; I < NSyms; I++) {
            UINT64 E = SymOff + (UINT64) I * 20;              // symbol_info_t = 20 bytes
            if (E + 20 > Len) { break; }
            UINT32 NameRel = U32 (E + 0);                     // si_name (offset into strings)
            UINT16 Flags   = (UINT16) U16 (E + 8);            // si_flags
            if ((Flags & 0x1) != 0) {                         // SI_EXPORT_F -> exported
                UINT64 Na = StrOff + NameRel;
                if (Na < Len) {
                    CHAR8 CONST *pName = (CHAR8 CONST *) (p + Na);
                    size_t NameLen = strnlen (pName, (size_t) (Len - Na));
                    if (NameLen > 0) { pSink->Add (std::string (pName, NameLen)); }
                }
            }
        }
    }
};

// ===========================================================================
//  IBM OS/360 object deck: 80-byte ESD records; SD/LD items are defined symbols,
//  names are 8 EBCDIC characters.
// ===========================================================================

// Translate an EBCDIC (CP037) byte to ASCII for the symbol-name character set.
static char
EbcdicToAscii (UINT8 C)
{
    if (C >= 0xC1 && C <= 0xC9) { return (char) ('A' + (C - 0xC1)); }
    if (C >= 0xD1 && C <= 0xD9) { return (char) ('J' + (C - 0xD1)); }
    if (C >= 0xE2 && C <= 0xE9) { return (char) ('S' + (C - 0xE2)); }
    if (C >= 0x81 && C <= 0x89) { return (char) ('a' + (C - 0x81)); }
    if (C >= 0x91 && C <= 0x99) { return (char) ('j' + (C - 0x91)); }
    if (C >= 0xA2 && C <= 0xA9) { return (char) ('s' + (C - 0xA2)); }
    if (C >= 0xF0 && C <= 0xF9) { return (char) ('0' + (C - 0xF0)); }
    if (C == 0x40) { return ' '; }
    if (C == 0x5B) { return '$'; }
    if (C == 0x7B) { return '#'; }
    if (C == 0x7C) { return '@'; }
    if (C == 0x6D) { return '_'; }
    return '?';
}

class Os360Reader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatOs360; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        return Len >= 4 && p[0] == 0x02 && p[1] == 0xC5 && p[2] == 0xE2 && p[3] == 0xC4;   // 02 + "ESD"
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        for (UINT64 Rec = 0; Rec + 80 <= Len; Rec += 80) {   // object deck = 80-byte records
            if (p[Rec] != 0x02 || p[Rec + 1] != 0xC5 || p[Rec + 2] != 0xE2 || p[Rec + 3] != 0xC4) {
                continue;                                     // not an ESD record
            }
            UINT32 Count = Be16 (p + Rec + 10);               // bytes of ESD items (each 16)
            for (UINT32 O = 0; O + 16 <= Count && Rec + 16 + O + 16 <= Rec + 80; O += 16) {
                UINT64 It = Rec + 16 + O;
                UINT8 Type = p[It + 8];
                if (Type == 0x00 || Type == 0x01) {           // SD (CSECT) / LD (entry) -> defined
                    std::string Nm;
                    for (int I = 0; I < 8; I++) { Nm.push_back (EbcdicToAscii (p[It + I])); }
                    while (!Nm.empty () && Nm.back () == ' ') { Nm.pop_back (); }
                    if (!Nm.empty ()) { pSink->Add (std::move (Nm)); }
                }
            }
        }
    }
};

// ===========================================================================
//  OpenVMS image GST: EIHD -> EIHS -> global symbol table (EGSD records). Universal
//  symbols are EGSD__C_SYMG (8) subrecords with a length-prefixed name at +37.
// ===========================================================================

class VmsReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatVms; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        if (Len < 24 || Le32 (p + 8) != 3 || Le32 (p + 12) != 0) { return false; }
        UINT32 Size = Le32 (p);
        return Size >= 0x20 && Size <= 0x4000;
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT32 SymDbg = Le32 (p + 20);                        // EIHD__L_SYMDBGOFF -> EIHS
        if (SymDbg == 0 || (UINT64) SymDbg + 24 > Len) { return; }
        UINT32 GstVbn  = Le32 (p + SymDbg + 16);              // EIHS__L_GSTVBN
        UINT32 GstSize = Le32 (p + SymDbg + 20);              // EIHS__L_GSTSIZE
        if (GstVbn == 0) { return; }
        UINT64 O = (UINT64) (GstVbn - 1) * 512;               // VBN 1 == file offset 0
        UINT64 End = O + GstSize;
        if (End > Len) { End = Len; }
        while (O + 8 <= End) {                                // EGSD records: rectyp,recsiz,alignlw
            UINT16 RecSiz = Le16 (p + O + 2);
            if (RecSiz < 8 || O + RecSiz > End) { break; }
            UINT64 S = O + 8;                                 // subrecords
            while (S + 4 <= O + RecSiz) {
                UINT16 GsdTyp = Le16 (p + S);
                UINT16 GsdSiz = Le16 (p + S + 2);
                if (GsdSiz < 4) { break; }
                if (GsdTyp == 8 && S + 37 <= Len) {           // EGSD__C_SYMG (GST universal symbol)
                    UINT8 NamLng = p[S + 36];                 // EGST: namlng@36, name@37
                    if (S + 37 + NamLng <= Len) {
                        pSink->Add (std::string ((CHAR8 CONST *) (p + S + 37), NamLng));
                    }
                }
                S += GsdSiz;
            }
            O += RecSiz;
        }
    }
};

// ===========================================================================
//  XENIX x.out: header (32 bytes), XSYMPOS = sizeof(xexec) + x_ext + x_text + x_data.
//  x_relsym & 0x0f selects the symbol format; XR_SAOUT (2) is the nlist form -- a 16-byte
//  record with an inline 8-char name, classic a.out n_type bits.
// ===========================================================================

class XenixReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatXenixXOut; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override { return Len >= 32 && Le16 (p) == 0x0206; }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT16 Ext  = Le16 (p + 2);                          // x_ext
        UINT32 Text = Le32 (p + 4), Data = Le32 (p + 8), Syms = Le32 (p + 16);
        UINT8  RelSym = p[29];                               // x_relsym
        if ((RelSym & 0x0F) != 2) { return; }                // only XR_SAOUT (nlist) is implemented
        UINT64 SymOff = 32 + (UINT64) Ext + Text + Data;     // sizeof(struct xexec) == 32
        if (SymOff > Len || Syms > Len - SymOff) { return; }
        for (UINT64 O = 0; O + 16 <= Syms; O += 16) {        // nlist: n_name[8], n_type(4), n_value(4)
            UINT64 E = SymOff + O;
            UINT32 Type = Le32 (p + E + 8);
            bool   Ext_ = (Type & 0x01) != 0;                // N_EXT
            UINT8  Ty   = Type & 0x1E;                       // N_TYPE
            bool   Defined = (Ty == 0x04 || Ty == 0x06 || Ty == 0x08);   // TEXT/DATA/BSS
            if (Ext_ && Defined) {
                char Buf[9];
                std::memcpy (Buf, p + E, 8);
                Buf[8] = '\0';
                std::string Nm (Buf, strnlen (Buf, 8));
                if (!Nm.empty ()) { pSink->Add (std::move (Nm)); }
            }
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
//  Unix "ar" archive (static library, .a). Magic "!<arch>\n", then a sequence of members,
//  each a 60-byte header (name[16], mtime[12], uid[6], gid[6], mode[8], size[10], "`\n")
//  followed by `size` bytes of member data padded to a 2-byte boundary. Members are object
//  files, so symbols are harvested by recursing each member back through the registry; the
//  ranlib index (__.SYMDEF, GNU "/"/"//") simply matches no reader and is skipped. macOS
//  archives use BSD extended names ("#1/NN"), where the real filename occupies the first NN
//  bytes of the member data and the object begins after it.
// ===========================================================================

static UINT64 ArDecimal (UINT8 CONST *p, UINT32 n) {          // space-padded ASCII decimal field
    UINT64 v = 0;
    for (UINT32 i = 0; i < n; ++i) {
        if (p[i] < '0' || p[i] > '9') { break; }
        v = v * 10 + (UINT64) (p[i] - '0');
    }
    return v;
}

class ArReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatArchive; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        return Len >= 8 && std::memcmp (p, "!<arch>\n", 8) == 0;
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        for (UINT64 O = 8; O + 60 <= Len; ) {
            UINT8 CONST *H = p + O;
            if (H[58] != 0x60 || H[59] != 0x0A) { break; }    // member header terminator "`\n"
            UINT64 Size = ArDecimal (H + 48, 10);
            UINT64 Data = O + 60;
            if (Size > Len - Data) { break; }
            UINT64 ObjOff = Data, ObjLen = Size;
            if (H[0] == '#' && H[1] == '1' && H[2] == '/') {  // BSD extended name precedes the object
                UINT64 NameLen = ArDecimal (H + 3, 13);
                if (NameLen <= ObjLen) { ObjOff += NameLen; ObjLen -= NameLen; }
            }
            for (FormatReader CONST *pReader : Registry ()) {
                if (pReader->Format () == SymbolFormatArchive) { continue; }   // never recurse into ourselves
                if (pReader->Detect (p + ObjOff, ObjLen)) {
                    pReader->Extract (p + ObjOff, ObjLen, pSink);
                    break;
                }
            }
            O = Data + Size + (Size & 1);                     // members are aligned to 2 bytes
        }
    }
};

// ARM Object Format (AOF) -- the Acorn/RISC OS chunk file an ARM assembler/C compiler emits.
// A chunk-file header (ChunkFileId 0xC3CBC6C5 @0, maxChunks @4, numChunks @8) is followed by
// 16-byte chunk entries (8-byte chunkId, then file offset and size words). The OBJ_SYMT chunk
// holds 16-byte symbol entries (name index @0 into OBJ_STRT, attributes @4, value @8, area
// name index @12); the OBJ_STRT chunk is a length word then NUL-terminated strings. A symbol
// is exported when its attribute bits 1,0 == 11 (a global definition). Layout per the RISC OS
// PRM; the chunk file is little-endian.
class AofReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatAof; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        return Len >= 12 && Le32 (p) == 0xC3CBC6C5u;
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT32 MaxChunks = Le32 (p + 4);
        UINT64 SymtOff = 0, SymtSize = 0, StrtOff = 0, StrtSize = 0;
        for (UINT32 I = 0; I < MaxChunks; ++I) {
            UINT64 E = 12 + (UINT64) I * 16;
            if (E + 16 > Len) { break; }
            UINT64 Off = Le32 (p + E + 8), Size = Le32 (p + E + 12);
            if (Off == 0) { continue; }                       // an unused chunk entry
            if (std::memcmp (p + E, "OBJ_SYMT", 8) == 0) { SymtOff = Off; SymtSize = Size; }
            else if (std::memcmp (p + E, "OBJ_STRT", 8) == 0) { StrtOff = Off; StrtSize = Size; }
        }
        if (SymtOff == 0 || StrtOff == 0) { return; }
        if (SymtOff + SymtSize > Len || StrtOff + StrtSize > Len) { return; }
        for (UINT64 O = 0; O + 16 <= SymtSize; O += 16) {     // entry = name, AT, value, area name
            UINT64 E = SymtOff + O;
            UINT32 NameIdx = Le32 (p + E);
            UINT32 At      = Le32 (p + E + 4);
            if ((At & 0x3) != 0x3) { continue; }              // only global definitions (bits 1,0 = 11)
            if (NameIdx >= StrtSize) { continue; }
            UINT64 Na = StrtOff + NameIdx;
            CHAR8 CONST *pName = (CHAR8 CONST *) (p + Na);
            size_t Max = (size_t) (Len - Na);
            size_t NameLen = strnlen (pName, Max);
            if (NameLen > 0 && NameLen < Max) { pSink->Add (std::string (pName, NameLen)); }
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

static bool DetectMz (UINT8 CONST *p, UINT64 Len) { return Len >= 2 && p[0] == 'M' && p[1] == 'Z'; }
static bool DetectUefiTe (UINT8 CONST *p, UINT64 Len) { return Len >= 2 && p[0] == 'V' && p[1] == 'Z'; }
static bool DetectPharLap (UINT8 CONST *p, UINT64 Len) {
    return Len >= 2 && ((p[0] == 'M' && p[1] == 'P') || (p[0] == 'P' && (p[1] == '2' || p[1] == '3')));
}
// Sharp X68000 Human68k .X executable. 64-byte big-endian header: 'HU' (0x4855) @0 with the
// load mode in the low byte of the first longword (0/1/2); textSize @0x0C, dataSize @0x10,
// relocSize @0x18, symbolSize @0x1C. The symbol table follows text+data+reloc; each entry is
// a 2-byte type (high byte 0x02 = defined, 0x01 = external reference; low byte = section),
// a 4-byte value, a NUL-terminated name, then a pad byte to a word boundary. Defined symbols
// are reported. Layout per the erique/ghidra-human68k loader.
class X68kReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatX68000; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        return Len >= 0x40 && p[0] == 'H' && p[1] == 'U' && p[2] == 0x00 && p[3] <= 0x02;
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT32 TextSize = Be32 (p + 0x0C), DataSize = Be32 (p + 0x10);
        UINT32 RelocSize = Be32 (p + 0x18), SymSize = Be32 (p + 0x1C);
        UINT64 SymOff = 0x40 + (UINT64) TextSize + DataSize + RelocSize;
        if (SymSize == 0 || SymOff > Len || SymSize > Len - SymOff) { return; }
        UINT64 End = SymOff + SymSize;
        for (UINT64 O = SymOff; O + 6 <= End; ) {
            UINT16 Type = Be16 (p + O);                       // high byte: scope; low byte: section
            O += 6;                                           // skip type (2) + value (4)
            UINT64 S = O;
            while (O < End && p[O] != 0) { ++O; }
            std::string Name ((CHAR8 CONST *) (p + S), (size_t) (O - S));
            if (O < End) { ++O; }                             // skip the NUL
            if (((O - SymOff) & 1) != 0 && O < End) { ++O; }  // pad to a word boundary
            if (!Name.empty () && (Type >> 8) == 0x02) { pSink->Add (std::move (Name)); }   // defined
        }
    }
};
static bool DetectAif (UINT8 CONST *p, UINT64 Len) { return Len >= 0x14 && Le32 (p + 0x10) == 0xEF000011u; }
static bool DetectOs360 (UINT8 CONST *p, UINT64 Len) {     // X'02' + EBCDIC "ESD"
    return Len >= 4 && p[0] == 0x02 && p[1] == 0xC5 && p[2] == 0xE2 && p[3] == 0xC4;
}
// IBM GOFF (Generalized Object File Format, z/OS): a stream of 80-byte records, each prefixed
// with X'03' then a byte whose high nibble is the record type (0=ESD, 1=TXT, 2=RLD, 3=LEN,
// 4=END, F=HDR) and whose bit 7 (0x01) marks a continuation record. An ESD record names a
// symbol: symbol type @3 (0=SD section, 1=ED element, 2=LD label, 3=PR part, 4=ER external
// reference), name length @70 (big-endian) and name @72 in EBCDIC. The symbols a module
// exports are the SD (section) and LD (label) names; ER references (imports) are skipped.
// A name longer than the 8 bytes left in the first record continues in the following
// continuation records, after each record's 3-byte prefix.
class GoffReader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatGoff; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override {
        return Len >= 3 && p[0] == 0x03 && p[1] == 0xF0 && p[2] == 0x00;   // HDR record, version 0
    }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        for (UINT64 O = 0; O + 80 <= Len; O += 80) {
            if (p[O] != 0x03) { break; }                      // not a GOFF record
            if ((p[O + 1] & 0x01) != 0) { continue; }         // continuation of a prior record
            if (((p[O + 1] >> 4) & 0x0F) != 0x00) { continue; }   // only ESD records name symbols
            UINT8 SymType = p[O + 3];
            if (SymType != 0x00 && SymType != 0x02) { continue; }   // SD (section) / LD (label) = defined
            UINT32 NameLen = Be16 (p + O + 70);
            if (NameLen == 0) { continue; }
            std::string Nm;
            UINT64 Pos = O + 72, Avail = 8;                   // 80 - 72 bytes left in the first record
            UINT64 Rec = O, Remain = NameLen;
            while (Remain > 0 && Pos < Len) {
                UINT64 Take = Remain < Avail ? Remain : Avail;
                if (Pos + Take > Len) { Take = Len - Pos; }
                for (UINT64 K = 0; K < Take; K++) { Nm.push_back (EbcdicToAscii (p[Pos + K])); }
                Remain -= Take;
                Pos += Take;
                if (Remain > 0) {                             // continue in the next record, past its prefix
                    Rec += 80;
                    if (Rec + 3 > Len || p[Rec] != 0x03) { break; }
                    Pos = Rec + 3;
                    Avail = 77;
                }
            }
            if (!Nm.empty ()) { pSink->Add (std::move (Nm)); }
        }
    }
};
// DRI CMD command file (CP/M-86 / Concurrent CP/M / DOS Plus / FlexOS 186/286): a 128-byte
// header of up to eight 9-byte group descriptors (g_type, g_length, a_base, g_min, g_max),
// then the section contents. The .286 and .CMD command files share this layout; symbols are
// not stored in it (they go to the separate .SYM file and the OMF object modules). There is
// no magic word, so the descriptor chain is validated instead of compared.
static bool DetectDriCmd (UINT8 CONST *p, UINT64 Len) {
    if (Len < 128) { return false; }
    if (p[0] != 0x01 && p[0] != 0x09) { return false; }       // first group must be code (01 plain / 09 shared)
    UINT32 CodeLen = 0;
    UINT32 i = 0;
    for (; i < 8; ++i) {
        UINT8 Type = p[i * 9];
        if (Type == 0) { break; }                             // 0 terminates the descriptor list
        if (Type > 9) { return false; }                       // valid DRI group types are 1..9
        if (Type == 0x01 || Type == 0x09) { CodeLen = Le16 (p + i * 9 + 1); }   // length in 16-byte paragraphs
    }
    if (i == 8) { return false; }                             // eight groups with no terminator -> not a CMD header
    for (UINT64 O = (UINT64) i * 9; O < 128; ++O) {           // the rest of the header must be zero padding
        if (p[O] != 0) { return false; }
    }
    return 128 + (UINT64) CodeLen * 16 <= Len;                // the code section must fit within the file
}
static bool IsHex (UINT8 c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
// IEEE-695 (MUFOM) object module. The file opens with an MB (module-begin) record (0xE0)
// carrying the processor and module-name identifiers, an AD (address-descriptor) record, then
// a table of eight W-variable assignments (ASW = 0xE2 0xD7 <n> <offset>) giving the file
// offsets of the module's parts. W[2] is the external-symbol part: a run of NN/NI/NX name
// records interleaved with AS value and AT attribute records. NI (0xE8) declares a defined
// (public) symbol -- those are the exports; NX (0xE9) is an external reference (import). The
// value/attribute records are walked and skipped so every NI is reached. MUFOM numbers are a
// literal 0x00-0x7F or 0x80+n followed by n big-endian bytes; identifiers are a length byte
// (0xDE/0xDF extend it) then the characters. Encodings/record tags per binutils bfd/ieee.c.
class Ieee695Reader : public FormatReader {
public:
    SYMBOL_FORMAT Format () CONST override { return SymbolFormatIeee695; }
    bool Detect (UINT8 CONST *p, UINT64 Len) CONST override { return Len >= 2 && p[0] == 0xE0; }
    void Extract (UINT8 CONST *p, UINT64 Len, SymbolSink *pSink) CONST override {
        UINT64 O = 0;
        auto Num = [&] () -> UINT64 {                         // MUFOM number; advances O
            if (O >= Len) { return 0; }
            UINT8 B = p[O];
            if (B <= 0x7F) { O += 1; return B; }
            if (B >= 0x80 && B <= 0x88) {
                UINT32 C = B & 0x0F; O += 1; UINT64 V = 0;
                while (C-- && O < Len) { V = (V << 8) | p[O++]; }
                return V;
            }
            O += 1;
            return 0;
        };
        auto IdLen = [&] () -> UINT64 {                       // identifier length prefix; advances O
            if (O >= Len) { return 0; }
            UINT8 B = p[O++];
            if (B <= 0x7F) { return B; }
            if (B == 0xDE) { return O < Len ? p[O++] : 0; }
            if (B == 0xDF) { UINT64 N = (O + 1 < Len) ? (((UINT64) p[O] << 8) | p[O + 1]) : 0; O += 2; return N; }
            return 0;
        };
        auto SkipExpr = [&] () {                              // value expression (binutils parse_expression)
            while (O < Len) {
                UINT8 B = p[O];
                if (B == 0xD0 || B == 0xCC || B == 0xD2 || B == 0xD3 || B == 0xC9 || B == 0xD8) { O += 1; Num (); }
                else if (B == 0xA5 || B == 0xA6 || B == 0x90) { O += 1; }   // +, -, comma
                else if (B <= 0x88) { Num (); }              // a numeric literal term
                else { break; }
            }
        };

        // --- MB header: walk to the W-variable table and capture the part offsets. ---
        if (p[O] != 0xE0) { return; }
        O += 1;
        O += IdLen ();                                        // processor identifier
        O += IdLen ();                                        // module-name identifier
        if (O >= Len || p[O] != 0xEC) { return; }            // AD (address descriptor)
        O += 1;
        Num (); Num ();                                       // bits/MAU, MAUs/address
        if (O < Len && (p[O] == 0xCC || p[O] == 0xCD)) { O += 1; }   // optional byte-order (L/M)
        UINT64 Parts[8] = { 0 };
        for (UINT32 I = 0; I < 8; ++I) {
            if (O + 3 > Len || p[O] != 0xE2 || p[O + 1] != 0xD7 || p[O + 2] != I) { return; }   // ASW <I>
            O += 3;
            Parts[I] = Num ();
        }
        UINT64 ExtPart = Parts[2];                            // the external (symbol) part
        if (ExtPart == 0 || ExtPart >= Len) { return; }
        UINT64 End = Len;                                     // bound by the next part that follows it
        for (UINT32 I = 3; I <= 6; ++I) {
            if (Parts[I] > ExtPart && Parts[I] < End) { End = Parts[I]; }
        }

        // --- External part: emit NI (defined) names; skip NX/NN and the value/attribute records. ---
        O = ExtPart;
        while (O < End) {
            UINT8 B = p[O];
            if (B == 0xE8 || B == 0xE9 || B == 0xF0) {        // NI / NX / NN: tag, index, name
                O += 1;
                Num ();
                UINT64 N = IdLen ();
                if (O + N > Len) { N = Len - O; }
                std::string Nm ((CHAR8 CONST *) (p + O), (size_t) N);
                O += N;
                if (B == 0xE8 && !Nm.empty ()) { pSink->Add (std::move (Nm)); }   // NI = defined/exported
            } else if (B == 0xE2 && O + 2 <= End && p[O + 1] == 0xC9) {   // AS value record: index + expression
                O += 2;
                Num ();
                SkipExpr ();
            } else if (B == 0xF1 && O + 2 <= End) {           // AT attribute record (2-byte tag)
                UINT8 T = p[O + 1];
                O += 2;
                if (T == 0xC9) {                              // ATI: name, type, attr [, value]
                    Num (); Num ();
                    UINT64 Attr = Num ();
                    if (Attr == 8 || Attr == 19) { Num (); }
                } else if (T == 0xD8) {                       // ATX: four numbers
                    Num (); Num (); Num (); Num ();
                } else if (T == 0xCE) {                       // ATN: header numbers, then a run of ASN pairs
                    Num (); Num (); Num (); Num ();
                    UINT64 Cnt = Num ();
                    while (Cnt-- && O + 2 <= End && p[O] == 0xE2 && p[O + 1] == 0xCE) { O += 2; Num (); Num (); }
                } else {
                    break;
                }
            } else {
                break;                                        // unrecognised record -> end of the symbol part
            }
        }
    }
};
static bool DetectSrec (UINT8 CONST *p, UINT64 Len) { return Len >= 4 && p[0] == 'S' && p[1] >= '0' && p[1] <= '9' && IsHex (p[2]) && IsHex (p[3]); }
static bool DetectIntelHex (UINT8 CONST *p, UINT64 Len) { return Len >= 3 && p[0] == ':' && IsHex (p[1]) && IsHex (p[2]); }
static bool DetectTekHex (UINT8 CONST *p, UINT64 Len) { return Len >= 3 && (p[0] == '/' || p[0] == '%') && IsHex (p[1]) && IsHex (p[2]); }
static bool DetectVerilogHex (UINT8 CONST *p, UINT64 Len) { return Len >= 2 && p[0] == '@' && IsHex (p[1]); }
// Commodore 64 GEOS: the file header block carries the ASCII tag "PRG formatted GEOS file"
// or "SEQ formatted GEOS file" at offset 0x1E, i.e. "formatted GEOS file" at 0x22.
static bool DetectGeosC64 (UINT8 CONST *p, UINT64 Len) {
    return Len >= 0x22 + 19 && std::memcmp (p + 0x22, "formatted GEOS file", 19) == 0;
}

} // anonymous namespace

// --- registry ---------------------------------------------------------------

// All readers, in priority order: specific magics before the catch-alls they could shadow
// (bigobj before WinCOFF; PE/NE/LE/LX before the bare MZ fallback; OMF's heuristic last).
static std::vector<FormatReader CONST *> CONST &
Registry ()
{
    static MachOReader     S_MachO;
    static OsfRoseReader   S_OsfRose;
    static ElfReader       S_Elf;
    static RdoffReader     S_Rdoff;
    static GemdosReader    S_Gemdos;
    static CpmZ8000Reader  S_CpmZ8000;
    static CpmVaxReader    S_CpmVax;
    static SignatureReader S_UefiTe (SymbolFormatUefiTe, DetectUefiTe);
    static SignatureReader S_PharLap (SymbolFormatPharLap, DetectPharLap);
    static X68kReader      S_X68000;
    static SignatureReader S_Aif (SymbolFormatAif, DetectAif);
    static Os360Reader     S_Os360;
    static GoffReader      S_Goff;
    static GeosReader      S_Geos;
    static SignatureReader S_GeosC64 (SymbolFormatGeosC64, DetectGeosC64);
    static PalmReader      S_PalmPrc (SymbolFormatPalmPrc, true);
    static PalmReader      S_PalmPdb (SymbolFormatPalmPdb, false);
    static AmigaHunkReader S_AmigaHunk;
    static AmigaLibReader  S_AmigaLib;
    static MwobReader      S_Mwob;
    static AofReader       S_Aof;
    static PefReader       S_CfmPpc (SymbolFormatCfmPpc, "pwpc");
    static PefReader       S_Cfm68k (SymbolFormatCfm68k, "m68k");
    static PefReader       S_Pef (SymbolFormatPef, nullptr);
    static NlmReader       S_Nlm;
    static VmsReader       S_Vms;
    static AOutReader      S_AOut;
    static BoutReader      S_Bout;
    static Plan9Reader     S_Plan9;
    static MinixReader     S_MinixAOut;
    static XenixReader     S_XenixXOut;
    static Ieee695Reader   S_Ieee695;
    static SignatureReader S_Srec (SymbolFormatSrec, DetectSrec);
    static SignatureReader S_IntelHex (SymbolFormatIntelHex, DetectIntelHex);
    static SignatureReader S_TekHex (SymbolFormatTekHex, DetectTekHex);
    static SignatureReader S_VerilogHex (SymbolFormatVerilogHex, DetectVerilogHex);
    static BigObjReader    S_BigObj;
    static WinCoffReader   S_WinCoff;
    static EcoffReader     S_Ecoff;
    static XcoffReader     S_Xcoff;
    static SomReader       S_Som;
    static PeReader        S_Pe;
    static NeReader        S_Ne;
    static LeLxReader      S_Le (SymbolFormatLe, 'E');
    static LeLxReader      S_Lx (SymbolFormatLx, 'X');
    static SignatureReader S_Mz (SymbolFormatMz, DetectMz);
    static OmfReader       S_Omf;
    static OmfLibReader    S_OmfLib;
    static CpmLbrReader    S_CpmLbr;
    static MpwReader       S_Mpw;
    static Pdp10SavReader  S_Pdp10Sav;
    static SignatureReader S_DriCmd (SymbolFormatDriCmd, DetectDriCmd);
    static TbdReader       S_Tbd;

    static ArReader        S_Ar;

    static std::vector<FormatReader CONST *> List = {
        &S_Ar,                                               // static-library container, unwrapped first
        &S_Tbd,                                              // text stub, tried first
        &S_MachO, &S_OsfRose, &S_Elf, &S_Rdoff, &S_Gemdos, &S_CpmZ8000, &S_CpmVax,
        &S_AmigaHunk, &S_AmigaLib, &S_Mwob, &S_Aof, &S_CfmPpc, &S_Cfm68k, &S_Pef,
        &S_Nlm, &S_Vms, &S_Aif, &S_Geos,
        &S_AOut, &S_Bout, &S_Plan9, &S_MinixAOut, &S_XenixXOut,
        &S_BigObj, &S_WinCoff, &S_Ecoff, &S_Xcoff, &S_Som,
        &S_UefiTe, &S_PharLap, &S_X68000, &S_Pe, &S_Ne, &S_Le, &S_Lx, &S_Mz,
        &S_Omf, &S_OmfLib, &S_Pdp10Sav, &S_Os360, &S_Goff, &S_DriCmd, &S_CpmLbr, &S_Mpw, &S_PalmPrc, &S_PalmPdb,   // record-type / structural heuristics, last
        &S_Ieee695, &S_Srec, &S_IntelHex, &S_TekHex, &S_VerilogHex, &S_GeosC64   // ASCII / record formats, last
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
        case SymbolFormatLe:
        case SymbolFormatLx:
        case SymbolFormatNlm:
        case SymbolFormatEcoff:
        case SymbolFormatPlan9:
        case SymbolFormatPdp10Sav:
        case SymbolFormatSom:
        case SymbolFormatMinixAOut:
        case SymbolFormatGemdos:
        case SymbolFormatRdoff:
        case SymbolFormatOs360:
        case SymbolFormatVms:
        case SymbolFormatBout:
        case SymbolFormatOsfRose:
        case SymbolFormatXenixXOut:
        case SymbolFormatCpmZ8000:
        case SymbolFormatCpmVax:
        case SymbolFormatPalmPrc:
        case SymbolFormatArchive:
        case SymbolFormatOmfLib:
        case SymbolFormatCpmLbr:
        case SymbolFormatAmigaLib:
        case SymbolFormatMpw:
        case SymbolFormatMwob:
        case SymbolFormatX68000:
        case SymbolFormatGoff:
        case SymbolFormatIeee695:
        case SymbolFormatAof:
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
