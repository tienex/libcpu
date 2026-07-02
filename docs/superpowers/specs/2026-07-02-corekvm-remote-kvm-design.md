# CoreKVM — Remote KVM Stack, Slice 1 Design

**Date:** 2026-07-02
**Status:** Approved design (brainstorming output) — precursor to implementation plan
**Scope:** First vertical slice of a multi-protocol remote-KVM stack.

---

## 1. Overview

CoreKVM is a portable remote keyboard/video/mouse (KVM) stack. The protocol
engines are written in **strict C90** so they are reusable both by a .NET/UNO
Platform GUI client and by native clients, with **zero dependency on libcpu**.
Each protocol library is **symmetric**: it implements both the **client** and the
**server** side.

The long-term stack covers **RDP, VNC (any encoding), and SPICE**. This document
specifies only the **first vertical slice**:

- `CoreKVM` — the C90 sans-I/O engine (framebuffer, input, transport model,
  session/handshake state machines, and the multiplexed side-channel framework).
- `libvnc` — a symmetric RFB implementation (client + server) with the full
  encoding set including Tight-JPEG, and VeNCrypt/TLS security.
- A `KVM*` CoreFoundation-style C ABI.
- An **UNO Platform** client that runs on **all UNO targets** (Windows, macOS,
  Linux, iOS, Android, WebAssembly, and any other UNO head) using **per-head
  native rendering surfaces**, with adaptive touch+pointer UI and **multiple
  simultaneous sessions**.
- A **minimal native reference client** that proves the C90 ABI is reusable
  outside .NET.

SPICE, RDP, and production implementations of the redirection backends are
**explicitly deferred** to later specs that reuse this core. This slice must
prove the whole stack end-to-end: a live remote desktop over VNC on desktop,
mobile, and the browser, plus the extension-channel framework wired end-to-end.

### 1.1 Design pillars

1. **Sans-I/O core.** The protocol engines never perform I/O or own threads.
   They are byte-in / byte-out state machines. This is the only model that is
   *identical* on native threads and in the cooperatively-scheduled browser
   (WASM), and it makes record/replay and deterministic testing nearly free.
2. **Symmetric.** Every protocol library is both client and server.
3. **Portable to WASM.** Everything compiles native (P/Invoke) and to
   WebAssembly via Emscripten (JS interop). C90 is the most portable path to
   both.
4. **House style.** NT/UEFI/COM C++ conventions where C++ is used; the public C
   ABI is CoreFoundation-style (`KVM*`, retain/release, Create/Get rule, no
   fixed-size arrays). 4-space indent, `INT64_C`/`UINT64_C`, MSVC + OpenWatcom
   awareness, C90-strict.

---

## 2. Repository layout

```
CoreKVM/
  CMakeLists.txt                # native (static+shared) + Emscripten/WASM targets
  include/CoreKVM/              # public KVM* CF-style headers (C90)
    KVMBase.h                   # KVMRef, KVMRetain/KVMRelease, KVMStatus, KVMTypeID
    KVMFrameBuffer.h            # KVMFrameBufferRef, pixel formats, dirty regions, screens
    KVMInput.h                  # KVMInputEvent (pointer/key/scroll/relative)
    KVMTransport.h              # KVMTransportRef vtable + provided backends
    KVMSession.h                # KVMSessionRef, handshake/state, flow control, record/replay
    KVMChannel.h                # KVMChannelRef, KVMChannelKind, KVMDeviceClassRef, KVMMediumRef
    KVMVnc.h                    # KVMVncClientRef, KVMVncServerRef, encodings, security
  core/                         # libcorekvm: sans-I/O engine (protocol-agnostic)
  vnc/                          # libvnc: RFB client FSM + server FSM, encoders + decoders
  transport/                    # tcp (BSD/Winsock), websocket (RFC 6455), tls (mbedTLS BIO)
  channels/                     # channel framework + per-kind handlers (clipboard/monitor real)
  codecs/                       # vendored: miniz (zlib), stb_image + stb_image_write (JPEG)
  tls/                          # mbedTLS (git submodule)
  native-client/                # headless capture-to-PPM (CI) + optional SDL2 viewer
  tests/                        # loopback + interop + fuzz CTest suites

uno/
  KvmRemote.sln
  KvmRemote/                    # shared UNO project: view-models, adaptive XAML, input mapping,
                                #   session manager (multi-session), connection profiles
  KvmRemote.Native/             # P/Invoke bindings to KVM* (+ Emscripten JS interop for WASM)
  KvmRemote.Surfaces/           # per-head IRemoteSurface implementations
  KvmRemote.Security/           # secure credential storage + TOFU cert pinning per platform
```

macOS packaging follows house convention: `CoreKVM.framework`.

---

## 3. Architecture

### 3.1 `CoreKVM` (libcorekvm) — protocol-agnostic, sans-I/O

Owns everything not specific to a wire protocol:

- **`KVMFrameBufferRef`** — one or more **screens** (multi-monitor), each with an
  arbitrary RFB-style `PIXEL_FORMAT` and a canonical internal BGRA8888 surface.
  Provides pixel-format conversion and a **dirty-region accumulator**
  (`KVMRect` list, coalesced). Get-rule accessors expose pixel storage backed by
  owner memory (no fixed-size arrays, no copies on the hot path).
- **`KVMInputEvent`** — unified input model: pointer (absolute + **relative**
  mode), button bitmask, wheel/scroll, and **key events by both keysym and
  scancode** (extended key events), plus LED state.
- **Byte-pump FSM substrate** — the base class for protocol state machines:
  `feed(bytes)` appends inbound data; `pump()` advances the machine, producing
  outbound bytes and draining an **emitted-event queue** (damage, resize, cursor,
  channel data, status). No sockets, no threads, no globals — **fully
  multi-instance**, so many `KVMSessionRef`s run concurrently in one process.
- **Flow control** — `Fence` / `ContinuousUpdates` cadence management lives here
  because it governs pump timing and back-pressure, independent of encoding.
- **Record/replay tap** — an optional hook on the byte pump that mirrors the raw
  inbound/outbound stream to a sink (file). Doubles as a user feature
  (record/playback, screenshots) and as a CI fixture format (record a real peer,
  replay offline, deterministically).
- **`KVMChannel` framework** — see §4.

### 3.2 `libvnc` — symmetric RFB

Two state machines built on the pump substrate:

- **Client FSM:** ProtocolVersion → Security → ClientInit/ServerInit →
  framebuffer-update request/response loop.
- **Server FSM:** the mirror image, serving a `KVMFrameBufferRef` and delivering
  received input to an input sink.

**Encodings** (each with an encode side for the server and a decode side for the
client):

- Raw, CopyRect, RRE, Hextile
- ZRLE (zlib via miniz)
- Tight, including **Tight-JPEG** (miniz for the zlib sub-streams; stb_image /
  stb_image_write for JPEG decode/encode)
- Pseudo-encodings: DesktopSize, **ExtendedDesktopSize** (multi-screen +
  client-initiated resize), Cursor, **extended clipboard** (Unicode / large),
  Fence, ContinuousUpdates, and the CoreKVM channels pseudo-encoding (§4).

**Security / authentication** (both directions):

- Type 1 — None
- Type 2 — VNC DES challenge/response
- **VeNCrypt / TLS** — negotiated sub-types wrapping the connection in TLS via
  the mbedTLS transport (§3.3). Security-type negotiation is written to accept
  SASL and other types in later specs.

### 3.3 Transport backends — pluggable `KVMTransportRef` vtable

The core defines a non-blocking transport vtable: `send`, `recv`, `poll`,
`close` (all may return "would block"). Provided backends:

- **TCP** — BSD sockets / Winsock.
- **WebSocket** — RFC 6455 framing, both directions, so a browser client can
  reach a CoreKVM server and a CoreKVM client can reach WebSocket servers.
- **TLS** — drives **mbedTLS** through custom BIO send/recv callbacks over an
  *underlying* transport. VeNCrypt is therefore `TLS ∘ TCP`, and `wss://` is
  `TLS ∘ WebSocket` (native); in the browser the WebSocket backend delegates to
  the JS `WebSocket` object and `wss` TLS is handled by the browser.
- **TOFU certificate pinning** — the TLS backend exposes the peer certificate to
  a verification callback; the client persists a pin on first use and rejects
  changes thereafter (surfaced to the user).

Because the core is sans-I/O, a transport is only ever *driven* by the embedder's
loop; the core never calls a transport itself.

### 3.4 Vendored dependencies (native + WASM clean)

- **miniz** — single-file C90 zlib (inflate + deflate) for ZRLE and Tight.
- **stb_image + stb_image_write** — public-domain baseline JPEG decode + encode
  (client decodes Tight-JPEG; server encodes it).
- **mbedTLS** — TLS for VeNCrypt, as a git submodule. Apache-2.0, C, builds to
  WASM.

All are statically linked and compiled in both toolchains.

---

## 4. Extension channels (`KVMChannel` framework)

A protocol-agnostic, multiplexed, ordered, message-framed side-channel layer over
one authenticated session. It lives in `CoreKVM` so SPICE and RDP inherit it
later. Over VNC it is negotiated as a `KVM-Channels` pseudo-encoding and carried
in a vendor-specific RFB message type; if the peer does not advertise it,
channels are simply unavailable (graceful, never an error).

Every channel has **real wire framing and negotiation now**. Handler maturity for
the slice:

| `KVMChannelKind`        | Purpose                                                                                                   | Slice-1 state                                             |
|-------------------------|-----------------------------------------------------------------------------------------------------------|-----------------------------------------------------------|
| `kKVMChannelClipboard`  | Clipboard sync, both directions (Unicode / large).                                                        | **Fully wired** (smallest — proves the framework)         |
| `kKVMChannelMonitor`    | Telemetry sidechannel: session stats (fps, bandwidth, latency, per-encoding counters), machine health, structured log/event feed. Bridges to libcpu `LCLog` later. | **Live session stats wired**; machine-health source stubbed |
| `kKVMChannelDebug`      | Remote debugging — byte-stream carrying GDB Remote Serial Protocol between a client debugger and the remote guest. Bridges to libcpu's debugger later. | Framing real; **stub endpoint** (attach point / loopback)  |
| `kKVMChannelConfig`     | Remote machine configuration/management RPC (device-tree edits, power/reset, mount media).                | Framing + negotiation real; **stubbed handlers**          |
| `kKVMChannelFolder`     | File-level shared-folder redirection (listing + transfer).                                                | Framing real; one direction wired, rest stubbed           |
| `kKVMChannelStorage`    | Block-level removable-media emulation: attach client-side floppy/HDD image/CD-ROM/ISO/USB mass storage as a real drive on the remote (block read/write + insert/eject + media descriptor). Bridges to libcpu `IBlockMedium`/`IStorageController` later. | Framing + block protocol real; **stub medium backend** (one image wired) |
| `kKVMChannelAudio`      | Audio redirection (remote→client playback; optional client mic→remote).                                   | Framing + negotiation real; **stub codec/sink**           |
| `kKVMChannelCamera`     | Camera/webcam redirection (client cam→remote).                                                            | Framing + negotiation real; **stub source**               |
| `kKVMChannelPrint`      | Printer redirection — remote print jobs spooled to a local printer.                                       | Framing + negotiation real; **stub spooler**              |
| `kKVMChannelDevice`     | **Generic** remotely-controlled device redirection — a device-*class* framework. Each device carries a `KVMDeviceClassRef` descriptor (class + id + capabilities) so USB (usbip-style control/bulk/interrupt), serial/COM, smartcard, HID, block, etc. plug in as registered classes **without changing the wire format**. | Framework + framing + negotiation real; **stub backends** (one loopback class wired) |

Three deliberately distinct redirection models — matching how RDP/SPICE separate
them: **STORAGE** = block-level media attachment; **FOLDER** = file-level folder
sharing; **DEVICE** = generic device-class passthrough.

**C ABI:** `KVMSessionOpenChannel(session, kind)` opens the fixed kinds;
`kKVMChannelDevice` additionally takes a `KVMDeviceClassRef`, `kKVMChannelStorage`
a `KVMMediumRef` (path/handle + medium type + read-only + geometry). Channels use
the same `feed`/`pump` sans-I/O discipline. `kKVMChannelMonitor` exposes a pull
API (`KVMMonitorCopyStats`) plus a pushed event stream.

---

## 5. C ABI (`KVM*`, CoreFoundation-style)

- Base handle `KVMRef` with `KVMRetain` / `KVMRelease` / `KVMGetTypeID`.
- **Create/Get rule:** `*Create*` returns a +1 reference the caller owns; `*Get*`
  returns a borrowed pointer backed by owner storage (no fixed-size arrays).
- Opaque handles: `KVMSessionRef`, `KVMFrameBufferRef`, `KVMTransportRef`,
  `KVMChannelRef`, `KVMDeviceClassRef`, `KVMMediumRef`, `KVMVncClientRef`,
  `KVMVncServerRef`.
- **Status:** `KVMStatus`, an `HRESULT`-like code returned throughout;
  CF-style enum constants (`kKVMSuccess`, `kKVMErr…`).
- **Callbacks:** registered as C function pointers plus a `void *userData` — no
  C++ types cross the ABI, so it is P/Invoke- and Emscripten-clean.
- Enums follow the CF-style `k`-prefixed convention (`KVMChannelKind`,
  `KVMPixelFormat`, `KVMSecurityType`, …).

---

## 6. Data flow (sans-I/O)

Inbound:

```
transport.recv() ──▶ KVMSessionFeed(bytes)
                     KVMSessionPump()  ──▶ emitted events drained by embedder:
                                            { framebuffer damage, screen resize,
                                              cursor, channel data, status change }
```

Outbound (client input / server updates / channel writes):

```
embedder ──▶ KVMSession* API (input, channel write, update request)
             KVMSessionPump() ──▶ outbound bytes ──▶ transport.send()
```

The **embedder owns the loop**. Native: a worker thread `poll()`s the transport
and pumps. WASM: the JS event loop / `requestAnimationFrame` pumps. The core code
is identical in both.

---

## 7. UNO Platform client

### 7.1 Rendering — per-head native surfaces

`IRemoteSurface` abstraction; each UNO head uploads dirty tiles from the core's
BGRA framebuffer into a GPU texture and draws a textured quad:

- WinAppSDK → `SwapChainPanel` + D3D/Direct2D
- macOS / iOS → Metal `CAMetalLayer`
- Android → `SurfaceView` + GLES
- Linux / DirectFB → Skia/GL
- WASM → WebGL

A `WriteableBitmap`-backed `IRemoteSurface` is the **portable fallback**, so every
head is runnable before its native surface lands (incremental delivery).

### 7.2 Input & adaptive UI

- UNO unified pointer events → RFB PointerEvent (button bitmask); hardware key
  events → RFB KeyEvent (keysym + scancode); relative-pointer mode for
  full-screen guests.
- **Adaptive** (one XAML, `AdaptiveTrigger`/visual states): touch heads get
  on-screen affordances — virtual-keyboard toggle, sticky modifier keys,
  long-press→right-click, pinch-zoom/pan of the remote surface; pointer heads get
  direct passthrough. View-only and scale-to-fit modes on all heads.

### 7.3 Sessions, channels, security

- **Multi-session manager** — tabs/windows multiplexing N independent
  `KVMSessionRef`s, each with its own surface, channels, and monitor HUD.
- **Connection profiles / address book**, persisted per platform.
- **Secure credential storage + TOFU cert pinning** (`KvmRemote.Security`):
  Keychain (Apple), Credential Manager (Windows), KeyStore (Android), encrypted
  IndexedDB (WASM).
- **Channel drawer** — per-session panel: Devices section (enumerated local
  devices with per-device redirect toggles), Print toggle, Storage "Attach
  media…" (file picker for `.img`/`.iso`/`.vfd`, medium-type + read-only +
  insert/eject), Folder share, Audio/Camera toggles, a Debug console
  (`kKVMChannelDebug`), a Config form (`kKVMChannelConfig`, stubbed RPC). Where a
  platform can't provide a capability the control is disabled **with a stated
  reason** — never a silent failure.
- **Monitor/stats HUD** — live fps/bandwidth/latency + machine-health panel bound
  to `kKVMChannelMonitor`.
- **Record/replay + screenshots** driven by the core's stream tap.
- **Adaptive quality presets** — auto encoding + JPEG-quality selection.

### 7.4 Binding

`KvmRemote.Native` = `[LibraryImport]` P/Invoke on native heads; the same C API
is reached through Emscripten JS interop on WASM. The browser transport uses the
WebSocket backend delegating to the JS `WebSocket`.

---

## 8. Native reference client

`native-client/` links libcorekvm + libvnc directly through the `KVM*` ABI — the
second consumer that proves the C90 libraries are reusable outside .NET.

- **Headless capture-to-PPM** mode for CI (deterministic, no display).
- **Optional SDL2 viewer** for interactive use.
- Exercises `kKVMChannelDebug` (attach a GDB stub) and `kKVMChannelClipboard` to
  prove channels work outside .NET.

---

## 9. Error handling

- `KVMStatus` return codes throughout the C ABI.
- FSMs **never abort**: a protocol violation transitions to a terminal `Failed`
  state carrying a diagnostic; transport errors surface as status and the
  embedder decides whether to reconnect.
- **No silent failure.** All attacker-controllable input paths (this is
  network-facing) are bounds-checked; malformed frames fail the session cleanly
  with a diagnostic, never out-of-bounds.

---

## 10. Testing (CTest, matching the repo)

- **Loopback** — wire the server FSM ↔ client FSM through in-memory buffers (no
  sockets): full TDD of every encoding round-trip, handshake, and the VeNCrypt
  handshake against an mbedTLS loopback.
- **Channel tests** — clipboard round-trip fully; framing/negotiation for every
  channel kind; storage block-protocol round-trip against a temp image.
- **Record/replay corpus** — record real peers, replay offline deterministically.
- **Malformed-frame fuzz** — a corpus fed to every decoder; must fail cleanly.
- **Interop** — libvnc client vs a reference server, and libvnc server probed by
  a reference client, gated behind CTest.

---

## 11. Build system

- **CMake** for the C tree: native static + shared libraries, plus a separate
  **Emscripten** toolchain target producing the WASM artifact.
- Targets **MSVC + clang/gcc**, with **OpenWatcom** awareness per house rules;
  **C90-strict**.
- mbedTLS as a git submodule; miniz and stb single-file codecs vendored in-tree.
- The `.NET`/UNO solution consumes the native artifacts (P/Invoke) and the WASM
  artifact.

---

## 12. Non-goals (later specs, designed-for but not built)

- **libspice** and **librdp** (this slice's core is designed to host them).
- Production implementations of audio / camera / USB / printer / folder
  redirection, and a complete Config-RPC schema (framework + framing shipped;
  backends stubbed).
- Real host-screen-capture / input-injection server backends beyond the
  embedder interface and a reference one.
- SASL and additional security types.
- mDNS/Bonjour server discovery, IME/composition polish, per-monitor DPI,
  H.264 (OpenH264) encoding.

---

## 13. Open defaults (chosen unless overridden)

- Native reference client = **headless capture-to-PPM for CI + optional SDL2
  viewer**.
- mbedTLS = **git submodule** (cleaner updates than an in-tree copy).
- Interim renderer per head = **`WriteableBitmap` fallback** until each native
  surface lands.
