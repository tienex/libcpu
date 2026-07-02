/** @file
  The PEF (Preferred Executable Format) container -- Classic Mac OS / CFM (PowerPC, and CFM-68K) --
  as a ".loader" module.

  A loader knows the FILE FORMAT, never a CPU: this parses the PEF container header, places each
  code/data section at its default address (or contiguously), and reports the arch named by the
  PEF architecture tag ("pwpc" -> ppc, "m68k") + abi "macos". Packed (pattern-initialised) data and
  the loader-section imports are deferred to the run-time linker; this places the raw section bytes.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/ILoader.h"
#include "LibCPU/Module.h"

#include <cstdio>
#include <cstring>

namespace LibCPU {
namespace {

enum : UINT32
{
    PEF_TAG1 = 0x4a6f7921,   // 'Joy!'
    PEF_TAG2 = 0x70656666,   // 'peff'
    PEF_ARCH_PPC  = 0x70777063,  // 'pwpc'
    PEF_ARCH_M68K = 0x6d36386b   // 'm68k'
};
enum
{
    PEF_OFF_ARCH   = 8,    // architecture tag (4 bytes)
    PEF_OFF_SECTCNT = 0x20, // section count (2 bytes)
    PEF_HDR_SZ     = 0x28, // container header size
    PEF_SECT_SZ    = 28,   // section header size
    PEF_SK_CODE    = 0,    // sectionKind: executable code
    PEF_SK_UDATA   = 1,    // unpacked data
    PEF_SK_CONST   = 3     // constant (read-only) data
};

class PefLoader final : public ComObject<ILoader>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_ILoader, ppvObject);
    }

    CHAR8 CONST *STDMETHODCALLTYPE GetName (VOID) override { return "pef"; }

    UINT32 STDMETHODCALLTYPE Probe (UINT8 CONST *pImage, UINT64 Len) override
    {
        return (Len >= PEF_HDR_SZ && Be32 (pImage, 0) == PEF_TAG1 && Be32 (pImage, 4) == PEF_TAG2)
                   ? 92 : 0;
    }

    HRESULT STDMETHODCALLTYPE Load (UINT8 CONST *pImage, UINT64 Len, ILoaderMemory *pMem,
                                    LOADER_REQUEST CONST *pRequest, LOADER_RESULT *pResult) override
    {
        if (pImage == nullptr || pMem == nullptr || pResult == nullptr) {
            return E_POINTER;
        }
        if (Probe (pImage, Len) == 0) {
            std::printf ("lcx: not a PEF container\n");
            return E_FAIL;
        }
        (void) pRequest;
        std::memset (pResult, 0, sizeof (*pResult));
        m_pImage = pImage;
        m_Len    = Len;
        UINT64 RamSize = pMem->Size ();

        UINT32 ArchTag = Be32 (pImage, PEF_OFF_ARCH);
        UINT16 NSect   = (UINT16) ((pImage[PEF_OFF_SECTCNT] << 8) | pImage[PEF_OFF_SECTCNT + 1]);

        UINT64 NextBase = 0, LoadEnd = 0, EntryBase = 0;
        bool   HaveEntry = false;
        for (UINT16 I = 0; I < NSect; I++) {
            UINT64 S = PEF_HDR_SZ + (UINT64) I * PEF_SECT_SZ;
            if (S + PEF_SECT_SZ > Len) {
                break;
            }
            UINT32 DefAddr   = Be32 (pImage, S + 4);    // defaultAddress
            UINT32 TotalSz   = Be32 (pImage, S + 8);    // totalSize (in memory)
            UINT32 PackedSz  = Be32 (pImage, S + 16);   // packedSize (in file)
            UINT32 ContOff   = Be32 (pImage, S + 20);   // containerOffset (file)
            UINT8  Kind      = pImage[S + 24];
            if (Kind != PEF_SK_CODE && Kind != PEF_SK_UDATA && Kind != PEF_SK_CONST) {
                continue;   // loader/debug/exception sections are not memory-resident code+data
            }
            UINT64 Dst = (DefAddr != 0) ? DefAddr : NextBase;
            if (TotalSz != 0 && Dst + TotalSz <= RamSize) {
                UINT64 Copy = (PackedSz < TotalSz) ? PackedSz : TotalSz;
                if (Copy != 0 && ContOff + Copy <= Len) {
                    pMem->Write (Dst, pImage + ContOff, Copy);   // raw (packed data left as-is)
                }
                if (TotalSz > Copy) {
                    pMem->Zero (Dst + Copy, TotalSz - Copy);
                }
                if (Dst + TotalSz > LoadEnd) {
                    LoadEnd = Dst + TotalSz;
                }
                if (DefAddr == 0) {
                    NextBase = Dst + ((TotalSz + 15) & ~UINT64_C (15));
                }
            }
            if (Kind == PEF_SK_CODE && !HaveEntry) {
                EntryBase = Dst; HaveEntry = true;
            }
        }

        pResult->Entry    = EntryBase;   // CFM has no plain entry; the first code section's base
        pResult->LoadEnd  = LoadEnd;
        pResult->BrkBase  = LoadEnd;
        pResult->Endian   = LoaderEndianBig;   // PEF is big-endian
        pResult->WordBits = 32;
        pResult->Arch     = (ArchTag == PEF_ARCH_PPC) ? "ppc"
                          : (ArchTag == PEF_ARCH_M68K) ? "m68k"
                          : nullptr;
        pResult->Abi      = "macos";
        pResult->AbiVendor = "apple";
        std::printf ("lcx: loaded PEF (%u sections, arch=%s, abi=macos): entry=0x%llx end=0x%llx\n",
                     (unsigned) NSect, pResult->Arch ? pResult->Arch : "?",
                     (unsigned long long) pResult->Entry, (unsigned long long) LoadEnd);
        return S_OK;
    }

private:
    static UINT32 PEF_ARCH_M68k () { return PEF_ARCH_M68K; }
    static UINT32 Be32 (UINT8 CONST *p, UINT64 O)
    {
        return (UINT32) (((UINT32) p[O] << 24) | (p[O + 1] << 16) | (p[O + 2] << 8) | p[O + 3]);
    }
    UINT8 CONST *m_pImage = nullptr;
    UINT64       m_Len    = 0;
};

ILoader *
CreatePefLoader (VOID)
{
    return new PefLoader ();
}

} // namespace
} // namespace LibCPU

LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreatePefLoader)
