/** @file
  On-disk translation cache.

  Translating a guest region is the expensive step; its result (a serialised
  backend artifact) depends only on the guest bytes, the frontend, and the backend.
  LcTranslationCache hashes those into a key and stores the artifact under a cache
  directory, so a later run with the same inputs RELOADS the artifact instead of
  re-translating. It works for both JIT and AOT (the caller just routes its
  translate call through LcCachedTranslate). The cache is manageable: it can be
  listed, sized, capped to a byte budget (oldest-first eviction), and cleaned.

  Caching requires a backend that serialises its artifacts (ICpuCodeSerialize) and
  reloads them (ICpuBackendCache); the interpreter does. A backend that does neither
  is simply never cached -- LcCachedTranslate falls back to a plain translation.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_TRANSLATIONCACHE_H
#define LIBCPU_TRANSLATIONCACHE_H

#include "LibCPU/ICpu.h"
#include <string>
#include <vector>

namespace LibCPU {

class LcTranslationCache {
public:
    explicit LcTranslationCache (std::string Dir);

    // Default cache directory: $LIBCPU_CACHE, else $XDG_CACHE_HOME/libcpu, else
    // $HOME/.cache/libcpu.
    static std::string DefaultDir ();

    // Look up the artifact for (Arch, Backend, Entry, End, Region). On a hit fills
    // Blob and returns true; on a miss returns false.
    bool Lookup (CHAR8 CONST *pArch, CHAR8 CONST *pBackend, CPU_ADDR Entry, CPU_ADDR End,
                 UINT8 CONST *pRegion, UINT32 RegionLen, std::vector<UINT8> &Blob);

    // Store an artifact blob under the key.
    void Store (CHAR8 CONST *pArch, CHAR8 CONST *pBackend, CPU_ADDR Entry, CPU_ADDR End,
                UINT8 CONST *pRegion, UINT32 RegionLen, UINT8 CONST *pBlob, UINT32 BlobLen);

    // --- management -------------------------------------------------------
    typedef struct _LC_CACHE_ENTRY {
        std::string Path;
        std::string Arch;
        std::string Backend;
        UINT64      Size;       // file size in bytes
        UINT64      Mtime;      // last-write time (seconds)
    } LC_CACHE_ENTRY;

    std::vector<LC_CACHE_ENTRY> List () CONST;          // every cached artifact
    UINT64 TotalSize () CONST;                          // total bytes on disk
    UINT32 Clean ();                                    // remove all; returns count
    UINT32 Cap (UINT64 MaxBytes);                       // evict oldest until <= MaxBytes

    UINT32 Hits () CONST { return m_Hits; }
    UINT32 Misses () CONST { return m_Misses; }

private:
    std::string m_Dir;
    UINT32      m_Hits;
    UINT32      m_Misses;

    UINT64      Key (CHAR8 CONST *pArch, CHAR8 CONST *pBackend, CPU_ADDR Entry, CPU_ADDR End,
                     UINT8 CONST *pRegion, UINT32 RegionLen) CONST;
    std::string PathFor (CHAR8 CONST *pArch, UINT64 Key) CONST;
};

//
// Translate [Entry, End) through pBackend, going through the cache: on a hit the
// artifact is reloaded from disk (no translation); on a miss it is translated (via
// the whole-program CFG path) and stored. pRamBase points at guest memory so the
// region bytes can be hashed. *pHit, if non-null, reports whether the cache hit.
// Returns the same HRESULT the underlying translate would.
//
HRESULT LcCachedTranslate (LcTranslationCache &Cache, ICpuArchitecture *pArch, ICpuBackend *pBackend,
                           UINT8 CONST *pRamBase, CPU_ADDR Entry, CPU_ADDR End,
                           OUT ICpuCode **ppCode, OUT bool *pHit);

} // namespace LibCPU

#endif // LIBCPU_TRANSLATIONCACHE_H
