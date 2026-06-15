/** @file  DeviceTree public C API -- a thin CoreFoundation-style wrapper over the C++
  DeviceTree class. An LCDeviceTreeRef is a DeviceTree*; an LCDtNodeRef is a DtNode* borrowed
  from it (valid until the tree is released or structurally mutated). See LibCPU/DeviceTree.h. */

#include "LibCPU/DeviceTree.h"      // the C API
#include "DeviceTree.h"             // the C++ implementation it wraps
#include <cstdlib>
#include <cstring>
#include <new>

using namespace LibCPU;

// The C and C++ format enums are declared separately but must stay in lockstep.
static_assert ((int) LCDtFormatFdtBlob == (int) DtFormatFdtBlob, "DT_FORMAT out of sync");
static_assert ((int) LCDtFormatAppleBinary == (int) DtFormatAppleBinary, "DT_FORMAT out of sync");
static_assert ((int) LCDtFormatOpenFirmware == (int) DtFormatOpenFirmware, "DT_FORMAT out of sync");

extern "C" {

LCDeviceTreeRef
LCDeviceTreeCreate (VOID)
{
    return (LCDeviceTreeRef) new (std::nothrow) DeviceTree ();
}

LCDeviceTreeRef
LCDeviceTreeCreateFromFile (IN CONST CHAR8 *pPath)
{
    DeviceTree *pTree = new (std::nothrow) DeviceTree ();
    if (pTree == nullptr) { return nullptr; }
    std::string Error;
    if (!pTree->Load (pPath, &Error)) { delete pTree; return nullptr; }
    return (LCDeviceTreeRef) pTree;
}

LCDeviceTreeRef
LCDeviceTreeCreateFromBytes (IN CONST VOID *pData, UINTN Len, LC_DT_FORMAT Fmt)
{
    DeviceTree *pTree = new (std::nothrow) DeviceTree ();
    if (pTree == nullptr) { return nullptr; }
    std::string Error;
    if (!pTree->LoadBytes ((UINT8 CONST *) pData, (size_t) Len, (DT_FORMAT) Fmt, &Error)) {
        delete pTree;
        return nullptr;
    }
    return (LCDeviceTreeRef) pTree;
}

VOID
LCDeviceTreeRelease (IN LCDeviceTreeRef Tree)
{
    delete (DeviceTree *) Tree;
}

LC_DT_FORMAT
LCDeviceTreeDetectFormat (IN CONST VOID *pData, UINTN Len)
{
    return (LC_DT_FORMAT) DeviceTree::DetectFormat ((UINT8 CONST *) pData, (size_t) Len);
}

BOOLEAN
LCDeviceTreeSave (IN LCDeviceTreeRef Tree, IN CONST CHAR8 *pPath, LC_DT_FORMAT Fmt)
{
    if (Tree == nullptr) { return FALSE; }
    std::string Error;
    return ((DeviceTree *) Tree)->Save (pPath, (DT_FORMAT) Fmt, &Error) ? TRUE : FALSE;
}

static VOID *
DupBytes (UINT8 CONST *pData, size_t Len)
{
    VOID *pBuf = std::malloc (Len != 0 ? Len : 1);
    if (pBuf != nullptr && Len != 0) { std::memcpy (pBuf, pData, Len); }
    return pBuf;
}

VOID *
LCDeviceTreeCopyBytes (IN LCDeviceTreeRef Tree, LC_DT_FORMAT Fmt, OUT UINTN *pLen)
{
    if (pLen != nullptr) { *pLen = 0; }
    if (Tree == nullptr) { return nullptr; }
    std::vector<UINT8> Bytes;
    std::string Error;
    if (!((DeviceTree *) Tree)->EmitBytes ((DT_FORMAT) Fmt, &Bytes, &Error)) { return nullptr; }
    if (pLen != nullptr) { *pLen = (UINTN) Bytes.size (); }
    return DupBytes (Bytes.data (), Bytes.size ());
}

CHAR8 *
LCDeviceTreeCopyText (IN LCDeviceTreeRef Tree, LC_DT_FORMAT Fmt)
{
    if (Tree == nullptr) { return nullptr; }
    std::string Text;
    std::string Error;
    if (!((DeviceTree *) Tree)->EmitText ((DT_FORMAT) Fmt, &Text, &Error)) { return nullptr; }
    CHAR8 *pOut = (CHAR8 *) std::malloc (Text.size () + 1);
    if (pOut != nullptr) { std::memcpy (pOut, Text.c_str (), Text.size () + 1); }
    return pOut;
}

VOID
LCDeviceTreeFreeBuffer (IN VOID *pBuffer)
{
    std::free (pBuffer);
}

BOOLEAN
LCDeviceTreeApplyOverlay (IN LCDeviceTreeRef Tree, IN LCDeviceTreeRef Fragment)
{
    if (Tree == nullptr || Fragment == nullptr) { return FALSE; }
    std::string Error;
    return ((DeviceTree *) Tree)->ApplyOverlay (*(DeviceTree *) Fragment, &Error) ? TRUE : FALSE;
}

LCDtNodeRef
LCDeviceTreeGetRoot (IN LCDeviceTreeRef Tree)
{
    if (Tree == nullptr) { return nullptr; }
    return (LCDtNodeRef) &((DeviceTree *) Tree)->Root;
}

CONST CHAR8 *
LCDtNodeGetName (IN LCDtNodeRef Node)
{
    if (Node == nullptr) { return nullptr; }
    return ((DtNode *) Node)->Name.c_str ();
}

UINTN
LCDtNodeGetChildCount (IN LCDtNodeRef Node)
{
    if (Node == nullptr) { return 0; }
    return (UINTN) ((DtNode *) Node)->Children.size ();
}

LCDtNodeRef
LCDtNodeGetChildAt (IN LCDtNodeRef Node, UINTN Index)
{
    if (Node == nullptr) { return nullptr; }
    DtNode *pNode = (DtNode *) Node;
    if (Index >= pNode->Children.size ()) { return nullptr; }
    return (LCDtNodeRef) &pNode->Children[(size_t) Index];
}

LCDtNodeRef
LCDtNodeFindChild (IN LCDtNodeRef Node, IN CONST CHAR8 *pName)
{
    if (Node == nullptr || pName == nullptr) { return nullptr; }
    std::string Name (pName);
    return (LCDtNodeRef) ((DtNode *) Node)->FindChild (Name);
}

UINTN
LCDtNodeGetPropCount (IN LCDtNodeRef Node)
{
    if (Node == nullptr) { return 0; }
    return (UINTN) ((DtNode *) Node)->Props.size ();
}

CONST CHAR8 *
LCDtNodeGetPropNameAt (IN LCDtNodeRef Node, UINTN Index)
{
    if (Node == nullptr) { return nullptr; }
    DtNode *pNode = (DtNode *) Node;
    if (Index >= pNode->Props.size ()) { return nullptr; }
    return pNode->Props[(size_t) Index].Name.c_str ();
}

CONST VOID *
LCDtNodeGetPropValue (IN LCDtNodeRef Node, IN CONST CHAR8 *pName, OUT UINTN *pLen)
{
    if (pLen != nullptr) { *pLen = 0; }
    if (Node == nullptr || pName == nullptr) { return nullptr; }
    std::string Name (pName);
    DT_PROP *pProp = ((DtNode *) Node)->FindProp (Name);
    if (pProp == nullptr) { return nullptr; }
    if (pLen != nullptr) { *pLen = (UINTN) pProp->Value.size (); }
    return pProp->Value.empty () ? (CONST VOID *) "" : (CONST VOID *) pProp->Value.data ();
}

LCDtNodeRef
LCDtNodeAddChild (IN LCDtNodeRef Node, IN CONST CHAR8 *pName)
{
    if (Node == nullptr || pName == nullptr) { return nullptr; }
    std::string Name (pName);
    return (LCDtNodeRef) ((DtNode *) Node)->AddChild (Name);
}

VOID
LCDtNodeSetProp (IN LCDtNodeRef Node, IN CONST CHAR8 *pName, IN CONST VOID *pValue, UINTN Len)
{
    if (Node == nullptr || pName == nullptr) { return; }
    std::string Name (pName);
    std::vector<UINT8> Bytes;
    if (pValue != nullptr && Len != 0) {
        Bytes.assign ((UINT8 CONST *) pValue, (UINT8 CONST *) pValue + (size_t) Len);
    }
    ((DtNode *) Node)->SetProp (Name, Bytes);
}

VOID
LCDtNodeSetPropString (IN LCDtNodeRef Node, IN CONST CHAR8 *pName, IN CONST CHAR8 *pValue)
{
    if (Node == nullptr || pName == nullptr || pValue == nullptr) { return; }
    std::string Name (pName);
    std::string Value (pValue);
    ((DtNode *) Node)->SetPropString (Name, Value);
}

VOID
LCDtNodeSetPropU32 (IN LCDtNodeRef Node, IN CONST CHAR8 *pName, UINT32 Value)
{
    if (Node == nullptr || pName == nullptr) { return; }
    std::string Name (pName);
    ((DtNode *) Node)->SetPropU32 (Name, Value);
}

} // extern "C"
