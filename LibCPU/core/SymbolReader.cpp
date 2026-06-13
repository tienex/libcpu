/** @file  SymbolReader -- read exported symbols from .tbd stubs and Mach-O binaries. */

#include "SymbolReader.h"
#include <cstdio>
#include <cstring>

namespace LibCPU {

// --- minimal Mach-O layout (declared here so the reader builds without Apple headers) --

static UINT32 CONST MH_MAGIC_64 = 0xFEEDFACFu;   // 64-bit Mach-O, host-endian
static UINT32 CONST FAT_MAGIC   = 0xCAFEBABEu;   // fat header (stored big-endian)
static UINT32 CONST FAT_CIGAM   = 0xBEBAFECAu;   // fat magic as read native on an LE host
static UINT32 CONST FAT_MAGIC_64 = 0xCAFEBABFu;
static UINT32 CONST FAT_CIGAM_64 = 0xBFBAFECAu;
static UINT32 CONST LC_SYMTAB   = 0x2u;
static UINT32 CONST LC_ID_DYLIB = 0xDu;           // this image's install name
static UINT8  CONST N_STAB      = 0xE0u;          // any debug-symbol bit set => not exported
static UINT8  CONST N_EXT       = 0x01u;          // external symbol
static UINT8  CONST N_TYPE_MASK = 0x0Eu;
static UINT8  CONST N_SECT      = 0x0Eu;          // defined in a section of this image
static UINT32 CONST MH_MAGIC_32 = 0xFEEDFACEu;    // 32-bit Mach-O (we detect; extract 64-bit)
static UINT32 CONST MH_CIGAM_32 = 0xCEFAEDFEu;
static UINT32 CONST MH_CIGAM_64 = 0xCFFAEDFEu;

typedef struct _MACHO_HEADER_64 {
    UINT32 Magic;
    UINT32 CpuType;
    UINT32 CpuSubtype;
    UINT32 FileType;
    UINT32 NCmds;
    UINT32 SizeOfCmds;
    UINT32 Flags;
    UINT32 Reserved;
} MACHO_HEADER_64;

typedef struct _MACHO_LOAD_COMMAND {
    UINT32 Cmd;
    UINT32 CmdSize;
} MACHO_LOAD_COMMAND;

typedef struct _MACHO_SYMTAB_COMMAND {
    UINT32 Cmd;
    UINT32 CmdSize;
    UINT32 SymOff;
    UINT32 NSyms;
    UINT32 StrOff;
    UINT32 StrSize;
} MACHO_SYMTAB_COMMAND;

typedef struct _MACHO_NLIST_64 {
    UINT32 StrX;
    UINT8  Type;
    UINT8  Sect;
    UINT16 Desc;
    UINT64 Value;
} MACHO_NLIST_64;

// The fat header and its arch table are stored big-endian regardless of host.
static UINT32
Swap32 (UINT32 V)
{
    return ((V & 0x000000FFu) << 24) | ((V & 0x0000FF00u) << 8) |
           ((V & 0x00FF0000u) >> 8)  | ((V & 0xFF000000u) >> 24);
}

// --- generic helpers -------------------------------------------------------

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

void
SymbolReader::AddSymbol (std::string Name)
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
SymbolReader::Has (std::string CONST &Name) CONST
{
    for (std::string CONST &S : m_Symbols) {
        if (S == Name) { return true; }
    }
    return false;
}

// --- format detection ------------------------------------------------------

CHAR8 CONST *
SymbolFormatName (SYMBOL_FORMAT Format)
{
    switch (Format) {
        case SymbolFormatTbd:       return "tbd";
        case SymbolFormatMachO:     return "mach-o";
        case SymbolFormatElf:       return "elf";
        case SymbolFormatAOut:      return "a.out";
        case SymbolFormatPeCoff:    return "pe/coff";
        case SymbolFormatWinCoff:   return "wincoff";
        case SymbolFormatBigObjCoff: return "bigobj-coff";
        case SymbolFormatXcoff:     return "xcoff";
        case SymbolFormatEcoff:     return "ecoff";
        case SymbolFormatMz:        return "mz";
        case SymbolFormatNe:        return "ne";
        case SymbolFormatLe:        return "le";
        case SymbolFormatLx:        return "lx";
        case SymbolFormatMinixAOut: return "minix-a.out";
        case SymbolFormatXenixXOut: return "xenix-x.out";
        case SymbolFormatPef:       return "pef";
        case SymbolFormatCfm68k:    return "cfm-68k";
        case SymbolFormatCfmPpc:    return "cfm-ppc";
        case SymbolFormatSom:       return "som";
        case SymbolFormatAmigaHunk: return "amiga-hunk";
        case SymbolFormatOmf:       return "omf";
        case SymbolFormatNlm:       return "nlm";
        case SymbolFormatVms:       return "vms";
        default:                    return "unknown";
    }
}

bool
SymbolFormatHasExtractor (SYMBOL_FORMAT Format)
{
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
            return true;
        default:
            return false;
    }
}

// Read a big-endian 16/32-bit word (several legacy formats store headers big-endian).
static UINT16 Be16 (UINT8 CONST *p) { return (UINT16) ((p[0] << 8) | p[1]); }
static UINT32 Be32 (UINT8 CONST *p) { return ((UINT32) p[0] << 24) | ((UINT32) p[1] << 16) | ((UINT32) p[2] << 8) | p[3]; }
static UINT16 Le16 (UINT8 CONST *p) { return (UINT16) (p[0] | (p[1] << 8)); }
static UINT32 Le32 (UINT8 CONST *p) { return (UINT32) (p[0] | (p[1] << 8) | (p[2] << 16) | ((UINT32) p[3] << 24)); }

// Identify the artifact format by magic. Covers many legacy/obscure object & executable
// formats; only some have symbol extraction (see SYMBOL_FORMAT), but all are named.
static SYMBOL_FORMAT
DetectFormat (UINT8 CONST *p, UINT64 Len)
{
    if (Len >= 4) {
        UINT32 M = Le32 (p);
        if (M == MH_MAGIC_64 || M == MH_CIGAM_64 || M == MH_MAGIC_32 || M == MH_CIGAM_32 ||
            M == FAT_MAGIC || M == FAT_CIGAM || M == FAT_MAGIC_64 || M == FAT_CIGAM_64) {
            return SymbolFormatMachO;
        }
        if (p[0] == 0x7F && p[1] == 'E' && p[2] == 'L' && p[3] == 'F') {
            return SymbolFormatElf;
        }
        if (Be32 (p) == 0x000003F3u) {                 // AmigaOS HUNK_HEADER
            return SymbolFormatAmigaHunk;
        }
        if (std::memcmp (p, "Joy!", 4) == 0) {          // classic Mac OS PEF container
            if (Len >= 12 && std::memcmp (p + 8, "pwpc", 4) == 0) { return SymbolFormatCfmPpc; }
            if (Len >= 12 && std::memcmp (p + 8, "m68k", 4) == 0) { return SymbolFormatCfm68k; }
            return SymbolFormatPef;
        }
    }
    if (Len >= 24 && std::memcmp (p, "NetWare Loadable Module", 23) == 0) {
        return SymbolFormatNlm;                         // NLM signature string
    }
    // OpenVMS Alpha/Itanium image: the EIHD has majorid==3 / minorid==0 (longwords at +8/+12)
    // and a plausible header size at +0 -- the signature BFD's vms-alpha target keys on.
    if (Len >= 20 && Le32 (p + 8) == 3 && Le32 (p + 12) == 0) {
        UINT32 EihdSize = Le32 (p + 0);
        if (EihdSize >= 0x20 && EihdSize <= 0x4000) {
            return SymbolFormatVms;
        }
    }
    // a.out family: a_info magic in the first 32-bit word (either endianness).
    if (Len >= 4) {
        UINT32 Lo = Le32 (p), Be = Be32 (p);
        UINT32 Info = (Lo & 0xFFFF) ? Lo : Be;          // pick the half that carries the magic
        UINT16 AMagic = (UINT16) (Info & 0xFFFF);
        if (AMagic == 0x0107 || AMagic == 0x0108 || AMagic == 0x010B || AMagic == 0xCC) {
            return SymbolFormatAOut;                     // OMAGIC/NMAGIC/ZMAGIC/QMAGIC
        }
        if (Be16 (p) == 0x0301 || Le16 (p) == 0x0301) { return SymbolFormatMinixAOut; }
        if (Le16 (p) == 0x0206 || Be16 (p) == 0x0206) { return SymbolFormatXenixXOut; }
    }
    // Microsoft /bigobj COFF: Sig1=IMAGE_FILE_MACHINE_UNKNOWN(0), Sig2=0xFFFF, and the
    // anon-object bigobj class-id GUID at offset 12.
    static UINT8 CONST BigObjMagic[16] = {
        0xC7, 0xA1, 0xBA, 0xD1, 0xEE, 0xBA, 0xA9, 0x4B,
        0xAF, 0x20, 0xFA, 0xF6, 0x6A, 0xA4, 0xDC, 0xB8
    };
    if (Len >= 28 && Le16 (p) == 0x0000 && Le16 (p + 2) == 0xFFFF &&
        std::memcmp (p + 12, BigObjMagic, 16) == 0) {
        return SymbolFormatBigObjCoff;
    }
    // COFF-family machine magics (16-bit, little-endian) when there's no MZ wrapper.
    if (Len >= 2) {
        UINT16 Mach = Le16 (p);
        UINT16 MachBe = Be16 (p);
        if (Mach == 0x014C || Mach == 0x8664 || Mach == 0x01C0 || Mach == 0x01C4 || Mach == 0x0200) {
            return SymbolFormatWinCoff;                  // i386/x64/arm/arm-thumb/ia64
        }
        if (Mach == 0x0162 || Mach == 0x0166 || Mach == 0x0140 || Mach == 0x0184) {
            return SymbolFormatEcoff;                    // MIPS / Alpha ECOFF
        }
        if (MachBe == 0x01DF || MachBe == 0x01F7) {
            return SymbolFormatXcoff;                    // AIX XCOFF 32/64 (big-endian)
        }
        if (MachBe == 0x0210 || MachBe == 0x020B || MachBe == 0x0211) {
            return SymbolFormatSom;                      // HP-UX SOM a_magic values
        }
    }
    // MZ and the new-executable family it can prefix (NE/LE/LX/PE).
    if (Len >= 2 && p[0] == 'M' && p[1] == 'Z') {
        if (Len >= 0x40) {
            UINT32 e_lfanew = Le32 (p + 0x3C);
            if (e_lfanew + 2 <= Len && e_lfanew >= 0x40) {
                UINT8 CONST *q = p + e_lfanew;
                if (q[0] == 'P' && q[1] == 'E') { return SymbolFormatPeCoff; }
                if (q[0] == 'N' && q[1] == 'E') { return SymbolFormatNe; }
                if (q[0] == 'L' && q[1] == 'E') { return SymbolFormatLe; }
                if (q[0] == 'L' && q[1] == 'X') { return SymbolFormatLx; }
            }
        }
        return SymbolFormatMz;
    }
    // OMF: an object/library begins with a THEADR(0x80)/LHEADR(0x82)/LibHeader(0xF0) record.
    if (Len >= 3 && (p[0] == 0x80 || p[0] == 0x82 || p[0] == 0xF0)) {
        UINT16 RecLen = Le16 (p + 1);
        if ((UINT64) RecLen + 3 <= Len + 8) {            // plausible record length
            return SymbolFormatOmf;
        }
    }
    return SymbolFormatUnknown;
}

// --- public entry ----------------------------------------------------------

bool
SymbolReader::Read (CHAR8 CONST *pPath, std::string *pError)
{
    m_Format = SymbolFormatUnknown;
    m_InstallName.clear ();
    m_Symbols.clear ();

    std::vector<UINT8> Data;
    if (!SlurpFile (pPath, &Data)) {
        if (pError) { *pError = std::string ("cannot read '") + pPath + "'"; }
        return false;
    }

    // Text first: a .tbd stub is the only text format.
    std::string Text ((CHAR8 CONST *) Data.data (), Data.size ());
    if (Text.find ("!tapi") != std::string::npos || Text.find ("tbd-version") != std::string::npos) {
        m_Format = SymbolFormatTbd;
        return ReadTbd (Text);
    }

    m_Format = DetectFormat (Data.data (), Data.size ());
    switch (m_Format) {
        case SymbolFormatMachO:  return ReadMachO (Data.data (), Data.size (), pError);
        case SymbolFormatElf:    return ReadElf (Data.data (), Data.size (), pError);
        case SymbolFormatAOut:   return ReadAOut (Data.data (), Data.size (), pError);
        case SymbolFormatPeCoff: return ReadPeCoff (Data.data (), Data.size (), pError);
        case SymbolFormatOmf:    return ReadOmf (Data.data (), Data.size (), pError);
        case SymbolFormatNe:     return ReadNe (Data.data (), Data.size (), pError);
        case SymbolFormatWinCoff:    return ReadWinCoff (Data.data (), Data.size (), pError);
        case SymbolFormatBigObjCoff: return ReadBigObjCoff (Data.data (), Data.size (), pError);
        case SymbolFormatUnknown:
            if (pError) { *pError = std::string ("'") + pPath + "': unrecognised object/executable format"; }
            return false;
        default:
            // Recognised but no extractor yet: succeed with the format named and no symbols,
            // so the caller can report it honestly rather than failing.
            return true;
    }
}

// --- .tbd (tapi-tbd YAML) --------------------------------------------------

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

bool
SymbolReader::ReadTbd (std::string CONST &Text)
{
    // install-name: '...'
    size_t In = Text.find ("install-name:");
    if (In != std::string::npos) {
        size_t Eol = Text.find ('\n', In);
        m_InstallName = Unquote (Text.substr (In + 13, (Eol == std::string::npos ? Text.size () : Eol) - (In + 13)));
    }

    // Collect every "symbols:" flow sequence (this also catches weak-symbols / re-exported
    // -symbols / thread-local-symbols, which all end in "symbols:"). targets:/libraries:
    // lists are not symbols and are not matched.
    size_t Pos = 0;
    while ((Pos = Text.find ("symbols:", Pos)) != std::string::npos) {
        size_t Open = Text.find ('[', Pos);
        if (Open == std::string::npos) {                    // not a flow sequence: nothing to take
            Pos += 8;
            continue;
        }
        size_t Close = Text.find (']', Open);
        if (Close == std::string::npos) {
            break;
        }
        std::string Inner = Text.substr (Open + 1, Close - Open - 1);
        // Split the flow sequence on commas (symbol names never contain commas/brackets).
        size_t Start = 0;
        while (Start <= Inner.size ()) {
            size_t Comma = Inner.find (',', Start);
            std::string Item = Inner.substr (Start, (Comma == std::string::npos ? Inner.size () : Comma) - Start);
            AddSymbol (Unquote (Item));
            if (Comma == std::string::npos) { break; }
            Start = Comma + 1;
        }
        Pos = Close + 1;
    }
    return true;
}

// --- Mach-O ----------------------------------------------------------------

// Parse one thin, host-endian 64-bit Mach-O image at pImage[0..Len) and harvest its
// defined external symbols. Symtab offsets are relative to this image's base (the slice
// base, for a fat member), so all bounds are checked against Len.
static void
HarvestMachO64 (UINT8 CONST *pImage, UINT64 Len, void (*Add)(void *, std::string), void *pCtx,
                std::string *pInstallName)
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
        if (Lc.Cmd == LC_ID_DYLIB && pInstallName != nullptr && pInstallName->empty () && Lc.CmdSize >= 16) {
            // dylib_command: cmd, cmdsize, name.offset(@8), timestamp, cur, compat; the
            // install-name string sits at cmd_start + name.offset, bounded by cmdsize.
            UINT32 NameOff;
            std::memcpy (&NameOff, pImage + Off + 8, 4);
            if (NameOff < Lc.CmdSize && Off + NameOff < Len) {
                CHAR8 CONST *pName = (CHAR8 CONST *) (pImage + Off + NameOff);
                size_t Max = (size_t) (Lc.CmdSize - NameOff);
                size_t NameLen = strnlen (pName, Max);
                *pInstallName = std::string (pName, NameLen);
            }
        }
        if (Lc.Cmd == LC_SYMTAB && Lc.CmdSize >= sizeof (MACHO_SYMTAB_COMMAND)) {
            MACHO_SYMTAB_COMMAND St;
            std::memcpy (&St, pImage + Off, sizeof (St));
            // Overflow-safe range checks (St.* come from an untrusted file): verify each
            // region [off, off+len) lies within the image using subtraction, never addition.
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
                        // Bound the name to what remains of the string table; a malformed
                        // table without a terminator must not over-read past the image.
                        CHAR8 CONST *pName = (CHAR8 CONST *) (pImage + St.StrOff + Sym.StrX);
                        size_t MaxLen = (size_t) (St.StrSize - Sym.StrX);
                        size_t NameLen = strnlen (pName, MaxLen);
                        if (NameLen > 0 && NameLen < MaxLen) {
                            Add (pCtx, std::string (pName, NameLen));
                        }
                    }
                }
            }
        }
        Off += Lc.CmdSize;
    }
}

bool
SymbolReader::ReadMachO (UINT8 CONST *pData, UINT64 Len, std::string *pError)
{
    // A small adaptor so HarvestMachO64 can append into this object.
    struct Ctx { SymbolReader *pSelf; };
    Ctx C{ this };
    auto Add = [] (void *pCtx, std::string Name) {
        ((Ctx *) pCtx)->pSelf->AddSymbol (std::move (Name));
    };

    UINT32 Magic;
    std::memcpy (&Magic, pData, 4);
    if (Magic == MH_MAGIC_64) {
        HarvestMachO64 (pData, Len, Add, &C, &m_InstallName);
        return true;
    }
    if (Magic == FAT_MAGIC || Magic == FAT_CIGAM || Magic == FAT_MAGIC_64 || Magic == FAT_CIGAM_64) {
        bool Is64 = (Magic == FAT_MAGIC_64 || Magic == FAT_CIGAM_64);
        if (Len < 8) {
            if (pError) { *pError = "truncated fat header"; }
            return false;
        }
        UINT32 NFat;
        std::memcpy (&NFat, pData + 4, 4);
        NFat = Swap32 (NFat);                                // fat header is big-endian
        UINT64 Off = 8;
        for (UINT32 I = 0; I < NFat; I++) {
            // fat_arch{cputype,cpusubtype,offset,size,align}; the _64 variant widens
            // offset/size to 64-bit. We only need offset and size.
            UINT32  ArchOff32, ArchSize32;
            UINT64 SliceOff, SliceSize;
            if (!Is64) {
                if (Off + 20 > Len) { break; }
                std::memcpy (&ArchOff32, pData + Off + 8, 4);
                std::memcpy (&ArchSize32, pData + Off + 12, 4);
                SliceOff = Swap32 (ArchOff32);
                SliceSize = Swap32 (ArchSize32);
                Off += 20;
            } else {
                if (Off + 32 > Len) { break; }
                UINT32 Hi, Lo;
                std::memcpy (&Hi, pData + Off + 8, 4);  std::memcpy (&Lo, pData + Off + 12, 4);
                SliceOff = ((UINT64) Swap32 (Hi) << 32) | Swap32 (Lo);
                std::memcpy (&Hi, pData + Off + 16, 4); std::memcpy (&Lo, pData + Off + 20, 4);
                SliceSize = ((UINT64) Swap32 (Hi) << 32) | Swap32 (Lo);
                Off += 32;
            }
            // Subtraction-based bound: SliceOff/SliceSize are untrusted, so an addition
            // could wrap and pass a too-large slice through.
            if (SliceOff <= Len && SliceSize <= Len - SliceOff && SliceSize >= 4) {
                UINT32 SliceMagic;
                std::memcpy (&SliceMagic, pData + SliceOff, 4);
                if (SliceMagic == MH_MAGIC_64) {
                    HarvestMachO64 (pData + SliceOff, SliceSize, Add, &C, &m_InstallName);   // host-endian slice
                }
            }
        }
        return true;
    }
    if (pError) { *pError = "unrecognised Mach-O magic"; }
    return false;
}

// --- ELF (section-header symbol table; 32/64, little/big endian) ------------

bool
SymbolReader::ReadElf (UINT8 CONST *p, UINT64 Len, std::string * /*pError*/)
{
    if (Len < 24) {
        return true;
    }
    bool Is64 = p[4] == 2;                                    // EI_CLASS
    bool Be   = p[5] == 2;                                    // EI_DATA (2 = MSB)
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

    // Find a symbol table section (.dynsym preferred for a shared object, else .symtab)
    // and its linked string table.
    UINT64 SymOff = 0, SymSize = 0, SymEnt = 0, StrOff = 0, StrSize = 0;
    int Best = -1;
    for (UINT32 I = 0; I < ShNum; I++) {
        UINT64 Sh = ShOff + (UINT64) I * ShEnt;
        if (ShEnt == 0 || Sh + ShEnt > Len) { break; }
        UINT32 Type = U32 (Sh + 4);
        if (Type != 2 && Type != 11) {                       // SHT_SYMTAB / SHT_DYNSYM
            continue;
        }
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
        return true;
    }
    for (UINT64 O = 0; O + SymEnt <= SymSize; O += SymEnt) {
        UINT64 E = SymOff + O;
        UINT32 NameX = U32 (E + 0);
        UINT8  Info  = Is64 ? p[E + 4] : p[E + 12];
        UINT16 Shndx = Is64 ? U16 (E + 6) : U16 (E + 14);
        UINT32 Bind  = Info >> 4;                             // STB_GLOBAL=1, STB_WEAK=2
        bool   Defined = Shndx != 0 && Shndx < 0xFF00;        // not UNDEF / reserved
        if ((Bind == 1 || Bind == 2) && Defined && NameX != 0 && StrOff + NameX < Len) {
            CHAR8 CONST *pName = (CHAR8 CONST *) (p + StrOff + NameX);
            size_t Max = (size_t) (StrSize - NameX);
            size_t NameLen = strnlen (pName, Max);
            if (NameLen > 0 && NameLen < Max) {
                AddSymbol (std::string (pName, NameLen));
            }
        }
    }
    return true;
}

// --- classic a.out (32-byte exec header + nlist symbol table) ---------------

bool
SymbolReader::ReadAOut (UINT8 CONST *p, UINT64 Len, std::string * /*pError*/)
{
    if (Len < 32) {
        return true;
    }
    UINT16 M = (UINT16) (Le32 (p) & 0xFFFF);
    bool Be = !(M == 0x0107 || M == 0x0108 || M == 0x010B || M == 0xCC);
    auto U32 = [&] (UINT64 o) -> UINT32 { return Be ? Be32 (p + o) : Le32 (p + o); };

    UINT32 ATextSize = U32 (4), ADataSize = U32 (8), ASyms = U32 (16);
    UINT32 ATrSize = U32 (24), ADrSize = U32 (28);
    UINT64 SymOff = 32 + (UINT64) ATextSize + ADataSize + ATrSize + ADrSize;
    UINT64 StrOff = SymOff + ASyms;
    if (SymOff > Len || ASyms > Len - SymOff || StrOff > Len) {
        return true;
    }
    for (UINT64 O = 0; O + 12 <= ASyms; O += 12) {           // struct nlist = 12 bytes
        UINT64 E = SymOff + O;
        UINT32 NameX = U32 (E + 0);
        UINT8  Type  = p[E + 4];
        bool   Ext   = (Type & 0x01) != 0;                   // N_EXT
        UINT8  Ty    = Type & 0x1E;                          // N_TYPE
        bool   Defined = (Ty == 0x04 || Ty == 0x06 || Ty == 0x08);   // TEXT/DATA/BSS
        if (Ext && Defined && NameX >= 4 && StrOff + NameX < Len) {
            CHAR8 CONST *pName = (CHAR8 CONST *) (p + StrOff + NameX);
            size_t Max = (size_t) (Len - (StrOff + NameX));
            size_t NameLen = strnlen (pName, Max);
            if (NameLen > 0) {
                AddSymbol (std::string (pName, NameLen));
            }
        }
    }
    return true;
}

// --- PE/COFF (COFF symbol table; present in objects and unstripped images) --

bool
SymbolReader::ReadPeCoff (UINT8 CONST *p, UINT64 Len, std::string * /*pError*/)
{
    if (Len < 0x40) {
        return true;
    }
    UINT32 Pe = Le32 (p + 0x3C);                             // e_lfanew
    if ((UINT64) Pe + 24 > Len) {
        return true;
    }
    UINT64 Coff = (UINT64) Pe + 4;                           // past "PE\0\0"
    UINT16 NumSec = Le16 (p + Coff + 2);
    UINT32 SymPtr = Le32 (p + Coff + 8);
    UINT32 NSym   = Le32 (p + Coff + 12);
    UINT16 OptSize = Le16 (p + Coff + 16);

    // (1) COFF symbol table -- present in object files and unstripped images.
    if (SymPtr != 0 && NSym != 0) {
        HarvestCoff (p, Len, SymPtr, NSym, /*BigObj=*/ false);
    }

    // (2) PE export directory -- where stripped images / DLLs publish their exports. The
    // directory's fields are RVAs (virtual addresses), so each must be translated to a file
    // offset through the section table.
    UINT64 Opt = Coff + 20;
    if (Opt + 2 > Len || OptSize < 96) {
        return true;
    }
    UINT16 Magic = Le16 (p + Opt);
    UINT64 DDOff = 0;
    UINT32 NRva = 0;
    if (Magic == 0x10B) {                                    // PE32
        NRva = Le32 (p + Opt + 92);
        DDOff = Opt + 96;
    } else if (Magic == 0x20B) {                             // PE32+
        NRva = (Opt + 108 + 4 <= Len) ? Le32 (p + Opt + 108) : 0;
        DDOff = Opt + 112;
    } else {
        return true;
    }
    if (NRva < 1 || DDOff + 8 > Len) {
        return true;
    }
    UINT32 ExpRva = Le32 (p + DDOff + 0);                    // data directory entry 0 = export table
    if (ExpRva == 0) {
        return true;
    }
    UINT64 SecOff = Opt + OptSize;                           // section headers follow the opt header
    auto Rva2Off = [&] (UINT32 Rva) -> UINT64 {
        for (UINT32 I = 0; I < NumSec; I++) {
            UINT64 Sh = SecOff + (UINT64) I * 40;            // section header = 40 bytes
            if (Sh + 40 > Len) { break; }
            UINT32 VSize = Le32 (p + Sh + 8);
            UINT32 VAddr = Le32 (p + Sh + 12);
            UINT32 RawSz = Le32 (p + Sh + 16);
            UINT32 PRaw  = Le32 (p + Sh + 20);
            UINT32 Span  = VSize > RawSz ? VSize : RawSz;
            if (Rva >= VAddr && (UINT64) Rva < (UINT64) VAddr + Span) {
                return (UINT64) PRaw + (Rva - VAddr);
            }
        }
        return (UINT64) -1;
    };
    UINT64 ExpOff = Rva2Off (ExpRva);
    if (ExpOff == (UINT64) -1 || ExpOff + 40 > Len) {
        return true;
    }
    UINT32 NNames   = Le32 (p + ExpOff + 24);                // IMAGE_EXPORT_DIRECTORY.NumberOfNames
    UINT32 NamesRva = Le32 (p + ExpOff + 32);                // .AddressOfNames (RVA of RVA array)
    UINT64 NamesOff = Rva2Off (NamesRva);
    if (NamesOff == (UINT64) -1 || NNames > (1u << 20)) {
        return true;                                         // missing / implausible name table
    }
    for (UINT32 I = 0; I < NNames; I++) {
        UINT64 E = NamesOff + (UINT64) I * 4;
        if (E + 4 > Len) { break; }
        UINT64 NmOff = Rva2Off (Le32 (p + E));
        if (NmOff != (UINT64) -1 && NmOff < Len) {
            CHAR8 CONST *pName = (CHAR8 CONST *) (p + NmOff);
            size_t Max = (size_t) (Len - NmOff);
            size_t NameLen = strnlen (pName, Max);
            if (NameLen > 0 && NameLen < Max) {
                AddSymbol (std::string (pName, NameLen));
            }
        }
    }
    return true;
}

// --- OMF (object/library): PUBDEF records list public (exported) names ------

bool
SymbolReader::ReadOmf (UINT8 CONST *p, UINT64 Len, std::string * /*pError*/)
{
    // OMF index: 1 byte if < 0x80, else two bytes ((b & 0x7F) << 8 | next). Advances *pOff.
    auto Index = [&] (UINT64 *pOff) -> UINT32 {
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
        UINT16 RecLen = Le16 (p + Off + 1);                  // data length incl. 1 checksum byte
        UINT64 Data = Off + 3;
        UINT64 End  = Data + RecLen;                         // points just past the checksum
        if (RecLen < 1 || End > Len) {
            break;                                           // malformed / truncated
        }
        UINT64 DataEnd = End - 1;                            // exclude the checksum byte
        if (Type == 0x90 || Type == 0x91) {                 // PUBDEF (16-bit / 32-bit)
            bool Wide = (Type == 0x91);
            UINT64 Q = Data;
            Index (&Q);                                      // base group index
            UINT32 Seg = Index (&Q);                         // base segment index
            if (Seg == 0) { Q += 2; }                        // base frame present when seg == 0
            while (Q < DataEnd) {
                UINT8 NameLen = p[Q++];
                if (Q + NameLen > DataEnd) { break; }
                if (NameLen > 0) {
                    AddSymbol (std::string ((CHAR8 CONST *) (p + Q), NameLen));
                }
                Q += NameLen;
                Q += Wide ? 4 : 2;                           // public offset
                Index (&Q);                                  // type index
            }
        }
        Off = End;
    }
    return true;
}

// --- NE (16-bit Windows/OS2): resident + non-resident name tables -----------

// Read a length-prefixed name table { len(1), name[len], ordinal(2) } terminated by len==0,
// adding every entry except the first (entry 0 is the module's own name, not an export).
void
ReadNeNameTable (SymbolReader *pSelf, void (*Add) (SymbolReader *, std::string),
                 UINT8 CONST *p, UINT64 Len, UINT64 Off)
{
    bool First = true;
    while (Off < Len) {
        UINT8 NameLen = p[Off++];
        if (NameLen == 0) { break; }                         // end of table
        if (Off + NameLen + 2 > Len) { break; }
        if (!First) {                                        // skip the module name (entry 0)
            Add (pSelf, std::string ((CHAR8 CONST *) (p + Off), NameLen));
        }
        First = false;
        Off += NameLen + 2;                                  // name + 2-byte ordinal
    }
}

// --- shared COFF symbol-table harvest (WinCOFF / PE / bigobj) ---------------

void
SymbolReader::HarvestCoff (UINT8 CONST *p, UINT64 Len, UINT64 SymOff, UINT32 NSym, bool BigObj)
{
    UINT32 RecSize = BigObj ? 20 : 18;                       // bigobj widens SectionNumber to 32-bit
    if (SymOff > Len || (UINT64) NSym * RecSize > Len - SymOff) {
        return;
    }
    UINT64 StrOff = SymOff + (UINT64) NSym * RecSize;
    for (UINT32 I = 0; I < NSym; ) {
        UINT64 E = SymOff + (UINT64) I * RecSize;
        INT32 Section;
        UINT8 Class, Aux;
        if (BigObj) {
            Section = (INT32) Le32 (p + E + 12);
            Class   = p[E + 18];
            Aux     = p[E + 19];
        } else {
            Section = (INT16) Le16 (p + E + 12);
            Class   = p[E + 16];
            Aux     = p[E + 17];
        }
        if (Class == 2 && Section > 0) {                     // IMAGE_SYM_CLASS_EXTERNAL, defined
            std::string Name;
            if (Le32 (p + E) == 0) {                         // long name: offset into string table
                UINT32 So = Le32 (p + E + 4);
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
            if (!Name.empty ()) {
                AddSymbol (std::move (Name));
            }
        }
        I += 1 + Aux;                                        // skip auxiliary records
    }
}

// Bare Windows COFF object: a 20-byte COFF header at offset 0, then the symbol table.
bool
SymbolReader::ReadWinCoff (UINT8 CONST *p, UINT64 Len, std::string * /*pError*/)
{
    if (Len < 20) {
        return true;
    }
    UINT32 SymPtr = Le32 (p + 8);
    UINT32 NSym   = Le32 (p + 12);
    if (SymPtr != 0 && NSym != 0) {
        HarvestCoff (p, Len, SymPtr, NSym, /*BigObj=*/ false);
    }
    return true;
}

// Microsoft /bigobj COFF: a 56-byte anon-object bigobj header (PointerToSymbolTable@48,
// NumberOfSymbols@52), then a symbol table of 20-byte records.
bool
SymbolReader::ReadBigObjCoff (UINT8 CONST *p, UINT64 Len, std::string * /*pError*/)
{
    if (Len < 56) {
        return true;
    }
    UINT32 SymPtr = Le32 (p + 48);
    UINT32 NSym   = Le32 (p + 52);
    if (SymPtr != 0 && NSym != 0) {
        HarvestCoff (p, Len, SymPtr, NSym, /*BigObj=*/ true);
    }
    return true;
}

bool
SymbolReader::ReadNe (UINT8 CONST *p, UINT64 Len, std::string * /*pError*/)
{
    UINT32 NeOff = Le32 (p + 0x3C);                          // e_lfanew -> NE header
    if ((UINT64) NeOff + 0x40 > Len || p[NeOff] != 'N' || p[NeOff + 1] != 'E') {
        return true;
    }
    auto Add = [] (SymbolReader *pSelf, std::string Name) { pSelf->AddSymbol (std::move (Name)); };

    // Resident name table: offset is relative to the NE header.
    UINT16 ResRva = Le16 (p + NeOff + 0x26);
    if (ResRva != 0) {
        ReadNeNameTable (this, Add, p, Len, (UINT64) NeOff + ResRva);
    }
    // Non-resident name table: a file offset, with its length at ne+0x30.
    UINT32 NonResOff = Le32 (p + NeOff + 0x2C);
    if (NonResOff != 0 && NonResOff < Len) {
        ReadNeNameTable (this, Add, p, Len, NonResOff);
    }
    return true;
}

} // namespace LibCPU
