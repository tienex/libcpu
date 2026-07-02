/** @file
  The LE / LX (Linear Executable) format -- OS/2 2.x (LX), Windows VxD and DOS-extender (LE, e.g.
  DOS/4GW) programs -- as a ".loader" module.

  A loader knows the FILE FORMAT, never a CPU: this parses the LE/LX header (reached via the MZ
  stub's e_lfanew), places each object at its preferred linear (reloc) base, and reports the arch
  named by the header's CPU type + the ABI named by its OS type, with the entry from the EIP object.
  Full page-map iteration and per-page fixups are deferred to the run-time linker; this places the
  object image best-effort (contiguous pages from the data-pages region).

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
    LX_OFF_CPU     = 0x08,   // cpu type (2 bytes)
    LX_OFF_OS      = 0x0a,   // target OS (2 bytes)
    LX_OFF_EIPOBJ  = 0x1c,   // EIP object number (1-based)
    LX_OFF_EIP     = 0x20,   // entry EIP within that object
    LX_OFF_PAGESZ  = 0x2c,   // page size
    LX_OFF_OBJTAB  = 0x40,   // object table offset (relative to the LX header)
    LX_OFF_OBJCNT  = 0x44,   // object count
    LX_OFF_DATAPAGE = 0x80,  // data-pages file offset
    LX_OBJ_SZ      = 24,     // object-table entry size
    MZ_OFF_LFANEW  = 0x3c
};
enum { LX_CPU_286 = 1, LX_CPU_386 = 2, LX_CPU_486 = 3 };
enum { LX_OS_OS2 = 1, LX_OS_WINDOWS = 2, LX_OS_DOS4 = 3, LX_OS_WIN386 = 4 };

class LxLoader final : public ComObject<ILoader>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_ILoader, ppvObject);
    }

    CHAR8 CONST *STDMETHODCALLTYPE GetName (VOID) override { return "lx"; }

    UINT32 STDMETHODCALLTYPE Probe (UINT8 CONST *pImage, UINT64 Len) override
    {
        return LxOffset (pImage, Len) != 0 ? 88 : 0;
    }

    HRESULT STDMETHODCALLTYPE Load (UINT8 CONST *pImage, UINT64 Len, ILoaderMemory *pMem,
                                    LOADER_REQUEST CONST *pRequest, LOADER_RESULT *pResult) override
    {
        if (pImage == nullptr || pMem == nullptr || pResult == nullptr) {
            return E_POINTER;
        }
        UINT64 Lx = LxOffset (pImage, Len);
        if (Lx == 0) {
            std::printf ("lcx: not an LE/LX image\n");
            return E_FAIL;
        }
        (void) pRequest;
        std::memset (pResult, 0, sizeof (*pResult));
        m_pImage = pImage;
        m_Len    = Len;
        bool   IsLx = (pImage[Lx] == 'L' && pImage[Lx + 1] == 'X');
        UINT64 RamSize = pMem->Size ();

        UINT16 Cpu     = Rd16 (Lx + LX_OFF_CPU);
        UINT16 Os      = Rd16 (Lx + LX_OFF_OS);
        UINT32 EipObj  = Rd32 (Lx + LX_OFF_EIPOBJ);
        UINT32 Eip     = Rd32 (Lx + LX_OFF_EIP);
        UINT32 PageSz  = Rd32 (Lx + LX_OFF_PAGESZ);
        UINT32 ObjTab  = Rd32 (Lx + LX_OFF_OBJTAB);
        UINT32 ObjCnt  = Rd32 (Lx + LX_OFF_OBJCNT);
        UINT32 DataPg  = Rd32 (Lx + LX_OFF_DATAPAGE);
        if (PageSz == 0) {
            PageSz = 0x1000;
        }

        UINT64 LoadEnd = 0, EntryBase = 0;
        for (UINT32 I = 0; I < ObjCnt; I++) {
            UINT64 O = Lx + ObjTab + (UINT64) I * LX_OBJ_SZ;
            if (O + LX_OBJ_SZ > Len) {
                break;
            }
            UINT32 VSize   = Rd32 (O + 0);
            UINT32 Base    = Rd32 (O + 4);    // preferred linear (reloc) base address
            UINT32 PageIdx = Rd32 (O + 12);   // 1-based first page-map index
            UINT32 PageCnt = Rd32 (O + 16);
            if (VSize == 0) {
                continue;
            }
            if ((UINT64) Base + VSize > RamSize) {
                std::printf ("lcx: LE/LX object %u out of range\n", (unsigned) I);
                return E_FAIL;
            }
            // Best effort: the object's pages are taken contiguously from the data-pages region,
            // starting at (PageIdx-1). Full page-map indirection is deferred to the linker.
            UINT64 Src   = (UINT64) DataPg + (UINT64) (PageIdx ? PageIdx - 1 : 0) * PageSz;
            UINT64 Bytes = (UINT64) PageCnt * PageSz;
            if (Bytes > VSize) {
                Bytes = VSize;
            }
            if (Bytes != 0 && Src + Bytes <= Len) {
                pMem->Write (Base, pImage + Src, Bytes);
            }
            if ((UINT32) (I + 1) == EipObj) {   // object numbers are 1-based
                EntryBase = Base;
            }
            if ((UINT64) Base + VSize > LoadEnd) {
                LoadEnd = Base + VSize;
            }
        }

        pResult->Entry    = EntryBase + Eip;
        pResult->LoadEnd  = LoadEnd;
        pResult->BrkBase  = LoadEnd;
        pResult->Endian   = LoaderEndianLittle;
        pResult->WordBits = (Cpu >= LX_CPU_386) ? 32 : 16;
        pResult->Arch     = (Cpu >= LX_CPU_386) ? "i386" : "8086";
        pResult->Abi      = (Os == LX_OS_OS2) ? "os2"
                          : (Os == LX_OS_WINDOWS || Os == LX_OS_WIN386) ? "windows"
                          : "dos";
        pResult->AbiVendor = (Os == LX_OS_OS2) ? "ibm"
                           : (Os == LX_OS_WINDOWS || Os == LX_OS_WIN386) ? "microsoft"
                           : nullptr;
        std::printf ("lcx: loaded %s (%u objects, arch=%s, abi=%s): entry=0x%llx end=0x%llx\n",
                     IsLx ? "LX" : "LE", (unsigned) ObjCnt, pResult->Arch, pResult->Abi,
                     (unsigned long long) pResult->Entry, (unsigned long long) LoadEnd);
        return S_OK;
    }

private:
    // Offset of the "LE"/"LX" signature via the MZ stub, or 0 if not an LE/LX image.
    static UINT64 LxOffset (UINT8 CONST *p, UINT64 Len)
    {
        if (Len < 0x84 || p[0] != 'M' || p[1] != 'Z') {
            return 0;
        }
        UINT64 Off = (UINT32) (p[MZ_OFF_LFANEW] | (p[MZ_OFF_LFANEW + 1] << 8)
                               | (p[MZ_OFF_LFANEW + 2] << 16) | ((UINT32) p[MZ_OFF_LFANEW + 3] << 24));
        if (Off == 0 || Off + 0x84 > Len) {
            return 0;
        }
        bool Le = (p[Off] == 'L' && p[Off + 1] == 'E');
        bool Lx = (p[Off] == 'L' && p[Off + 1] == 'X');
        return (Le || Lx) ? Off : 0;
    }

    UINT16 Rd16 (UINT64 O) { return (O + 1 < m_Len) ? (UINT16) (m_pImage[O] | (m_pImage[O + 1] << 8)) : 0; }
    UINT32 Rd32 (UINT64 O)
    {
        return (O + 3 < m_Len)
                   ? (UINT32) (m_pImage[O] | (m_pImage[O + 1] << 8) | (m_pImage[O + 2] << 16)
                               | ((UINT32) m_pImage[O + 3] << 24))
                   : 0;
    }

    UINT8 CONST *m_pImage = nullptr;
    UINT64       m_Len    = 0;
};

ILoader *
CreateLxLoader (VOID)
{
    return new LxLoader ();
}

} // namespace
} // namespace LibCPU

LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreateLxLoader)
