/** @file
  The AmigaOS HUNK executable (Motorola 68000) as a ".loader" module.

  A loader knows the FILE FORMAT, never a CPU: this parses the big-endian HUNK_HEADER, places each
  loadable hunk (CODE/DATA verbatim, BSS zero-filled) contiguously in the flat address space (Amiga
  hunks are relocatable, so the loader assigns the addresses as AmigaOS's LoadSeg would), and reports
  arch "m68k" + abi "amigaos". Relocation hunks (HUNK_RELOC32, ...) are deferred to the run-time
  linker; this is static placement.

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
    HUNK_HEADER = 0x3f3,
    HUNK_CODE   = 0x3e9, HUNK_DATA = 0x3ea, HUNK_BSS = 0x3eb,
    HUNK_RELOC32 = 0x3ec, HUNK_SYMBOL = 0x3f0, HUNK_DEBUG = 0x3f1, HUNK_END = 0x3f2
};

class HunkLoader final : public ComObject<ILoader>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_ILoader, ppvObject);
    }

    CHAR8 CONST *STDMETHODCALLTYPE GetName (VOID) override { return "hunk"; }

    UINT32 STDMETHODCALLTYPE Probe (UINT8 CONST *pImage, UINT64 Len) override
    {
        return (Len >= 4 && Be32 (pImage, 0) == HUNK_HEADER) ? 90 : 0;
    }

    HRESULT STDMETHODCALLTYPE Load (UINT8 CONST *pImage, UINT64 Len, ILoaderMemory *pMem,
                                    LOADER_REQUEST CONST *pRequest, LOADER_RESULT *pResult) override
    {
        if (pImage == nullptr || pMem == nullptr || pResult == nullptr) {
            return E_POINTER;
        }
        if (Len < 4 || Be32 (pImage, 0) != HUNK_HEADER) {
            std::printf ("lcx: not an AmigaOS HUNK image\n");
            return E_FAIL;
        }
        (void) pRequest;
        std::memset (pResult, 0, sizeof (*pResult));
        m_pImage = pImage;
        m_Len    = Len;
        UINT64 RamSize = pMem->Size ();

        // HUNK_HEADER: a NULL-terminated list of resident-library names, then table_size, first,
        // last, and (last-first+1) hunk sizes.
        UINT64 P = 4;
        while (P + 4 <= Len && Be32 (pImage, P) != 0) {   // skip library-name strings
            UINT32 Words = Be32 (pImage, P);
            P += 4 + (UINT64) Words * 4;
        }
        P += 4;                                           // the terminating 0 longword
        if (P + 12 > Len) {
            std::printf ("lcx: truncated HUNK header\n");
            return E_FAIL;
        }
        UINT32 First = Be32 (pImage, P + 4);
        UINT32 Last  = Be32 (pImage, P + 8);
        P += 12;
        UINT32 NHunk = (Last >= First) ? (Last - First + 1) : 0;
        P += (UINT64) NHunk * 4;                          // the per-hunk size table

        UINT64 NextBase = 0, LoadEnd = 0;
        UINT32 Placed = 0;
        while (P + 4 <= Len && Placed < NHunk) {
            UINT32 Type = Be32 (pImage, P) & 0x3fffffff;  // mask hunk-flags (mem-attr) bits
            P += 4;
            if (Type == HUNK_CODE || Type == HUNK_DATA) {
                UINT32 Words = (P + 4 <= Len) ? Be32 (pImage, P) : 0;
                P += 4;
                UINT64 Bytes = (UINT64) Words * 4;
                if (NextBase + Bytes <= RamSize && P + Bytes <= Len) {
                    pMem->Write (NextBase, pImage + P, Bytes);
                }
                if (NextBase + Bytes > LoadEnd) { LoadEnd = NextBase + Bytes; }
                NextBase += Bytes;
                P += Bytes;
                Placed++;
            } else if (Type == HUNK_BSS) {
                UINT32 Words = (P + 4 <= Len) ? Be32 (pImage, P) : 0;
                P += 4;
                UINT64 Bytes = (UINT64) Words * 4;
                if (NextBase + Bytes <= RamSize) { pMem->Zero (NextBase, Bytes); }
                if (NextBase + Bytes > LoadEnd) { LoadEnd = NextBase + Bytes; }
                NextBase += Bytes;
                Placed++;
            } else if (Type == HUNK_END) {
                continue;
            } else {                                       // reloc/symbol/debug: skip its payload
                UINT32 Words = (P + 4 <= Len) ? Be32 (pImage, P) : 0;
                P += 4 + (UINT64) Words * 4;
            }
        }

        pResult->Entry     = 0;   // AmigaOS enters at the first hunk
        pResult->LoadEnd   = LoadEnd;
        pResult->BrkBase   = LoadEnd;
        pResult->Endian    = LoaderEndianBig;
        pResult->WordBits  = 32;
        pResult->Arch      = "m68k";
        pResult->Abi       = "amigaos";
        pResult->AbiVendor = "commodore";
        std::printf ("lcx: loaded HUNK (%u hunks, arch=m68k, abi=amigaos): entry=0x0 end=0x%llx\n",
                     (unsigned) NHunk, (unsigned long long) LoadEnd);
        return S_OK;
    }

private:
    static UINT32 Be32 (UINT8 CONST *p, UINT64 O)
    {
        return (UINT32) (((UINT32) p[O] << 24) | (p[O + 1] << 16) | (p[O + 2] << 8) | p[O + 3]);
    }
    UINT8 CONST *m_pImage = nullptr;
    UINT64       m_Len    = 0;
};

ILoader *
CreateHunkLoader (VOID)
{
    return new HunkLoader ();
}

} // namespace
} // namespace LibCPU

LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreateHunkLoader)
