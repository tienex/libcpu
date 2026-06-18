/** @file
  Content-addressed, host-aware translation cache -- the DBM-like code store.

  Where TranslationCache keys an artifact by its absolute guest addresses (one file per
  region), CodeCache keys it by the CONTENT of the guest region plus the exact target the
  artifact was compiled for, and keeps every artifact in ONE archive (a ZOO container, the
  same one the knowledge library uses). The key has two halves:

    guest half  { guest-arch, guest-cpu, content-hash, pic }   -- host-independent; the
                                                                  "same code" across machines
    host  half  { host-fingerprint, abi }                      -- which hosts may run the blob

  One guest region (the guest half) therefore hangs a family of host-targeted artifacts: a
  cache file moved between machines lets each host find its own variant, or miss and add one,
  so the file accretes multi-architecture coverage. The host fingerprint comes from the
  producing backend (ICpuBackendTarget); 0 means host-independent (an interpreter's bytecode).

  Each artifact also carries a small PROFILE (run count, tier reached, optimization level) so
  a region resumes at its earned tier on a later run instead of climbing from cold.

  The archive is write-once, so an online cache accretes new artifacts in memory and merges
  them with the loaded set on Flush -- the on-disk consolidation step. Code artifacts are
  stored page-aligned (optionally compressed) so a loaded archive can map them directly.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_CODECACHE_H
#define LIBCPU_CODECACHE_H

#include "LibCPU/Base.h"
#include "ZooArchive.h"
#include <map>
#include <string>
#include <vector>

namespace LibCPU {

// 128-bit content hash of a guest region. Wide enough that a collision (which would run the
// wrong code) is negligible; the key string also pins arch/cpu/length so unequal regions of
// different shape can never alias even on a hash clash.
typedef struct _LC_CONTENT_HASH {
    UINT64 Lo;
    UINT64 Hi;
} LC_CONTENT_HASH;

// Per-artifact profile, persisted beside the code so tiering survives across runs.
typedef struct _LC_CACHE_PROFILE {
    UINT64 Runs;        // how many times the region has executed
    UINT32 Tier;        // highest tier reached (index into the tier ladder)
    UINT32 OptLevel;    // optimization level the artifact was built at (LC_OPT_DEFAULT if N/A)
} LC_CACHE_PROFILE;

class CodeCache {
public:
    explicit CodeCache (std::string Path);
    ~CodeCache ();

    // Load an existing archive at the path (a missing file is not an error -- the cache is
    // simply empty and will be created on Flush). Returns false only on a corrupt file.
    bool Load (std::string *pError);

    // 128-bit content hash of a guest region's bytes.
    static LC_CONTENT_HASH Hash (UINT8 CONST *pRegion, UINT32 RegionLen);

    // Build the composite key string for a region + target. The string is the archive
    // member name, so the ZOO directory itself is the cache index.
    static std::string MakeKey (CHAR8 CONST *pGuestArch, CHAR8 CONST *pGuestCpu,
                                LC_CONTENT_HASH CONST &Content, UINT32 RegionLen, BOOLEAN Pic,
                                UINT64 HostFingerprint, UINT32 Abi);

    // Look up an artifact. On a hit fills *ppBlob/*pLen (zero-copy, valid until the cache is
    // destroyed or re-flushed) and, if non-null, *pProfile; returns true. On a miss returns
    // false. A blob just Store()d this session is returned too (from the pending set).
    bool Lookup (std::string CONST &Key, OUT UINT8 CONST **ppBlob, OUT UINT64 *pLen,
                 OUT LC_CACHE_PROFILE *pProfile);

    // Add (or replace) an artifact + its profile under Key. Held in memory until Flush.
    void Store (std::string CONST &Key, UINT8 CONST *pBlob, UINT64 Len,
                LC_CACHE_PROFILE CONST &Profile, BOOLEAN Compress);

    // Merge the loaded artifacts with everything Store()d this session and write the archive
    // back to the path atomically (temp file + rename). A no-op flush still rewrites.
    bool Flush (std::string *pError);

    UINT32 Count () CONST;                       // distinct artifacts (loaded + pending)
    std::vector<std::string> Keys () CONST;      // every distinct artifact key (loaded + pending)

    // Directory metadata for a key without inflating it: uncompressed size, stored
    // compressed length (0 = verbatim), alignment. Reads the loaded archive only.
    bool RawInfo (std::string CONST &Key, OUT UINT64 *pLen, OUT UINT64 *pCompLength,
                  OUT UINT32 *pAlignment) CONST;

    UINT32 Hits () CONST { return m_Hits; }
    UINT32 Misses () CONST { return m_Misses; }

private:
    typedef struct _PENDING {
        std::vector<UINT8> Blob;
        LC_CACHE_PROFILE   Profile;
        BOOLEAN            Compress;
    } PENDING;

    std::string                    m_Path;
    ZooArchive                     m_Loaded;     // mmap-backed view of the on-disk archive
    BOOLEAN                        m_HaveLoaded; // m_Loaded holds a valid (possibly empty) archive
    std::map<std::string, PENDING> m_Pending;    // artifacts stored this session, not yet flushed
    UINT32                         m_Hits;
    UINT32                         m_Misses;

    static std::string ProfileName (std::string CONST &Key) { return Key + "#p"; }
};

} // namespace LibCPU

#endif // LIBCPU_CODECACHE_H
