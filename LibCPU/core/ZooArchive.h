/** @file
  A ZOO-style solid archive with zstd compression -- the on-disk container for
  knowledge libraries.

  In the spirit of the classic ZOO archiver (a named directory of members) but with
  SOLID zstd compression: every member's bytes are concatenated into one block and
  that whole block is compressed as a single zstd stream, so cross-member redundancy
  (the very repetitive header/symbol-derived entities a knowledge library holds)
  compresses far better than per-member compression would. The directory records each
  member's offset/length within the uncompressed solid block; extracting decompresses
  the block once and slices it.

  This is not bit-compatible with classic .zoo (which used per-member LZH); it is a
  solid zstd container that keeps the ZOO model.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_ZOOARCHIVE_H
#define LIBCPU_ZOOARCHIVE_H

#include "LibCPU/Base.h"
#include <string>
#include <vector>

namespace LibCPU {

//
// Page alignment for RAW (uncompressed, directly mappable) members. A raw member is
// stored verbatim at a file offset that is a multiple of its alignment, so a memory map
// of the archive exposes it at an aligned address -- the prerequisite for using the
// bytes as host code pages without a copy. 16K is the universal choice: a 16K-aligned
// offset is also 4K-aligned, so one archive maps correctly on both x86 (4K page) and
// Apple-silicon (16K page) hosts. The rule the loader relies on: the requested
// alignment must not exceed the host page size's largest multiple it divides -- in
// practice keep it <= 16K so base + offset stays aligned on every target.
//
#define ZOO_ALIGN_NONE   UINT32_C (0)
#define ZOO_ALIGN_4K     UINT32_C (0x1000)
#define ZOO_ALIGN_16K    UINT32_C (0x4000)
#define ZOO_ALIGN_64K    UINT32_C (0x10000)

//
// Build a ZOO/zstd archive: add SOLID members (concatenated + one zstd stream, for
// redundant text) and/or RAW members (verbatim, page-aligned, for direct mapping such
// as cached host code), then Save.
//
class ZooWriter {
public:
    // Add a solid (compressed) member. Name must be unique (caller's contract).
    void Add (std::string Name, UINT8 CONST *pData, UINT64 Len);
    void Add (std::string Name, std::string CONST &Data) { Add (std::move (Name), (UINT8 CONST *) Data.data (), Data.size ()); }

    // Add a raw member: placed at an Alignment-aligned file offset (ZOO_ALIGN_*; 0 =
    // byte) so a memory map exposes it at an aligned address. When Compress is FALSE the
    // bytes are stored verbatim and can be mapped/executed in place (zero-copy). When
    // TRUE the slot stays the uncompressed size (offsets/alignment unchanged) but holds a
    // zstd stream in its head; Save then best-effort punches the unused tail into a
    // filesystem hole (reclaimed only where the backing FS supports it). A compressed
    // member is inflated on GetRaw, so it is not executable in place.
    void AddRaw (std::string Name, UINT8 CONST *pData, UINT64 Len, UINT32 Alignment, BOOLEAN Compress = FALSE);

    // Compress the solid block and write the archive. Level is the zstd level (1..22);
    // 19 is a good solid default. Returns false (and fills *pError) on failure.
    bool Save (CHAR8 CONST *pPath, INT32 Level, std::string *pError);

    UINT32 MemberCount () CONST { return (UINT32) m_Members.size (); }
    UINT32 RawCount () CONST { return (UINT32) m_Raw.size (); }

private:
    typedef struct _MEMBER {
        std::string        Name;
        std::vector<UINT8> Data;
    } MEMBER;
    typedef struct _RAWMEMBER {
        std::string        Name;
        std::vector<UINT8> Data;
        UINT32             Alignment;
        BOOLEAN            Compress;
    } RAWMEMBER;
    std::vector<MEMBER>    m_Members;
    std::vector<RAWMEMBER> m_Raw;
};

//
// Read a ZOO/zstd archive. Solid members are decompressed once into memory on Load and
// sliced by Extract. Raw members are NOT loaded into memory by Load; call Map to memory-
// map the file, then GetRaw to obtain a zero-copy, alignment-honored pointer into it.
//
class ZooArchive {
public:
    ZooArchive () = default;
    ~ZooArchive () { Unmap (); }

    bool Load (CHAR8 CONST *pPath, std::string *pError);

    std::vector<std::string> List () CONST;
    bool Has (std::string CONST &Name) CONST;
    bool Extract (std::string CONST &Name, std::vector<UINT8> *pOut) CONST;

    // Raw (directly mappable) members. Map memory-maps the archive file (read-only);
    // GetRaw then returns a pointer into the mapping at the member's aligned address.
    // The pointer is valid until Unmap / destruction. Where the OS lacks a mapping API
    // the whole file is read into a page-aligned heap buffer instead (same contract).
    bool   Map (std::string *pError);
    VOID   Unmap ();
    bool   GetRaw (std::string CONST &Name, UINT8 CONST **ppData, UINT64 *pLen) CONST;
    UINT32 RawCount () CONST { return (UINT32) m_Raw.size (); }
    std::vector<std::string> ListRaw () CONST;

    UINT32 MemberCount () CONST { return (UINT32) m_Entries.size (); }
    UINT64 UncompressedSize () CONST { return m_Solid.size (); }
    UINT64 CompressedSize () CONST { return m_CompressedSize; }

private:
    typedef struct _ENTRY {
        std::string Name;
        UINT64      Offset;       // into m_Solid
        UINT64      Length;
    } ENTRY;
    typedef struct _RAWENTRY {
        std::string Name;
        UINT64      FileOffset;   // absolute offset in the archive file (Alignment-aligned)
        UINT64      Length;       // logical (uncompressed) size = the slot size
        UINT64      CompLength;   // 0 = stored verbatim; else zstd bytes in the slot head
        UINT32      Alignment;
    } RAWENTRY;
    std::vector<ENTRY>    m_Entries;
    std::vector<RAWENTRY> m_Raw;
    std::vector<UINT8>    m_Solid;          // the decompressed solid block
    UINT64                m_CompressedSize = 0;

    // Lazily-inflated payloads for compressed raw members (index-parallel to m_Raw);
    // empty until first GetRaw. Mutable so GetRaw can stay logically const.
    mutable std::vector<std::vector<UINT8>> m_RawInflated;

    std::string           m_Path;           // remembered for Map
    UINT8 CONST          *m_pMapBase = nullptr;
    UINT64                m_MapLen   = 0;
    BOOLEAN               m_MapIsHeap = FALSE;   // base came from a heap fallback, not the OS mapper
    VOID                 *m_pHeapOwner = nullptr; // original malloc block backing a heap fallback
};

} // namespace LibCPU

#endif // LIBCPU_ZOOARCHIVE_H
