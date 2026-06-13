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
// Build a ZOO/zstd solid archive: add named members, then Save (concatenate + one
// zstd compress + write).
//
class ZooWriter {
public:
    // Add a member. Name must be unique (last wins is not enforced; caller's contract).
    void Add (std::string Name, UINT8 CONST *pData, UINT64 Len);
    void Add (std::string Name, std::string CONST &Data) { Add (std::move (Name), (UINT8 CONST *) Data.data (), Data.size ()); }

    // Compress the solid block and write the archive. Level is the zstd level (1..22);
    // 19 is a good solid default. Returns false (and fills *pError) on failure.
    bool Save (CHAR8 CONST *pPath, INT32 Level, std::string *pError);

    UINT32 MemberCount () CONST { return (UINT32) m_Members.size (); }

private:
    typedef struct _MEMBER {
        std::string        Name;
        std::vector<UINT8> Data;
    } MEMBER;
    std::vector<MEMBER> m_Members;
};

//
// Read a ZOO/zstd solid archive: load, list members, extract by name. The solid block
// is decompressed once on Load and held in memory.
//
class ZooArchive {
public:
    bool Load (CHAR8 CONST *pPath, std::string *pError);

    std::vector<std::string> List () CONST;
    bool Has (std::string CONST &Name) CONST;
    bool Extract (std::string CONST &Name, std::vector<UINT8> *pOut) CONST;

    UINT32 MemberCount () CONST { return (UINT32) m_Entries.size (); }
    UINT64 UncompressedSize () CONST { return m_Solid.size (); }
    UINT64 CompressedSize () CONST { return m_CompressedSize; }

private:
    typedef struct _ENTRY {
        std::string Name;
        UINT64      Offset;       // into m_Solid
        UINT64      Length;
    } ENTRY;
    std::vector<ENTRY> m_Entries;
    std::vector<UINT8> m_Solid;        // the decompressed solid block
    UINT64             m_CompressedSize = 0;
};

} // namespace LibCPU

#endif // LIBCPU_ZOOARCHIVE_H
