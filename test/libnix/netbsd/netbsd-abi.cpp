/** @file
  NetBSD guest-OS ABI as a COM .abi bundle. Bridges the IAbi discovery/creation seam to the C
  nix_personality_create factory; version coverage is netbsd:1.0 .. netbsd:10.1.
**/
#include "LibCPU/IAbi.h"
#include "LibCPU/PCom.h"

#include "nix-personality.h" /* C header, self-guarded with extern "C" */
#include "nix-version.h"

namespace LibCPU {
namespace {

class NetBsdAbi final : public ComObject<IAbi>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_IAbi, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetFamily (VOID) override { return "netbsd"; }
    CHAR8 CONST *STDMETHODCALLTYPE GetVersionMin (VOID) override { return "1.0"; }
    CHAR8 CONST *STDMETHODCALLTYPE GetVersionMax (VOID) override { return "10.1"; }
    struct _nix_personality *STDMETHODCALLTYPE CreatePersonality (CHAR8 CONST *pVersion) override
    {
        nix_version_t Target = (pVersion != nullptr) ? nix_version_parse (pVersion) : NIX_VERSION_LATEST;
        return nix_personality_create (Target);
    }
};

} // namespace

IAbi *
CreateNetBsdAbi (VOID)
{
    return new NetBsdAbi ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_ABI (LibCPU::CreateNetBsdAbi)
