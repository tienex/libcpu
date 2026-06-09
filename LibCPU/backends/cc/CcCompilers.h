/** @file
  Compiler discovery for the "cc" (emit-C) backend. The backend scans the system
  for every installed C/C++ compiler, categorizes each by family, and exposes the
  list (and a selection) through ICpuCcCompilers, obtained by QueryInterface on
  the cc ICpuBackend.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_CCCOMPILERS_H
#define LIBCPU_CCCOMPILERS_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

//
// Compiler families the cc backend recognizes.
//
typedef enum _CC_FAMILY {
    CcFamilyUnknown = 0,
    CcFamilyClang,        // clang, clang++, clang-NN, clang-cl
    CcFamilyGcc,          // gcc, g++, gcc-NN, <triple>-gcc (incl. MinGW *-w64-mingw32-gcc)
    CcFamilyMsvc,         // cl, cl386, clarm, clsh, clmips, clppc
    CcFamilyWatcom,       // wcc, wcc386, wpp, wpp386, wcl, wcl386, wcl{ppc,axp,mps}, owcc
    CcFamilyBorland,      // bcc, bcc32, bcc64
    CcFamilyIntel,        // icc, icx, icl, iec, icpc, icpx
    CcFamilyMetrowerks,   // mwcc, mwccppc, mwcceppc
    CcFamilyDigitalMars,  // dmc
    CcFamilyIbmXl,        // xlc, xlC, xlc++
    CcFamilyEfiByteCode,  // iec (Intel EFI Byte Code compiler; target "ebc")
    CcFamilyGeneric       // cc, CC, c++ (host-default front ends)
} CC_FAMILY;

//
// Description of one discovered compiler (POD; fixed buffers for a stable C ABI).
//
typedef struct _CC_COMPILER_INFO {
    CHAR8     Path[1024];      // absolute path of the executable
    CHAR8     Name[128];       // basename
    CHAR8     Version[160];    // first --version line, if obtainable
    CHAR8     Target[128];     // -dumpmachine triple, if obtainable (e.g. x86_64-w64-mingw32)
    CC_FAMILY Family;
    BOOLEAN   UsableForHost;   // can it build a loadable module for THIS process?
    BOOLEAN   ViaWine;         // a Windows .exe to be invoked through wine (Unix hosts)
} CC_COMPILER_INFO;

/**
  ICpuCcCompilers -- query/select the cc backend's discovered compilers.
  Obtain via ICpuBackend::QueryInterface(IID_ICpuCcCompilers, ...).
**/
DECLARE_INTERFACE_ (ICpuCcCompilers, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD_ (UINT32, GetCompilerCount)(THIS) PURE;
    STDMETHOD (GetCompilerInfo)(THIS_ UINT32 Index, OUT CC_COMPILER_INFO *pInfo) PURE;
    STDMETHOD (SelectCompiler)(THIS_ UINT32 Index) PURE;
    STDMETHOD_ (UINT32, GetSelectedCompiler)(THIS) PURE;
};

// {1C9A0002-0002-4C50-9A00-000000000010}
inline constexpr IID IID_ICpuCcCompilers =
    { 0x1C9A0002, 0x0002, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 } };

} // namespace LibCPU

#endif // LIBCPU_CCCOMPILERS_H
