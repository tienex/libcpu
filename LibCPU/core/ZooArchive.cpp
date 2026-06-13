/** @file  ZOO/zstd solid archive. See ZooArchive.h. */

#include "ZooArchive.h"
#include <cstdio>
#include <cstring>
#include <zstd.h>

namespace LibCPU {

// On-disk layout (little-endian):
//   HEADER         magic, version, member count, solid + compressed sizes
//   DIRECTORY      MemberCount x { offset, length, nameLen, name[nameLen] }
//   SOLID BLOCK    one zstd stream of the concatenated member bytes
static UINT32 CONST ZOO_MAGIC   = 0x315A4F4F;   // 'OOZ1'
static UINT32 CONST ZOO_VERSION = 1;

typedef struct _ZOO_HEADER {
    UINT32 Magic;
    UINT32 Version;
    UINT32 MemberCount;
    UINT32 Reserved;
    UINT64 SolidSize;        // uncompressed concatenated size
    UINT64 CompressedSize;   // length of the zstd stream
} ZOO_HEADER;

void
ZooWriter::Add (std::string Name, UINT8 CONST *pData, UINT64 Len)
{
    MEMBER M;
    M.Name = std::move (Name);
    M.Data.assign (pData, pData + Len);
    m_Members.push_back (std::move (M));
}

bool
ZooWriter::Save (CHAR8 CONST *pPath, INT32 Level, std::string *pError)
{
    // 1. Concatenate every member's bytes into one solid block, recording offsets.
    std::vector<UINT8> Solid;
    std::vector<UINT64> Offsets, Lengths;
    for (MEMBER CONST &M : m_Members) {
        Offsets.push_back (Solid.size ());
        Lengths.push_back (M.Data.size ());
        Solid.insert (Solid.end (), M.Data.begin (), M.Data.end ());
    }

    // 2. Compress the whole block as ONE zstd stream (solid compression).
    size_t Bound = ZSTD_compressBound (Solid.size ());
    std::vector<UINT8> Comp (Bound ? Bound : 1);
    size_t CompLen = ZSTD_compress (Comp.data (), Comp.size (),
                                    Solid.data (), Solid.size (), Level);
    if (ZSTD_isError (CompLen)) {
        if (pError) { *pError = std::string ("zstd compress: ") + ZSTD_getErrorName (CompLen); }
        return false;
    }

    std::FILE *pf = std::fopen (pPath, "wb");
    if (pf == nullptr) {
        if (pError) { *pError = std::string ("cannot create '") + pPath + "'"; }
        return false;
    }
    ZOO_HEADER H;
    H.Magic          = ZOO_MAGIC;
    H.Version        = ZOO_VERSION;
    H.MemberCount    = (UINT32) m_Members.size ();
    H.Reserved       = 0;
    H.SolidSize      = Solid.size ();
    H.CompressedSize = CompLen;
    std::fwrite (&H, sizeof (H), 1, pf);
    for (UINT32 I = 0; I < m_Members.size (); I++) {
        std::fwrite (&Offsets[I], sizeof (UINT64), 1, pf);
        std::fwrite (&Lengths[I], sizeof (UINT64), 1, pf);
        UINT32 NameLen = (UINT32) m_Members[I].Name.size ();
        std::fwrite (&NameLen, sizeof (UINT32), 1, pf);
        std::fwrite (m_Members[I].Name.data (), 1, NameLen, pf);
    }
    std::fwrite (Comp.data (), 1, CompLen, pf);
    std::fclose (pf);
    return true;
}

bool
ZooArchive::Load (CHAR8 CONST *pPath, std::string *pError)
{
    std::FILE *pf = std::fopen (pPath, "rb");
    if (pf == nullptr) {
        if (pError) { *pError = std::string ("cannot open '") + pPath + "'"; }
        return false;
    }
    ZOO_HEADER H;
    bool Ok = std::fread (&H, sizeof (H), 1, pf) == 1 && H.Magic == ZOO_MAGIC && H.Version == ZOO_VERSION;
    if (!Ok) {
        if (pError) { *pError = "not a ZOO/zstd archive"; }
        std::fclose (pf);
        return false;
    }
    m_Entries.clear ();
    for (UINT32 I = 0; Ok && I < H.MemberCount; I++) {
        ENTRY E;
        UINT32 NameLen = 0;
        Ok = std::fread (&E.Offset, sizeof (UINT64), 1, pf) == 1
             && std::fread (&E.Length, sizeof (UINT64), 1, pf) == 1
             && std::fread (&NameLen, sizeof (UINT32), 1, pf) == 1;
        if (Ok) {
            E.Name.resize (NameLen);
            Ok = NameLen == 0 || std::fread (&E.Name[0], 1, NameLen, pf) == NameLen;
            m_Entries.push_back (std::move (E));
        }
    }
    std::vector<UINT8> Comp (H.CompressedSize);
    if (Ok) {
        Ok = H.CompressedSize == 0 || std::fread (Comp.data (), 1, H.CompressedSize, pf) == H.CompressedSize;
    }
    std::fclose (pf);
    if (!Ok) {
        if (pError) { *pError = "truncated archive"; }
        return false;
    }

    // Decompress the solid block once; members are slices of it.
    m_Solid.resize (H.SolidSize);
    size_t Got = ZSTD_decompress (m_Solid.data (), m_Solid.size (), Comp.data (), Comp.size ());
    if (ZSTD_isError (Got) || Got != H.SolidSize) {
        if (pError) { *pError = std::string ("zstd decompress: ") + (ZSTD_isError (Got) ? ZSTD_getErrorName (Got) : "size mismatch"); }
        return false;
    }
    m_CompressedSize = H.CompressedSize;
    return true;
}

std::vector<std::string>
ZooArchive::List () CONST
{
    std::vector<std::string> Out;
    for (ENTRY CONST &E : m_Entries) {
        Out.push_back (E.Name);
    }
    return Out;
}

bool
ZooArchive::Has (std::string CONST &Name) CONST
{
    for (ENTRY CONST &E : m_Entries) {
        if (E.Name == Name) { return true; }
    }
    return false;
}

bool
ZooArchive::Extract (std::string CONST &Name, std::vector<UINT8> *pOut) CONST
{
    for (ENTRY CONST &E : m_Entries) {
        if (E.Name == Name) {
            if (E.Offset + E.Length > m_Solid.size ()) { return false; }
            pOut->assign (m_Solid.begin () + E.Offset, m_Solid.begin () + E.Offset + E.Length);
            return true;
        }
    }
    return false;
}

} // namespace LibCPU
