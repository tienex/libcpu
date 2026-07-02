/** @file
  The NE (New Executable) format -- 16-bit segmented Windows (Win16) and OS/2 1.x programs -- as a
  ".loader" module.

  A loader knows the FILE FORMAT, never a CPU: this parses the NE header (reached via the MZ stub's
  e_lfanew), places each segment's file image at a running paragraph base in the flat address space
  (a 16-bit NE segment has no fixed linear address, so the loader assigns one, as the OS/2/Windows
  loader would), and reports arch "8086" + the ABI named by the NE target-OS field. It takes the
  entry from ne_csip (segment:offset). Per-segment relocation fixups are deferred to the run-time
  linker; this is static placement.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/ILoader.h"
#include "LibCPU/Module.h"

#include <cstdio>
#include <cstring>

namespace LibCPU {
namespace {

// NE header field offsets (relative to the "NE" signature) and layout constants.
enum
{
    NE_MAGIC     = 0x454e,   // 'NE', little-endian
    NE_OFF_CSIP  = 0x14,     // initial CS:IP (IP low word, CS = 1-based segment index high word)
    NE_OFF_CSEG  = 0x1c,     // segment count
    NE_OFF_SEGTAB = 0x22,    // segment-table offset (relative to the NE header)
    NE_OFF_ALIGN = 0x32,     // sector alignment shift count (0 => 9, i.e. 512-byte sectors)
    NE_OFF_EXETYP = 0x36,    // target OS
    NE_SEGENT_SZ = 8,        // segment-table entry size
    MZ_OFF_LFANEW = 0x3c
};
enum { NE_OS_OS2 = 1, NE_OS_WIN = 2, NE_OS_DOS4 = 3, NE_OS_WIN386 = 4 };

class NeLoader final : public ComObject<ILoader>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_ILoader, ppvObject);
    }

    CHAR8 CONST *STDMETHODCALLTYPE GetName (VOID) override { return "ne"; }

    UINT32 STDMETHODCALLTYPE Probe (UINT8 CONST *pImage, UINT64 Len) override
    {
        return NeOffset (pImage, Len) != 0 ? 88 : 0;
    }

    HRESULT STDMETHODCALLTYPE Load (UINT8 CONST *pImage, UINT64 Len, ILoaderMemory *pMem,
                                    LOADER_REQUEST CONST *pRequest, LOADER_RESULT *pResult) override
    {
        if (pImage == nullptr || pMem == nullptr || pResult == nullptr) {
            return E_POINTER;
        }
        UINT64 Ne = NeOffset (pImage, Len);
        if (Ne == 0) {
            std::printf ("lcx: not an NE image\n");
            return E_FAIL;
        }
        (void) pRequest;   // 16-bit segmented; kernel/blob placement not applicable
        std::memset (pResult, 0, sizeof (*pResult));
        m_pImage = pImage;
        m_Len    = Len;
        UINT64 RamSize = pMem->Size ();

        UINT16 NSeg    = Rd16 (Ne + NE_OFF_CSEG);
        UINT16 SegTab  = Rd16 (Ne + NE_OFF_SEGTAB);
        UINT16 AlignSh = Rd16 (Ne + NE_OFF_ALIGN);
        UINT16 ExeTyp  = (UINT16) (pImage[Ne + NE_OFF_EXETYP]);
        UINT32 CsIp    = (UINT32) Rd16 (Ne + NE_OFF_CSIP) | ((UINT32) Rd16 (Ne + NE_OFF_CSIP + 2) << 16);
        UINT16 Ip      = (UINT16) (CsIp & 0xffff);
        UINT16 CsSeg   = (UINT16) (CsIp >> 16);        // 1-based segment index
        UINT32 SecShift = (AlignSh == 0) ? 9 : AlignSh; // sector size = 1 << shift

        UINT64 NextBase = 0;      // running paragraph base for placing segments
        UINT64 LoadEnd  = 0;
        UINT64 EntryBase = 0;
        for (UINT16 I = 0; I < NSeg; I++) {
            UINT64 E = Ne + SegTab + (UINT64) I * NE_SEGENT_SZ;
            if (E + NE_SEGENT_SZ > Len) {
                break;
            }
            UINT64 FileOff = (UINT64) Rd16 (E + 0) << SecShift;   // sector-scaled file offset
            UINT64 SegLen  = Rd16 (E + 2);
            UINT64 Dst     = NextBase * 16;
            if (SegLen != 0 && FileOff != 0) {
                if (Dst + SegLen > RamSize || FileOff + SegLen > Len) {
                    std::printf ("lcx: NE segment %u out of range\n", (unsigned) I);
                    return E_FAIL;
                }
                pMem->Write (Dst, pImage + FileOff, SegLen);
            }
            if (Dst + SegLen > LoadEnd) {
                LoadEnd = Dst + SegLen;
            }
            if ((UINT16) (I + 1) == CsSeg) {   // segment indices are 1-based
                EntryBase = Dst;
            }
            NextBase += (SegLen + 15) / 16;    // next paragraph, packed
        }

        pResult->Entry    = EntryBase + Ip;
        pResult->LoadEnd  = LoadEnd;
        pResult->BrkBase  = LoadEnd;
        pResult->Endian   = LoaderEndianLittle;
        pResult->WordBits = 16;
        // NE is 286-class protected-mode segmented code (never plain 8086); a Win386 module is 386.
        pResult->Arch     = (ExeTyp == NE_OS_WIN386) ? "i386" : "i286";
        pResult->Abi      = (ExeTyp == NE_OS_WIN || ExeTyp == NE_OS_WIN386) ? "windows"
                          : (ExeTyp == NE_OS_OS2) ? "os2"
                          : "dos";
        pResult->AbiVendor = (ExeTyp == NE_OS_OS2) ? "ibm"
                           : (ExeTyp == NE_OS_WIN || ExeTyp == NE_OS_WIN386) ? "microsoft"
                           : nullptr;
        std::printf ("lcx: loaded NE (%u segments, arch=%s, abi=%s): CS:IP=%u:%04x entry=0x%llx end=0x%llx\n",
                     (unsigned) NSeg, pResult->Arch, pResult->Abi, (unsigned) CsSeg, (unsigned) Ip,
                     (unsigned long long) pResult->Entry, (unsigned long long) LoadEnd);
        return S_OK;
    }

private:
    // Offset of the "NE" signature via the MZ stub, or 0 if not an NE image.
    static UINT64 NeOffset (UINT8 CONST *p, UINT64 Len)
    {
        if (Len < 0x40 || p[0] != 'M' || p[1] != 'Z') {
            return 0;
        }
        UINT64 Off = (UINT32) (p[MZ_OFF_LFANEW] | (p[MZ_OFF_LFANEW + 1] << 8)
                               | (p[MZ_OFF_LFANEW + 2] << 16) | ((UINT32) p[MZ_OFF_LFANEW + 3] << 24));
        if (Off == 0 || Off + 0x40 > Len) {
            return 0;
        }
        return (p[Off] == 'N' && p[Off + 1] == 'E') ? Off : 0;
    }

    UINT16 Rd16 (UINT64 O) { return (O + 1 < m_Len) ? (UINT16) (m_pImage[O] | (m_pImage[O + 1] << 8)) : 0; }

    UINT8 CONST *m_pImage = nullptr;
    UINT64       m_Len    = 0;
};

ILoader *
CreateNeLoader (VOID)
{
    return new NeLoader ();
}

} // namespace
} // namespace LibCPU

LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreateNeLoader)
