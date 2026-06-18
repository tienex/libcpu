/** @file  ZOO/zstd solid archive. See ZooArchive.h. */

#include "ZooArchive.h"
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <zstd.h>

#if defined (_WIN32)
#  include <windows.h>
#elif defined (__unix__) || defined (__APPLE__)
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  define ZOO_HAVE_POSIX_MMAP 1
#  if defined (__linux__)
#    include <linux/falloc.h>
#  endif
#endif

namespace LibCPU {

// On-disk layout (little-endian -- the format's fixed endianness on every host):
//   ZOO_HEADER         magic, version, member counts, solid + compressed sizes
//   SOLID DIRECTORY    MemberCount x { ZOO_DIRENT, name[NameLen] }
//   RAW DIRECTORY      RawCount    x { ZOO_RAWENT, name[NameLen] }
//   SOLID BLOCK        one zstd stream of the concatenated solid member bytes
//   RAW BLOBS          each raw member at its (aligned) fileOffset; tails may be holes
//
// Every fixed record is a POD written/read as a unit and run through SwapStructure, which
// byte-swaps its fields only on a big-endian host (a no-op on little-endian) -- so an
// archive built on any host loads on any other. Only the variable-length member names are
// read/written directly, as they must be. Version 1 archives have no raw members and load.
static UINT32 CONST ZOO_MAGIC    = 0x315A4F4F;   // 'OOZ1'
static UINT32 CONST ZOO_VERSION  = 2;            // 1 = solid only; 2 = + raw aligned members

typedef struct _ZOO_HEADER {
    UINT32 Magic;
    UINT32 Version;
    UINT32 MemberCount;      // solid (compressed) members
    UINT32 RawCount;         // raw (aligned, directly mappable) members; was Reserved in v1 (0)
    UINT64 SolidSize;        // uncompressed concatenated size of solid members
    UINT64 CompressedSize;   // length of the zstd stream
} ZOO_HEADER;

typedef struct _ZOO_DIRENT {
    UINT64 Offset;           // into the (decompressed) solid block
    UINT64 Length;
    UINT32 NameLen;          // followed on disk by NameLen name bytes
} ZOO_DIRENT;

typedef struct _ZOO_RAWENT {
    UINT64 FileOffset;       // absolute, alignment-aligned, in the archive file
    UINT64 Length;           // uncompressed (slot) size
    UINT64 CompLength;       // 0 = stored verbatim; else zstd bytes at the slot head
    UINT32 Alignment;
    UINT32 Flags;
    UINT32 NameLen;          // followed on disk by NameLen name bytes
} ZOO_RAWENT;

// Round Value up to the next multiple of Align (Align a power of two, or 0/1 = no-op).
static UINT64
ZooAlignUp (UINT64 Value, UINT32 Align)
{
    if (Align <= 1) { return Value; }
    return (Value + (Align - 1)) & ~((UINT64) Align - 1);
}

// Best-effort: deallocate [Offset, Offset+Len) of an open file as a filesystem hole, so a
// compressed raw slot's unused tail and the inter-member alignment padding cost no physical
// storage. The bytes still read back as zero; only the backing blocks are released, and
// only where the FS supports it (APFS, ext4, XFS, btrfs, NTFS) -- elsewhere this is a
// harmless no-op and the slot keeps its zero bytes. A plain seek-past-write is NOT enough:
// APFS in particular zero-fills such gaps rather than leaving holes, so the punch is
// explicit. The file's logical size is unchanged (the slot stays uncompressed-sized).
static VOID
ZooPunchHole (std::FILE *pf, UINT64 Offset, UINT64 Len)
{
    if (Len == 0) { return; }
    // Hole deallocation operates on whole filesystem blocks, so shrink the request to the
    // block-aligned interior of [Offset, Offset+Len): round the start up and the end down
    // to a 4K boundary (the common block size). Up to one block at each edge keeps its zero
    // bytes; everything in between is reclaimed. Nothing to do if no whole block fits.
    UINT64 CONST Grain = UINT64_C (4096);
    UINT64 End   = (Offset + Len) & ~(Grain - 1);
    UINT64 Start = (Offset + (Grain - 1)) & ~(Grain - 1);
    if (Start >= End) { return; }
    Offset = Start;
    Len    = End - Start;
    std::fflush (pf);
#if defined (_WIN32)
    HANDLE hf = (HANDLE) _get_osfhandle (_fileno (pf));
    if (hf == INVALID_HANDLE_VALUE) { return; }
    FILE_ZERO_DATA_INFORMATION Z;
    Z.FileOffset.QuadPart      = (LONGLONG) Offset;
    Z.BeyondFinalZero.QuadPart = (LONGLONG) (Offset + Len);
    DWORD Got = 0;
    DeviceIoControl (hf, FSCTL_SET_ZERO_DATA, &Z, sizeof (Z), nullptr, 0, &Got, nullptr);
#elif defined (__linux__) && defined (FALLOC_FL_PUNCH_HOLE)
    fallocate (fileno (pf), FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
               (off_t) Offset, (off_t) Len);
#elif defined (__APPLE__) && defined (F_PUNCHHOLE)
    struct fpunchhole Arg;
    std::memset (&Arg, 0, sizeof (Arg));
    Arg.fp_offset = (off_t) Offset;
    Arg.fp_length = (off_t) Len;
    fcntl (fileno (pf), F_PUNCHHOLE, &Arg);
#else
    (VOID) pf; (VOID) Offset;
#endif
}

// Portable byte reversal (no intrinsics, so MSVC/OpenWatcom compile it too; optimizers
// fold these into a single bswap instruction).
static UINT32
ZooBswap32 (UINT32 V)
{
    return  ((V & UINT32_C (0x000000FF)) << 24) | ((V & UINT32_C (0x0000FF00)) << 8)
          | ((V & UINT32_C (0x00FF0000)) >> 8)  | ((V & UINT32_C (0xFF000000)) >> 24);
}

static UINT64
ZooBswap64 (UINT64 V)
{
    return  ((UINT64) ZooBswap32 ((UINT32) V) << 32) | ZooBswap32 ((UINT32) (V >> 32));
}

// SwapStructure: convert a fixed record between host order and the on-disk little-endian
// spec. The conversion is its own inverse, so the same call serves both write (host -> LE)
// and read (LE -> host). On a little-endian host it compiles to nothing.
static VOID
SwapStructure (ZOO_HEADER &H)
{
    if constexpr (std::endian::native == std::endian::little) { (VOID) H; return; }
    H.Magic          = ZooBswap32 (H.Magic);
    H.Version        = ZooBswap32 (H.Version);
    H.MemberCount    = ZooBswap32 (H.MemberCount);
    H.RawCount       = ZooBswap32 (H.RawCount);
    H.SolidSize      = ZooBswap64 (H.SolidSize);
    H.CompressedSize = ZooBswap64 (H.CompressedSize);
}

static VOID
SwapStructure (ZOO_DIRENT &E)
{
    if constexpr (std::endian::native == std::endian::little) { (VOID) E; return; }
    E.Offset  = ZooBswap64 (E.Offset);
    E.Length  = ZooBswap64 (E.Length);
    E.NameLen = ZooBswap32 (E.NameLen);
}

static VOID
SwapStructure (ZOO_RAWENT &E)
{
    if constexpr (std::endian::native == std::endian::little) { (VOID) E; return; }
    E.FileOffset = ZooBswap64 (E.FileOffset);
    E.Length     = ZooBswap64 (E.Length);
    E.CompLength = ZooBswap64 (E.CompLength);
    E.Alignment  = ZooBswap32 (E.Alignment);
    E.Flags      = ZooBswap32 (E.Flags);
    E.NameLen    = ZooBswap32 (E.NameLen);
}

void
ZooWriter::Add (std::string Name, UINT8 CONST *pData, UINT64 Len)
{
    MEMBER M;
    M.Name = std::move (Name);
    M.Data.assign (pData, pData + Len);
    m_Members.push_back (std::move (M));
}

void
ZooWriter::AddRaw (std::string Name, UINT8 CONST *pData, UINT64 Len, UINT32 Alignment, BOOLEAN Compress)
{
    RAWMEMBER M;
    M.Name      = std::move (Name);
    M.Alignment = Alignment;
    M.Compress  = Compress;
    M.Data.assign (pData, pData + Len);
    m_Raw.push_back (std::move (M));
}

// On-disk size of a directory entry's fixed part (the POD, written/read as a unit; any
// natural-alignment padding is part of the record). The name bytes follow, separately.
static UINT64 CONST ZOO_RAWDIR_FIXED = sizeof (ZOO_RAWENT);
static UINT64 CONST ZOO_DIR_FIXED    = sizeof (ZOO_DIRENT);

bool
ZooWriter::Save (CHAR8 CONST *pPath, INT32 Level, std::string *pError)
{
    // 1. Concatenate every solid member's bytes into one block, recording offsets.
    std::vector<UINT8> Solid;
    std::vector<UINT64> Offsets, Lengths;
    for (MEMBER CONST &M : m_Members) {
        Offsets.push_back (Solid.size ());
        Lengths.push_back (M.Data.size ());
        Solid.insert (Solid.end (), M.Data.begin (), M.Data.end ());
    }

    // 2. Compress the whole solid block as ONE zstd stream (solid compression).
    size_t Bound = ZSTD_compressBound (Solid.size ());
    std::vector<UINT8> Comp (Bound ? Bound : 1);
    size_t CompLen = ZSTD_compress (Comp.data (), Comp.size (),
                                    Solid.data (), Solid.size (), Level);
    if (ZSTD_isError (CompLen)) {
        if (pError) { *pError = std::string ("zstd compress: ") + ZSTD_getErrorName (CompLen); }
        return false;
    }

    // 3. Prepare each raw member's stored payload. A compressed member keeps its slot
    //    sized for the uncompressed bytes (so offsets/alignment are unchanged) but stores
    //    only the zstd stream at the slot head; the unused tail is left as a filesystem
    //    hole below. Compression that fails to shrink falls back to verbatim storage.
    std::vector<std::vector<UINT8>> RawPayload (m_Raw.size ());
    std::vector<UINT64>             RawComp (m_Raw.size (), 0);
    for (UINT32 I = 0; I < m_Raw.size (); I++) {
        if (m_Raw[I].Compress && !m_Raw[I].Data.empty ()) {
            size_t RBound = ZSTD_compressBound (m_Raw[I].Data.size ());
            std::vector<UINT8> Z (RBound);
            size_t ZLen = ZSTD_compress (Z.data (), Z.size (),
                                         m_Raw[I].Data.data (), m_Raw[I].Data.size (), Level);
            if (!ZSTD_isError (ZLen) && ZLen < m_Raw[I].Data.size ()) {
                Z.resize (ZLen);
                RawPayload[I] = std::move (Z);
                RawComp[I]    = ZLen;
            } else {
                RawPayload[I] = m_Raw[I].Data;     // no win: store verbatim
            }
        } else {
            RawPayload[I] = m_Raw[I].Data;
        }
    }

    // 4. Lay out the file. The raw slots follow the solid block; each starts at the next
    //    offset satisfying its alignment, and reserves the FULL uncompressed size in the
    //    address space, so the directory records absolute (aligned) offsets the loader
    //    uses verbatim against the memory map.
    UINT64 DirSize = 0;
    for (MEMBER CONST &M : m_Members) { DirSize += ZOO_DIR_FIXED + M.Name.size (); }
    UINT64 RawDirSize = 0;
    for (RAWMEMBER CONST &M : m_Raw) { RawDirSize += ZOO_RAWDIR_FIXED + M.Name.size (); }

    UINT64 Pos = sizeof (ZOO_HEADER) + DirSize + RawDirSize + CompLen;
    std::vector<UINT64> RawOffsets (m_Raw.size ());
    for (UINT32 I = 0; I < m_Raw.size (); I++) {
        Pos              = ZooAlignUp (Pos, m_Raw[I].Alignment);
        RawOffsets[I]    = Pos;
        Pos             += m_Raw[I].Data.size ();   // reserve the uncompressed slot
    }

    std::FILE *pf = std::fopen (pPath, "wb");
    if (pf == nullptr) {
        if (pError) { *pError = std::string ("cannot create '") + pPath + "'"; }
        return false;
    }
    ZOO_HEADER H;
    std::memset (&H, 0, sizeof (H));   // zero padding -> deterministic bytes on disk
    H.Magic          = ZOO_MAGIC;
    H.Version        = ZOO_VERSION;
    H.MemberCount    = (UINT32) m_Members.size ();
    H.RawCount       = (UINT32) m_Raw.size ();
    H.SolidSize      = Solid.size ();
    H.CompressedSize = CompLen;
    SwapStructure (H);
    std::fwrite (&H, sizeof (H), 1, pf);
    for (UINT32 I = 0; I < m_Members.size (); I++) {
        UINT32 NameLen = (UINT32) m_Members[I].Name.size ();
        ZOO_DIRENT E;
        std::memset (&E, 0, sizeof (E));
        E.Offset  = Offsets[I];
        E.Length  = Lengths[I];
        E.NameLen = NameLen;
        SwapStructure (E);
        std::fwrite (&E, sizeof (E), 1, pf);
        std::fwrite (m_Members[I].Name.data (), 1, NameLen, pf);
    }
    for (UINT32 I = 0; I < m_Raw.size (); I++) {
        UINT32 NameLen = (UINT32) m_Raw[I].Name.size ();
        ZOO_RAWENT E;
        std::memset (&E, 0, sizeof (E));
        E.FileOffset = RawOffsets[I];
        E.Length     = m_Raw[I].Data.size ();   // uncompressed (slot) size
        E.CompLength = RawComp[I];               // 0 = verbatim
        E.Alignment  = m_Raw[I].Alignment;
        E.Flags      = 0;
        E.NameLen    = NameLen;
        SwapStructure (E);
        std::fwrite (&E, sizeof (E), 1, pf);
        std::fwrite (m_Raw[I].Name.data (), 1, NameLen, pf);
    }
    std::fwrite (Comp.data (), 1, CompLen, pf);

    // 5. Write each raw payload at its aligned offset (the head of its reserved slot).
    UINT64 BodyEnd = sizeof (ZOO_HEADER) + DirSize + RawDirSize + CompLen;   // end of the solid block
    for (UINT32 I = 0; I < m_Raw.size (); I++) {
        if (std::fseek (pf, (long) RawOffsets[I], SEEK_SET) != 0) {
            if (pError) { *pError = "seek failed writing raw member"; }
            std::fclose (pf);
            return false;
        }
        std::fwrite (RawPayload[I].data (), 1, RawPayload[I].size (), pf);
    }
    // With no raw members the file already ends exactly at the solid block (Pos == BodyEnd);
    // the steps below apply only when raw slots exist. (Extending to Pos-1 unconditionally
    // would overwrite the last byte of the solid zstd stream.)
    if (!m_Raw.empty ()) {
        std::fseek (pf, (long) (Pos - 1), SEEK_SET);
        UINT8 CONST Last = 0;
        std::fwrite (&Last, 1, 1, pf);   // reserve the last slot's full uncompressed extent

        // 6. Punch every unused region into a hole (best-effort, FS-gated): the gap before
        //    the first slot, then for each slot the span from its written payload end up to
        //    the next slot's start (a compressed tail plus the following alignment padding).
        //    The logical size is unchanged -- slots stay as big as the uncompressed payload.
        ZooPunchHole (pf, BodyEnd, RawOffsets[0] - BodyEnd);
        for (UINT32 I = 0; I < m_Raw.size (); I++) {
            UINT64 PayloadEnd = RawOffsets[I] + RawPayload[I].size ();
            UINT64 SlotEnd    = (I + 1 < m_Raw.size ()) ? RawOffsets[I + 1] : Pos;
            ZooPunchHole (pf, PayloadEnd, SlotEnd - PayloadEnd);
        }
    }
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
    bool Ok = std::fread (&H, sizeof (H), 1, pf) == 1;
    if (Ok) { SwapStructure (H); }
    Ok = Ok && H.Magic == ZOO_MAGIC && H.Version >= 1 && H.Version <= ZOO_VERSION;
    if (!Ok) {
        if (pError) { *pError = "not a ZOO/zstd archive"; }
        std::fclose (pf);
        return false;
    }
    if (H.Version < 2) { H.RawCount = 0; }   // v1 had no raw members (field was Reserved)
    m_Entries.clear ();
    m_Raw.clear ();
    for (UINT32 I = 0; Ok && I < H.MemberCount; I++) {
        ZOO_DIRENT D;
        Ok = std::fread (&D, sizeof (D), 1, pf) == 1;
        if (Ok) {
            SwapStructure (D);
            ENTRY E;
            E.Offset = D.Offset;
            E.Length = D.Length;
            E.Name.resize (D.NameLen);
            Ok = D.NameLen == 0 || std::fread (&E.Name[0], 1, D.NameLen, pf) == D.NameLen;
            m_Entries.push_back (std::move (E));
        }
    }
    // Raw directory (v2+): each raw member's absolute aligned file offset, uncompressed
    // slot size, and compressed length (0 = stored verbatim).
    for (UINT32 I = 0; Ok && I < H.RawCount; I++) {
        ZOO_RAWENT D;
        Ok = std::fread (&D, sizeof (D), 1, pf) == 1;
        if (Ok) {
            SwapStructure (D);
            RAWENTRY E;
            E.FileOffset = D.FileOffset;
            E.Length     = D.Length;
            E.CompLength  = D.CompLength;
            E.Alignment  = D.Alignment;
            E.Name.resize (D.NameLen);
            Ok = D.NameLen == 0 || std::fread (&E.Name[0], 1, D.NameLen, pf) == D.NameLen;
            m_Raw.push_back (std::move (E));
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

    // Decompress the solid block once; solid members are slices of it. Raw members are
    // left on disk -- mapped lazily by Map/GetRaw -- so the archive can hold large code
    // blobs without paging them all into memory here.
    m_Solid.resize (H.SolidSize);
    size_t Got = ZSTD_decompress (m_Solid.data (), m_Solid.size (), Comp.data (), Comp.size ());
    if (ZSTD_isError (Got) || Got != H.SolidSize) {
        if (pError) { *pError = std::string ("zstd decompress: ") + (ZSTD_isError (Got) ? ZSTD_getErrorName (Got) : "size mismatch"); }
        return false;
    }
    m_CompressedSize = H.CompressedSize;
    m_Path           = pPath;
    m_RawInflated.assign (m_Raw.size (), std::vector<UINT8> ());
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

std::vector<std::string>
ZooArchive::ListRaw () CONST
{
    std::vector<std::string> Out;
    for (RAWENTRY CONST &E : m_Raw) {
        Out.push_back (E.Name);
    }
    return Out;
}

bool
ZooArchive::Map (std::string *pError)
{
    if (m_pMapBase != nullptr) { return true; }   // already mapped
    if (m_Path.empty ()) {
        if (pError) { *pError = "archive not loaded"; }
        return false;
    }

#if defined (_WIN32)
    HANDLE hf = CreateFileA (m_Path.c_str (), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) {
        if (pError) { *pError = std::string ("cannot open '") + m_Path + "'"; }
        return false;
    }
    LARGE_INTEGER Sz;
    HANDLE hm = nullptr;
    UINT8 CONST *pBase = nullptr;
    if (GetFileSizeEx (hf, &Sz) && Sz.QuadPart > 0) {
        hm = CreateFileMappingA (hf, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (hm != nullptr) {
            pBase = (UINT8 CONST *) MapViewOfFile (hm, FILE_MAP_READ, 0, 0, 0);
            CloseHandle (hm);
        }
    }
    CloseHandle (hf);
    if (pBase == nullptr) {
        if (pError) { *pError = "MapViewOfFile failed"; }
        return false;
    }
    m_pMapBase  = pBase;
    m_MapLen    = (UINT64) Sz.QuadPart;
    m_MapIsHeap = FALSE;
    return true;
#elif defined (ZOO_HAVE_POSIX_MMAP)
    int Fd = ::open (m_Path.c_str (), O_RDONLY);
    if (Fd < 0) {
        if (pError) { *pError = std::string ("cannot open '") + m_Path + "'"; }
        return false;
    }
    struct stat St;
    if (fstat (Fd, &St) != 0 || St.st_size <= 0) {
        ::close (Fd);
        if (pError) { *pError = "cannot stat archive"; }
        return false;
    }
    VOID *pBase = mmap (nullptr, (size_t) St.st_size, PROT_READ, MAP_PRIVATE, Fd, 0);
    ::close (Fd);
    if (pBase == MAP_FAILED) {
        if (pError) { *pError = "mmap failed"; }
        return false;
    }
    m_pMapBase  = (UINT8 CONST *) pBase;
    m_MapLen    = (UINT64) St.st_size;
    m_MapIsHeap = FALSE;
    return true;
#else
    // No OS mapping API (e.g. OpenWatcom/DOS): read the whole file into a page-aligned
    // heap buffer. Same GetRaw contract -- an aligned base + the file's aligned offsets
    // keep each raw member aligned -- just without sharing or demand paging.
    std::FILE *pf = std::fopen (m_Path.c_str (), "rb");
    if (pf == nullptr) {
        if (pError) { *pError = std::string ("cannot open '") + m_Path + "'"; }
        return false;
    }
    std::fseek (pf, 0, SEEK_END);
    long Sz = std::ftell (pf);
    std::fseek (pf, 0, SEEK_SET);
    if (Sz <= 0) {
        std::fclose (pf);
        if (pError) { *pError = "empty archive"; }
        return false;
    }
    UINT32 CONST Align = ZOO_ALIGN_16K;
    VOID *pRaw = std::malloc ((size_t) Sz + Align);
    if (pRaw == nullptr) {
        std::fclose (pf);
        if (pError) { *pError = "out of memory"; }
        return false;
    }
    UINTN Aligned = ((UINTN) pRaw + (Align - 1)) & ~((UINTN) Align - 1);
    bool Ok = std::fread ((VOID *) Aligned, 1, (size_t) Sz, pf) == (size_t) Sz;
    std::fclose (pf);
    if (!Ok) {
        std::free (pRaw);
        if (pError) { *pError = "short read"; }
        return false;
    }
    m_pHeapOwner = pRaw;
    m_pMapBase   = (UINT8 CONST *) Aligned;
    m_MapLen     = (UINT64) Sz;
    m_MapIsHeap  = TRUE;
    return true;
#endif
}

VOID
ZooArchive::Unmap ()
{
    if (m_pMapBase == nullptr) { return; }
    if (m_MapIsHeap) {
#if !defined (_WIN32) && !defined (ZOO_HAVE_POSIX_MMAP)
        std::free (m_pHeapOwner);
        m_pHeapOwner = nullptr;
#endif
    } else {
#if defined (_WIN32)
        UnmapViewOfFile ((LPCVOID) m_pMapBase);
#elif defined (ZOO_HAVE_POSIX_MMAP)
        munmap ((VOID *) m_pMapBase, (size_t) m_MapLen);
#endif
    }
    m_pMapBase = nullptr;
    m_MapLen   = 0;
}

bool
ZooArchive::GetRaw (std::string CONST &Name, UINT8 CONST **ppData, UINT64 *pLen) CONST
{
    for (UINT32 I = 0; I < m_Raw.size (); I++) {
        RAWENTRY CONST &E = m_Raw[I];
        if (E.Name != Name) { continue; }
        if (m_pMapBase == nullptr) { return false; }   // call Map first

        if (E.CompLength == 0) {
            // Stored verbatim: hand back a zero-copy pointer into the mapping. base is
            // page-aligned and FileOffset is a multiple of the member's alignment, so the
            // address is aligned for any host whose page size divides that alignment.
            if (E.FileOffset + E.Length > m_MapLen) { return false; }
            if (ppData) { *ppData = m_pMapBase + E.FileOffset; }
            if (pLen)   { *pLen   = E.Length; }
            return true;
        }

        // Compressed: inflate the zstd head once into a cached buffer and serve that.
        if (E.FileOffset + E.CompLength > m_MapLen) { return false; }
        std::vector<UINT8> &Buf = m_RawInflated[I];
        if (Buf.empty () && E.Length != 0) {
            Buf.resize (E.Length);
            size_t Got = ZSTD_decompress (Buf.data (), Buf.size (),
                                          m_pMapBase + E.FileOffset, (size_t) E.CompLength);
            if (ZSTD_isError (Got) || Got != E.Length) {
                Buf.clear ();
                return false;
            }
        }
        if (ppData) { *ppData = Buf.data (); }
        if (pLen)   { *pLen   = E.Length; }
        return true;
    }
    return false;
}

} // namespace LibCPU
