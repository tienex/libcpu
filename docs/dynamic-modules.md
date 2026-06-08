# Loadable Modules: Frontends and Backends as Dynamic Libraries (an Any-to-Any AOT/JIT Library)

> How to make libcpu's **frontends** (guest ISAs) and **backends** (codegen
> targets) into independently-shipped, dynamically-loaded modules
> (`.so`/`.dll`/`.dylib`) discovered and instantiated at runtime through the
> portable-COM model — so *any* frontend pairs with *any* backend, for *either*
> AOT or JIT, without recompiling the host or any other module.
>
> This is the packaging/loading companion to
> **[com-architecture-and-backends.md](com-architecture-and-backends.md)** — read
> that first for the `pcom.h` substrate and the interface decomposition
> (`ICpuArchitecture`, `ICpuEmitter`, `ICpuBackend`, `ICpuCode`, `IClassFactory`).
> This document adds *crossing the module boundary*.

---

## 0. Status & goal

**Today libcpu is one statically-linked binary.** `cpu_new()` switches on a
`cpu_arch_t` enum and binds a compiled-in `arch_func_t` global
(`interface.cpp:98`: `arch_func_6502`, `arch_func_m68k`, …); the only backend
(LLVM) is compiled and linked in; execution is JIT-only. The frontend↔backend
pairing is fixed at link time.

**The goal:** decouple three axes into separately-distributable modules —

```
        FRONTEND module          BACKEND module           MODE
        (a guest ISA)            (a codegen target)       (a backend capability)
   ┌─────────────────────┐   ┌────────────────────┐   ┌──────────────┐
   │ 6502, MIPS, ARM,     │ × │ interpreter,        │ × │ JIT  (run)   │
   │ M88K, x86, IA-64, …  │   │ LLVM, Cranelift,    │   │ AOT  (emit)  │
   │ (or UPCL-generated)  │   │ MIR, WASM, …        │   └──────────────┘
   └─────────────────────┘   └────────────────────┘
```

— so an M×N×{AOT,JIT} matrix is realized from independent plugins. Drop in a new
guest by shipping a frontend `.so`; add a new code generator by shipping a backend
`.so`; the host and every other module are untouched.

Tags below: **[EXISTS]** present today · **[BUILD]** new work.

---

## 1. Why this is only possible on top of the abstract emitter

Dynamic any-to-any pairing works **only because a frontend speaks `ICpuEmitter`,
not LLVM** (com-arch doc §3). A frontend module emits architecture-neutral IR; the
backend module consumes it. If the frontend still constructed LLVM `Value*`
(today's `frontend.h`), every frontend would hard-depend on LLVM and "any backend"
would be impossible. So the prerequisite chain is:

```
abstract emitter (ICpuEmitter)  →  in-process multi-backend  →  THIS: cross-module multi-backend
   (com-arch §3)                    (com-arch §4–§5)              (dynamic loading + stable ABI)
```

Dynamic loading does not relax any of that; it *multiplies* the decoupling across a
compiled boundary — which is where ABI stability stops being a nicety and becomes a
hard correctness requirement (§9).

---

## 2. The module *is* a COM server

A libcpu module is a shared library that exposes COM class objects. It exports
**exactly one** well-known C symbol; everything else is reached through COM
interfaces (so the ABI surface is one function, maximally stable).

### 2.1 The module entry point  **[BUILD]**
Two viable contracts (the choice is §16); the recommended self-describing form:

```c
/*
 * The ONE exported symbol of every libcpu module. extern "C", default-visibility.
 * The host calls it once after loading the library, injecting host services and
 * receiving the module's self-description (its provided classes + ABI version).
 */
LIBCPU_MODULE_EXPORT HRESULT STDMETHODCALLTYPE
LibcpuModuleOpen(UINT32 host_abi_version, ILibcpuHost *pHost, ILibcpuModule **ppModule);
```

`ILibcpuModule` lets the host enumerate what this `.so` provides and create
instances — it is the per-module `IClassFactory` registrar:

```c
#undef  INTERFACE
#define INTERFACE ILibcpuModule
DECLARE_INTERFACE_(ILibcpuModule, IUnknown)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID, PVOID *) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;

    /* ILibcpuModule */
    STDMETHOD_(UINT32, GetAbiVersion)(THIS) PURE;                 // pcom/interface ABI it was built against
    STDMETHOD_(ULONG,  GetClassCount)(THIS) PURE;
    STDMETHOD(GetClassInfo)(THIS_ ULONG index, LIBCPU_CLASS_DESC *pDesc) PURE; // CLSID, kind, name, caps
    STDMETHOD(CreateInstance)(THIS_ REFCLSID rclsid, REFIID riid, PVOID *ppv) PURE; // == IClassFactory
};
```

### 2.2 The class descriptor — what a module advertises  **[BUILD]**
```c
typedef enum {
    LIBCPU_CLASS_FRONTEND = 1,   /* a guest ISA  (ICpuArchitecture)            */
    LIBCPU_CLASS_BACKEND  = 2,   /* a codegen target (ICpuBackend)             */
    LIBCPU_CLASS_DEVICE   = 3    /* an MMIO device / board (system-emulation)  */
} LIBCPU_CLASS_KIND;

typedef struct {
    CLSID              clsid;        /* stable identity of this class           */
    LIBCPU_CLASS_KIND  kind;
    CHAR CONST        *name;         /* "mips", "llvm-aot", "interpreter", ...   */
    UINT32             caps;         /* capability bitset (JIT/AOT, tier, ...)   */
    CHAR CONST        *host_triple;  /* backends: native target, or NULL (portable/interp) */
} LIBCPU_CLASS_DESC;
```

A frontend module reports one or more `FRONTEND` classes (one per ISA it carries);
a backend module reports `BACKEND` classes (e.g. `llvm-jit` and `llvm-aot` may be
two classes in one `.so`).

---

## 3. The portable dynamic-loader shim  **[BUILD]**

Like `pcom.h`, wrap the OS loader once for MSVC / OpenWatcom / GCC / Clang:

```c
/* pdl.h - portable dynamic loader (POSIX dlopen / Win32 LoadLibrary). */
#if defined(_WIN32)
#  define LIBCPU_MODULE_EXPORT __declspec(dllexport)
   typedef HMODULE pdl_handle_t;
#  define pdl_open(path)        ((pdl_handle_t)LoadLibraryA(path))
#  define pdl_sym(h, name)      ((void *)GetProcAddress((h), (name)))
#  define pdl_close(h)          FreeLibrary(h)
#  define LIBCPU_MODULE_SUFFIX  ".dll"
#else
#  define LIBCPU_MODULE_EXPORT __attribute__((visibility("default")))
   typedef void *pdl_handle_t;
#  define pdl_open(path)        dlopen((path), RTLD_NOW | RTLD_LOCAL)
#  define pdl_sym(h, name)      dlsym((h), (name))
#  define pdl_close(h)          dlclose(h)
#  if defined(__APPLE__)
#    define LIBCPU_MODULE_SUFFIX ".dylib"
#  else
#    define LIBCPU_MODULE_SUFFIX ".so"
#  endif
#endif
```

Rules: build modules with **hidden default visibility** so only the entry escapes
(`-fvisibility=hidden` + the export attribute on `LibcpuModuleOpen`);
`RTLD_LOCAL` so module-private symbols don't collide; the entry is `extern "C"` with
`STDMETHODCALLTYPE`.

---

## 4. Discovery and the class registry  **[BUILD]**

The host owns an `ICpuModuleRegistry` that turns "I want a frontend for `mips`" or
"an optimizing JIT for this host" into an instantiated object:

```c
DECLARE_INTERFACE_(ICpuModuleRegistry, IUnknown)
{
    ... IUnknown ...
    STDMETHOD(AddSearchPath)(THIS_ CHAR CONST *dir) PURE;          // scan dirs for *.so/.dll
    STDMETHOD(ScanAndRegister)(THIS) PURE;                         // discover modules
    STDMETHOD(RegisterStatic)(THIS_ ILibcpuModule *pModule) PURE;  // §11 built-in modules

    /* by identity */
    STDMETHOD(CreateClass)(THIS_ REFCLSID, REFIID, PVOID *) PURE;
    /* by query — the any-to-any pairing helpers */
    STDMETHOD(FindFrontend)(THIS_ CHAR CONST *arch, ICpuArchitecture **) PURE;
    STDMETHOD(FindBackend)(THIS_ UINT32 caps_required, CHAR CONST *host_triple, ICpuBackend **) PURE;
    STDMETHOD(EnumClasses)(THIS_ LIBCPU_CLASS_KIND kind, ILibcpuClassEnum **) PURE;
};
```

### Eager vs. lazy discovery
- **Eager:** `dlopen` every candidate, call `LibcpuModuleOpen`, read its
  `GetClassInfo` descriptors, keep the handle. Simple; pays load cost up front.
- **Lazy (recommended at scale):** a tiny **sidecar manifest** next to each module
  (`mips.frontend.json` / a `.manifest` section) lists its CLSIDs/kind/caps so the
  registry can index *without* dlopen; the library is loaded only when a class is
  actually instantiated. Mirrors how OS COM uses the registry vs. loading the
  server. Always reconcile the manifest against the module's own `GetClassInfo` on
  first load (manifests can lie / go stale).

---

## 5. Host services injected into modules (inversion of control)  **[BUILD]**

A module must **not** link back to host symbols (that breaks independent
shipping and creates symbol-collision/version hell). Instead the host passes an
`ILibcpuHost` into `LibcpuModuleOpen`; the module pulls what it needs:

```c
DECLARE_INTERFACE_(ILibcpuHost, IUnknown)
{
    ... IUnknown ...
    STDMETHOD(GetService)(THIS_ REFIID riid, PVOID *ppv) PURE;   // logging, allocator, address space...
    STDMETHOD_(VOID, Log)(THIS_ UINT32 level, CHAR CONST *msg) PURE;
    STDMETHOD(GetAllocator)(THIS_ ICpuMalloc **pp) PURE;         // the shared cross-module allocator (§10)
};
```

So a backend module reaches the address space (`ICpuAddressSpace`,
system-emulation doc) or the on-disk cache (com-arch §6) via `GetService`, never
via a link dependency. Dependencies flow *in*, by interface.

---

## 6. AOT vs JIT is a backend *capability*, not a code path  ★  **[BUILD]**

The same emitted IR drives both; the host picks by loading/selecting the backend
class with the right capability, discovered via `QueryInterface`:

```c
/* A JIT backend returns directly-callable code. */
DECLARE_INTERFACE_(ICpuBackendJIT, IUnknown) {
    ... IUnknown ...
    STDMETHOD(Compile)(THIS_ ICpuEmitter *pIR, ICpuCode **ppCode) PURE;  // ppCode->Execute(...)
};

/* An AOT backend serializes code to an artifact (object/bitcode/.wasm). */
DECLARE_INTERFACE_(ICpuBackendAOT, IUnknown) {
    ... IUnknown ...
    STDMETHOD(Emit)(THIS_ ICpuEmitter *pIR, IStream *pOut, LIBCPU_AOT_FORMAT fmt) PURE;
};
```

- `llvm-jit` module → implements `ICpuBackendJIT` (the AOT doc's
  `getFunctionAddress` path).
- `llvm-aot` module → implements `ICpuBackendAOT` (the AOT doc's
  `cpu_emit_object`, §5 there).
- `interpreter` module → `ICpuBackendJIT` whose `ICpuCode::Execute` interprets.
- `wasm` module → `ICpuBackendAOT` emitting `.wasm` (com-arch §8).

So **"any-to-any AOT/JIT"** = `FindBackend(caps_required = JIT|AOT, host_triple)`
returns whatever module satisfies it. One frontend, one IR, the host chooses the
fate. No `#ifdef AOT` anywhere.

---

## 7. The any-to-any composition flow

```
registry.FindFrontend("mips", &arch);          // loads mips.frontend.so on demand
registry.FindBackend(CAP_AOT, "x86_64-elf", &be);  // loads llvm-aot.so

ICpuEmitter *ir; be->CreateEmitter(ctx, &ir);   // backend supplies the emitter impl
arch->TranslateRegion(pc, ir);                  // frontend emits neutral IR into it
                                                // (frontend never knows it's LLVM/AOT)
IStream *obj = open_file_stream("guest.o");
QI(be, ICpuBackendAOT)->Emit(ir, obj, FMT_OBJECT);   // -> native object

// swap one line to JIT instead:
//   registry.FindBackend(CAP_JIT, host_triple, &be);
//   QI(be, ICpuBackendJIT)->Compile(ir, &code); code->Execute(state);

// swap the frontend to run a different guest with the SAME backend:
//   registry.FindFrontend("arm", &arch);
```

The frontend module, the backend module, and the host were built independently,
possibly by different people; they meet only at the COM vtables.

---

## 8. Modules and the rest of the stack

- **UPCL** (docs 6–9) becomes a *frontend-module factory*: `def → frontend.so` (or,
  via dynamic lowering, a frontend module that emits to `ICpuEmitter` at runtime).
- **Tiered execution** (com-arch §5): a tier manager loads several backend modules
  (interpreter + baseline + optimizing) and promotes between them — modules of
  different tiers coexisting at runtime is exactly what dynamic loading buys.
- **On-disk cache** (com-arch §6): cache keys must include the **backend module's
  CLSID + ABI/build version** (§9) so a cached artifact is never reused under a
  different/upgraded backend.
- **Devices** (system-emulation §6) ship as `LIBCPU_CLASS_DEVICE` modules — a board
  is an assembly of device plugins.

---

## 9. ABI stability across the boundary — the central constraint  ★

In-process, the COM ceremony (immutable vtables, refcounting) is good hygiene.
**Across a separately-compiled module boundary it is mandatory correctness**, because
the two sides may be built by different compilers/versions. Rules:

- [ ] **Interfaces are immutable once published.** Never reorder, remove, or insert
      vtable methods. Extend only by deriving a new interface with a **new IID**
      (`ICpuBackend2`) and `QueryInterface` for it.
- [ ] **Negotiate ABI version at load.** `LibcpuModuleOpen(host_abi_version, …)` and
      `ILibcpuModule::GetAbiVersion()` — the host refuses a module built against an
      incompatible `pcom.h`/interface revision. Fail closed, with a clear log.
- [ ] **Only POD and interface pointers cross the boundary.** No STL types, no C++
      exceptions, no `new`/`delete` of objects across modules — those have no stable
      ABI. Strings cross as `CHAR CONST *` or via `IStream`.
- [ ] **One calling convention.** `STDMETHODCALLTYPE` on every interface method.
- [ ] **Errors via `HRESULT`,** never exceptions thrown across the boundary.
- [ ] **`__uuidof`/IID equality is the only identity** — never compare class names
      for dispatch.

These are the price of any-to-any; they are not optional once a third party can
ship a module you didn't compile.

---

## 10. Cross-module memory & lifetime  **[BUILD]**

The classic cross-DLL footgun is "module A frees what module B allocated" with
mismatched heaps. Rules:

- **Every object frees itself** via `Release` — the allocating module's code runs,
  using its own heap. Callers never `free()` an interface.
- **Out-of-band buffers** use a single shared **task allocator**
  (`ICpuMalloc`, fetched from `ILibcpuHost::GetAllocator`) so allocate/free pair on
  the same heap regardless of which module calls which.
- **Lifetime crosses by refcount only**: a frontend holding a backend's
  `ICpuEmitter` `AddRef`s it; unload a module only after all its objects' refcounts
  hit zero (the registry tracks per-module outstanding objects and refuses
  `pdl_close` while any remain).

---

## 11. Static registration — dynamic loading is *optional*  **[BUILD]**

Embedded targets, single-binary deployments, and platforms without a usable
dynamic loader must still work. The registry accepts **statically-linked modules**:
a built-in module calls `RegisterStatic(pModule)` at startup (e.g. from a
constructor or an explicit `libcpu_register_builtins()`), populating the *same*
registry the dynamic loader would. Same interfaces, same composition code — only
the *source* of the `ILibcpuModule` differs (link-time vs `dlopen`). This preserves
today's single-binary mode as a first-class configuration, not a fork.

---

## 12. Security & trust  **[BUILD]**

Loading a native module is arbitrary code execution; combined with the on-disk
code cache (com-arch §6) it is a real attack surface. Mitigations:

- **Allowlist / signing**: verify modules (and cached artifacts) before load/map —
  signature or HMAC over the file; refuse unsigned in hardened deployments.
- **Trusted search paths only**; never load modules from world-writable dirs.
- **Sandbox option**: prefer the **WASM backend module** when running untrusted
  guest code — generated code is confined to linear memory (com-arch §8).
- **Version + integrity** in cache keys so a tampered/old module can't smuggle
  stale executable code.

---

## 13. Migration from the static model

| Today **[EXISTS]** | Becomes **[BUILD]** |
|--------------------|---------------------|
| `cpu_new(CPU_ARCH_MIPS)` switch (`interface.cpp:98`) | `registry.FindFrontend("mips", &arch)` |
| `extern arch_func_t arch_func_mips;` globals | a `FRONTEND` class in `mips.frontend.so` (or statically registered) |
| LLVM compiled into the core | `llvm-jit` / `llvm-aot` backend modules |
| `arch/<name>/CMakeLists.txt` static libs **[EXISTS]** | add a shared-module target + `LibcpuModuleOpen` wrapper |
| JIT-only, fixed | `FindBackend(JIT\|AOT, triple)` chooses at runtime |

The arch subdirectories are *already* separate build units (`arch/<name>/`), so
making each a loadable module is largely packaging + the entry-point wrapper, not a
rewrite — once the frontends speak `ICpuEmitter` (§1).

---

## 14. Roadmap

```
P1  pcom.h + pdl.h substrates; ILibcpuModule / LibcpuModuleOpen contract;
    static registration path (§11) — single binary, no dlopen yet.
P2  ICpuModuleRegistry with static modules only; route cpu_new() through it.
P3  Wrap ONE frontend (mips) + the existing LLVM backend as the first two classes,
    still statically registered — prove the interface boundary in-process.
P4  Dynamic loading: pdl_open/scan, eager discovery, refcounted unload.
P5  Split frontend.so / llvm-jit.so / llvm-aot.so; prove any-to-any (mips×{jit,aot}).
P6  Lazy manifests; capability queries (FindBackend by caps/triple).
P7  Third-party modules: ABI versioning hardening, signing, host-service injection.
P8  Interpreter + WASM backend modules; tiered manager loads multiple backends.
```

P1–P3 deliver the whole architecture **in-process** (no `dlopen`), de-risking the
interface boundary before the loader is introduced.

---

## 15. Risks & correctness checklist

- [ ] **ABI immutability** enforced and version-negotiated (§9).
- [ ] **No STL/exceptions/`new` across the boundary**; POD + interfaces only (§10).
- [ ] **Allocator coherence** via the shared task allocator (§10).
- [ ] **No module unload while objects live** (refcount-gated `pdl_close`).
- [ ] **Symbol hygiene**: hidden visibility, `RTLD_LOCAL`, one exported entry.
- [ ] **Static-link mode unaffected** (§11) — embedded builds keep working.
- [ ] **Cache keyed by backend CLSID + version** (§8) — no cross-backend reuse.
- [ ] **Trust**: signed/allowlisted modules and cache entries (§12).
- [ ] **Capability honesty**: a backend advertising `CAP_AOT` must actually emit for
      the queried `host_triple`; fail closed otherwise.

---

## 16. A design decision worth your input

The **module entry-point contract** sets how every plugin is written, discovered,
and loaded — and how OS-COM-familiar it feels.

```c
// (A) CLASSIC COM: per-CLSID class objects, registry-driven (like DllGetClassObject).
LIBCPU_MODULE_EXPORT HRESULT DllGetClassObject(REFCLSID rclsid, REFIID riid, PVOID *ppv);
//   + maps 1:1 onto real COM; external registry (manifest) drives discovery;
//     lazy by construction (load only to create a class).
//   - needs a separate registration/manifest step; host injection of services is
//     awkward (no natural place to pass ILibcpuHost).

// (B) SELF-DESCRIBING REGISTRAR: one entry returns an ILibcpuModule (recommended).
LIBCPU_MODULE_EXPORT HRESULT LibcpuModuleOpen(UINT32 abi, ILibcpuHost *host, ILibcpuModule **out);
//   + module self-describes (GetClassInfo); host services injected at open (IoC, §5);
//     ABI negotiated at the door; no global registration step.
//   - one mandatory dlopen to read descriptors unless paired with a sidecar manifest
//     (§4) for lazy indexing.
```

The trade-off: **(A)** is maximally COM-faithful and lazy but pushes discovery into
an external registry and makes host-service injection clumsy; **(B)** is simpler,
self-describing, and injection-friendly (cleaner for our `ILibcpuHost` model), at
the cost of needing a sidecar manifest to recover laziness at scale. The
recommendation (§2/§4) is **B + optional manifests**. Which matters more to you:
*strict OS-COM familiarity* (A), or *self-description + clean host injection* (B)?

---

## 17. References / prior art

- **COM in-process servers** — `DllGetClassObject`, `IClassFactory`, apartment ABI
  rules; the model for §2/§16.
- **Portable loaders** — `dlopen`/`dlsym` (POSIX), `LoadLibrary`/`GetProcAddress`
  (Win32), symbol visibility.
- **Plugin ABIs in practice** — Wine PE/ELF interop, GStreamer/LADSPA plugin
  registries, LLVM's plugin/pass-loading, QEMU TCG-vs-host backend split.
- **Sibling docs** — `com-architecture-and-backends.md` (interfaces, `pcom.h`,
  tiering, cache, WASM), `aot-userspace-binaries.md` (the AOT backend's object
  emission), `system-emulation.md` (devices/address space as services),
  `upcl-cpu-fpu-simd.md` (UPCL as a frontend-module factory).
```

---

### One-paragraph summary

Once frontends speak the abstract `ICpuEmitter` (com-arch §3), they no longer
depend on LLVM, and that decoupling can be carried across a compiled boundary:
package each **frontend** (guest ISA) and each **backend** (codegen target) as a
shared library that exports a single `extern "C"` entry (`LibcpuModuleOpen`)
returning a self-describing `ILibcpuModule` (a per-module `IClassFactory` +
capability descriptors), loaded through a portable `dlopen`/`LoadLibrary` shim and
indexed by an `ICpuModuleRegistry` that resolves "find a frontend for `mips`" /
"find a JIT/AOT backend for this host triple." **AOT vs JIT collapses to a backend
capability** discovered by `QueryInterface` (`ICpuBackendAOT::Emit` to an `IStream`
vs `ICpuBackendJIT::Compile` to an executable `ICpuCode`), so one frontend + one
emitted IR yields either fate by choosing which backend module to load — realizing
an M×N×{AOT,JIT} any-to-any matrix from independent plugins. The price, paid in
full only because third parties can ship modules you didn't compile, is strict
cross-boundary discipline: immutable IID-versioned vtables, `HRESULT`-only errors,
POD-and-interfaces-only across the line, a shared task allocator, refcount-gated
unload, and signed/allowlisted modules — with **static registration preserved as a
first-class mode** so single-binary/embedded builds keep working without any
dynamic loader at all.
