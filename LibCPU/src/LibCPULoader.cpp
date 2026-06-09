/** @file
  LibCPU framework core: bundle loader. See Loader.h.

  On macOS this loads a real CFBundle (Foo.backend/Contents/MacOS/Foo) via the
  CoreFoundation CFBundle API; elsewhere it falls back to dlopen of a shared
  module. The bundle/handle is retained for the process lifetime so the backend's
  code stays mapped.
**/
#include "LibCPU/Loader.h"
#include "LibCPU/Module.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#else
#include <dlfcn.h>
#endif

namespace LibCPU {

typedef ICpuBackend *(*EntryFn) (void);

ICpuBackend *
LoadBackendBundle (CHAR8 CONST *pPath)
{
#if defined(__APPLE__)
    CFStringRef PathStr = CFStringCreateWithCString (nullptr, pPath, kCFStringEncodingUTF8);
    if (PathStr == nullptr) {
        return nullptr;
    }
    CFURLRef Url = CFURLCreateWithFileSystemPath (nullptr, PathStr, kCFURLPOSIXPathStyle, true);
    CFRelease (PathStr);
    if (Url == nullptr) {
        return nullptr;
    }
    CFBundleRef Bundle = CFBundleCreate (nullptr, Url);
    CFRelease (Url);
    if (Bundle == nullptr) {
        return nullptr;
    }
    CFStringRef FnName = CFStringCreateWithCString (nullptr, LIBCPU_MODULE_ENTRY_NAME, kCFStringEncodingUTF8);
    EntryFn pEntry = (EntryFn) CFBundleGetFunctionPointerForName (Bundle, FnName);
    CFRelease (FnName);
    if (pEntry == nullptr) {
        CFRelease (Bundle);
        return nullptr;
    }
    // Bundle intentionally retained (not released) so the code stays mapped.
    return pEntry ();
#else
    void *pHandle = dlopen (pPath, RTLD_NOW | RTLD_LOCAL);
    if (pHandle == nullptr) {
        return nullptr;
    }
    EntryFn pEntry = (EntryFn) dlsym (pHandle, LIBCPU_MODULE_ENTRY_NAME);
    if (pEntry == nullptr) {
        dlclose (pHandle);
        return nullptr;
    }
    return pEntry ();
#endif
}

} // namespace LibCPU
