/** @file
  PE / PE32+ (Windows Portable Executable), as a ".loader" module.

  A loader knows the FILE FORMAT, never a CPU: this maps each section to ImageBase+VirtualAddress
  through the ILoaderMemory sink and REPORTS what it read -- the COFF Machine, PE32 vs PE32+ width,
  entry (ImageBase + AddressOfEntryPoint). PE is always little-endian and always dynamically linked;
  the imported DLLs (the import directory) are EXPOSED as the NEEDED list, not resolved. Machine is
  reported, never interpreted.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/ILoader.h"
#include "LibCPU/Module.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace LibCPU {
namespace {

// PE IMAGE_FILE_MACHINE_* values, for naming the architecture (never acted on).
enum
{
    PE_MACH_I386  = 0x014c, PE_MACH_AMD64 = 0x8664, PE_MACH_ARM   = 0x01c0,
    PE_MACH_ARMNT = 0x01c4, PE_MACH_ARM64 = 0xaa64, PE_MACH_IA64  = 0x0200,
    PE_MACH_MIPS  = 0x0166, PE_MACH_MIPS16= 0x0266, PE_MACH_PPC   = 0x01f0,
    PE_MACH_PPCFP = 0x01f1, PE_MACH_SH3   = 0x01a2, PE_MACH_SH4   = 0x01a6,
    PE_MACH_RISCV32 = 0x5032, PE_MACH_RISCV64 = 0x5064
};

// Heap-break page-alignment granularity (a loader convention, not a CPU page size).
static UINT64 CONST kPePageMask = 0xfff;

static CHAR8 CONST *
PeArch (UINT16 M)
{
    switch (M) {
    case PE_MACH_I386:  return "i386";     case PE_MACH_AMD64: return "x86_64";
    case PE_MACH_ARM:   case PE_MACH_ARMNT: return "arm";
    case PE_MACH_ARM64: return "aarch64";  case PE_MACH_IA64:  return "ia64";
    case PE_MACH_MIPS:  case PE_MACH_MIPS16: return "mips";
    case PE_MACH_PPC:   case PE_MACH_PPCFP: return "ppc";
    case PE_MACH_SH3:   case PE_MACH_SH4:  return "sh";
    case PE_MACH_RISCV32: return "riscv";  case PE_MACH_RISCV64: return "riscv";
    default:            return nullptr;
    }
}

class PeLoader final : public ComObject<ILoader>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_ILoader, ppvObject);
    }

    CHAR8 CONST *STDMETHODCALLTYPE GetName (VOID) override { return "pe"; }

    UINT32 STDMETHODCALLTYPE Probe (UINT8 CONST *pImage, UINT64 Len) override
    {
        return PeOffset (pImage, Len) != 0 ? 90 : 0;
    }

    HRESULT STDMETHODCALLTYPE Load (UINT8 CONST *pImage, UINT64 Len, ILoaderMemory *pMem,
                                    LOADER_REQUEST CONST *pRequest, LOADER_RESULT *pResult) override
    {
        if (pImage == nullptr || pMem == nullptr || pResult == nullptr) {
            return E_POINTER;
        }
        UINT64 Pe = PeOffset (pImage, Len);
        if (Pe == 0) {
            std::printf ("lcx: not a PE image\n");
            return E_FAIL;
        }
        std::memset (pResult, 0, sizeof (*pResult));
        m_Needed.clear ();
        m_NeededPtrs.clear ();
        m_pImage = pImage;
        m_Len    = Len;
        UINT64 RamSize = pMem->Size ();

        // COFF file header (right after the 4-byte "PE\0\0").
        UINT64 Coff    = Pe + 4;
        UINT16 Machine = (UINT16) Rd (Coff + 0, 2);
        UINT16 NSect   = (UINT16) Rd (Coff + 2, 2);
        UINT16 OptSz   = (UINT16) Rd (Coff + 16, 2);
        UINT64 Opt     = Coff + 20;             // optional header
        UINT16 OptMag  = (UINT16) Rd (Opt + 0, 2);
        bool   Plus    = (OptMag == 0x20b);     // PE32+ (64-bit); 0x10b = PE32
        UINT32 EntryRva = (UINT32) Rd (Opt + 16, 4);
        UINT64 ImageBase = Plus ? Rd (Opt + 24, 8) : Rd (Opt + 28, 4);

        // Data directory 1 = import table (RVA at optional-header-magic-dependent offset).
        UINT64 DirBase = Opt + (Plus ? 112 : 96);   // start of the data directories
        UINT32 ImpRva  = (UINT32) Rd (DirBase + 1 * 8, 4);

        UINT64 SectTab = Opt + OptSz;
        UINT64 LoadEnd = 0;
        m_NSect  = NSect;
        m_SectTab = SectTab;

        for (UINT16 I = 0; I < NSect; I++) {
            UINT64 S = SectTab + (UINT64) I * 40;
            if (S + 40 > Len) {
                break;
            }
            UINT32 VSize = (UINT32) Rd (S + 8, 4);
            UINT32 VAddr = (UINT32) Rd (S + 12, 4);
            UINT32 RawSz = (UINT32) Rd (S + 16, 4);
            UINT32 RawPtr = (UINT32) Rd (S + 20, 4);
            UINT64 Dst   = ImageBase + VAddr;
            UINT64 Mem   = (VSize != 0) ? VSize : RawSz;
            if (Mem == 0) {
                continue;
            }
            if (Dst + Mem > RamSize || Dst + Mem < Dst) {
                std::printf ("lcx: PE section out of range\n");
                return E_FAIL;
            }
            UINT32 Copy = (RawSz < VSize || VSize == 0) ? RawSz : VSize;
            if (Copy != 0 && RawPtr + Copy <= Len) {
                pMem->Write (Dst, pImage + RawPtr, Copy);
            }
            if (Mem > Copy) {
                pMem->Zero (Dst + Copy, Mem - Copy);
            }
            if (Dst + Mem > LoadEnd) {
                LoadEnd = Dst + Mem;
            }
        }

        if (ImpRva != 0) {
            CollectImports (ImpRva, ImageBase);
        }

        pResult->Entry    = ImageBase + EntryRva;
        pResult->LoadEnd  = LoadEnd;
        pResult->BrkBase  = (LoadEnd + kPePageMask) & ~kPePageMask;
        pResult->Endian   = LoaderEndianLittle;   // PE is always little-endian
        pResult->WordBits = Plus ? 64 : 32;
        pResult->Arch     = PeArch (Machine);     // canonical name, never interpreted
        pResult->Abi      = "windows";

        pResult->Dynamic.IsDynamic = 1;              // PE resolves imports through the IAT
        pResult->Dynamic.Interp    = nullptr;
        for (std::string CONST &N : m_Needed) {
            m_NeededPtrs.push_back (N.c_str ());
        }
        pResult->Dynamic.NeededCount = (UINT32) m_NeededPtrs.size ();
        pResult->Dynamic.Needed      = m_NeededPtrs.empty () ? nullptr : m_NeededPtrs.data ();

        std::printf ("lcx: loaded PE (%s LE, arch=%s, abi=windows): entry=0x%llx end=0x%llx",
                     Plus ? "PE32+/64-bit" : "PE32/32-bit", pResult->Arch ? pResult->Arch : "?",
                     (unsigned long long) pResult->Entry, (unsigned long long) LoadEnd);
        if (pResult->Dynamic.NeededCount != 0) {
            std::printf (" needed=%u", (unsigned) pResult->Dynamic.NeededCount);
        }
        std::printf ("\n");
        return S_OK;
    }

private:
    // Return the file offset of the "PE\0\0" signature, or 0 if this is not a PE image.
    static UINT64 PeOffset (UINT8 CONST *p, UINT64 Len)
    {
        if (Len < 0x40 || p[0] != 'M' || p[1] != 'Z') {
            return 0;
        }
        UINT64 Off = (UINT32) (p[0x3c] | (p[0x3d] << 8) | (p[0x3e] << 16) | ((UINT32) p[0x3f] << 24));
        if (Off == 0 || Off + 24 > Len) {
            return 0;
        }
        if (p[Off] != 'P' || p[Off + 1] != 'E' || p[Off + 2] != 0 || p[Off + 3] != 0) {
            return 0;
        }
        return Off;
    }

    UINT64 Rd (UINT64 Off, UINT32 Size)   // PE fields are little-endian
    {
        if (Off + Size > m_Len) {
            return 0;
        }
        UINT64 V = 0;
        for (UINT32 I = 0; I < Size; I++) { V |= (UINT64) m_pImage[Off + I] << (8 * I); }
        return V;
    }

    // Map a relative virtual address to a file offset via the section table.
    bool RvaToOff (UINT32 Rva, UINT64 *pOff)
    {
        for (UINT16 I = 0; I < m_NSect; I++) {
            UINT64 S = m_SectTab + (UINT64) I * 40;
            if (S + 40 > m_Len) {
                break;
            }
            UINT32 VSize = (UINT32) Rd (S + 8, 4);
            UINT32 VAddr = (UINT32) Rd (S + 12, 4);
            UINT32 RawSz = (UINT32) Rd (S + 16, 4);
            UINT32 RawPtr = (UINT32) Rd (S + 20, 4);
            UINT32 Span  = (VSize > RawSz) ? VSize : RawSz;
            if (Rva >= VAddr && Rva < VAddr + Span) {
                *pOff = RawPtr + (Rva - VAddr);
                return true;
            }
        }
        return false;
    }

    // Walk the import directory, collecting the imported DLL names as NEEDED.
    void CollectImports (UINT32 ImpRva, UINT64 ImageBase)
    {
        (void) ImageBase;
        UINT64 Dir;
        if (!RvaToOff (ImpRva, &Dir)) {
            return;
        }
        for (UINT32 I = 0; I < 4096; I++) {   // IMAGE_IMPORT_DESCRIPTOR is 20 bytes, zero-terminated
            UINT64 D       = Dir + (UINT64) I * 20;
            UINT32 NameRva = (UINT32) Rd (D + 12, 4);
            UINT32 FirstT  = (UINT32) Rd (D + 16, 4);
            UINT32 OrigT   = (UINT32) Rd (D + 0, 4);
            if (NameRva == 0 && FirstT == 0 && OrigT == 0) {
                break;
            }
            UINT64 NameOff;
            if (NameRva != 0 && RvaToOff (NameRva, &NameOff) && NameOff < m_Len) {
                UINT64 End = NameOff;
                while (End < m_Len && m_pImage[End] != '\0') { End++; }
                m_Needed.push_back (std::string ((CHAR8 CONST *) (m_pImage + NameOff),
                                                 (size_t) (End - NameOff)));
            }
        }
    }

    UINT8 CONST               *m_pImage = nullptr;
    UINT64                     m_Len    = 0;
    UINT16                     m_NSect  = 0;
    UINT64                     m_SectTab = 0;
    std::vector<std::string>   m_Needed;
    std::vector<CHAR8 CONST *> m_NeededPtrs;
};

ILoader *
CreatePeLoader (VOID)
{
    return new PeLoader ();
}

} // namespace
} // namespace LibCPU

LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreatePeLoader)
