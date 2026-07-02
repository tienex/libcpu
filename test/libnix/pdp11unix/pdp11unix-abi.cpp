/** @file
  Classic PDP-11 UNIX guest-OS ABI as a COM .abi bundle. Packaged like the BSD ABIs for parity, but
  its hand-written syscall dispatch is NOT version-gated -- the requested version is forwarded and
  ignored (the V6/V7/2BSD/Venix lineage as gated versions is future work).
**/
#include "LibCPU/IAbi.h"
#include "LibCPU/PCom.h"

#include "nix-personality.h" /* C header, self-guarded with extern "C" */
#include "nix-version.h"

namespace LibCPU {
namespace {

class Pdp11UnixAbi final : public ComObject<IAbi>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_IAbi, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetFamily (VOID) override { return "pdp11unix"; }
    CHAR8 CONST *STDMETHODCALLTYPE GetVersionMin (VOID) override { return "7.0"; }
    CHAR8 CONST *STDMETHODCALLTYPE GetVersionMax (VOID) override { return "7.0"; }
    struct _nix_personality *STDMETHODCALLTYPE CreatePersonality (CHAR8 CONST *pVersion) override
    {
        nix_version_t Target = (pVersion != nullptr) ? nix_version_parse (pVersion) : NIX_VERSION_LATEST;
        return nix_personality_create (Target);
    }
};

} // namespace

IAbi *
CreatePdp11UnixAbi (VOID)
{
    return new Pdp11UnixAbi ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_ABI (LibCPU::CreatePdp11UnixAbi)
