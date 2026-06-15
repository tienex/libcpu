/** @file
  LibCPU hardware-component COM interfaces.

  A machine is assembled from hardware components, each a COM object packaged in its own
  loadable ".device" bundle. The bundle's property list (Info.plist) carries an "LCDeviceMatch"
  array naming the device-tree "compatible" strings it binds to, so the matcher can select a
  component from the plist alone -- before loading its code -- the way IOKit matches a driver
  personality to an IODeviceTree node.

  Every component implements IDevice; it advertises further capabilities (port-mapped I/O,
  being an interrupt source, ...) by implementing the corresponding capability interface, which
  the machine discovers through QueryInterface. That QueryInterface-based discovery is COM's
  composition: a component is the sum of the interfaces it answers to.

  The device tree itself is presented to a component as COM (IDeviceNode), so a component reads
  its own "reg"/"interrupts" properties to learn its I/O base and IRQ.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_IDEVICE_H
#define LIBCPU_IDEVICE_H

#include "LibCPU/PCom.h"

namespace LibCPU {

//
// A device-tree node, presented to a component as COM. Property values are the raw bytes the
// tree stores (big-endian cells by convention); GetPropertyCell decodes one 32-bit cell.
//
DECLARE_INTERFACE_ (IDeviceNode, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    // The node's name (e.g. "pic@20"); the root node's name is empty.
    STDMETHOD_ (CHAR8 CONST *, GetName)(THIS) PURE;
    // Raw bytes of a property, or E_INVALIDARG if absent. *ppData points into node storage.
    STDMETHOD (GetProperty)(THIS_ IN CONST CHAR8 *pName, OUT UINT8 CONST **ppData, OUT UINT32 *pLen) PURE;
    // Decode the Index'th big-endian 32-bit cell of a property; E_INVALIDARG if out of range.
    STDMETHOD (GetPropertyCell)(THIS_ IN CONST CHAR8 *pName, UINT32 Index, OUT UINT32 *pValue) PURE;
    // Children, for components that own sub-devices.
    STDMETHOD_ (UINT32, GetChildCount)(THIS) PURE;
    STDMETHOD (GetChildAt)(THIS_ UINT32 Index, OUT IDeviceNode **ppChild) PURE;     // *ppChild is AddRef'd
};

//
// Every hardware component. Configure is called once, after creation, with the component's own
// device-tree node so it can read its addresses/interrupts.
//
DECLARE_INTERFACE_ (IDevice, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD_ (CHAR8 CONST *, GetName)(THIS) PURE;          // human-readable component name
    STDMETHOD (Configure)(THIS_ IN IDeviceNode *pNode) PURE; // wire up from the device-tree node
    STDMETHOD (Reset)(THIS) PURE;                            // power-on / reset state
};

//
// Capability: the component decodes a range of I/O ports. Widths are 1 or 2 (byte / word).
//
DECLARE_INTERFACE_ (IPortDevice, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD_ (BOOLEAN, OwnsPort)(THIS_ UINT16 Port) PURE;
    STDMETHOD (ReadPort)(THIS_ UINT16 Port, UINT32 Width, OUT UINT32 *pValue) PURE;
    STDMETHOD (WritePort)(THIS_ UINT16 Port, UINT32 Width, UINT32 Value) PURE;
};

//
// Capability: the component can raise a hardware interrupt. PollInterrupt returns S_OK and the
// IRQ line number when one is pending, or S_FALSE when none is.
//
DECLARE_INTERFACE_ (IInterruptSource, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD (PollInterrupt)(THIS_ OUT UINT32 *pIrq) PURE;
};

//
// Capability: the component occupies a region of the physical address space (RAM, ROM, a
// memory-mapped framebuffer, ...). GetBase/GetSize describe the region; IsReadOnly marks a ROM.
//
DECLARE_INTERFACE_ (IMemoryDevice, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD_ (UINT32, GetBase)(THIS) PURE;
    STDMETHOD_ (UINT32, GetSize)(THIS) PURE;
    STDMETHOD_ (BOOLEAN, IsReadOnly)(THIS) PURE;
};

//
// Capability: the component arbitrates hardware interrupts (the 8259 PIC). A raised IRQ line is
// presented to AcceptInterrupt; if the line is enabled (not masked) it returns S_OK and the CPU
// interrupt vector to dispatch (the controller's vector base + the line), otherwise S_FALSE.
//
DECLARE_INTERFACE_ (IInterruptController, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD (AcceptInterrupt)(THIS_ UINT32 Irq, OUT UINT32 *pVector) PURE;
};

//
// Capability: the component is a text display backed by a character framebuffer in memory
// (e.g. the MDA at 0xB0000, 80x25, char+attribute pairs). GetFramebufferBase/Size locate the
// framebuffer in the address space; RenderText draws the given framebuffer bytes as a screen.
//
DECLARE_INTERFACE_ (IDisplayDevice, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD_ (UINT32, GetFramebufferBase)(THIS) PURE;
    STDMETHOD_ (UINT32, GetFramebufferSize)(THIS) PURE;
    STDMETHOD (RenderText)(THIS_ IN UINT8 CONST *pFrameBuffer, UINT32 Len) PURE;
};

//
// Interface identifiers. Device family base {1C9A0002-0001-4C50-9A00-0000000000NN}.
//
inline constexpr IID IID_IDeviceNode =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 } };
inline constexpr IID IID_IDevice =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 } };
inline constexpr IID IID_IPortDevice =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03 } };
inline constexpr IID IID_IInterruptSource =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04 } };
inline constexpr IID IID_IMemoryDevice =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05 } };
inline constexpr IID IID_IInterruptController =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06 } };
inline constexpr IID IID_IDisplayDevice =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07 } };

} // namespace LibCPU

//
// Device-bundle entry contract. A ".device" bundle exports one C symbol returning a new, owned
// IDevice (Release when done). Define it in a small per-bundle shim:
//
//     LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreatePic8259)
//
#if defined(_WIN32)
#define LIBCPU_DEVICE_EXPORT __declspec(dllexport)
#else
#define LIBCPU_DEVICE_EXPORT __attribute__((visibility("default")))
#endif

#define LIBCPU_MODULE_CREATE_DEVICE(Creator) \
    extern "C" LIBCPU_DEVICE_EXPORT ::LibCPU::IDevice *LibCPUModuleCreateDevice (void) { return (Creator) (); }

#define LIBCPU_MODULE_DEVICE_ENTRY_NAME "LibCPUModuleCreateDevice"

// The Info.plist key (an array of "compatible" strings) the matcher reads to bind a bundle to
// device-tree nodes, without loading the bundle's code.
#define LIBCPU_DEVICE_MATCH_KEY "LCDeviceMatch"

#endif // LIBCPU_IDEVICE_H
