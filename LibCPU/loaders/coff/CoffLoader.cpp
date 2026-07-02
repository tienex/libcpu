/** @file
  The COFF family -- classic COFF, ECOFF (MIPS/Alpha) and XCOFF32 (AIX/PowerPC) -- as a ".loader"
  module.

  A loader knows the FILE FORMAT, never a CPU: this reads the COFF file header (auto-detecting byte
  order from the f_magic value), lays the loadable sections out at their s_vaddr through the
  ILoaderMemory sink, takes the entry from the a.out-style optional header, and REPORTS the f_magic
  (machine) + endian + word width. It never interprets the machine.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/ILoader.h"
#include "LibCPU/Module.h"

#include <cstdio>
#include <cstring>

namespace LibCPU {
namespace {

// COFF f_magic (machine) values, used only to recognise the format + its byte order + name it.
enum
{
    COFF_MAG_I386    = 0x014c, COFF_MAG_M68K    = 0x0150, COFF_MAG_MIPSEB  = 0x0160,
    COFF_MAG_MIPSEL  = 0x0162, COFF_MAG_MIPSEL3 = 0x0166, COFF_MAG_ALPHA   = 0x0183,
    COFF_MAG_ALPHA2  = 0x0184, COFF_MAG_XCOFF32 = 0x01df, COFF_MAG_XCOFF64 = 0x01f7,
    COFF_MAG_WE32K   = 0x0170, COFF_MAG_WE32K2  = 0x0175, COFF_MAG_CLIPPER = 0x017f,
    COFF_MAG_M68KSV  = 0x0268, COFF_MAG_SH      = 0x0500
};

// Heap-break page-alignment granularity (a loader convention, not a CPU page size).
static UINT64 CONST kCoffPageMask = 0xfff;

static bool
CoffMagicKnown (UINT16 M)
{
    switch (M) {
    case COFF_MAG_I386:  case COFF_MAG_M68K:    case COFF_MAG_MIPSEB: case COFF_MAG_MIPSEL:
    case COFF_MAG_MIPSEL3: case COFF_MAG_ALPHA: case COFF_MAG_ALPHA2: case COFF_MAG_XCOFF32:
    case COFF_MAG_XCOFF64: case COFF_MAG_WE32K: case COFF_MAG_WE32K2: case COFF_MAG_CLIPPER:
    case COFF_MAG_M68KSV: case COFF_MAG_SH:
        return true;
    default:
        return false;
    }
}

// Canonical architecture name for a COFF f_magic. Never acted on.
static CHAR8 CONST *
CoffArch (UINT16 M)
{
    switch (M) {
    case COFF_MAG_I386:    return "i386";
    case COFF_MAG_M68K:    case COFF_MAG_M68KSV: return "m68k";
    case COFF_MAG_MIPSEB:  case COFF_MAG_MIPSEL: case COFF_MAG_MIPSEL3: return "mips";
    case COFF_MAG_ALPHA:   case COFF_MAG_ALPHA2: return "alpha";
    case COFF_MAG_XCOFF32: return "ppc";
    case COFF_MAG_XCOFF64: return "ppc64";
    case COFF_MAG_WE32K:   case COFF_MAG_WE32K2: return "we32k";
    case COFF_MAG_CLIPPER: return "clipper";
    case COFF_MAG_SH:      return "sh";
    default:               return nullptr;
    }
}

// XCOFF is AIX; the rest of the COFF family is System V-derived UNIX.
static CHAR8 CONST *
CoffAbi (UINT16 M)
{
    return (M == COFF_MAG_XCOFF32 || M == COFF_MAG_XCOFF64) ? "aix" : "sysv";
}

class CoffLoader final : public ComObject<ILoader>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_ILoader, ppvObject);
    }

    CHAR8 CONST *STDMETHODCALLTYPE GetName (VOID) override { return "coff"; }

    UINT32 STDMETHODCALLTYPE Probe (UINT8 CONST *pImage, UINT64 Len) override
    {
        if (Len < 20 || (pImage[0] == 'M' && pImage[1] == 'Z')) {
            return 0;   // an MZ prefix is a PE/NE/LE/LX, not a bare COFF
        }
        bool   Big;
        UINT16 Magic;
        if (!DetectMagic (pImage, &Magic, &Big)) {
            return 0;
        }
        // Sanity-check the header so a weak 2-byte magic does not false-match.
        UINT16 NScns  = Rd16 (pImage, 2, Big);
        UINT16 OptSz  = Rd16 (pImage, 16, Big);
        if (NScns > 96 || OptSz > 512) {
            return 0;
        }
        return 70;   // below ELF/Mach-O/PE (stronger magics)
    }

    HRESULT STDMETHODCALLTYPE Load (UINT8 CONST *pImage, UINT64 Len, ILoaderMemory *pMem,
                                    LOADER_REQUEST CONST *pRequest, LOADER_RESULT *pResult) override
    {
        if (pImage == nullptr || pMem == nullptr || pResult == nullptr) {
            return E_POINTER;
        }
        UINT16 Magic;
        if (Len < 20 || (pImage[0] == 'M' && pImage[1] == 'Z') || !DetectMagic (pImage, &Magic, &m_Big)) {
            std::printf ("lcx: not a COFF image\n");
            return E_FAIL;
        }
        std::memset (pResult, 0, sizeof (*pResult));
        m_pImage = pImage;
        m_Len    = Len;
        UINT64 RamSize = pMem->Size ();

        UINT16 NScns   = (UINT16) Rd (2, 2);
        UINT16 OptSz   = (UINT16) Rd (16, 2);
        UINT64 OptHdr  = 20;
        UINT64 SectTab = OptHdr + OptSz;
        // The a.out-style optional header carries the entry at offset 16 (magic, vstamp, tsize,
        // dsize, bsize, entry) in both classic COFF and ECOFF.
        UINT64 Entry   = (OptSz >= 20) ? Rd (OptHdr + 16, 4) : 0;

        UINT64 LoadEnd = 0;
        for (UINT16 I = 0; I < NScns; I++) {
            UINT64 S = SectTab + (UINT64) I * 40;
            if (S + 40 > Len) {
                break;
            }
            UINT64 VAddr  = Rd (S + 12, 4);   // s_vaddr
            UINT64 SSize  = Rd (S + 16, 4);   // s_size
            UINT64 ScnPtr = Rd (S + 20, 4);   // s_scnptr (0 => .bss, no file data)
            UINT32 Flags  = (UINT32) Rd (S + 36, 4);
            if (SSize == 0 || ScnPtr == 0 || (Flags & 0x80) != 0) {   // STYP_BSS = 0x80, or no file data
                if (SSize != 0 && VAddr + SSize <= RamSize) {
                    pMem->Zero (VAddr, SSize);
                    if (VAddr + SSize > LoadEnd) { LoadEnd = VAddr + SSize; }
                }
                continue;
            }
            if (VAddr + SSize > RamSize || VAddr + SSize < VAddr) {
                std::printf ("lcx: COFF section out of range\n");
                return E_FAIL;
            }
            if (ScnPtr != 0 && ScnPtr + SSize <= Len) {
                pMem->Write (VAddr, pImage + ScnPtr, SSize);
            } else {
                pMem->Zero (VAddr, SSize);
            }
            if (VAddr + SSize > LoadEnd) {
                LoadEnd = VAddr + SSize;
            }
        }

        (void) pRequest;   // kernel/blob placement is a follow-up for COFF
        pResult->Entry    = Entry;
        pResult->LoadEnd  = LoadEnd;
        pResult->BrkBase  = (LoadEnd + kCoffPageMask) & ~kCoffPageMask;
        pResult->Endian   = m_Big ? LoaderEndianBig : LoaderEndianLittle;
        pResult->WordBits = (Magic == COFF_MAG_XCOFF64) ? 64 : 32;
        pResult->Arch     = CoffArch (Magic);   // canonical name, never interpreted
        pResult->Abi      = CoffAbi (Magic);
        // Classic COFF/ECOFF executables are statically linked; XCOFF imports live in a .loader
        // section whose parse is deferred to the run-time-linker phase.
        pResult->Dynamic.IsDynamic = 0;

        std::printf ("lcx: loaded COFF (arch=%s, abi=%s, %s, %s): entry=0x%llx end=0x%llx\n",
                     pResult->Arch ? pResult->Arch : "?", pResult->Abi ? pResult->Abi : "?",
                     m_Big ? "BE" : "LE", (Magic == COFF_MAG_XCOFF64) ? "64-bit" : "32-bit",
                     (unsigned long long) Entry, (unsigned long long) LoadEnd);
        return S_OK;
    }

private:
    static UINT16 Rd16 (UINT8 CONST *p, UINT64 O, bool Big)
    {
        return Big ? (UINT16) ((p[O] << 8) | p[O + 1]) : (UINT16) (p[O] | (p[O + 1] << 8));
    }

    // Recognise a COFF f_magic at offset 0 in either byte order.
    static bool DetectMagic (UINT8 CONST *p, UINT16 *pMagic, bool *pBig)
    {
        UINT16 Le = (UINT16) (p[0] | (p[1] << 8));
        UINT16 Be = (UINT16) ((p[0] << 8) | p[1]);
        if (CoffMagicKnown (Le)) { *pMagic = Le; *pBig = false; return true; }
        if (CoffMagicKnown (Be)) { *pMagic = Be; *pBig = true;  return true; }
        return false;
    }

    UINT64 Rd (UINT64 Off, UINT32 Size)
    {
        if (Off + Size > m_Len) {
            return 0;
        }
        UINT64 V = 0;
        if (m_Big) {
            for (UINT32 I = 0; I < Size; I++) { V = (V << 8) | m_pImage[Off + I]; }
        } else {
            for (UINT32 I = 0; I < Size; I++) { V |= (UINT64) m_pImage[Off + I] << (8 * I); }
        }
        return V;
    }

    UINT8 CONST *m_pImage = nullptr;
    UINT64       m_Len    = 0;
    bool         m_Big    = false;
};

ILoader *
CreateCoffLoader (VOID)
{
    return new CoffLoader ();
}

} // namespace
} // namespace LibCPU

LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreateCoffLoader)
