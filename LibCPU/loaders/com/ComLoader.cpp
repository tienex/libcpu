/** @file
  The flat .COM executable, as a ".loader" module: CP/M (8080/Z80/8086) and MS-DOS/DR-DOS/PTS-DOS
  .COM programs.

  A .COM file has NO header -- it is a raw memory image loaded at offset 0x100 of the program segment
  (the TPA on CP/M, just past the PSP on DOS), with the entry at 0x100. Because it has no magic it
  cannot be auto-probed; it is selected explicitly with `--load com`. A loader knows the FILE FORMAT,
  never a CPU: the .COM format is the same byte image whether the code is 8080, Z80 or 8086, so this
  reports nothing machine-specific (the host picks the arch).

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/ILoader.h"
#include "LibCPU/Module.h"

#include <cstdio>
#include <cstring>

namespace LibCPU {
namespace {

// The program-segment offset a .COM image is loaded at (past the 256-byte PSP / CP/M base page).
static UINT64 CONST COM_BASE = 0x100;

class ComLoader final : public ComObject<ILoader>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_ILoader, ppvObject);
    }

    CHAR8 CONST *STDMETHODCALLTYPE GetName (VOID) override { return "com"; }

    // A headerless flat image has no signature -- it must be requested with `--load com`.
    UINT32 STDMETHODCALLTYPE Probe (UINT8 CONST *, UINT64) override { return 0; }

    HRESULT STDMETHODCALLTYPE Load (UINT8 CONST *pImage, UINT64 Len, ILoaderMemory *pMem,
                                    LOADER_REQUEST CONST *pRequest, LOADER_RESULT *pResult) override
    {
        if (pImage == nullptr || pMem == nullptr || pResult == nullptr) {
            return E_POINTER;
        }
        (void) pRequest;   // .COM is a fixed-base flat image
        std::memset (pResult, 0, sizeof (*pResult));
        if (COM_BASE + Len > pMem->Size ()) {
            std::printf ("lcx: .COM image too large for RAM\n");
            return E_FAIL;
        }
        if (Len != 0) {
            pMem->Write (COM_BASE, pImage, Len);
        }
        pResult->Entry    = COM_BASE;
        pResult->LoadEnd  = COM_BASE + Len;
        pResult->BrkBase  = COM_BASE + Len;
        pResult->Endian   = LoaderEndianLittle;
        pResult->WordBits = 0;         // the .COM format is arch/abi-agnostic; the host picks the CPU
        pResult->Arch     = nullptr;   // ambiguous: 8080 / Z80 / 8086
        pResult->Abi      = nullptr;   // ambiguous: CP/M or DOS
        std::printf ("lcx: loaded .COM (%llu bytes at 0x%llx), entry=0x%llx\n",
                     (unsigned long long) Len, (unsigned long long) COM_BASE,
                     (unsigned long long) COM_BASE);
        return S_OK;
    }
};

ILoader *
CreateComLoader (VOID)
{
    return new ComLoader ();
}

} // namespace
} // namespace LibCPU

LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreateComLoader)
