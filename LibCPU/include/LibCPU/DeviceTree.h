/** @file
  Device-tree public C API -- the CoreFoundation-style, "LC"-prefixed flat C surface for
  describing the machine to be emulated. Offered in parallel with the C++ DeviceTree class
  (core/DeviceTree.h), not as a wrapper that hides it.

  Opaque reference types follow the CoreFoundation idiom: an LCDeviceTreeRef is owned by the
  caller (the Create rule -- release it with LCDeviceTreeRelease), while an LCDtNodeRef is
  borrowed (the Get rule -- it lives as long as the tree that owns it and is never released).
  Buffers returned by the Copy functions are owned by the caller and freed with
  LCDeviceTreeFreeBuffer. Compiles as C23 and C++20.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_DEVICETREE_H
#define LIBCPU_DEVICETREE_H

#include "LibCPU/Base.h"

#ifdef __cplusplus
extern "C" {
#endif

//
// Opaque reference types. A tree is owned (Create rule); a node is borrowed from its tree
// (Get rule).
//
typedef struct _LCDeviceTree *LCDeviceTreeRef;
typedef struct _LCDtNode     *LCDtNodeRef;

//
// The device-tree formats (kept in sync with the C++ DT_FORMAT enum).
//
typedef enum _LC_DT_FORMAT {
    LCDtFormatUnknown = 0,
    LCDtFormatFdtBlob,          // Flat Device Tree binary (.dtb)
    LCDtFormatFdtSource,        // Flat Device Tree source (.dts)
    LCDtFormatAppleBinary,      // Apple flattened device tree (.adt)
    LCDtFormatAppleText,        // Apple device tree, textual (reserved)
    LCDtFormatOpenFirmware      // Open Firmware textual dump (.ofd)
} LC_DT_FORMAT;

//
// Lifetime. Create returns an owned reference; release it with LCDeviceTreeRelease. The
// From* creators return NULL on failure.
//
LCDeviceTreeRef LCDeviceTreeCreate (VOID);
LCDeviceTreeRef LCDeviceTreeCreateFromFile (IN CONST CHAR8 *pPath);
LCDeviceTreeRef LCDeviceTreeCreateFromBytes (IN CONST VOID *pData, UINTN Len, LC_DT_FORMAT Fmt);
VOID            LCDeviceTreeRelease (IN LCDeviceTreeRef Tree);

//
// Detect a format from a buffer's leading bytes / content.
//
LC_DT_FORMAT LCDeviceTreeDetectFormat (IN CONST VOID *pData, UINTN Len);

//
// Emit. Save writes a file in the given format. The Copy functions return a caller-owned
// buffer (free with LCDeviceTreeFreeBuffer): CopyBytes for the binary formats (sets *pLen),
// CopyText for the textual formats (NUL-terminated). They return NULL on failure.
//
BOOLEAN LCDeviceTreeSave (IN LCDeviceTreeRef Tree, IN CONST CHAR8 *pPath, LC_DT_FORMAT Fmt);
VOID   *LCDeviceTreeCopyBytes (IN LCDeviceTreeRef Tree, LC_DT_FORMAT Fmt, OUT UINTN *pLen);
CHAR8  *LCDeviceTreeCopyText (IN LCDeviceTreeRef Tree, LC_DT_FORMAT Fmt);
VOID    LCDeviceTreeFreeBuffer (IN VOID *pBuffer);

//
// Compose a fragment/overlay onto a tree (properties overwrite, children merge by path).
//
BOOLEAN LCDeviceTreeApplyOverlay (IN LCDeviceTreeRef Tree, IN LCDeviceTreeRef Fragment);

//
// Navigation. Returned nodes are borrowed (Get rule). GetName returns the node's name (the
// root's is the empty string). GetPropValue returns a pointer into the node's own storage
// and sets *pLen; it returns NULL if the property is absent.
//
LCDtNodeRef   LCDeviceTreeGetRoot (IN LCDeviceTreeRef Tree);
CONST CHAR8  *LCDtNodeGetName (IN LCDtNodeRef Node);
UINTN         LCDtNodeGetChildCount (IN LCDtNodeRef Node);
LCDtNodeRef   LCDtNodeGetChildAt (IN LCDtNodeRef Node, UINTN Index);
LCDtNodeRef   LCDtNodeFindChild (IN LCDtNodeRef Node, IN CONST CHAR8 *pName);
UINTN         LCDtNodeGetPropCount (IN LCDtNodeRef Node);
CONST CHAR8  *LCDtNodeGetPropNameAt (IN LCDtNodeRef Node, UINTN Index);
CONST VOID   *LCDtNodeGetPropValue (IN LCDtNodeRef Node, IN CONST CHAR8 *pName, OUT UINTN *pLen);

//
// Mutation. AddChild returns the existing or newly created child (borrowed).
//
LCDtNodeRef LCDtNodeAddChild (IN LCDtNodeRef Node, IN CONST CHAR8 *pName);
VOID        LCDtNodeSetProp (IN LCDtNodeRef Node, IN CONST CHAR8 *pName, IN CONST VOID *pValue, UINTN Len);
VOID        LCDtNodeSetPropString (IN LCDtNodeRef Node, IN CONST CHAR8 *pName, IN CONST CHAR8 *pValue);
VOID        LCDtNodeSetPropU32 (IN LCDtNodeRef Node, IN CONST CHAR8 *pName, UINT32 Value);

#ifdef __cplusplus
}
#endif

#endif // LIBCPU_DEVICETREE_H
