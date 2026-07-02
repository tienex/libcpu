/** @file
  The classic Bell-Labs a.out family, as a ".loader" module.

  A loader knows the FILE FORMAT, never a CPU: this parses the a.out header, lays the segments out
  per the a.out magic, and REPORTS the machine id / byte order / word width it read (in
  LOADER_RESULT) for the host to act on. It never branches on, or names, a particular architecture.

  Two header widths are recognised (a format property, told apart from the leading bytes):

    * 16-bit a.out -- an 8-word (16-byte) little-endian header
         a_magic a_text a_data a_bss a_syms a_entry a_unused a_flag
      0407 OMAGIC -- impure: text+data contiguous at 0 (text writable);
      0410 NMAGIC -- pure:   text at 0, data at the next segment click, bss above;
      0411 split I&D / 0413  -- modelled like NMAGIC in one flat space.
      (No machine-id field; MachineHint is left 0.)

    * 32-bit a.out -- a 32-byte header whose first big-endian word packs {mid, magic}. ZMAGIC 0x10b
      maps the whole file at 0x1000; OMAGIC/NMAGIC strip the header and load at 0. The mid and byte
      order are reported, not interpreted.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/ILoader.h"
#include "LibCPU/Module.h"

#include <cstdio>
#include <cstring>

namespace LibCPU {
namespace {

// A 16-bit a.out's segment click (the granularity text/data are aligned to for the pure magics).
// It is a property of the 16-bit a.out ABI, not of any CPU.
static UINT64 CONST AOUT16_CLICK = 020000;   // 8 KiB

// a.out machine ids (a_info mid field) named for arch reporting; heap page-align granularity.
enum { AOUT_MID_M88K = 153 };
static UINT64 CONST kAoutPageMask = 0xfff;

class AoutLoader final : public ComObject<ILoader>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_ILoader, ppvObject);
    }

    CHAR8 CONST *STDMETHODCALLTYPE GetName (VOID) override { return "aout"; }

    UINT32 STDMETHODCALLTYPE Probe (UINT8 CONST *pImage, UINT64 Len) override
    {
        if (Len >= 32 && Is32BitAout (pImage)) {
            return 85;
        }
        if (Len >= 16 && Is16BitAout (pImage)) {
            return 80;
        }
        return 0;
    }

    HRESULT STDMETHODCALLTYPE Load (UINT8 CONST *pImage, UINT64 Len, ILoaderMemory *pMem,
                                    LOADER_REQUEST CONST *pRequest, LOADER_RESULT *pResult) override
    {
        if (pImage == nullptr || pMem == nullptr || pResult == nullptr) {
            return E_POINTER;
        }
        (void) pRequest;   // a.out is thin; kernel/blob placement is a follow-up
        std::memset (pResult, 0, sizeof (*pResult));
        UINT64 RamSize = pMem->Size ();

        if (Len >= 32 && Is32BitAout (pImage)) {
            return Load32 (pImage, Len, pMem, RamSize, pResult);
        }
        if (Len >= 16 && Is16BitAout (pImage)) {
            return Load16 (pImage, Len, pMem, RamSize, pResult);
        }
        std::printf ("lcx: not an a.out image\n");
        return E_FAIL;
    }

private:
    static bool Is16BitAout (UINT8 CONST *pImage)
    {
        UINT16 Magic = (UINT16) (pImage[0] | (pImage[1] << 8));
        switch (Magic) {
        case 0407: case 0410: case 0411: case 0405: case 0413: case 0430: case 0431:
            return true;
        default:
            return false;
        }
    }

    // A 32-bit a.out header's first big-endian word is {mid:10, magic:16 (+ flags)}. We recognise it
    // by a known object magic in the low half; the mid names the machine but we do not act on it.
    static bool Is32BitAout (UINT8 CONST *pImage)
    {
        UINT32 W     = (UINT32) ((pImage[0] << 24) | (pImage[1] << 16) | (pImage[2] << 8) | pImage[3]);
        UINT32 Magic = W & 0xffff;
        return (Magic == 0x107 || Magic == 0x108 || Magic == 0x10b);   // OMAGIC/NMAGIC/ZMAGIC
    }

    static HRESULT Load16 (UINT8 CONST *pImage, UINT64 Len, ILoaderMemory *pMem, UINT64 RamSize,
                           LOADER_RESULT *pResult)
    {
        auto   W   = [&] (UINT64 O) -> UINT32 { return (UINT32) (pImage[O] | (pImage[O + 1] << 8)); };
        UINT32 Mag = W (0), ATxt = W (2), AData = W (4), ABss = W (6), AEntry = W (10);
        UINT64 Hdr = 16;
        UINT64 Seg = (UINT64) ATxt + AData;
        if (Hdr + Seg > Len) {
            std::printf ("lcx: a.out truncated (text+data)\n");
            return E_FAIL;
        }

        UINT64      BssEnd, LoadEnd;
        CHAR8 CONST *pKind;
        if (Mag == 0407) {                                   // OMAGIC: text+data contiguous at 0
            if (Seg + ABss > RamSize) {
                std::printf ("lcx: a.out too large\n");
                return E_FAIL;
            }
            pMem->Write (0, pImage + Hdr, Seg);
            pMem->Zero (Seg, ABss);
            BssEnd = Seg + ABss; LoadEnd = Seg; pKind = "OMAGIC";
        } else {                                             // NMAGIC / split I&D: data at next click
            UINT64 DataBase = (ATxt + AOUT16_CLICK - 1) & ~(AOUT16_CLICK - 1);
            BssEnd          = DataBase + AData + ABss;
            if (BssEnd > RamSize) {
                std::printf ("lcx: a.out too large\n");
                return E_FAIL;
            }
            pMem->Write (0, pImage + Hdr, ATxt);                 // text at 0
            pMem->Write (DataBase, pImage + Hdr + ATxt, AData);  // data at the next click
            pMem->Zero (DataBase + AData, ABss);
            LoadEnd = DataBase + AData;
            pKind = (Mag == 0410) ? "NMAGIC" : (Mag == 0411 ? "0411 split-I/D" : "0413");
        }
        pResult->Entry    = AEntry;
        pResult->LoadEnd  = LoadEnd;
        pResult->BrkBase  = (BssEnd + 1) & ~UINT64_C (1);   // word-align the heap
        pResult->Endian   = LoaderEndianLittle;
        pResult->WordBits = 16;
        pResult->Arch     = "pdp11";   // the 16-bit a.out is the classic PDP-11 exec
        pResult->Abi      = "unix";    // classic (V6/V7-lineage) UNIX
        std::printf ("lcx: loaded a.out (16-bit %s, arch=pdp11): text=0%o data=0%o bss=0%o entry=0%o\n",
                     pKind, (unsigned) ATxt, (unsigned) AData, (unsigned) ABss, (unsigned) AEntry);
        return S_OK;
    }

    static HRESULT Load32 (UINT8 CONST *pImage, UINT64 Len, ILoaderMemory *pMem, UINT64 RamSize,
                           LOADER_RESULT *pResult)
    {
        auto Be32 = [&] (UINT64 O) -> UINT32 {
            return (UINT32) ((pImage[O] << 24) | (pImage[O + 1] << 16) | (pImage[O + 2] << 8) | pImage[O + 3]);
        };
        UINT32 MidMag = Be32 (0);
        UINT32 Mid    = (MidMag >> 16) & 0x3ff;
        UINT32 Magic  = MidMag & 0xffff;
        UINT32 ATxt = Be32 (4), AData = Be32 (8), ABss = Be32 (12), AEntry = Be32 (20);
        UINT64 Seg  = (UINT64) ATxt + AData;

        UINT64      TxtBase, BssEnd;
        CHAR8 CONST *pKind;
        if (Magic == 0x10b) {                                // ZMAGIC: map whole file at the text base
            TxtBase = 0x1000;
            if (Len < Seg) {
                std::printf ("lcx: a.out truncated (text+data)\n");
                return E_FAIL;
            }
            if (TxtBase + Seg + ABss > RamSize) {
                std::printf ("lcx: a.out too large for RAM\n");
                return E_FAIL;
            }
            pMem->Zero (0, TxtBase);
            pMem->Write (TxtBase, pImage, Seg);
            pMem->Zero (TxtBase + Seg, ABss);
            BssEnd = TxtBase + Seg + ABss;
            pKind  = "ZMAGIC";
        } else {                                             // OMAGIC/NMAGIC: strip header, load at 0
            TxtBase = 0;
            if (32 + Seg > RamSize) {
                std::printf ("lcx: a.out too large for RAM\n");
                return E_FAIL;
            }
            pMem->Write (0, pImage + 32, Seg);
            pMem->Zero (Seg, ABss);
            BssEnd = Seg + ABss;
            pKind  = (Magic == 0x108) ? "NMAGIC" : "OMAGIC";
        }
        pResult->Entry    = AEntry;
        pResult->LoadEnd  = TxtBase + Seg;
        pResult->BrkBase  = (BssEnd + kAoutPageMask) & ~kAoutPageMask;   // page-align the heap base
        pResult->Endian   = LoaderEndianBig;
        pResult->WordBits = 32;
        pResult->Arch     = (Mid == AOUT_MID_M88K) ? "m88k" : nullptr;   // reported from mid
        std::printf ("lcx: loaded a.out (32-bit %s, mid=%u, arch=%s): text=0x%x data=0x%x bss=0x%x entry=0x%x base=0x%llx\n",
                     pKind, (unsigned) Mid, pResult->Arch ? pResult->Arch : "?", (unsigned) ATxt,
                     (unsigned) AData, (unsigned) ABss, (unsigned) AEntry, (unsigned long long) TxtBase);
        return S_OK;
    }
};

ILoader *
CreateAoutLoader (VOID)
{
    return new AoutLoader ();
}

} // namespace
} // namespace LibCPU

LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreateAoutLoader)
