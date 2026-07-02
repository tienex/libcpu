/** @file
  The HP-UX SOM (System Object Module) executable -- PA-RISC -- as a ".loader" module.

  A loader knows the FILE FORMAT, never a CPU: this recognises the big-endian SOM file header
  (system_id + a_magic), places the text and data described by the exec auxiliary header, and reports
  arch "hppa" + abi "hpux" (vendor "hp"), with the entry from the auxiliary header. Space/subspace
  relocation and shared-library binding are deferred to the run-time linker.

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
    // PA-RISC system_id values.
    SOM_SID_PA10 = 0x020b, SOM_SID_PA11 = 0x0210, SOM_SID_PA20 = 0x0214,
    // a_magic values.
    SOM_MAG_SHARE = 0x0104, SOM_MAG_EXEC = 0x0107, SOM_MAG_DEMAND = 0x010b,
    // SOM file-header field offsets (big-endian).
    SOM_OFF_AUXLOC = 0x28,  // aux_header_location (file offset of the aux headers)
    SOM_OFF_AUXSZ  = 0x2c,  // aux_header_size
    // exec auxiliary header (HP_AUX_HEADER) field offsets, relative to the aux header start.
    SOM_AUX_TSIZE = 0x08, SOM_AUX_TMEM = 0x0c, SOM_AUX_TFILE = 0x10,
    SOM_AUX_DSIZE = 0x14, SOM_AUX_DMEM = 0x18, SOM_AUX_DFILE = 0x1c,
    SOM_AUX_BSIZE = 0x20, SOM_AUX_ENTRY = 0x24
};

class SomLoader final : public ComObject<ILoader>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_ILoader, ppvObject);
    }

    CHAR8 CONST *STDMETHODCALLTYPE GetName (VOID) override { return "som"; }

    UINT32 STDMETHODCALLTYPE Probe (UINT8 CONST *pImage, UINT64 Len) override
    {
        if (Len < 0x30) {
            return 0;
        }
        UINT16 Sid = Be16 (pImage, 0), Mag = Be16 (pImage, 2);
        bool   SidOk = (Sid == SOM_SID_PA10 || Sid == SOM_SID_PA11 || Sid == SOM_SID_PA20);
        bool   MagOk = (Mag == SOM_MAG_SHARE || Mag == SOM_MAG_EXEC || Mag == SOM_MAG_DEMAND);
        return (SidOk && MagOk) ? 85 : 0;
    }

    HRESULT STDMETHODCALLTYPE Load (UINT8 CONST *pImage, UINT64 Len, ILoaderMemory *pMem,
                                    LOADER_REQUEST CONST *pRequest, LOADER_RESULT *pResult) override
    {
        if (pImage == nullptr || pMem == nullptr || pResult == nullptr) {
            return E_POINTER;
        }
        if (Probe (pImage, Len) == 0) {
            std::printf ("lcx: not a SOM image\n");
            return E_FAIL;
        }
        (void) pRequest;
        std::memset (pResult, 0, sizeof (*pResult));
        m_pImage = pImage;
        m_Len    = Len;
        UINT64 RamSize = pMem->Size ();

        UINT16 Sid = Be16 (pImage, 0);
        UINT64 Aux = Be32 (pImage, SOM_OFF_AUXLOC);
        UINT64 LoadEnd = 0, Entry = 0;
        if (Aux != 0 && Aux + SOM_AUX_ENTRY + 4 <= Len) {
            UINT64 TMem = Be32 (pImage, Aux + SOM_AUX_TMEM), TSz = Be32 (pImage, Aux + SOM_AUX_TSIZE);
            UINT64 TFile = Be32 (pImage, Aux + SOM_AUX_TFILE);
            UINT64 DMem = Be32 (pImage, Aux + SOM_AUX_DMEM), DSz = Be32 (pImage, Aux + SOM_AUX_DSIZE);
            UINT64 DFile = Be32 (pImage, Aux + SOM_AUX_DFILE), BSz = Be32 (pImage, Aux + SOM_AUX_BSIZE);
            Entry = Be32 (pImage, Aux + SOM_AUX_ENTRY);
            if (TSz != 0 && TMem + TSz <= RamSize && TFile + TSz <= Len) {
                pMem->Write (TMem, pImage + TFile, TSz);
                if (TMem + TSz > LoadEnd) { LoadEnd = TMem + TSz; }
            }
            if (DSz != 0 && DMem + DSz <= RamSize && DFile + DSz <= Len) {
                pMem->Write (DMem, pImage + DFile, DSz);
                if (DMem + DSz > LoadEnd) { LoadEnd = DMem + DSz; }
            }
            if (BSz != 0 && DMem + DSz + BSz <= RamSize) {
                pMem->Zero (DMem + DSz, BSz);
                if (DMem + DSz + BSz > LoadEnd) { LoadEnd = DMem + DSz + BSz; }
            }
        }

        pResult->Entry     = Entry;
        pResult->LoadEnd   = LoadEnd;
        pResult->BrkBase   = LoadEnd;
        pResult->Endian    = LoaderEndianBig;
        pResult->WordBits  = 32;
        pResult->Arch      = "hppa";
        pResult->Abi       = "hpux";
        pResult->AbiVendor = "hp";
        std::printf ("lcx: loaded SOM (system_id=0x%x, arch=hppa, abi=hpux): entry=0x%llx end=0x%llx\n",
                     (unsigned) Sid, (unsigned long long) Entry, (unsigned long long) LoadEnd);
        return S_OK;
    }

private:
    static UINT16 Be16 (UINT8 CONST *p, UINT64 O) { return (UINT16) ((p[O] << 8) | p[O + 1]); }
    static UINT32 Be32 (UINT8 CONST *p, UINT64 O)
    {
        return (UINT32) (((UINT32) p[O] << 24) | (p[O + 1] << 16) | (p[O + 2] << 8) | p[O + 3]);
    }
    UINT8 CONST *m_pImage = nullptr;
    UINT64       m_Len    = 0;
};

ILoader *
CreateSomLoader (VOID)
{
    return new SomLoader ();
}

} // namespace
} // namespace LibCPU

LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreateSomLoader)
