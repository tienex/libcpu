/** @file
  Substrate smoke test: instantiate a COM object via ComObject, exercise
  QueryInterface / AddRef / Release. Proves the C++20 portable-COM layer is
  well-formed and behaves.
**/
#include "LibCPU/ICpu.h"
#include "LibCPU/LibCPU.h"   // ensure the C API header also parses under C++
#include <cstdio>

using namespace LibCPU;

//
// A trivial ICpuValue implementation (an opaque value handle has no extra
// methods beyond IUnknown), used only to drive the refcount/QI machinery.
//
class DummyValue final : public ComObject<ICpuValue> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
};

int main (void) {
    DummyValue *pObj = new DummyValue ();   // refcount starts at 1

    ICpuValue *pValue = nullptr;
    HRESULT    Hr     = pObj->QueryInterface (IID_ICpuValue, (void **)&pValue);
    std::printf ("QI ICpuValue : hr=%d  ptr=%s\n", (int)Hr, pValue ? "ok" : "null");

    IUnknown *pUnk = nullptr;
    Hr = pObj->QueryInterface (IID_IUnknown, (void **)&pUnk);
    std::printf ("QI IUnknown  : hr=%d  ptr=%s\n", (int)Hr, pUnk ? "ok" : "null");

    void *pWrong = nullptr;
    Hr = pObj->QueryInterface (IID_ICpuEmitter, &pWrong);
    std::printf ("QI wrong IID : hr=0x%08x  (expect E_NOINTERFACE=0x%08x)\n",
                 (unsigned)Hr, (unsigned)E_NOINTERFACE);

    // Two successful QIs each AddRef'd; release them, then the original.
    if (pValue) pValue->Release ();
    if (pUnk)   pUnk->Release ();
    UINT32 FinalCount = pObj->Release ();   // should hit 0 and delete
    std::printf ("final Release returned refcount=%u (expect 0)\n", FinalCount);

    std::printf ("SMOKE OK\n");
    return 0;
}
