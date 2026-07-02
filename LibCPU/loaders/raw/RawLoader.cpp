/** @file
  The raw/blob loader, as a ".loader" module: places an unstructured payload verbatim at a chosen
  guest address. It is what backs qemu-style ramdisk/initrd, firmware and device-tree-blob loading,
  and headerless raw kernels.

  A blob has no header, so it is never auto-probed -- it is requested with `--load raw` (or used by
  the host for `--initrd`). It carries no architecture/ABI of its own; placement comes from the
  request's LoadAddr (0 = the base of RAM).

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/ILoader.h"
#include "LibCPU/Module.h"

#include <cstdio>
#include <cstring>

namespace LibCPU {
namespace {

class RawLoader final : public ComObject<ILoader>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_ILoader, ppvObject);
    }

    CHAR8 CONST *STDMETHODCALLTYPE GetName (VOID) override { return "raw"; }

    // A headerless blob has no signature; it must be requested explicitly.
    UINT32 STDMETHODCALLTYPE Probe (UINT8 CONST *, UINT64) override { return 0; }

    HRESULT STDMETHODCALLTYPE Load (UINT8 CONST *pImage, UINT64 Len, ILoaderMemory *pMem,
                                    LOADER_REQUEST CONST *pRequest, LOADER_RESULT *pResult) override
    {
        if (pImage == nullptr || pMem == nullptr || pResult == nullptr) {
            return E_POINTER;
        }
        std::memset (pResult, 0, sizeof (*pResult));
        UINT64 Addr = (pRequest != nullptr) ? pRequest->LoadAddr : 0;
        if (Addr + Len > pMem->Size () || Addr + Len < Addr) {
            std::printf ("lcx: raw blob does not fit at 0x%llx\n", (unsigned long long) Addr);
            return E_FAIL;
        }
        if (Len != 0) {
            pMem->Write (Addr, pImage, Len);
        }
        pResult->Entry   = Addr;   // a headerless kernel enters at its load address
        pResult->LoadEnd = Addr + Len;
        pResult->BrkBase = Addr + Len;
        std::printf ("lcx: loaded raw blob (%llu bytes at 0x%llx)\n", (unsigned long long) Len,
                     (unsigned long long) Addr);
        return S_OK;
    }
};

ILoader *
CreateRawLoader (VOID)
{
    return new RawLoader ();
}

} // namespace
} // namespace LibCPU

LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreateRawLoader)
