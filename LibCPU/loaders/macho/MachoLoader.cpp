/** @file
  Mach-O (32/64-bit, little/big-endian, thin or fat), as a ".loader" module. Also covers OSF/Rose,
  which is Mach-O for OSF/1.

  A loader knows the FILE FORMAT, never a CPU: this maps the LC_SEGMENT(_64) segments to their
  virtual addresses through the ILoaderMemory sink and REPORTS what it read -- cputype, byte order,
  word width, entry (from LC_MAIN). It EXPOSES the dynamic facts (LC_LOAD_DYLINKER + LC_LOAD_DYLIB)
  without resolving them, and never interprets cputype.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/ILoader.h"
#include "LibCPU/Module.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace LibCPU {
namespace {

enum : UINT32
{
    MH_MAGIC    = 0xfeedface, MH_CIGAM    = 0xcefaedfe,   // 32-bit, host/reversed
    MH_MAGIC_64 = 0xfeedfacf, MH_CIGAM_64 = 0xcffaedfe,   // 64-bit
    FAT_MAGIC   = 0xcafebabe, FAT_CIGAM   = 0xbebafeca    // universal (fat)
};
enum : UINT32
{
    LC_SEGMENT       = 0x1,
    LC_LOAD_DYLIB    = 0xc,
    LC_LOAD_DYLINKER = 0xe,
    LC_SEGMENT_64    = 0x19,
    LC_MAIN          = 0x80000028
};
enum : UINT32 { MH_DYLDLINK = 0x4 };

class MachoLoader final : public ComObject<ILoader>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_ILoader, ppvObject);
    }

    CHAR8 CONST *STDMETHODCALLTYPE GetName (VOID) override { return "macho"; }

    UINT32 STDMETHODCALLTYPE Probe (UINT8 CONST *pImage, UINT64 Len) override
    {
        if (Len < 4) {
            return 0;
        }
        UINT32 M = Le32 (pImage, 0);   // read native-endian; compare against every Mach-O magic
        UINT32 B = Be32 (pImage, 0);
        if (M == MH_MAGIC || M == MH_MAGIC_64 || M == MH_CIGAM || M == MH_CIGAM_64
            || B == FAT_MAGIC) {
            return 90;
        }
        return 0;
    }

    HRESULT STDMETHODCALLTYPE Load (UINT8 CONST *pImage, UINT64 Len, ILoaderMemory *pMem,
                                    LOADER_RESULT *pResult) override
    {
        if (pImage == nullptr || pMem == nullptr || pResult == nullptr) {
            return E_POINTER;
        }
        std::memset (pResult, 0, sizeof (*pResult));
        m_Interp.clear ();
        m_Needed.clear ();
        m_NeededPtrs.clear ();

        // Universal (fat) file: pick the first architecture slice and load it thin. The fat header
        // is always big-endian.
        UINT64 Base = 0;
        if (Len >= 8 && Be32 (pImage, 0) == FAT_MAGIC) {
            UINT32 NArch = Be32 (pImage, 4);
            if (NArch == 0 || 8 + 20 > Len) {
                std::printf ("lcx: empty fat Mach-O\n");
                return E_FAIL;
            }
            Base = Be32 (pImage, 8 + 8);   // fat_arch[0].offset
            if (Base + 4 > Len) {
                std::printf ("lcx: fat Mach-O slice out of range\n");
                return E_FAIL;
            }
        }

        UINT32 Magic = Le32 (pImage, Base);
        bool   Is64  = (Magic == MH_MAGIC_64 || Magic == MH_CIGAM_64);
        m_Big        = (Magic == MH_CIGAM || Magic == MH_CIGAM_64);
        if (Magic != MH_MAGIC && Magic != MH_MAGIC_64 && Magic != MH_CIGAM && Magic != MH_CIGAM_64) {
            std::printf ("lcx: not a Mach-O image\n");
            return E_FAIL;
        }
        m_pImage = pImage;
        m_Len    = Len;
        UINT64 RamSize = pMem->Size ();

        UINT32 CpuType = Rd (Base + 4, 4);
        UINT32 NCmds   = Rd (Base + 16, 4);
        UINT32 Flags   = Rd (Base + 24, 4);
        UINT64 Cmd     = Base + (Is64 ? 32 : 28);   // first load command

        UINT64 LoadEnd = 0, TextVmaddr = 0, EntryOff = 0;
        bool   HaveText = false, HaveMain = false;

        for (UINT32 I = 0; I < NCmds; I++) {
            if (Cmd + 8 > Len) {
                break;
            }
            UINT32 C    = Rd (Cmd, 4);
            UINT32 CSz  = Rd (Cmd + 4, 4);
            if (CSz < 8 || Cmd + CSz > Len) {
                break;
            }
            if (C == LC_SEGMENT || C == LC_SEGMENT_64) {
                bool    Seg64  = (C == LC_SEGMENT_64);
                CHAR8 CONST *pName = (CHAR8 CONST *) (pImage + Cmd + 8);   // segname[16]
                UINT64  VmAddr = Seg64 ? Rd (Cmd + 24, 8) : Rd (Cmd + 24, 4);
                UINT64  VmSize = Seg64 ? Rd (Cmd + 32, 8) : Rd (Cmd + 28, 4);
                UINT64  FileOff = Seg64 ? Rd (Cmd + 40, 8) : Rd (Cmd + 32, 4);
                UINT64  FileSz  = Seg64 ? Rd (Cmd + 48, 8) : Rd (Cmd + 36, 4);
                if (std::strncmp (pName, "__TEXT", 16) == 0) {
                    TextVmaddr = VmAddr; HaveText = true;
                }
                if (VmSize != 0) {
                    if (Base + FileOff + FileSz > Len || VmAddr + VmSize > RamSize
                        || VmAddr + VmSize < VmAddr) {
                        std::printf ("lcx: Mach-O segment out of range\n");
                        return E_FAIL;
                    }
                    if (FileSz != 0) {
                        pMem->Write (VmAddr, pImage + Base + FileOff, FileSz);
                    }
                    if (VmSize > FileSz) {
                        pMem->Zero (VmAddr + FileSz, VmSize - FileSz);
                    }
                    if (VmAddr + VmSize > LoadEnd) {
                        LoadEnd = VmAddr + VmSize;
                    }
                }
            } else if (C == LC_MAIN) {
                EntryOff = Rd (Cmd + 8, 8);   // file offset of the entry, relative to the image
                HaveMain = true;
            } else if (C == LC_LOAD_DYLINKER) {
                UINT32 Off = Rd (Cmd + 8, 4);   // lc_str offset within the command
                if (Off < CSz) {
                    m_Interp.assign ((CHAR8 CONST *) (pImage + Cmd + Off));
                }
            } else if (C == LC_LOAD_DYLIB) {
                UINT32 Off = Rd (Cmd + 8, 4);
                if (Off < CSz) {
                    m_Needed.push_back (std::string ((CHAR8 CONST *) (pImage + Cmd + Off)));
                }
            }
            Cmd += CSz;
        }

        // LC_MAIN's entryoff is a file offset; the entry virtual address is __TEXT.vmaddr + entryoff
        // (the __TEXT segment maps the file from offset 0). LC_UNIXTHREAD's PC is arch-specific and
        // is left to the host; we report the text base as a best effort.
        UINT64 Entry = HaveText ? TextVmaddr : 0;
        if (HaveMain && HaveText) {
            Entry = TextVmaddr + EntryOff;
        }

        pResult->Entry       = Entry;
        pResult->LoadEnd     = LoadEnd;
        pResult->BrkBase     = (LoadEnd + 0xfff) & ~UINT64_C (0xfff);
        pResult->Endian      = m_Big ? LoaderEndianBig : LoaderEndianLittle;
        pResult->WordBits    = Is64 ? 64 : 32;
        pResult->MachineHint = CpuType;   // reported, not interpreted

        bool Dyn = (Flags & MH_DYLDLINK) != 0 || !m_Interp.empty () || !m_Needed.empty ();
        pResult->Dynamic.IsDynamic = (BOOLEAN) Dyn;
        pResult->Dynamic.Interp    = m_Interp.empty () ? nullptr : m_Interp.c_str ();
        for (std::string CONST &N : m_Needed) {
            m_NeededPtrs.push_back (N.c_str ());
        }
        pResult->Dynamic.NeededCount = (UINT32) m_NeededPtrs.size ();
        pResult->Dynamic.Needed      = m_NeededPtrs.empty () ? nullptr : m_NeededPtrs.data ();

        std::printf ("lcx: loaded Mach-O (%s %s, cputype=0x%x, %s): entry=0x%llx end=0x%llx",
                     Is64 ? "64-bit" : "32-bit", m_Big ? "BE" : "LE", (unsigned) CpuType,
                     Dyn ? "dynamic" : "static", (unsigned long long) Entry,
                     (unsigned long long) LoadEnd);
        if (pResult->Dynamic.NeededCount != 0) {
            std::printf (" needed=%u", (unsigned) pResult->Dynamic.NeededCount);
        }
        std::printf ("\n");
        return S_OK;
    }

private:
    static UINT32 Le32 (UINT8 CONST *p, UINT64 O)
    {
        return (UINT32) (p[O] | (p[O + 1] << 8) | (p[O + 2] << 16) | ((UINT32) p[O + 3] << 24));
    }
    static UINT32 Be32 (UINT8 CONST *p, UINT64 O)
    {
        return (UINT32) (((UINT32) p[O] << 24) | (p[O + 1] << 16) | (p[O + 2] << 8) | p[O + 3]);
    }
    // Read a Size-byte field at offset Off honouring the Mach-O's byte order.
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

    UINT8 CONST               *m_pImage = nullptr;
    UINT64                     m_Len    = 0;
    bool                       m_Big    = false;
    std::string                m_Interp;
    std::vector<std::string>   m_Needed;
    std::vector<CHAR8 CONST *> m_NeededPtrs;
};

ILoader *
CreateMachoLoader (VOID)
{
    return new MachoLoader ();
}

} // namespace
} // namespace LibCPU

LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreateMachoLoader)
