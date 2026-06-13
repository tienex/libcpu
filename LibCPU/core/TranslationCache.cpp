/** @file  On-disk translation cache implementation. See TranslationCache.h. */

#include "TranslationCache.h"
#include "../aot/AotGenerator.h"
#include "LibCPU/PCom.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace LibCPU {

// On-disk file layout (version 1): a fixed header, then the variable parts. The key
// fields are stored so a hash collision is detected (and treated as a miss) rather
// than returning a wrong artifact.
static UINT32 CONST LC_CACHE_MAGIC   = 0x4343424C;   // 'LBCC'
static UINT32 CONST LC_CACHE_VERSION = 1;

typedef struct _LC_CACHE_HEADER {
    UINT32 Magic;
    UINT32 Version;
    UINT64 Entry;
    UINT64 End;
    UINT32 RegionLen;
    UINT64 RegionHash;
    UINT32 ArchLen;
    UINT32 BackendLen;
    UINT32 BlobLen;
} LC_CACHE_HEADER;

static UINT64
Fnv1a (VOID CONST *pData, size_t Len, UINT64 Seed)
{
    UINT8 CONST *p = (UINT8 CONST *) pData;
    UINT64 H = Seed;
    for (size_t I = 0; I < Len; I++) {
        H ^= p[I];
        H *= UINT64_C (0x100000001b3);
    }
    return H;
}

TranslationCache::TranslationCache (std::string Dir)
    : m_Dir (std::move (Dir)), m_Hits (0), m_Misses (0)
{
    std::error_code Ec;
    fs::create_directories (m_Dir, Ec);
}

std::string
TranslationCache::DefaultDir ()
{
    if (CHAR8 CONST *p = std::getenv ("LIBCPU_CACHE")) {
        return std::string (p);
    }
    if (CHAR8 CONST *p = std::getenv ("XDG_CACHE_HOME")) {
        return std::string (p) + "/libcpu";
    }
    if (CHAR8 CONST *p = std::getenv ("HOME")) {
        return std::string (p) + "/.cache/libcpu";
    }
    return std::string ("./.libcpu-cache");
}

UINT64
TranslationCache::Key (CHAR8 CONST *pArch, CHAR8 CONST *pBackend, CPU_ADDR Entry, CPU_ADDR End,
                         UINT8 CONST *pRegion, UINT32 RegionLen) CONST
{
    UINT64 H = UINT64_C (0xcbf29ce484222325);
    H = Fnv1a (pArch, std::strlen (pArch), H);
    H = Fnv1a (pBackend, std::strlen (pBackend), H);
    UINT64 E = (UINT64) Entry, F = (UINT64) End;
    H = Fnv1a (&E, sizeof (E), H);
    H = Fnv1a (&F, sizeof (F), H);
    H = Fnv1a (pRegion, RegionLen, H);
    return H;
}

std::string
TranslationCache::PathFor (CHAR8 CONST *pArch, UINT64 Key) CONST
{
    char Name[32];
    std::snprintf (Name, sizeof (Name), "%016llx.lcc", (unsigned long long) Key);
    return m_Dir + "/" + pArch + "/" + Name;
}

bool
TranslationCache::Lookup (CHAR8 CONST *pArch, CHAR8 CONST *pBackend, CPU_ADDR Entry, CPU_ADDR End,
                            UINT8 CONST *pRegion, UINT32 RegionLen, std::vector<UINT8> &Blob)
{
    std::string Path = PathFor (pArch, Key (pArch, pBackend, Entry, End, pRegion, RegionLen));
    std::FILE *pf = std::fopen (Path.c_str (), "rb");
    if (pf == nullptr) {
        m_Misses++;
        return false;
    }
    LC_CACHE_HEADER H;
    bool Ok = std::fread (&H, sizeof (H), 1, pf) == 1
              && H.Magic == LC_CACHE_MAGIC && H.Version == LC_CACHE_VERSION
              && H.Entry == (UINT64) Entry && H.End == (UINT64) End
              && H.RegionLen == RegionLen
              && H.RegionHash == Fnv1a (pRegion, RegionLen, UINT64_C (0xcbf29ce484222325));
    std::string Arch (H.ArchLen, '\0'), Backend (H.BackendLen, '\0');
    if (Ok && H.ArchLen)    { Ok = std::fread (&Arch[0], 1, H.ArchLen, pf) == H.ArchLen; }
    if (Ok && H.BackendLen) { Ok = std::fread (&Backend[0], 1, H.BackendLen, pf) == H.BackendLen; }
    Ok = Ok && Arch == pArch && Backend == pBackend;
    if (Ok) {
        Blob.resize (H.BlobLen);
        Ok = H.BlobLen == 0 || std::fread (Blob.data (), 1, H.BlobLen, pf) == H.BlobLen;
    }
    std::fclose (pf);
    if (Ok) { m_Hits++; } else { m_Misses++; }
    return Ok;
}

void
TranslationCache::Store (CHAR8 CONST *pArch, CHAR8 CONST *pBackend, CPU_ADDR Entry, CPU_ADDR End,
                           UINT8 CONST *pRegion, UINT32 RegionLen, UINT8 CONST *pBlob, UINT32 BlobLen)
{
    std::error_code Ec;
    fs::create_directories (m_Dir + "/" + pArch, Ec);
    std::string Path = PathFor (pArch, Key (pArch, pBackend, Entry, End, pRegion, RegionLen));
    std::FILE *pf = std::fopen (Path.c_str (), "wb");
    if (pf == nullptr) {
        return;
    }
    LC_CACHE_HEADER H;
    H.Magic      = LC_CACHE_MAGIC;
    H.Version    = LC_CACHE_VERSION;
    H.Entry      = (UINT64) Entry;
    H.End        = (UINT64) End;
    H.RegionLen  = RegionLen;
    H.RegionHash = Fnv1a (pRegion, RegionLen, UINT64_C (0xcbf29ce484222325));
    H.ArchLen    = (UINT32) std::strlen (pArch);
    H.BackendLen = (UINT32) std::strlen (pBackend);
    H.BlobLen    = BlobLen;
    std::fwrite (&H, sizeof (H), 1, pf);
    std::fwrite (pArch, 1, H.ArchLen, pf);
    std::fwrite (pBackend, 1, H.BackendLen, pf);
    std::fwrite (pBlob, 1, BlobLen, pf);
    std::fclose (pf);
}

std::vector<TranslationCache::LC_CACHE_ENTRY>
TranslationCache::List () CONST
{
    std::vector<LC_CACHE_ENTRY> Out;
    std::error_code Ec;
    if (!fs::exists (m_Dir, Ec)) {
        return Out;
    }
    for (auto CONST &Dir : fs::recursive_directory_iterator (m_Dir, Ec)) {
        if (!Dir.is_regular_file () || Dir.path ().extension () != ".lcc") {
            continue;
        }
        LC_CACHE_ENTRY E;
        E.Path = Dir.path ().string ();
        E.Size = (UINT64) fs::file_size (Dir.path (), Ec);
        E.Mtime = (UINT64) fs::last_write_time (Dir.path (), Ec).time_since_epoch ().count ();
        E.Arch = Dir.path ().parent_path ().filename ().string ();
        // Read the backend name out of the header (best-effort).
        std::FILE *pf = std::fopen (E.Path.c_str (), "rb");
        if (pf != nullptr) {
            LC_CACHE_HEADER H;
            if (std::fread (&H, sizeof (H), 1, pf) == 1 && H.Magic == LC_CACHE_MAGIC) {
                std::fseek (pf, (long) H.ArchLen, SEEK_CUR);
                std::string B (H.BackendLen, '\0');
                if (H.BackendLen && std::fread (&B[0], 1, H.BackendLen, pf) == H.BackendLen) {
                    E.Backend = B;
                }
            }
            std::fclose (pf);
        }
        Out.push_back (std::move (E));
    }
    return Out;
}

UINT64
TranslationCache::TotalSize () CONST
{
    UINT64 Total = 0;
    for (LC_CACHE_ENTRY CONST &E : List ()) {
        Total += E.Size;
    }
    return Total;
}

UINT32
TranslationCache::Clean ()
{
    UINT32 Count = 0;
    std::error_code Ec;
    for (LC_CACHE_ENTRY CONST &E : List ()) {
        if (fs::remove (E.Path, Ec)) {
            Count++;
        }
    }
    return Count;
}

UINT32
TranslationCache::Cap (UINT64 MaxBytes)
{
    std::vector<LC_CACHE_ENTRY> Entries = List ();
    UINT64 Total = 0;
    for (LC_CACHE_ENTRY CONST &E : Entries) {
        Total += E.Size;
    }
    // Oldest first (smallest mtime) so we evict least-recently-written.
    for (size_t A = 0; A + 1 < Entries.size (); A++) {
        for (size_t B = A + 1; B < Entries.size (); B++) {
            if (Entries[B].Mtime < Entries[A].Mtime) {
                std::swap (Entries[A], Entries[B]);
            }
        }
    }
    UINT32 Removed = 0;
    std::error_code Ec;
    for (LC_CACHE_ENTRY CONST &E : Entries) {
        if (Total <= MaxBytes) {
            break;
        }
        if (fs::remove (E.Path, Ec)) {
            Total -= E.Size;
            Removed++;
        }
    }
    return Removed;
}

HRESULT
CachedTranslate (TranslationCache &Cache, ICpuArchitecture *pArch, ICpuBackend *pBackend,
                   UINT8 CONST *pRamBase, CPU_ADDR Entry, CPU_ADDR End,
                   ICpuCode **ppCode, bool *pHit)
{
    *ppCode = nullptr;
    if (pHit != nullptr) {
        *pHit = false;
    }
    CPU_ARCH_INFO Info;
    std::memset (&Info, 0, sizeof (Info));
    pArch->GetInfo (&Info);
    CHAR8 CONST *pArchName    = Info.pName ? Info.pName : "?";
    CHAR8 CONST *pBackendName = pBackend->GetName ();
    UINT8 CONST *pRegion      = pRamBase + Entry;
    UINT32       RegionLen    = (UINT32) (End - Entry);

    // Cache hit: reload, if the backend can rebuild a serialised artifact.
    std::vector<UINT8> Blob;
    if (Cache.Lookup (pArchName, pBackendName, Entry, End, pRegion, RegionLen, Blob)) {
        ICpuBackendCache *pLoad = nullptr;
        if (SUCCEEDED (pBackend->QueryInterface (IID_ICpuBackendCache, (VOID **) &pLoad)) && pLoad != nullptr) {
            HRESULT hr = pLoad->LoadCode (Blob.data (), (UINT32) Blob.size (), ppCode);
            pLoad->Release ();
            if (SUCCEEDED (hr) && *ppCode != nullptr) {
                if (pHit != nullptr) { *pHit = true; }
                return S_OK;
            }
        }
        // Could not reload (backend lacks the capability): fall through and rebuild.
    }

    // Miss: translate the whole region, then serialise + store it for next time.
    HRESULT hr = GenerateAotCfg (pArch, pBackend, Entry, End, ppCode, nullptr);
    if (FAILED (hr) || *ppCode == nullptr) {
        return hr;
    }
    ICpuCodeSerialize *pSer = nullptr;
    if (SUCCEEDED ((*ppCode)->QueryInterface (IID_ICpuCodeSerialize, (VOID **) &pSer)) && pSer != nullptr) {
        UINT32 Needed = 0;
        pSer->Serialize (nullptr, 0, &Needed);
        if (Needed > 0) {
            std::vector<UINT8> Out (Needed);
            pSer->Serialize (Out.data (), Needed, nullptr);
            Cache.Store (pArchName, pBackendName, Entry, End, pRegion, RegionLen, Out.data (), Needed);
        }
        pSer->Release ();
    }
    return S_OK;
}

} // namespace LibCPU
