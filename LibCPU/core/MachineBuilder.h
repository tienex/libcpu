/** @file
  MachineBuilder -- assembles a machine from hardware-component bundles by matching a device
  tree against the bundles' property lists.

  A ".device" bundle's Info.plist carries an "LCDeviceMatch" array of the device-tree
  "compatible" strings it binds to. The builder reads those plists (without loading any code),
  then walks a device tree: each node whose "compatible" matches a bundle causes that bundle to
  be loaded, its COM component (IDevice) created and Configure()'d from the node. The result is
  the set of matched components, ready to be wired into a running machine.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_CORE_MACHINEBUILDER_H
#define LIBCPU_CORE_MACHINEBUILDER_H

#include "LibCPU/IDevice.h"
#include <string>
#include <vector>
#include <utility>

namespace LibCPU {

class DeviceTree;
class DtNode;

//
// One component the builder instantiated: the device-tree node it came from, the bundle that
// matched, the "compatible" string that did the matching, and the live COM component.
//
typedef struct _MATCHED_DEVICE {
    std::string  NodePath;       // device-tree path, e.g. "/pic@20"
    std::string  NodeName;       // node name, e.g. "pic@20"
    std::string  BundleName;     // matching bundle (e.g. "i8259")
    std::string  MatchedOn;      // the "compatible" string that matched
    IDevice     *pDevice;        // owned by the builder (Released on destruction)
    DtNode      *pNode;          // the device-tree node it came from (owned by the tree)
} MATCHED_DEVICE;

class MachineBuilder {
public:
    ~MachineBuilder ();

    // Register every "*.device" bundle in a directory, reading each one's LCDeviceMatch array
    // from its Info.plist (no code is loaded). Safe to call for several directories.
    void AddBundleDirectory (CHAR8 CONST *pDir);

    // Number of distinct "compatible" strings registered across all known bundles.
    size_t MatchCount () CONST { return m_Match.size (); }

    // Walk Tree; for each node with a "compatible" that matches a registered bundle, load the
    // bundle, create its component, and Configure() it from the node. A node whose compatible
    // matches nothing is recorded in Unmatched(). Returns false (and sets *pError) only on a
    // hard failure (e.g. a matched bundle that fails to load).
    bool Build (DeviceTree &Tree, std::string *pError);

    std::vector<MATCHED_DEVICE> CONST &Devices () CONST { return m_Devices; }
    std::vector<std::string>    CONST &Unmatched () CONST { return m_Unmatched; }

private:
    bool WalkNode (DtNode &Node, std::string CONST &Path, std::string *pError);

    // After matching, resolve the device tree's "signals = <&source LINE>, ..." links: connect each
    // signal-sink component to the signal-source component the phandle names, on the given line.
    void ResolveSignalLinks ();

    std::vector<std::pair<std::string, std::string>> m_Match;     // (compatible, bundle path)
    std::vector<MATCHED_DEVICE>                      m_Devices;
    std::vector<std::string>                         m_Unmatched; // "node: compatible" with no bundle
};

} // namespace LibCPU

#endif // LIBCPU_CORE_MACHINEBUILDER_H
