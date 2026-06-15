/** @file  DeviceNode -- IDeviceNode COM wrapper over a DtNode (see DeviceNode.h). */

#include "DeviceNode.h"
#include "DeviceTree.h"
#include <string>

namespace LibCPU {

namespace {

class DeviceNodeImpl : public ComObject<IDeviceNode> {
public:
    explicit DeviceNodeImpl (DtNode *pNode) : m_pNode (pNode) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_IDeviceNode, ppvObject);
    }

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override
    {
        return m_pNode->Name.c_str ();
    }

    HRESULT STDMETHODCALLTYPE GetProperty (CONST CHAR8 *pName, UINT8 CONST **ppData, UINT32 *pLen) override
    {
        if (pName == nullptr || ppData == nullptr || pLen == nullptr) { return E_POINTER; }
        std::string Name (pName);
        DT_PROP *pProp = m_pNode->FindProp (Name);
        if (pProp == nullptr) { return E_INVALIDARG; }
        *ppData = pProp->Value.empty () ? (UINT8 CONST *) "" : pProp->Value.data ();
        *pLen   = (UINT32) pProp->Value.size ();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyCell (CONST CHAR8 *pName, UINT32 Index, UINT32 *pValue) override
    {
        if (pName == nullptr || pValue == nullptr) { return E_POINTER; }
        std::string Name (pName);
        DT_PROP *pProp = m_pNode->FindProp (Name);
        if (pProp == nullptr) { return E_INVALIDARG; }
        size_t Off = (size_t) Index * 4;
        if (Off + 4 > pProp->Value.size ()) { return E_INVALIDARG; }
        UINT8 CONST *p = &pProp->Value[Off];
        *pValue = ((UINT32) p[0] << 24) | ((UINT32) p[1] << 16) | ((UINT32) p[2] << 8) | (UINT32) p[3];
        return S_OK;
    }

    UINT32 STDMETHODCALLTYPE GetChildCount (THIS) override
    {
        return (UINT32) m_pNode->Children.size ();
    }

    HRESULT STDMETHODCALLTYPE GetChildAt (UINT32 Index, IDeviceNode **ppChild) override
    {
        if (ppChild == nullptr) { return E_POINTER; }
        if (Index >= m_pNode->Children.size ()) { *ppChild = nullptr; return E_INVALIDARG; }
        *ppChild = MakeDeviceNode (&m_pNode->Children[(size_t) Index]);
        return S_OK;
    }

private:
    DtNode *m_pNode;       // borrowed; owned by the DeviceTree
};

} // anonymous namespace

IDeviceNode *
MakeDeviceNode (DtNode *pNode)
{
    return new DeviceNodeImpl (pNode);
}

} // namespace LibCPU
