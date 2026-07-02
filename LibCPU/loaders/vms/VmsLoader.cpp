/** @file
  The OpenVMS image (VAX / Alpha / IA-64) as a ".loader" module.

  A loader knows the FILE FORMAT, never a CPU: this recognises the VMS Extended Image Header (EIHD)
  and reports abi "vms" (vendor "dec"). The VMS image-section-descriptor (ISD) layout is intricate
  and version/architecture-specific; until it is parsed in full this places the image bytes verbatim
  at the requested address (default 0) -- a minimal, honest recogniser rather than a wrong parse. It
  is requested with `--load vms`.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/ILoader.h"
#include "LibCPU/Module.h"

#include <cstdio>
#include <cstring>

namespace LibCPU {
namespace {

enum
{
    // EIHD field offsets (little-endian). majorid/minorid identify the header revision.
    EIHD_OFF_MAJORID = 0x00, EIHD_OFF_MINORID = 0x04, EIHD_OFF_IMGTYPE = 0x10,
    EIHD_MAJORID     = 3     // the EIHD major id in use since VMS moved off the old IHD
};

class VmsLoader final : public ComObject<ILoader>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_ILoader, ppvObject);
    }

    CHAR8 CONST *STDMETHODCALLTYPE GetName (VOID) override { return "vms"; }

    // The EIHD has no strong magic, so VMS images are not auto-probed; request with `--load vms`.
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
            std::printf ("lcx: VMS image does not fit at 0x%llx\n", (unsigned long long) Addr);
            return E_FAIL;
        }
        if (Len != 0) {
            pMem->Write (Addr, pImage, Len);   // verbatim: ISD parsing is a follow-up
        }
        UINT32 Major = (Len >= 4) ? (UINT32) (pImage[EIHD_OFF_MAJORID] | (pImage[EIHD_OFF_MAJORID + 1] << 8)
                                              | (pImage[EIHD_OFF_MAJORID + 2] << 16)
                                              | ((UINT32) pImage[EIHD_OFF_MAJORID + 3] << 24))
                                 : 0;

        pResult->Entry     = Addr;
        pResult->LoadEnd   = Addr + Len;
        pResult->BrkBase   = Addr + Len;
        pResult->Endian    = LoaderEndianLittle;   // VAX/Alpha/IA-64 are little-endian
        pResult->Abi       = "vms";
        pResult->AbiVendor = "dec";
        std::printf ("lcx: loaded VMS image (EIHD major=%u, abi=vms) verbatim at 0x%llx "
                     "(ISD parsing pending)\n",
                     (unsigned) Major, (unsigned long long) Addr);
        return S_OK;
    }
};

ILoader *
CreateVmsLoader (VOID)
{
    return new VmsLoader ();
}

} // namespace
} // namespace LibCPU

LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreateVmsLoader)
