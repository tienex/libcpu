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
                    bool Exported = (Type & N_STAB) == 0 && (Type & N_EXT) != 0 &&
                                    (Type & N_TYPE_MASK) == N_SECT;
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
            bool Defined = (Sc >= 1 && Sc <= 6);              // text/data/bss/sdata/sbss/rdata
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

static bool DetectEcoff (UINT8 CONST *p, UINT64 Len) {
    if (Len < 2) { return false; }
    UINT16 M = Le16 (p);
    return M == 0x0162 || M == 0x0166 || M == 0x0140 || M == 0x0184;
}
static bool DetectMz (UINT8 CONST *p, UINT64 Len) { return Len >= 2 && p[0] == 'M' && p[1] == 'Z'; }
static bool DetectUefiTe (UINT8 CONST *p, UINT64 Len) { return Len >= 2 && p[0] == 'V' && p[1] == 'Z'; }
static bool DetectPharLap (UINT8 CONST *p, UINT64 Len) {
    return Len >= 2 && ((p[0] == 'M' && p[1] == 'P') || (p[0] == 'P' && (p[1] == '2' || p[1] == '3')));
}
static bool DetectX68000 (UINT8 CONST *p, UINT64 Len) { return Len >= 2 && p[0] == 'H' && p[1] == 'U'; }
static bool DetectAif (UINT8 CONST *p, UINT64 Len) { return Len >= 0x14 && Le32 (p + 0x10) == 0xEF000011u; }
static bool DetectOs360 (UINT8 CONST *p, UINT64 Len) {     // X'02' + EBCDIC "ESD"
    return Len >= 4 && p[0] == 0x02 && p[1] == 0xC5 && p[2] == 0xE2 && p[3] == 0xC4;
}
static bool DetectGoff (UINT8 CONST *p, UINT64 Len) { return Len >= 2 && p[0] == 0x03 && p[1] == 0xF0; }   // PTV + HDR
static bool IsHex (UINT8 c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
static bool DetectIeee695 (UINT8 CONST *p, UINT64 Len) { return Len >= 2 && p[0] == 0xE0; }                 // MB record
static bool DetectSrec (UINT8 CONST *p, UINT64 Len) { return Len >= 4 && p[0] == 'S' && p[1] >= '0' && p[1] <= '9' && IsHex (p[2]) && IsHex (p[3]); }
static bool DetectIntelHex (UINT8 CONST *p, UINT64 Len) { return Len >= 3 && p[0] == ':' && IsHex (p[1]) && IsHex (p[2]); }
static bool DetectTekHex (UINT8 CONST *p, UINT64 Len) { return Len >= 3 && (p[0] == '/' || p[0] == '%') && IsHex (p[1]) && IsHex (p[2]); }
static bool DetectVerilogHex (UINT8 CONST *p, UINT64 Len) { return Len >= 2 && p[0] == '@' && IsHex (p[1]); }

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
    static SignatureReader S_UefiTe (SymbolFormatUefiTe, DetectUefiTe);
    static SignatureReader S_PharLap (SymbolFormatPharLap, DetectPharLap);
    static SignatureReader S_X68000 (SymbolFormatX68000, DetectX68000);
    static SignatureReader S_Aif (SymbolFormatAif, DetectAif);
    static Os360Reader     S_Os360;
    static SignatureReader S_Goff (SymbolFormatGoff, DetectGoff);
    static AmigaHunkReader S_AmigaHunk;
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
    static SignatureReader S_Ieee695 (SymbolFormatIeee695, DetectIeee695);
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
    static Pdp10SavReader  S_Pdp10Sav;
    static TbdReader       S_Tbd;

    static std::vector<FormatReader CONST *> List = {
        &S_Tbd,                                              // text stub, tried first
        &S_MachO, &S_OsfRose, &S_Elf, &S_Rdoff, &S_Gemdos, &S_CpmZ8000, &S_AmigaHunk, &S_CfmPpc, &S_Cfm68k, &S_Pef,
        &S_Nlm, &S_Vms, &S_Aif,
        &S_AOut, &S_Bout, &S_Plan9, &S_MinixAOut, &S_XenixXOut,
        &S_BigObj, &S_WinCoff, &S_Ecoff, &S_Xcoff, &S_Som,
        &S_UefiTe, &S_PharLap, &S_X68000, &S_Pe, &S_Ne, &S_Le, &S_Lx, &S_Mz,
        &S_Omf, &S_Pdp10Sav, &S_Os360, &S_Goff,              // record-type / structural heuristics, last
        &S_Ieee695, &S_Srec, &S_IntelHex, &S_TekHex, &S_VerilogHex   // ASCII / record formats, last
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
