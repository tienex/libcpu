/** @file
  DeviceNode -- presents a device-tree node (DtNode) to a hardware component as the COM
  interface IDeviceNode, so a component reads its own properties (reg, interrupts, ...) through
  COM. The wrapper borrows the DtNode; the owning DeviceTree must outlive it.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_CORE_DEVICENODE_H
#define LIBCPU_CORE_DEVICENODE_H

#include "LibCPU/IDevice.h"

namespace LibCPU {

class DtNode;

// Wrap a DtNode as an IDeviceNode. Returns an owned reference (Release when done); the wrapper
// borrows pNode, so the tree that owns pNode must outlive the returned interface.
IDeviceNode *MakeDeviceNode (DtNode *pNode);

} // namespace LibCPU

#endif // LIBCPU_CORE_DEVICENODE_H
