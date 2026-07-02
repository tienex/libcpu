/** @file
  The CP/M-86 .CMD executable, as a ".loader" module.

  A .CMD file begins with a 128-byte header record of up to 8 group descriptors (9 bytes each:
  g_type, g_length, g_base, g_min, g_max -- lengths/addresses in 16-byte paragraphs, little-endian);
  the group data follows the header in descriptor order. A loader knows the FILE FORMAT, never a CPU:
  this places each group's bytes at its base paragraph (or contiguously from 0 when the base is 0,
  the relocatable "8080 model") and reports the code group as the entry. It has no magic, so it is
  requested with `--load cmd`.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/ILoader.h"
#include "LibCPU/Module.h"

#include <cstdio>
#include <cstring>

namespace LibCPU {
namespace {

enum { GT_CODE = 1, GT_DATA = 2, GT_EXTRA = 3, GT_STACK = 4, GT_ESCAPE = 9 };

class CmdLoader final : public ComObject<ILoader>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_ILoader, ppvObject);
    }

    CHAR8 CONST *STDMETHODCALLTYPE GetName (VOID) override { return "cmd"; }

    // .CMD has no signature; request it with `--load cmd`.
    UINT32 STDMETHODCALLTYPE Probe (UINT8 CONST *, UINT64) override { return 0; }

    HRESULT STDMETHODCALLTYPE Load (UINT8 CONST *pImage, UINT64 Len, ILoaderMemory *pMem,
                                    LOADER_REQUEST CONST *pRequest, LOADER_RESULT *pResult) override
    {
        if (pImage == nullptr || pMem == nullptr || pResult == nullptr) {
            return E_POINTER;
        }
        if (Len < 128) {
            std::printf ("lcx: .CMD too small (no header record)\n");
            return E_FAIL;
        }
        (void) pRequest;   // fixed group layout
        std::memset (pResult, 0, sizeof (*pResult));
        UINT64 RamSize = pMem->Size ();

        UINT64 FileData = 128;         // group data begins right after the header record
        UINT64 NextBase = 0;           // running paragraph base for relocatable (base 0) groups
        UINT64 LoadEnd  = 0;
        UINT64 CodeLinear = 0;
        bool   HaveCode = false;
        UINT32 NGroups  = 0;

        for (UINT32 I = 0; I < 8; I++) {
            UINT64 D    = (UINT64) I * 9;
            UINT8  Type = pImage[D];
            if (Type == 0) {
                break;                 // unused descriptor => end of table
            }
            UINT64 LenPar  = Rd16 (pImage, D + 1);   // group length, in paragraphs
            UINT64 BasePar = Rd16 (pImage, D + 3);   // group base paragraph (0 => relocatable)
            UINT64 Bytes   = LenPar * 16;
            NGroups++;

            UINT64 Base = (BasePar != 0) ? BasePar : NextBase;
            UINT64 Dst  = Base * 16;
            if (Bytes != 0) {
                if (Dst + Bytes > RamSize || FileData + Bytes > Len) {
                    std::printf ("lcx: .CMD group %u out of range\n", (unsigned) I);
                    return E_FAIL;
                }
                pMem->Write (Dst, pImage + FileData, Bytes);
                FileData += Bytes;
                if (Dst + Bytes > LoadEnd) {
                    LoadEnd = Dst + Bytes;
                }
            }
            if ((Type == GT_CODE || Type == GT_ESCAPE) && !HaveCode) {
                CodeLinear = Dst;      // CP/M-86 enters at CS:0000 of the code group
                HaveCode   = true;
            }
            if (BasePar == 0) {
                NextBase += LenPar;    // pack relocatable groups back to back
            }
        }

        pResult->Arch     = "8086";   // CP/M-86 is 16-bit x86
        pResult->Abi      = "cpm";
        pResult->Entry    = HaveCode ? CodeLinear : 0;
        pResult->LoadEnd  = LoadEnd;
        pResult->BrkBase  = LoadEnd;
        pResult->Endian   = LoaderEndianLittle;
        pResult->WordBits = 16;
        std::printf ("lcx: loaded CP/M-86 .CMD (%u groups): entry=0x%llx end=0x%llx\n",
                     (unsigned) NGroups, (unsigned long long) pResult->Entry,
                     (unsigned long long) LoadEnd);
        return S_OK;
    }

private:
    static UINT16 Rd16 (UINT8 CONST *p, UINT64 O) { return (UINT16) (p[O] | (p[O + 1] << 8)); }
};

ILoader *
CreateCmdLoader (VOID)
{
    return new CmdLoader ();
}

} // namespace
} // namespace LibCPU

LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreateCmdLoader)
