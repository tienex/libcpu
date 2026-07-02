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
enum { EM_ARM = 40, EM_SH = 42 };
enum : UINT32 { EF_ARM_FDPIC = 0x00400000, EF_SH_FDPIC = 0x8000 };

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
                                    LOADER_REQUEST CONST *pRequest, LOADER_RESULT *pResult) override
    {
        if (pImage == nullptr || pMem == nullptr || pResult == nullptr) {
            return E_POINTER;
        }
        if (Probe (pImage, Len) == 0) {
            std::printf ("lcx: not an ELF image\n");
            return E_FAIL;
        }
        // A load bias (request LoadAddr) relocates a position-independent object (ET_DYN / PIE):
        // the run-time linker uses it to place each shared object at a distinct base. It is 0 for a
        // normal ET_EXEC load, so placement is unchanged there.
        UINT64 Bias = (pRequest != nullptr) ? pRequest->LoadAddr : 0;
        std::memset (pResult, 0, sizeof (*pResult));
        m_Interp.clear ();
        m_Needed.clear ();
        m_NeededPtrs.clear ();
        m_Features.clear ();
        m_FeaturePtrs.clear ();
        m_AbiVer.clear ();

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
        UINT32 EFlags   = (UINT32) (m_Is64 ? Rd (48, 4) : Rd (36, 4));
        UINT8  OsAbi    = m_pImage[7];   // EI_OSABI
        UINT8  AbiVer   = m_pImage[8];   // EI_ABIVERSION

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
                UINT64 Dst = PVaddr + Bias;
                if (POff + PFilesz > Len || Dst + PMemsz > RamSize || Dst + PMemsz < Dst) {
                    std::printf ("lcx: ELF PT_LOAD out of range\n");
                    return E_FAIL;
                }
                if (PFilesz != 0) {
                    pMem->Write (Dst, pImage + POff, PFilesz);
                }
                if (PMemsz > PFilesz) {
                    pMem->Zero (Dst + PFilesz, PMemsz - PFilesz);   // .bss tail
                }
                if (Dst + PMemsz > LoadEnd) {
                    LoadEnd = Dst + PMemsz;
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

        ElfFeatures (EMachine, EFlags);   // fills m_Features from e_flags

        pResult->Entry    = Entry + Bias;
        pResult->LoadEnd  = LoadEnd;
        pResult->BrkBase  = (LoadEnd + 0xfff) & ~UINT64_C (0xfff);
        pResult->Endian   = m_Big ? LoaderEndianBig : LoaderEndianLittle;
        pResult->WordBits = m_Is64 ? 64 : 32;
        pResult->Arch     = ElfArch (EMachine);   // canonical name, never interpreted
        pResult->Abi      = ElfAbi (OsAbi);
        pResult->PicKind  = ((EMachine == EM_ARM && (EFlags & EF_ARM_FDPIC))
                             || (EMachine == EM_SH && (EFlags & EF_SH_FDPIC))) ? "fdpic"
                          : (EType == ET_DYN) ? (SawInterp ? "pie" : "pic")
                          : nullptr;   // ET_EXEC is fixed-address
        if (AbiVer != 0) {
            m_AbiVer        = std::to_string ((unsigned) AbiVer);
            pResult->AbiVersion = m_AbiVer.c_str ();
        }
        for (std::string CONST &F : m_Features) {
            m_FeaturePtrs.push_back (F.c_str ());
        }
        pResult->FeatureCount = (UINT32) m_FeaturePtrs.size ();
        pResult->Features     = m_FeaturePtrs.empty () ? nullptr : m_FeaturePtrs.data ();

        pResult->Dynamic.IsDynamic = (BOOLEAN) (EType == ET_DYN || SawInterp || SawDynamic);
        pResult->Dynamic.Interp    = m_Interp.empty () ? nullptr : m_Interp.c_str ();
        for (std::string CONST &N : m_Needed) {
            m_NeededPtrs.push_back (N.c_str ());
        }
        pResult->Dynamic.NeededCount = (UINT32) m_NeededPtrs.size ();
        pResult->Dynamic.Needed      = m_NeededPtrs.empty () ? nullptr : m_NeededPtrs.data ();

        std::printf ("lcx: loaded ELF (%s %s, arch=%s, abi=%s, %s): entry=0x%llx end=0x%llx",
                     m_Is64 ? "64-bit" : "32-bit", m_Big ? "BE" : "LE",
                     pResult->Arch ? pResult->Arch : "?", pResult->Abi ? pResult->Abi : "?",
                     pResult->Dynamic.IsDynamic ? "dynamic" : "static",
                     (unsigned long long) Entry, (unsigned long long) LoadEnd);
        if (pResult->Dynamic.Interp != nullptr) {
            std::printf (" interp=%s", pResult->Dynamic.Interp);
        }
        if (pResult->Dynamic.NeededCount != 0) {
            std::printf (" needed=%u", (unsigned) pResult->Dynamic.NeededCount);
        }
        if (pResult->FeatureCount != 0) {
            std::printf (" features=%u", (unsigned) pResult->FeatureCount);
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

    // Canonical architecture name for an ELF e_machine (EM_*). Never acted on.
    static CHAR8 CONST *ElfArch (UINT16 M)
    {
        switch (M) {
        case 2:   return "sparc";   case 3:   return "i386";    case 4:  return "m68k";
        case 5:   return "m88k";    case 7:   return "i860";    case 8:  return "mips";
        case 10:  return "mips";    case 15:  return "hppa";    case 18: return "sparc";
        case 20:  return "ppc";     case 21:  return "ppc64";   case 22: return "s390";
        case 40:  return "arm";     case 42:  return "sh";      case 43: return "sparc64";
        case 50:  return "ia64";    case 62:  return "x86_64";  case 183:return "aarch64";
        case 243: return "riscv";   case 258: return "loongarch";
        default:  return nullptr;
        }
    }

    // Canonical ABI/OS name for an ELF EI_OSABI.
    static CHAR8 CONST *ElfAbi (UINT8 A)
    {
        switch (A) {
        case 0:  return "sysv";    case 1:  return "hpux";     case 2:  return "netbsd";
        case 3:  return "linux";   case 6:  return "solaris";  case 7:  return "aix";
        case 8:  return "irix";    case 9:  return "freebsd";  case 10: return "tru64";
        case 12: return "openbsd"; case 13: return "dragonfly";case 15: return "nsk";
        case 97: return "arm";     case 255:return "standalone";
        default: return nullptr;
        }
    }

    // Derive the arch feature/extension names an image needs from ELF e_flags (per e_machine).
    void ElfFeatures (UINT16 M, UINT32 F)
    {
        if (M == 8 || M == 10) {   // MIPS
            if (F & 0x00000400) { m_Features.push_back ("nan2008"); }
            if (F & 0x02000000) { m_Features.push_back ("micromips"); }
            if (F & 0x04000000) { m_Features.push_back ("mips16"); }
            UINT32 Arch = F & 0xf0000000u;
            if (Arch == 0x50000000u) { m_Features.push_back ("mips32"); }
            else if (Arch == 0x70000000u) { m_Features.push_back ("mips32r2"); }
            else if (Arch == 0x60000000u) { m_Features.push_back ("mips64"); }
            else if (Arch == 0x80000000u) { m_Features.push_back ("mips64r2"); }
        } else if (M == 40) {      // ARM
            UINT32 Ver = (F >> 24) & 0xff;
            if (Ver != 0) { m_Features.push_back (std::string ("eabi") + std::to_string (Ver)); }
            if (F & 0x00000400) { m_Features.push_back ("hard-float"); }
            else if (F & 0x00000200) { m_Features.push_back ("soft-float"); }
        } else if (M == 243) {     // RISC-V
            if (F & 0x0001) { m_Features.push_back ("rvc"); }
            if (F & 0x0008) { m_Features.push_back ("rve"); }
            UINT32 Fl = F & 0x0006;
            if (Fl == 0x0002) { m_Features.push_back ("float-abi-single"); }
            else if (Fl == 0x0004) { m_Features.push_back ("float-abi-double"); }
            else if (Fl == 0x0006) { m_Features.push_back ("float-abi-quad"); }
        }
    }

    UINT8 CONST              *m_pImage = nullptr;
    UINT64                    m_Len    = 0;
    bool                      m_Is64   = false;
    bool                      m_Big    = false;
    std::string               m_Interp;
    std::string               m_AbiVer;
    std::vector<std::string>  m_Needed;
    std::vector<CHAR8 CONST *> m_NeededPtrs;
    std::vector<std::string>  m_Features;
    std::vector<CHAR8 CONST *> m_FeaturePtrs;
};

ILoader *
CreateElfLoader (VOID)
{
    return new ElfLoader ();
}

} // namespace
} // namespace LibCPU

LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreateElfLoader)
