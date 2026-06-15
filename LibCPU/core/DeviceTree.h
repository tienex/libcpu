/** @file
  DeviceTree -- a unifying model for describing the machine to be emulated, with readers
  and writers for the device-tree formats the emulated platforms actually use.

  One in-memory model (a tree of named nodes, each carrying named byte-valued properties)
  underlies every format, because they all descend from the same IEEE-1275 Open Firmware
  device tree:

    - Flat Device Tree (FDT) -- the ARM/PowerPC "DTB" blob (magic 0xd00dfeed) and its
      textual "DTS" source. Compile = DTS -> DTB, decompile = DTB -> DTS.
    - Apple Device Tree -- the flattened tree XNU/iBoot consume (fixed 32-byte property
      names, 4-byte-aligned), plus a readable textual form.
    - Open Firmware (OF) -- the IEEE-1275 tree itself; its portable binary form is the
      flattened tree (FDT is literally a flattened OF tree), and an OF-style textual dump.

  Property values are kept as raw bytes; device-tree convention stores integers as
  big-endian 32-bit cells, strings as NUL-terminated, and lists as concatenations. Typed
  accessors interpret on demand. Labels and "&ref" phandle references (the DTS mechanism
  for one node to point at another) are resolved at compile time into phandle cells, the
  way the reference device-tree compiler does it.

  Overlays/fragments compose: a fragment tree is merged onto a base tree by path, so a
  machine can be assembled from reusable pieces.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_CORE_DEVICETREE_H
#define LIBCPU_CORE_DEVICETREE_H

#include "LibCPU/Base.h"
#include <string>
#include <vector>

namespace LibCPU {

//
// The device-tree formats this module reads and writes.
//
typedef enum _DT_FORMAT {
    DtFormatUnknown = 0,
    DtFormatFdtBlob,            // Flat Device Tree binary (.dtb, magic 0xd00dfeed)
    DtFormatFdtSource,          // Flat Device Tree source (.dts text)
    DtFormatAppleBinary,        // Apple flattened device tree (.adt binary)
    DtFormatAppleText,          // Apple device tree, textual
    DtFormatOpenFirmware        // Open Firmware textual tree dump
} DT_FORMAT;

//
// A single property: a name and an opaque byte value (big-endian cells by convention).
//
typedef struct _DT_PROP {
    std::string         Name;
    std::vector<UINT8>  Value;
} DT_PROP;

//
// A node in the tree: a name (the root's is empty), its properties, and its children.
// The model is value-based (children are held by value) so a subtree copies cleanly --
// which is exactly what fragment composition needs.
//
class DtNode {
public:
    std::string           Name;        // e.g. "cpu@0"; the root node's name is empty
    std::vector<DT_PROP>  Props;
    std::vector<DtNode>   Children;

    // Property access. SetProp replaces an existing property of the same name in place.
    DT_PROP       *FindProp (std::string CONST &Name);
    DT_PROP CONST *FindProp (std::string CONST &Name) CONST;
    void           SetProp (std::string CONST &Name, std::vector<UINT8> CONST &Value);
    void           SetPropString (std::string CONST &Name, std::string CONST &Value);
    void           SetPropU32 (std::string CONST &Name, UINT32 Value);
    void           SetPropEmpty (std::string CONST &Name);                       // boolean/present-only

    // Child access by node name (the full "name@unit" string).
    DtNode *FindChild (std::string CONST &Name);
    DtNode *AddChild (std::string CONST &Name);                                  // returns existing or new
};

//
// A whole device tree: the root node plus the metadata the binary formats carry alongside
// it (the FDT memory-reservation block and boot-CPU id).
//
class DeviceTree {
public:
    DtNode                                    Root;
    std::vector<std::pair<UINT64, UINT64>>    MemReserve;     // (address, size) reservations
    UINT32                                    BootCpuId = 0;

    // Detect a format from a buffer's leading bytes / content.
    static DT_FORMAT DetectFormat (UINT8 CONST *pData, size_t Len);

    // Parse. Load auto-detects from the file's content; LoadBytes takes an explicit format
    // (use DtFormatUnknown to auto-detect). On failure both set *pError and return false.
    bool Load (CHAR8 CONST *pPath, std::string *pError);
    bool LoadBytes (UINT8 CONST *pData, size_t Len, DT_FORMAT Fmt, std::string *pError);

    // Emit. Binary formats go through EmitBytes, textual through EmitText; Save picks by
    // format and writes the file.
    bool EmitBytes (DT_FORMAT Fmt, std::vector<UINT8> *pOut, std::string *pError) CONST;
    bool EmitText (DT_FORMAT Fmt, std::string *pOut, std::string *pError) CONST;
    bool Save (CHAR8 CONST *pPath, DT_FORMAT Fmt, std::string *pError) CONST;

    // Compose a fragment/overlay onto this tree: every node of pFragment is merged by path
    // (properties overwrite, children recurse), so a machine is assembled from pieces. The
    // fragment may be a plain tree or a DTS overlay (fragment@N { target-path; __overlay__ }).
    bool ApplyOverlay (DeviceTree CONST &Fragment, std::string *pError);
};

} // namespace LibCPU

#endif // LIBCPU_CORE_DEVICETREE_H
