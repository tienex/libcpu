/** @file
  LibCPU guest-OS ABI (syscall personality) COM interface.

  Each guest-OS ABI (OpenBSD, NetBSD, classic PDP-11 UNIX, ...) is a COM object packaged in its own
  ".abi" bundle, exactly like the ".loader"/".device" plugins. The host enumerates the bundles,
  matches one by family name (the LCAbiMatch plist array, read without loading code), then loads the
  winning bundle and asks it to build a runtime personality for a resolved guest-OS version.

  The runtime seam a personality drives (the C nix_personality_t / nix_cpu_if_t) is unchanged; IAbi
  only moves DISCOVERY and CREATION onto COM. CreatePersonality vends that C personality.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_IABI_H
#define LIBCPU_IABI_H

#include "LibCPU/PCom.h"

struct _nix_personality; // the C runtime personality (test/libnix/nix/nix-personality.h)

namespace LibCPU {

//
// One guest-OS ABI. GetFamily/GetVersionMin/GetVersionMax describe the coverage the host uses to
// match `--abi family[:version]` and clamp the requested version ("closest approximation");
// CreatePersonality builds the runtime personality for the resolved version.
//
DECLARE_INTERFACE_ (IAbi, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD_ (CHAR8 CONST *, GetFamily)(THIS) PURE;      // canonical family, e.g. "openbsd"
    STDMETHOD_ (CHAR8 CONST *, GetVersionMin)(THIS) PURE;  // earliest supported, e.g. "2.0"
    STDMETHOD_ (CHAR8 CONST *, GetVersionMax)(THIS) PURE;  // latest supported, e.g. "7.9"
    //
    // Build a runtime personality for the resolved guest-OS version, a "MAJOR.MINOR[.PATCH]"
    // string (NULL = latest). The returned personality is owned by the caller and destroyed
    // through its own vtbl (nix_personality_destroy); the IAbi may be Released afterwards.
    //
    STDMETHOD_ (struct _nix_personality *, CreatePersonality)(THIS_ IN CHAR8 CONST *pVersion) PURE;
};

typedef IAbi *PIABI;

//
// Interface identifier. ABI family base {1C9A0004-0001-4C50-9A00-0000000000NN}.
//
inline constexpr IID IID_IAbi =
    { 0x1C9A0004, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 } };

} // namespace LibCPU

//
// The plist array key a ".abi" bundle carries (family name + legacy aliases), read WITHOUT loading
// the bundle -- exactly like LCDeviceMatch.
//
#define LIBCPU_ABI_MATCH_KEY "LCAbiMatch"

//
// ABI-bundle entry contract. A ".abi" bundle exports one C symbol returning a new, owned IAbi:
//
//     LIBCPU_MODULE_CREATE_ABI (LibCPU::CreateOpenBsdAbi)
//
#if defined(_WIN32)
#define LIBCPU_ABI_EXPORT __declspec(dllexport)
#else
#define LIBCPU_ABI_EXPORT __attribute__((visibility("default")))
#endif

#define LIBCPU_MODULE_CREATE_ABI(Creator) \
    extern "C" LIBCPU_ABI_EXPORT ::LibCPU::IAbi *LibCPUModuleCreateAbi (void) { return (Creator) (); }

#define LIBCPU_MODULE_ABI_ENTRY_NAME "LibCPUModuleCreateAbi"

#endif // LIBCPU_IABI_H
