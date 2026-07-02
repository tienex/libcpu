/** @file
  LibCPU executable-format loader COM interfaces.

  Each executable file format (ELF, PE, Mach-O, a.out, ...) is a COM object packaged in its own
  loadable ".loader" bundle, exactly like the ".backend"/".device" plugins. The host enumerates the
  bundles, asks each to Probe the image bytes, and lets the highest-scoring loader Load the image
  into the guest address space through an ILoaderMemory sink -- so a loader places segments at their
  virtual addresses without knowing the host's memory layout.

  A loader also REPORTS the image's dynamic-linking facts (whether it is dynamically linked, the
  requested interpreter, the shared objects it needs) in LOADER_DYNAMIC, but does not RESOLVE them:
  loading those objects and applying relocations/imports is a separate run-time-linker phase.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_ILOADER_H
#define LIBCPU_ILOADER_H

#include "LibCPU/PCom.h"

namespace LibCPU {

//
// The guest byte order an image implies.
//
typedef enum _LOADER_ENDIAN
{
    LoaderEndianLittle = 0,
    LoaderEndianBig    = 1
} LOADER_ENDIAN;

//
// The dynamic-linking facts a loader EXPOSES but does not resolve: whether the image needs a
// run-time linker, its requested interpreter (ELF PT_INTERP and the like), and the shared objects
// it names. The strings are owned by the loader object and stay valid until it is released.
//
typedef struct _LOADER_DYNAMIC
{
    BOOLEAN              IsDynamic;   // needs a run-time linker
    CHAR8 CONST         *Interp;      // requested interpreter path, or NULL
    UINT32               NeededCount; // number of NEEDED shared objects
    CHAR8 CONST * CONST *Needed;      // their names (loader-owned storage)
} LOADER_DYNAMIC;

//
// What a successful Load produced. Entry/BrkBase/LoadEnd are in the guest's own address units (byte
// addresses for byte-addressed machines; word addresses for word-addressed ones such as the PDP-10).
//
typedef struct _LOADER_RESULT
{
    UINT64         Entry;       // program entry point
    UINT64         BrkBase;     // initial heap break, or 0 if not applicable
    UINT64         LoadEnd;     // one past the highest address written
    LOADER_ENDIAN  Endian;      // guest byte order this image implies
    UINT32         WordBits;    // guest word width in bits (16/32/36/64), 0 = unknown
    UINT32         MachineHint; // the format's machine id (ELF EM_*, a.out mid, ...), 0 = none
    LOADER_DYNAMIC Dynamic;     // dynamic-linking metadata (exposed, unresolved)
} LOADER_RESULT;

//
// The guest address space a loader writes into. The host implements it over its memory (a flat RAM
// in lcx); the loader places bytes at guest addresses without knowing the host layout.
//
DECLARE_INTERFACE_ (ILoaderMemory, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD_ (UINT64, Size)(THIS) PURE;                                        // guest RAM size in bytes
    STDMETHOD (Write)(THIS_ UINT64 Addr, IN VOID CONST *pData, UINT64 Len) PURE; // copy bytes in
    STDMETHOD (Zero)(THIS_ UINT64 Addr, UINT64 Len) PURE;                        // zero-fill (bss)
};

//
// One executable-format loader. Probe scores the image (0 = not mine, higher = more confident); the
// host runs the highest scorer's Load.
//
DECLARE_INTERFACE_ (ILoader, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD_ (CHAR8 CONST *, GetName)(THIS) PURE;                     // format name ("elf", "aout", ...)
    STDMETHOD_ (UINT32, Probe)(THIS_ IN UINT8 CONST *pImage, UINT64 Len) PURE;   // confidence 0..100
    STDMETHOD (Load)(THIS_ IN UINT8 CONST *pImage, UINT64 Len,
                     IN ILoaderMemory *pMem, OUT LOADER_RESULT *pResult) PURE;
};

typedef ILoader       *PILOADER;
typedef ILoaderMemory *PILOADERMEMORY;
typedef LOADER_RESULT *PLOADER_RESULT;

//
// Interface identifiers. Loader family base {1C9A0003-0001-4C50-9A00-0000000000NN}.
//
inline constexpr IID IID_ILoader =
    { 0x1C9A0003, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 } };
inline constexpr IID IID_ILoaderMemory =
    { 0x1C9A0003, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 } };

} // namespace LibCPU

//
// Loader-bundle entry contract. A ".loader" bundle exports one C symbol returning a new, owned
// ILoader (Release when done). Define it in a small per-bundle shim:
//
//     LIBCPU_MODULE_CREATE_LOADER (LibCPU::CreateElfLoader)
//
#if defined(_WIN32)
#define LIBCPU_LOADER_EXPORT __declspec(dllexport)
#else
#define LIBCPU_LOADER_EXPORT __attribute__((visibility("default")))
#endif

#define LIBCPU_MODULE_CREATE_LOADER(Creator) \
    extern "C" LIBCPU_LOADER_EXPORT ::LibCPU::ILoader *LibCPUModuleCreateLoader (void) { return (Creator) (); }

#define LIBCPU_MODULE_LOADER_ENTRY_NAME "LibCPUModuleCreateLoader"

#endif // LIBCPU_ILOADER_H
