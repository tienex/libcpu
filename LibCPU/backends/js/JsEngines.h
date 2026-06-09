/** @file
  JS-engine discovery/selection for the "js" backend. The backend finds every
  installed JavaScript engine it can drive and exposes the list (and a selection)
  through ICpuJsEngines, obtained by QueryInterface on the js ICpuBackend.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_JSENGINES_H
#define LIBCPU_JSENGINES_H

#include "LibCPU/ICpu.h"

namespace LibCPU {

//
// Description of one discovered JS engine (POD; fixed buffers for a stable C ABI).
//
// Strings are non-owning (CF Get rule): valid for the backend's lifetime.
typedef struct _JS_ENGINE_INFO {
    CHAR8 CONST *Name;     // engine id: node, bun, deno, qjs, js
    CHAR8 CONST *Path;     // absolute path of the executable
    CHAR8 CONST *Family;   // node-like, deno, quickjs, spidermonkey
} JS_ENGINE_INFO;

/**
  ICpuJsEngines -- query/select the js backend's discovered engines.
  Obtain via ICpuBackend::QueryInterface(IID_ICpuJsEngines, ...).
**/
DECLARE_INTERFACE_ (ICpuJsEngines, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD_ (UINT32, GetEngineCount)(THIS) PURE;
    STDMETHOD (GetEngineInfo)(THIS_ UINT32 Index, OUT JS_ENGINE_INFO *pInfo) PURE;
    STDMETHOD (SelectEngine)(THIS_ UINT32 Index) PURE;
    STDMETHOD_ (UINT32, GetSelectedEngine)(THIS) PURE;
};

// {1C9A0003-0003-4A53-9A00-000000000020}
inline constexpr IID IID_ICpuJsEngines =
    { 0x1C9A0003, 0x0003, 0x4A53, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20 } };

} // namespace LibCPU

#endif // LIBCPU_JSENGINES_H
