/** @file
  ELF (32/64-bit, little/big-endian) as a ".loader" module.

  A loader knows the FILE FORMAT, never a CPU: this maps the PT_LOAD segments to their virtual
  addresses through the ILoaderMemory sink and REPORTS what it read -- e_machine, byte order, word
  width, and the dynamic-linking facts (PT_INTERP + DT_NEEDED). It does not resolve the dynamic
  metadata (loading the interpreter / shared objects and applying relocations is a later phase), and
  it never interprets e_machine.

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

// e_ident indices and the ELF constants we need (defined here so the loader has no system-header
// dependency and builds on every toolchain).
enum { EI_MAG0 = 0, EI_CLASS = 4, EI_DATA = 5, EI_NIDENT = 16 };
enum { ELFCLASS32 = 1, ELFCLASS64 = 2 };
enum { ELFDATA2LSB = 1, ELFDATA2MSB = 2 };
enum { ET_EXEC = 2, ET_DYN = 3 };
enum { PT_LOAD = 1, PT_DYNAMIC = 2, PT_INTERP = 3 };
enum { DT_NULL = 0, DT_NEEDED = 1, DT_STRTAB = 5, DT_STRSZ = 10 };

class ElfLoader final : public ComObject<ILoader>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_ILoader, ppvObject);
    }

    CHAR8 CONST *STDMETHODCALLTYPE GetName (VOID) override { return "elf"; }

    UINT32 STDMETHODCALLTYPE Probe (UINT8 CONST *pImage, UINT64 Len) override
    {
        if (Len < EI_NIDENT || pImage[0] != 0x7f || pImage[1] != 'E' || pImage[2] != 'L'
            || pImage[3] != 'F') {
            return 0;
        }
        UINT8 Class = pImage[EI_CLASS], Data = pImage[EI_DATA];
        if ((Class != ELFCLASS32 && Class != ELFCLASS64)
            || (Data != ELFDATA2LSB && Data != ELFDATA2MSB)) {
            return 0;
        }
        return 95;
    }

    HRESULT STDMETHODCALLTYPE Load (UINT8 CONST *pImage, UINT64 Len, ILoaderMemory *pMem,
                                    LOADER_RESULT *pResult) override
    {
        if (pImage == nullptr || pMem == nullptr || pResult == nullptr) {
            return E_POINTER;
        }
        if (Probe (pImage, Len) == 0) {
            std::printf ("lcx: not an ELF image\n");
            return E_FAIL;
        }
        std::memset (pResult, 0, sizeof (*pResult));
        m_Interp.clear ();
        m_Needed.clear ();
        m_NeededPtrs.clear ();

        m_pImage = pImage;
        m_Len    = Len;
        m_Is64   = (pImage[EI_CLASS] == ELFCLASS64);
        m_Big    = (pImage[EI_DATA] == ELFDATA2MSB);
        UINT64 RamSize = pMem->Size ();

        UINT64 EhPhoff  = m_Is64 ? Rd (32, 8) : Rd (28, 4);
        UINT16 PhEntSz  = (UINT16) (m_Is64 ? Rd (54, 2) : Rd (42, 2));
        UINT16 PhNum    = (UINT16) (m_Is64 ? Rd (56, 2) : Rd (44, 2));
        UINT16 EType    = (UINT16) Rd (16, 2);
        UINT16 EMachine = (UINT16) Rd (18, 2);
        UINT64 Entry    = m_Is64 ? Rd (24, 8) : Rd (24, 4);

        UINT64 LoadEnd     = 0;
        UINT64 DynOff = 0, DynSz = 0;   // PT_DYNAMIC file location
        bool   SawInterp = false, SawDynamic = false;

        for (UINT16 I = 0; I < PhNum; I++) {
            UINT64 Ph = EhPhoff + (UINT64) I * PhEntSz;
            if (Ph + (m_Is64 ? 56 : 32) > Len) {
                break;
            }
            UINT32 PType = (UINT32) Rd (Ph + 0, 4);
            UINT64 POff, PVaddr, PFilesz, PMemsz;
            if (m_Is64) {
                POff = Rd (Ph + 8, 8); PVaddr = Rd (Ph + 16, 8);
                PFilesz = Rd (Ph + 32, 8); PMemsz = Rd (Ph + 40, 8);
            } else {
                POff = Rd (Ph + 4, 4); PVaddr = Rd (Ph + 8, 4);
                PFilesz = Rd (Ph + 16, 4); PMemsz = Rd (Ph + 20, 4);
            }

            if (PType == PT_LOAD) {
                if (POff + PFilesz > Len || PVaddr + PMemsz > RamSize
                    || PVaddr + PMemsz < PVaddr) {
                    std::printf ("lcx: ELF PT_LOAD out of range\n");
                    return E_FAIL;
                }
                if (PFilesz != 0) {
                    pMem->Write (PVaddr, pImage + POff, PFilesz);
                }
                if (PMemsz > PFilesz) {
                    pMem->Zero (PVaddr + PFilesz, PMemsz - PFilesz);   // .bss tail
                }
                if (PVaddr + PMemsz > LoadEnd) {
                    LoadEnd = PVaddr + PMemsz;
                }
            } else if (PType == PT_INTERP) {
                if (POff + PFilesz <= Len && PFilesz > 0) {
                    m_Interp.assign ((CHAR8 CONST *) (pImage + POff),
                                     (size_t) (PFilesz - (pImage[POff + PFilesz - 1] == '\0' ? 1 : 0)));
                    SawInterp = true;
                }
            } else if (PType == PT_DYNAMIC) {
                DynOff = POff; DynSz = PFilesz; SawDynamic = true;
            }
        }

        if (SawDynamic) {
            CollectNeeded (DynOff, DynSz);
        }

        pResult->Entry       = Entry;
        pResult->LoadEnd     = LoadEnd;
        pResult->BrkBase     = (LoadEnd + 0xfff) & ~UINT64_C (0xfff);
        pResult->Endian      = m_Big ? LoaderEndianBig : LoaderEndianLittle;
        pResult->WordBits    = m_Is64 ? 64 : 32;
        pResult->MachineHint = EMachine;   // reported, not interpreted

        pResult->Dynamic.IsDynamic = (BOOLEAN) (EType == ET_DYN || SawInterp || SawDynamic);
        pResult->Dynamic.Interp    = m_Interp.empty () ? nullptr : m_Interp.c_str ();
        for (std::string CONST &N : m_Needed) {
            m_NeededPtrs.push_back (N.c_str ());
        }
        pResult->Dynamic.NeededCount = (UINT32) m_NeededPtrs.size ();
        pResult->Dynamic.Needed      = m_NeededPtrs.empty () ? nullptr : m_NeededPtrs.data ();

        std::printf ("lcx: loaded ELF (%s %s, machine=%u, %s): entry=0x%llx end=0x%llx",
                     m_Is64 ? "64-bit" : "32-bit", m_Big ? "BE" : "LE", (unsigned) EMachine,
                     pResult->Dynamic.IsDynamic ? "dynamic" : "static",
                     (unsigned long long) Entry, (unsigned long long) LoadEnd);
        if (pResult->Dynamic.Interp != nullptr) {
            std::printf (" interp=%s", pResult->Dynamic.Interp);
        }
        if (pResult->Dynamic.NeededCount != 0) {
            std::printf (" needed=%u", (unsigned) pResult->Dynamic.NeededCount);
        }
        std::printf ("\n");
        return S_OK;
    }

private:
    // Read a little/big-endian unsigned field of Size bytes at file offset Off.
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

    // Map a segment-relative virtual address back to a file offset (for DT_STRTAB, which is a vaddr).
    // Returns false if no PT_LOAD covers it.
    bool VaddrToOff (UINT64 Vaddr, UINT64 *pOff)
    {
        UINT64 EhPhoff = m_Is64 ? Rd (32, 8) : Rd (28, 4);
        UINT16 PhEntSz = (UINT16) (m_Is64 ? Rd (54, 2) : Rd (42, 2));
        UINT16 PhNum   = (UINT16) (m_Is64 ? Rd (56, 2) : Rd (44, 2));
        for (UINT16 I = 0; I < PhNum; I++) {
            UINT64 Ph = EhPhoff + (UINT64) I * PhEntSz;
            if ((UINT32) Rd (Ph + 0, 4) != PT_LOAD) {
                continue;
            }
            UINT64 POff   = m_Is64 ? Rd (Ph + 8, 8) : Rd (Ph + 4, 4);
            UINT64 PVaddr = m_Is64 ? Rd (Ph + 16, 8) : Rd (Ph + 8, 4);
            UINT64 PFsz   = m_Is64 ? Rd (Ph + 32, 8) : Rd (Ph + 16, 4);
            if (Vaddr >= PVaddr && Vaddr < PVaddr + PFsz) {
                *pOff = POff + (Vaddr - PVaddr);
                return true;
            }
        }
        return false;
    }

    // Walk PT_DYNAMIC for DT_STRTAB + the DT_NEEDED string-table offsets.
    void CollectNeeded (UINT64 DynOff, UINT64 DynSz)
    {
        UINT32              EntSz = m_Is64 ? 16 : 8;
        UINT64              StrVaddr = 0, StrOff = 0;
        bool                HaveStr = false;
        std::vector<UINT64> NeededOff;

        for (UINT64 O = 0; O + EntSz <= DynSz; O += EntSz) {
            UINT64 Tag = m_Is64 ? Rd (DynOff + O, 8) : Rd (DynOff + O, 4);
            UINT64 Val = m_Is64 ? Rd (DynOff + O + 8, 8) : Rd (DynOff + O + 4, 4);
            if (Tag == DT_NULL) {
                break;
            }
            if (Tag == DT_STRTAB) {
                StrVaddr = Val;
            } else if (Tag == DT_NEEDED) {
                NeededOff.push_back (Val);
            }
        }
        if (StrVaddr != 0 && VaddrToOff (StrVaddr, &StrOff)) {
            HaveStr = true;
        }
        if (!HaveStr) {
            return;
        }
        for (UINT64 NOff : NeededOff) {
            UINT64 P = StrOff + NOff;
            if (P >= m_Len) {
                continue;
            }
            UINT64 End = P;
            while (End < m_Len && m_pImage[End] != '\0') { End++; }
            m_Needed.push_back (std::string ((CHAR8 CONST *) (m_pImage + P), (size_t) (End - P)));
        }
    }

    UINT8 CONST              *m_pImage = nullptr;
    UINT64                    m_Len    = 0;
    bool                      m_Is64   = false;
    bool                      m_Big    = false;
    std::string               m_Interp;
    std::vector<std::string>  m_Needed;
    std::vector<CHAR8 CONST *> m_NeededPtrs;
};

ILoader *
CreateElfLoader (VOID)
{
    return new ElfLoader ();
}

} // namespace
} // namespace LibCPU

LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreateElfLoader)
