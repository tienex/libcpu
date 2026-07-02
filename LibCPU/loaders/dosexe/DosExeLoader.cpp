/** @file
  The DOS MZ .EXE executable, as a ".loader" module: MS-DOS / DR-DOS / PTS-DOS real-mode programs.

  A loader knows the FILE FORMAT, never a CPU: this reads the MZ header, copies the load module (the
  bytes after the header) into RAM, applies the segment relocations against a load base, and reports
  the CS:IP entry. A plain MZ image is claimed only when it is NOT a "new-executable" container (PE /
  NE / LE / LX), whose e_lfanew points at a distinct signature and which have their own loaders.

  The module is placed at load segment 0 (linear 0), so the segment values authored relative to the
  module become absolute and the relocations are a no-op add of 0; the entry is e_cs*16 + e_ip. (A
  real PSP/environment is the DOS personality's job, not the loader's.)

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/ILoader.h"
#include "LibCPU/Module.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace LibCPU {
namespace {

class DosExeLoader final : public ComObject<ILoader>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_ILoader, ppvObject);
    }

    CHAR8 CONST *STDMETHODCALLTYPE GetName (VOID) override { return "dosexe"; }

    UINT32 STDMETHODCALLTYPE Probe (UINT8 CONST *pImage, UINT64 Len) override
    {
        return IsPlainMz (pImage, Len) ? 80 : 0;   // below PE (which claims MZ+PE\0\0 at 90)
    }

    HRESULT STDMETHODCALLTYPE Load (UINT8 CONST *pImage, UINT64 Len, ILoaderMemory *pMem,
                                    LOADER_REQUEST CONST *pRequest, LOADER_RESULT *pResult) override
    {
        if (pImage == nullptr || pMem == nullptr || pResult == nullptr) {
            return E_POINTER;
        }
        if (!IsPlainMz (pImage, Len)) {
            std::printf ("lcx: not a plain DOS MZ .EXE\n");
            return E_FAIL;
        }
        (void) pRequest;   // fixed real-mode layout
        std::memset (pResult, 0, sizeof (*pResult));
        UINT64 RamSize = pMem->Size ();

        UINT16 Blocks  = Rd16 (pImage, 4);    // e_cp: 512-byte pages in the file
        UINT16 LastPag = Rd16 (pImage, 2);    // e_cblp: bytes used in the last page
        UINT16 NReloc  = Rd16 (pImage, 6);    // e_crlc
        UINT16 HdrPar  = Rd16 (pImage, 8);    // e_cparhdr: header size in paragraphs
        UINT16 Ss      = Rd16 (pImage, 14);
        UINT16 Sp      = Rd16 (pImage, 16);
        UINT16 Ip      = Rd16 (pImage, 20);   // e_ip
        UINT16 Cs      = Rd16 (pImage, 22);   // e_cs
        UINT16 RelOff  = Rd16 (pImage, 24);   // e_lfarlc: reloc table file offset

        UINT64 HdrSz   = (UINT64) HdrPar * 16;
        UINT64 ImgSz   = (LastPag != 0) ? ((UINT64) (Blocks - 1) * 512 + LastPag)
                                        : ((UINT64) Blocks * 512);
        if (ImgSz > Len) {
            ImgSz = Len;                       // trust the file if the header over-states it
        }
        if (HdrSz > ImgSz) {
            std::printf ("lcx: DOS .EXE header larger than image\n");
            return E_FAIL;
        }
        UINT64 ModSz = ImgSz - HdrSz;          // the load module

        // Load segment 0: the module goes at linear 0.
        UINT16 LoadSeg = 0;
        if ((UINT64) LoadSeg * 16 + ModSz > RamSize) {
            std::printf ("lcx: DOS .EXE too large for RAM\n");
            return E_FAIL;
        }

        // Copy the module and apply relocations to the copy first (the sink is write-only). Each
        // relocation entry is (offset:2, segment:2); the pointed-to 16-bit word inside the module
        // gets the load base segment added. With LoadSeg 0 this is a no-op, but it is correct for
        // any base.
        std::vector<UINT8> Mod (pImage + HdrSz, pImage + HdrSz + ModSz);
        for (UINT16 I = 0; I < NReloc; I++) {
            UINT64 E = RelOff + (UINT64) I * 4;
            if (E + 4 > Len) {
                break;
            }
            UINT64 At = (UINT64) Rd16 (pImage, E + 2) * 16 + Rd16 (pImage, E);   // offset within module
            if (At + 2 <= Mod.size ()) {
                UINT16 W = (UINT16) (Mod[At] | (Mod[At + 1] << 8));
                W = (UINT16) (W + LoadSeg);
                Mod[At]     = (UINT8) W;
                Mod[At + 1] = (UINT8) (W >> 8);
            }
        }
        if (!Mod.empty ()) {
            pMem->Write ((UINT64) LoadSeg * 16, Mod.data (), Mod.size ());
        }

        pResult->Entry    = (UINT64) (LoadSeg + Cs) * 16 + Ip;
        pResult->LoadEnd  = LoadSeg * 16 + ModSz;
        pResult->BrkBase  = LoadSeg * 16 + ModSz;
        pResult->Endian   = LoaderEndianLittle;
        pResult->WordBits = 16;
        pResult->Arch     = "8086";   // 16-bit x86 real mode
        pResult->Abi      = "dos";
        std::printf ("lcx: loaded DOS MZ .EXE (module=%llu bytes, relocs=%u): CS:IP=%04x:%04x "
                     "SS:SP=%04x:%04x entry=0x%llx\n",
                     (unsigned long long) ModSz, (unsigned) NReloc, (unsigned) Cs, (unsigned) Ip,
                     (unsigned) Ss, (unsigned) Sp, (unsigned long long) pResult->Entry);
        return S_OK;
    }

private:
    static UINT16 Rd16 (UINT8 CONST *p, UINT64 O) { return (UINT16) (p[O] | (p[O + 1] << 8)); }

    // A plain DOS MZ: begins "MZ" (or "ZM") and is NOT a PE/NE/LE/LX new-executable container.
    static bool IsPlainMz (UINT8 CONST *p, UINT64 Len)
    {
        if (Len < 0x1c) {
            return false;
        }
        if (!((p[0] == 'M' && p[1] == 'Z') || (p[0] == 'Z' && p[1] == 'M'))) {
            return false;
        }
        if (Len >= 0x40) {
            UINT64 New = (UINT32) (p[0x3c] | (p[0x3d] << 8) | (p[0x3e] << 16) | ((UINT32) p[0x3f] << 24));
            if (New != 0 && New + 2 <= Len) {
                UINT8 A = p[New], B = p[New + 1];
                bool  NewExe = (A == 'P' && B == 'E') || (A == 'N' && B == 'E')
                               || (A == 'L' && B == 'E') || (A == 'L' && B == 'X');
                if (NewExe) {
                    return false;   // a container -- let the PE/NE/LE/LX loader handle it
                }
            }
        }
        return true;
    }
};

ILoader *
CreateDosExeLoader (VOID)
{
    return new DosExeLoader ();
}

} // namespace
} // namespace LibCPU

LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreateDosExeLoader)
