/** @file  Content-addressed, host-aware translation cache. See CodeCache.h. */

#include "CodeCache.h"
#include <cstdio>
#include <cstring>

namespace LibCPU {

//
// 128-bit content hash: two independent FNV-1a streams (distinct offset bases and primes),
// the high stream also folding the length, so two regions hash equal only on a double
// collision. The key string additionally pins arch/cpu/length, so even that cannot alias
// genuinely different code.
//
LC_CONTENT_HASH
CodeCache::Hash (UINT8 CONST *pRegion, UINT32 RegionLen)
{
    UINT64 CONST Basis1 = UINT64_C (0xCBF29CE484222325), Prime1 = UINT64_C (0x00000100000001B3);
    UINT64 CONST Basis2 = UINT64_C (0x84222325CBF29CE4), Prime2 = UINT64_C (0x000000000000A7F3);
    UINT64 Lo = Basis1;
    UINT64 Hi = Basis2 ^ RegionLen;
    for (UINT32 I = 0; I < RegionLen; I++) {
        Lo = (Lo ^ pRegion[I]) * Prime1;
        Hi = (Hi ^ pRegion[RegionLen - 1 - I]) * Prime2;   // reverse scan -> independent of Lo
    }
    LC_CONTENT_HASH H;
    H.Lo = Lo;
    H.Hi = Hi;
    return H;
}

std::string
CodeCache::MakeKey (CHAR8 CONST *pGuestArch, CHAR8 CONST *pGuestCpu,
                    LC_CONTENT_HASH CONST &Content, UINT32 RegionLen, BOOLEAN Pic,
                    UINT64 HostFingerprint, UINT32 Abi)
{
    CHAR8 Buf[256];
    std::snprintf (Buf, sizeof (Buf),
                   "%s/%s/%016llx%016llx/n%u/p%u/fp%016llx/a%u",
                   pGuestArch, pGuestCpu,
                   (unsigned long long) Content.Hi, (unsigned long long) Content.Lo,
                   (unsigned) RegionLen, (unsigned) (Pic ? 1 : 0),
                   (unsigned long long) HostFingerprint, (unsigned) Abi);
    return std::string (Buf);
}

// --- profile (de)serialization: a fixed 16-byte little-endian payload --------------------
static VOID
PackProfile (LC_CACHE_PROFILE CONST &P, UINT8 *pBuf)
{
    for (UINT32 I = 0; I < 8; I++) { pBuf[I]     = (UINT8) (P.Runs >> (8 * I)); }
    for (UINT32 I = 0; I < 4; I++) { pBuf[8 + I] = (UINT8) (P.Tier >> (8 * I)); }
    for (UINT32 I = 0; I < 4; I++) { pBuf[12 + I] = (UINT8) (P.OptLevel >> (8 * I)); }
}

static VOID
UnpackProfile (UINT8 CONST *pBuf, UINT32 Len, LC_CACHE_PROFILE *pP)
{
    std::memset (pP, 0, sizeof (*pP));
    if (Len < 16) { return; }
    for (UINT32 I = 0; I < 8; I++) { pP->Runs     |= (UINT64) pBuf[I]      << (8 * I); }
    for (UINT32 I = 0; I < 4; I++) { pP->Tier     |= (UINT32) pBuf[8 + I]  << (8 * I); }
    for (UINT32 I = 0; I < 4; I++) { pP->OptLevel |= (UINT32) pBuf[12 + I] << (8 * I); }
}

CodeCache::CodeCache (std::string Path)
    : m_Path (std::move (Path)), m_HaveLoaded (FALSE), m_Hits (0), m_Misses (0)
{
}

CodeCache::~CodeCache ()
{
}

bool
CodeCache::Load (std::string *pError)
{
    // A missing file just means an empty cache; only a present-but-corrupt file is an error.
    std::FILE *pProbe = std::fopen (m_Path.c_str (), "rb");
    if (pProbe == nullptr) {
        m_HaveLoaded = FALSE;
        return true;
    }
    std::fclose (pProbe);

    if (!m_Loaded.Load (m_Path.c_str (), pError)) { return false; }
    if (!m_Loaded.Map (pError)) { return false; }
    m_HaveLoaded = TRUE;
    return true;
}

bool
CodeCache::Lookup (std::string CONST &Key, UINT8 CONST **ppBlob, UINT64 *pLen, LC_CACHE_PROFILE *pProfile)
{
    // Artifacts stored this session win (they are the freshest).
    std::map<std::string, PENDING>::const_iterator It = m_Pending.find (Key);
    if (It != m_Pending.end ()) {
        if (ppBlob)   { *ppBlob = It->second.Blob.data (); }
        if (pLen)     { *pLen   = It->second.Blob.size (); }
        if (pProfile) { *pProfile = It->second.Profile; }
        m_Hits++;
        return true;
    }

    if (m_HaveLoaded) {
        UINT8 CONST *pBytes = nullptr;
        UINT64       BLen   = 0;
        if (m_Loaded.GetRaw (Key, &pBytes, &BLen)) {
            if (ppBlob) { *ppBlob = pBytes; }
            if (pLen)   { *pLen   = BLen; }
            if (pProfile) {
                std::vector<UINT8> PBytes;
                std::string PName = ProfileName (Key);
                if (m_Loaded.Extract (PName, &PBytes)) {
                    UnpackProfile (PBytes.data (), (UINT32) PBytes.size (), pProfile);
                } else {
                    std::memset (pProfile, 0, sizeof (*pProfile));
                }
            }
            m_Hits++;
            return true;
        }
    }

    m_Misses++;
    return false;
}

void
CodeCache::Store (std::string CONST &Key, UINT8 CONST *pBlob, UINT64 Len,
                  LC_CACHE_PROFILE CONST &Profile, BOOLEAN Compress)
{
    PENDING P;
    P.Blob.assign (pBlob, pBlob + Len);
    P.Profile  = Profile;
    P.Compress = Compress;
    m_Pending[Key] = std::move (P);
}

bool
CodeCache::Flush (std::string *pError)
{
    ZooWriter Writer;

    // 1. Re-emit every loaded artifact not overridden by a Store() this session: the code as
    //    a page-aligned raw member, its profile as a (tiny, compressible) solid member.
    if (m_HaveLoaded) {
        std::vector<std::string> Keys = m_Loaded.ListRaw ();
        for (std::string CONST &Key : Keys) {
            if (m_Pending.find (Key) != m_Pending.end ()) { continue; }   // a fresh Store replaces it
            UINT8 CONST *pBytes = nullptr;
            UINT64       BLen   = 0;
            if (!m_Loaded.GetRaw (Key, &pBytes, &BLen)) { continue; }
            // Preserve the member's compression policy: a verbatim (map-in-place) member
            // must stay verbatim across a re-flush, not be silently compressed.
            UINT64 CompLen = 0;
            m_Loaded.RawInfo (Key, nullptr, &CompLen, nullptr);
            Writer.AddRaw (Key, pBytes, BLen, ZOO_ALIGN_16K, CompLen != 0 ? TRUE : FALSE);
            std::vector<UINT8> PBytes;
            if (m_Loaded.Extract (ProfileName (Key), &PBytes) && !PBytes.empty ()) {
                Writer.Add (ProfileName (Key), (UINT8 CONST *) PBytes.data (), PBytes.size ());
            }
        }
    }

    // 2. Add everything stored this session.
    for (std::map<std::string, PENDING>::const_iterator It = m_Pending.begin (); It != m_Pending.end (); ++It) {
        PENDING CONST &P = It->second;
        Writer.AddRaw (It->first, P.Blob.data (), P.Blob.size (), ZOO_ALIGN_16K, P.Compress);
        UINT8 Buf[16];
        PackProfile (P.Profile, Buf);
        Writer.Add (ProfileName (It->first), Buf, sizeof (Buf));
    }

    // 3. Write to a temp file and rename over the target so a concurrent reader never sees a
    //    half-written cache. Drop the old mapping first (the rename replaces the inode).
    std::string Tmp = m_Path + ".tmp";
    m_Loaded.Unmap ();
    m_HaveLoaded = FALSE;
    if (!Writer.Save (Tmp.c_str (), 19, pError)) { return false; }
    if (std::rename (Tmp.c_str (), m_Path.c_str ()) != 0) {
        if (pError) { *pError = "rename of cache temp file failed"; }
        std::remove (Tmp.c_str ());
        return false;
    }

    // 4. Re-map the freshly written archive; the pending set is now durable.
    m_Pending.clear ();
    return Load (pError);
}

UINT32
CodeCache::Count () CONST
{
    UINT32 N = (UINT32) m_Pending.size ();
    if (m_HaveLoaded) {
        std::vector<std::string> Loaded = m_Loaded.ListRaw ();
        for (std::string CONST &Key : Loaded) {
            if (m_Pending.find (Key) == m_Pending.end ()) { N++; }
        }
    }
    return N;
}

std::vector<std::string>
CodeCache::Keys () CONST
{
    std::vector<std::string> Out;
    if (m_HaveLoaded) {
        std::vector<std::string> Loaded = m_Loaded.ListRaw ();
        for (std::string CONST &Key : Loaded) { Out.push_back (Key); }
    }
    for (std::map<std::string, PENDING>::const_iterator It = m_Pending.begin (); It != m_Pending.end (); ++It) {
        if (!m_HaveLoaded || !m_Loaded.RawInfo (It->first, nullptr, nullptr, nullptr)) {
            Out.push_back (It->first);   // a fresh key not already in the loaded set
        }
    }
    return Out;
}

bool
CodeCache::RawInfo (std::string CONST &Key, UINT64 *pLen, UINT64 *pCompLength, UINT32 *pAlignment) CONST
{
    if (m_HaveLoaded && m_Loaded.RawInfo (Key, pLen, pCompLength, pAlignment)) { return true; }
    // Pending (not yet flushed): report the in-memory length; compression isn't applied until Flush.
    std::map<std::string, PENDING>::const_iterator It = m_Pending.find (Key);
    if (It != m_Pending.end ()) {
        if (pLen)        { *pLen        = It->second.Blob.size (); }
        if (pCompLength) { *pCompLength = 0; }
        if (pAlignment)  { *pAlignment  = ZOO_ALIGN_16K; }
        return true;
    }
    return false;
}

} // namespace LibCPU
