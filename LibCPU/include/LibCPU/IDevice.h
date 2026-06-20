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
// Capability: the component backs its memory region with its OWN host buffer (e.g. a graphics
// card's video RAM), rather than living in main RAM. The machine maps the component's
// IMemoryDevice region onto GetHostBuffer() and keeps the guest's flat RAM and this buffer in
// sync at execution-window boundaries -- so the memory is genuinely the device's: it can carry a
// separate framebuffer, switch banks, or be absent ("no card, no memory"). GetHostBuffer returns
// storage of at least the IMemoryDevice GetSize() bytes (the currently mapped bank).
//
DECLARE_INTERFACE_ (IHostMemory, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD_ (UINT8 *, GetHostBuffer)(THIS) PURE;
};

//
// Capability: the component arbitrates hardware interrupts (the 8259 PIC). A raised IRQ line is
// presented to AcceptInterrupt, which LATCHES the request (the 8259 IRR) -- so an edge raised while
// the line is masked is remembered, not lost -- and, if the line is enabled (not masked) and not
// blocked by an equal-or-higher-priority line already in service, returns S_OK with the CPU vector
// to dispatch (the controller's vector base + the line); otherwise S_FALSE (the request stays
// latched). PollPending delivers a previously-latched request that has since become deliverable
// (e.g. its line was unmasked) WITHOUT the peripheral having to re-assert -- the path a one-shot
// completion interrupt (disk, FDC) takes when software masks the line across the operation.
//
DECLARE_INTERFACE_ (IInterruptController, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD (AcceptInterrupt)(THIS_ UINT32 Irq, OUT UINT32 *pVector) PURE;
    STDMETHOD_ (BOOLEAN, CanAccept)(THIS_ UINT32 Irq) PURE;   // read-only: would this line be acknowledged now?
    STDMETHOD (PollPending)(THIS_ OUT UINT32 *pVector) PURE;  // grant the highest latched line now deliverable
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
// Board-level signal lines. Some parts are not addressed through I/O ports at all: they are wired
// to the output pins of another chip. The PC speaker and the (5150) cassette interface are driven
// this way -- by the 8255 PPI's port-B latch (timer-2 gate, speaker data, cassette-motor relay) and
// by the 8253 PIT's channel-2 output. A signal SOURCE (the PPI, the PIT) drives one or more named
// lines; a signal SINK (the speaker, the cassette) observes them. This models the physical board
// interconnect without violating port ownership (the PPI still solely owns port 0x61).
//
enum LC_SIGNAL_LINE {
    LCSignalPpiPortB = 1,           // 8255 port-B latch byte: bit0 timer-2 gate, bit1 speaker data,
                                    //   bit3 cassette-motor relay (active low: 0 = motor on)
    LCSignalPitCh2   = 2            // 8253 channel-2 reload divisor (the speaker/cassette tone clock)
};

//
// Capability: the component observes board-level signal lines driven by another component.
// OnSignal is called by the source whenever a line it drives changes; Line is an LC_SIGNAL_LINE
// and Value carries the line's new state (a latch byte, a divisor, ...).
//
DECLARE_INTERFACE_ (ISignalSink, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD (OnSignal)(THIS_ UINT32 Line, UINT32 Value) PURE;
};

//
// Capability: the component drives board-level signal lines that signal sinks observe. The wiring
// is explicit and per-line: the machine builder resolves the "signals = <&source LINE>, ..." links
// in the device tree and calls ConnectSink once for each declared (sink, line) edge. The source
// then pushes a change on a given line only to the sinks wired to that line. ConnectSink takes a
// reference to the sink for the source's lifetime.
//
DECLARE_INTERFACE_ (ISignalSource, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD (ConnectSink)(THIS_ ISignalSink *pSink, UINT32 Line) PURE;
};

//
// Capability: the DMA controller (the 8237). Exposes a channel's current transfer programming so a
// DMA bridge can move bytes between a peripheral and guest memory. GetChannel returns the channel's
// base address, byte count (the 8237 holds count-minus-one), and mode byte (bits 2-3 select the
// transfer direction: 01 = write to memory, 10 = read from memory).
//
DECLARE_INTERFACE_ (IDmaController, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD (GetChannel)(THIS_ UINT32 Channel, OUT UINT32 *pAddress, OUT UINT32 *pCount, OUT UINT32 *pMode) PURE;
    // Mark a channel's transfer finished: the controller latches terminal count (sets the TC status
    // bit and winds the current count to its wrapped value), as the real 8237 does when a peripheral
    // drives EOP. The BIOS reads the status register after a DMA op to confirm TC was reached.
    STDMETHOD (SetTerminalCount)(THIS_ UINT32 Channel) PURE;
};

//
// Capability: a peripheral that transfers through a DMA channel (e.g. the floppy controller). When
// a command needs a transfer, GetDmaRequest returns S_OK with the channel, direction (TRUE = device
// to memory), a pointer to the peripheral's own buffer, and the byte length; the machine's DMA
// bridge moves the bytes to/from guest memory at the controller's programmed address and then calls
// CompleteDma so the peripheral can finish (post its result and raise its interrupt). GetDmaRequest
// returns S_FALSE when no transfer is pending.
//
DECLARE_INTERFACE_ (IDmaPeripheral, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD (GetDmaRequest)(THIS_ OUT UINT32 *pChannel, OUT BOOLEAN *pToMemory, OUT UINT8 **ppBuffer, OUT UINT32 *pLength) PURE;
    STDMETHOD (CompleteDma)(THIS) PURE;
};

//
// Capability: the component is an expansion card that can host an option ROM (a BIOS extension in
// a ROM socket on the card). GetRomAddress returns the card's conventional option-ROM base in the
// upper-memory area (e.g. 0xC0000 for video, 0xC8000 for a hard-disk controller), or 0 to let the
// machine auto-assign a free slot. Motherboard chips (PIC, PIT, DMA, ...) do NOT implement this.
//
DECLARE_INTERFACE_ (IOptionRomHost, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD_ (UINT32, GetRomAddress)(THIS) PURE;
};

//
// Capability: a keyboard controller that accepts injected key events at run time. A console front
// end pushes raw scan codes (XT set-1 make/break) as the user types; the controller queues them
// and delivers each through its interrupt (IRQ1) as the guest reads the previous one. This is the
// run-time counterpart to the device-tree "libcpu,keystroke" seed.
//
DECLARE_INTERFACE_ (IKeyboardSink, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD (PushScanCode)(THIS_ UINT8 ScanCode) PURE;
};

//
// Capability: a display device that reports its current mode so a console can stream the right
// kind of frame. GetMode fills the geometry and pixel format (text cells vs a graphics bitmap);
// GetFramebuffer returns a pointer to the live framebuffer bytes (the card's own VRAM at the
// active page) and their length. A device exposing this drives the console's MODE/FRAME messages.
//
DECLARE_INTERFACE_ (IDisplayMode, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    // Format: 0 = text (Width=cols, Height=rows), 1 = graphics (Width x Height, Bpp bits/pixel).
    STDMETHOD (GetMode)(THIS_ OUT UINT32 *pFormat, OUT UINT32 *pWidth, OUT UINT32 *pHeight, OUT UINT32 *pBpp) PURE;
    STDMETHOD (GetFramebuffer)(THIS_ OUT UINT8 CONST **ppBytes, OUT UINT32 *pLength) PURE;
};

//
// Capability: a storage MEDIUM -- the platters, independent of the bus that reaches them. The same
// drive sits behind an ST-506, ESDI, SASI, IDE/ATA, SCSI, ... controller; each controller is just
// a different protocol in front of this. A medium exposes CHS geometry (for CHS controllers) and a
// total sector count (for LBA controllers), and reads/writes whole sectors by LBA. SectorSize is
// usually 512. This is what a generic disk component implements; controllers translate their wire
// protocol into these calls.
//
DECLARE_INTERFACE_ (IBlockMedium, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD (GetGeometry)(THIS_ OUT UINT32 *pCylinders, OUT UINT32 *pHeads, OUT UINT32 *pSectors, OUT UINT32 *pSectorSize) PURE;
    STDMETHOD_ (UINT64, GetSectorCount)(THIS) PURE;
    STDMETHOD (Read)(THIS_ UINT64 Lba, OUT UINT8 *pBuffer, UINT32 SectorCount) PURE;
    STDMETHOD (Write)(THIS_ UINT64 Lba, IN UINT8 CONST *pBuffer, UINT32 SectorCount) PURE;
};

//
// Capability: a storage CONTROLLER that drives one or more attached media. The machine builder
// resolves the controller node's "disks = <&drive0>, <&drive1>, ..." phandles to IBlockMedium
// components and hands each to the controller as a unit (0, 1, ...). The controller keeps the
// references and serves its bus protocol from them. A controller with no attached media reports
// an empty bus.
//
DECLARE_INTERFACE_ (IStorageController, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD (AttachMedium)(THIS_ UINT32 Unit, IN IBlockMedium *pMedium) PURE;
};

//
// Capability: the component is driven by the machine's time base. The host calls OnClock with a
// monotonically increasing cycle count (CPU_STATE.Cycles) as the guest runs, so a peripheral can
// model real elapsed time -- e.g. the 8253 PIT advances its down-counters by the cycles elapsed
// since each was loaded, instead of a fixed step per read. Cycles never decreases within a run.
//
DECLARE_INTERFACE_ (IClockSink, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD (OnClock)(THIS_ UINT64 Cycles) PURE;
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
inline constexpr IID IID_ISignalSink =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08 } };
inline constexpr IID IID_ISignalSource =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09 } };
inline constexpr IID IID_IHostMemory =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A } };
inline constexpr IID IID_IDmaController =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0B } };
inline constexpr IID IID_IDmaPeripheral =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C } };
inline constexpr IID IID_IOptionRomHost =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D } };
inline constexpr IID IID_IKeyboardSink =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0E } };
inline constexpr IID IID_IDisplayMode =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F } };
inline constexpr IID IID_IBlockMedium =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 } };
inline constexpr IID IID_IStorageController =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11 } };
inline constexpr IID IID_IClockSink =
    { 0x1C9A0002, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12 } };

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
