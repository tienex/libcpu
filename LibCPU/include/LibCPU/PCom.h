/** @file
  Portable COM substrate for LibCPU: real COM types and macros (IUnknown,
  STDMETHOD, DECLARE_INTERFACE_, refcounting) with no dependency on the
  Windows COM runtime. C++20.

  On Windows the genuine SDK headers may be used instead; elsewhere this
  provides an ABI-compatible reimplementation.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_PCOM_H
#define LIBCPU_PCOM_H

#ifndef __cplusplus
#error "PCom.h is the C++ (COM) layer; C consumers use the LC C API instead."
#endif

#include "LibCPU/Base.h"
#include <atomic>

//
// Method calling convention: __stdcall only where it is meaningful (Win32 x86).
//
#if defined(_WIN32) && defined(_M_IX86)
#define STDMETHODCALLTYPE __stdcall
#else
#define STDMETHODCALLTYPE
#endif

typedef IID CONST &REFIID;
typedef CLSID CONST &REFCLSID;

//
// Interface declaration macros, spelled as in objbase.h.
//
#define LC_INTERFACE struct
#define STDMETHOD(Method)        virtual HRESULT STDMETHODCALLTYPE Method
#define STDMETHOD_(Type, Method) virtual Type    STDMETHODCALLTYPE Method
#define PURE                     = 0
#define THIS_
#define THIS                     void
#define DECLARE_INTERFACE(Iface)        LC_INTERFACE Iface
#define DECLARE_INTERFACE_(Iface, Base) LC_INTERFACE Iface : public Base

/**
  IUnknown -- the root of every COM interface.
**/
DECLARE_INTERFACE (IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;
};

typedef IUnknown *PUNKNOWN;

// {00000000-0000-0000-C000-000000000046} -- canonical IID_IUnknown.
inline constexpr IID IID_IUnknown =
    { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

namespace LibCPU {

/**
  LcComObject -- a minimal reference-counted base for interface implementations.

  Derive an implementation from LcComObject<IFoo[, IBar...]> and implement
  QueryInterface; AddRef/Release are provided. Single-threaded refcount is
  promoted to atomic so objects may cross threads safely.
**/
template <class Iface>
class LcComObject : public Iface {
public:
    LcComObject () : m_RefCount (1) {}
    virtual ~LcComObject () = default;

    UINT32 STDMETHODCALLTYPE AddRef (THIS) override {
        return (UINT32)++m_RefCount;
    }

    UINT32 STDMETHODCALLTYPE Release (THIS) override {
        UINT32 Count = (UINT32)--m_RefCount;
        if (Count == 0) {
            delete this;
        }
        return Count;
    }

protected:
    //
    // Helper for QueryInterface: hand back this object for IID_IUnknown or the
    // primary interface id. Returns S_OK on a hit, E_NOINTERFACE otherwise.
    //
    HRESULT DefaultQuery (REFIID riid, REFIID PrimaryId, VOID **ppvObject) {
        if (ppvObject == nullptr) {
            return E_POINTER;
        }
        if (LcIsEqualGUID (&riid, &IID_IUnknown) ||
            LcIsEqualGUID (&riid, &PrimaryId)) {
            *ppvObject = static_cast<Iface *> (this);
            AddRef ();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

private:
    std::atomic<INT32> m_RefCount;
};

} // namespace LibCPU

#endif // LIBCPU_PCOM_H
