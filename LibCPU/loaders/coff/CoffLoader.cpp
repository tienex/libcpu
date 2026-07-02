/** @file
  The COFF family -- classic COFF, ECOFF (MIPS/Alpha) and XCOFF32 (AIX/PowerPC) -- as a ".loader"
  module.

  A loader knows the FILE FORMAT, never a CPU: this reads the COFF file header (auto-detecting byte
  order from the f_magic value), lays the loadable sections out at their s_vaddr through the
  ILoaderMemory sink, takes the entry from the a.out-style optional header, and REPORTS the f_magic
  (machine) + endian + word width. It never interprets the machine.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/ILoader.h"
#include "LibCPU/Module.h"

#include <cstdio>
#include <cstring>

namespace LibCPU {
namespace {

// Known COFF f_magic (machine) values, used only to recognise the format + its byte order.
static bool
CoffMagicKnown (UINT16 M)
{
    switch (M) {
    case 0x014c: /* i386      */ case 0x0150: /* m68k       */ case 0x0160: /* MIPS BE ECOFF */
    case 0x0162: /* MIPS LE   */ case 0x0166: /* MIPS LE r3k*/ case 0x0183: /* Alpha        */
    case 0x0184: /* Alpha     */ case 0x01df: /* XCOFF32    */ case 0x01f7: /* XCOFF64      */
    case 0x0170: /* WE32K     */ case 0x0175: /* WE32K      */ case 0x017f: /* clipper      */
    case 0x0268: /* m68k (sysV)*/ case 0x0500: /* SH        */
        return true;
    default:
        return false;
    }
}

class CoffLoader final : public ComObject<ILoader>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_ILoader, ppvObject);
    }

    CHAR8 CONST *STDMETHODCALLTYPE GetName (VOID) override { return "coff"; }

    UINT32 STDMETHODCALLTYPE Probe (UINT8 CONST *pImage, UINT64 Len) override
    {
        if (Len < 20 || (pImage[0] == 'M' && pImage[1] == 'Z')) {
            return 0;   // an MZ prefix is a PE/NE/LE/LX, not a bare COFF
        }
        bool   Big;
        UINT16 Magic;
        if (!DetectMagic (pImage, &Magic, &Big)) {
            return 0;
        }
        // Sanity-check the header so a weak 2-byte magic does not false-match.
        UINT16 NScns  = Rd16 (pImage, 2, Big);
        UINT16 OptSz  = Rd16 (pImage, 16, Big);
        if (NScns > 96 || OptSz > 512) {
            return 0;
        }
        return 70;   // below ELF/Mach-O/PE (stronger magics)
    }

    HRESULT STDMETHODCALLTYPE Load (UINT8 CONST *pImage, UINT64 Len, ILoaderMemory *pMem,
                                    LOADER_RESULT *pResult) override
    {
        if (pImage == nullptr || pMem == nullptr || pResult == nullptr) {
            return E_POINTER;
        }
        UINT16 Magic;
        if (Len < 20 || (pImage[0] == 'M' && pImage[1] == 'Z') || !DetectMagic (pImage, &Magic, &m_Big)) {
            std::printf ("lcx: not a COFF image\n");
            return E_FAIL;
        }
        std::memset (pResult, 0, sizeof (*pResult));
        m_pImage = pImage;
        m_Len    = Len;
        UINT64 RamSize = pMem->Size ();

        UINT16 NScns   = (UINT16) Rd (2, 2);
        UINT16 OptSz   = (UINT16) Rd (16, 2);
        UINT64 OptHdr  = 20;
        UINT64 SectTab = OptHdr + OptSz;
        // The a.out-style optional header carries the entry at offset 16 (magic, vstamp, tsize,
        // dsize, bsize, entry) in both classic COFF and ECOFF.
        UINT64 Entry   = (OptSz >= 20) ? Rd (OptHdr + 16, 4) : 0;

        UINT64 LoadEnd = 0;
        for (UINT16 I = 0; I < NScns; I++) {
            UINT64 S = SectTab + (UINT64) I * 40;
            if (S + 40 > Len) {
                break;
            }
            UINT64 VAddr  = Rd (S + 12, 4);   // s_vaddr
            UINT64 SSize  = Rd (S + 16, 4);   // s_size
            UINT64 ScnPtr = Rd (S + 20, 4);   // s_scnptr (0 => .bss, no file data)
            UINT32 Flags  = (UINT32) Rd (S + 36, 4);
            if (SSize == 0 || ScnPtr == 0 || (Flags & 0x80) != 0) {   // STYP_BSS = 0x80, or no file data
                if (SSize != 0 && VAddr + SSize <= RamSize) {
                    pMem->Zero (VAddr, SSize);
                    if (VAddr + SSize > LoadEnd) { LoadEnd = VAddr + SSize; }
                }
                continue;
            }
            if (VAddr + SSize > RamSize || VAddr + SSize < VAddr) {
                std::printf ("lcx: COFF section out of range\n");
                return E_FAIL;
            }
            if (ScnPtr != 0 && ScnPtr + SSize <= Len) {
                pMem->Write (VAddr, pImage + ScnPtr, SSize);
            } else {
                pMem->Zero (VAddr, SSize);
            }
            if (VAddr + SSize > LoadEnd) {
                LoadEnd = VAddr + SSize;
            }
        }

        pResult->Entry       = Entry;
        pResult->LoadEnd     = LoadEnd;
        pResult->BrkBase     = (LoadEnd + 0xfff) & ~UINT64_C (0xfff);
        pResult->Endian      = m_Big ? LoaderEndianBig : LoaderEndianLittle;
        pResult->WordBits    = (Magic == 0x01f7) ? 64 : 32;
        pResult->MachineHint = Magic;   // reported, not interpreted
        // Classic COFF/ECOFF executables are statically linked; XCOFF imports live in a .loader
        // section whose parse is deferred to the run-time-linker phase.
        pResult->Dynamic.IsDynamic = 0;

        std::printf ("lcx: loaded COFF (magic=0x%x, %s, %s): entry=0x%llx end=0x%llx\n",
                     (unsigned) Magic, m_Big ? "BE" : "LE", (Magic == 0x01f7) ? "64-bit" : "32-bit",
                     (unsigned long long) Entry, (unsigned long long) LoadEnd);
        return S_OK;
    }

private:
    static UINT16 Rd16 (UINT8 CONST *p, UINT64 O, bool Big)
    {
        return Big ? (UINT16) ((p[O] << 8) | p[O + 1]) : (UINT16) (p[O] | (p[O + 1] << 8));
    }

    // Recognise a COFF f_magic at offset 0 in either byte order.
    static bool DetectMagic (UINT8 CONST *p, UINT16 *pMagic, bool *pBig)
    {
        UINT16 Le = (UINT16) (p[0] | (p[1] << 8));
        UINT16 Be = (UINT16) ((p[0] << 8) | p[1]);
        if (CoffMagicKnown (Le)) { *pMagic = Le; *pBig = false; return true; }
        if (CoffMagicKnown (Be)) { *pMagic = Be; *pBig = true;  return true; }
        return false;
    }

    UINT64 Rd (UINT64 Off, UINT32 Size)
    {
        if (Off + Size > m_Len) {
            return 0;
        }
        UINT64 V = 0;
        if (m_Big) {
            for (UINT32 I = 0; I < Size; I++) { V = (V << 8) | m_pImage[Off + I]; }
        } else {
            for (UINT32 I = 0; I < Size; I++) { V |= (UINT64) m_pImage[Off + I] << (8 * I); }
        }
        return V;
    }

    UINT8 CONST *m_pImage = nullptr;
    UINT64       m_Len    = 0;
    bool         m_Big    = false;
};

ILoader *
CreateCoffLoader (VOID)
{
    return new CoffLoader ();
}

} // namespace
} // namespace LibCPU

LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreateCoffLoader)
