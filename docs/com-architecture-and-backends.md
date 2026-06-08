# A Portable-COM Architecture for libcpu: Pluggable Backends, Tiered (HotSpot-style) Execution, and On-Disk Caching

> A design for restructuring libcpu around **COM-style interfaces** — using the
> *real* COM types, macros, and idioms (`IUnknown`, `QueryInterface`, `HRESULT`,
> GUIDs, `STDMETHOD`) but **without any dependency on Windows COM** — so that the
> code-generation backend becomes pluggable: an **interpreter**, one or more
> **JITs**, and (if feasible) a **WASM** target, coordinated by a HotSpot-like
> tiered execution engine with a persistent **on-disk code cache**.
>
> Read `ARCHITECTURE.md` (current model), `docs/improving-dispatch.md` (dispatch),
> `docs/aot-userspace-binaries.md` (object emission), and
> `docs/system-emulation.md` (memory/MMU) first — this document unifies them
> behind a single interface model.

---

## 0. Why COM, why portable, what it buys

libcpu already has *one* plugin seam: the per-architecture frontend, expressed
as a struct of C function pointers (`arch_func_t` in `libcpu/libcpu.h:49`). That
pattern is COM in embryo — a vtable selected at runtime. The proposal is to make
that idea **first-class and bidirectional**: frontends, backends, code caches,
memory, and profilers all become **interfaces** discovered and composed at
runtime.

COM (the *shape*, not Microsoft's runtime) is the right tool because it gives us,
portably and in plain C/C++:

- **Capability discovery & versioning** via `QueryInterface` — a backend
  advertises `ICpuBackendOptimizing` *or not*; a frontend advertises
  `ICpuDisassembler`, `ICpuSystemMode` only if it implements them. No more
  "function pointer is NULL, hope the caller checks."
- **A stable binary contract** (vtable ABI) so backends can be separate
  shared objects / DLLs, even third-party, without recompiling the core.
- **Deterministic lifetime** via `AddRef`/`Release` reference counting — natural
  for objects shared between the tier manager, cache, and execution threads.
- **A neutral object model** that the user's existing NT/UEFI house style
  (`HRESULT`/`SCODE`, GUID-identified protocols) maps onto directly. UEFI's
  protocol model is literally GUID + vtable; this is the same discipline.

We do **not** want a dependency on `ole32`/`combase`. We want the *types and
macros* (`HRESULT`, `IID`, `REFIID`, `IUnknown`, `STDMETHOD`, `DECLARE_INTERFACE_`,
`SUCCEEDED`/`FAILED`) available identically on MSVC, OpenWatcom, GCC, and Clang.

### Prior art (all portable COM-likes worth studying)
- **Wine** — fully portable reimplementation of the COM headers/macros.
- **Mozilla XPCOM** — "Cross-Platform COM": `nsISupports` ≈ `IUnknown`,
  `nsresult` ≈ `HRESULT`, `do_QueryInterface`. Proves the model travels.
- **UEFI protocols** — GUID-identified vtable interfaces, no COM runtime; closest
  to what we want (and matches the house commenting/type style).
- **PipeWire/GStreamer, Banshee, BeOS/Haiku BMessage** — vtable+refcount object
  models in C.

---

## 1. The constraint we must break first

The current backend is **not abstracted** — it is welded into the frontend
contract. In `libcpu/frontend.h`:

```c
Value *arch_get_reg(cpu_t *cpu, uint32_t index, uint32_t bits, BasicBlock *bb);
Value *arch_adc(cpu_t *cpu, Value *dst, Value *src, Value *v, bool, bool, BasicBlock *bb);
#define LOAD(a)     new LoadInst(a, "", false, bb)
#define CONST32(v)  ConstantInt::get(getIntegerType(32), v)
```

and the frontend entry point itself (`libcpu/libcpu.h`):

```c
typedef int (*fp_translate_instr)(struct cpu *cpu, addr_t pc, BasicBlock *bb);
```

Every frontend is written against **LLVM `Value*` / `BasicBlock*` and LLVM
instruction constructors**. There is exactly one backend and the frontends know
it intimately. **No alternate backend (interpreter, WASM, another JIT) is
possible until IR emission is abstracted.** This is the central, unavoidable
refactor; §3 addresses it.

---

## 2. The portable COM substrate  **[BUILD]**

A single header, `libcpu/pcom.h` ("portable COM"), provides the real COM shapes
with zero Windows runtime dependency.

```c
/*
 * pcom.h - Portable COM substrate. Real COM types and macros, no ole32.
 *          Targets MSVC, OpenWatcom, GCC and Clang.
 */
#if defined(_WIN32)
#  include <unknwn.h>           /* Use the genuine SDK headers where present. */
#else
   /* ---- Minimal, ABI-compatible reimplementation (Wine/XPCOM-style). ---- */

   typedef int32_t HRESULT;
   typedef int32_t SCODE;

#  define S_OK            ((HRESULT)0)
#  define S_FALSE         ((HRESULT)1)
#  define E_NOINTERFACE   ((HRESULT)0x80004002L)
#  define E_OUTOFMEMORY   ((HRESULT)0x8007000EL)
#  define E_INVALIDARG    ((HRESULT)0x80070057L)
#  define E_NOTIMPL       ((HRESULT)0x80004001L)
#  define E_FAIL          ((HRESULT)0x80004005L)

#  define SUCCEEDED(hr)   (((HRESULT)(hr)) >= 0)
#  define FAILED(hr)      (((HRESULT)(hr)) <  0)

   /* Calling convention: __stdcall only where it means something (Win32 x86). */
#  if defined(__i386__) && defined(_WIN32)
#    define STDMETHODCALLTYPE __stdcall
#  else
#    define STDMETHODCALLTYPE
#  endif

   typedef struct _GUID {
       uint32_t Data1;
       uint16_t Data2;
       uint16_t Data3;
       uint8_t  Data4[8];
   } GUID;
   typedef GUID            IID;
   typedef GUID            CLSID;
   typedef CONST GUID     *PCGUID;     /* P/PC typedefs, single level. */
   typedef GUID           *PGUID;
#  define REFIID   CONST IID &         /* C++ ; a macro picks C vs C++ form. */
#  define REFCLSID CONST CLSID &

   /* Interface declaration macros - the genuine COM spelling. */
#  define interface struct
#  define STDMETHOD(m)        virtual HRESULT STDMETHODCALLTYPE m
#  define STDMETHOD_(t, m)    virtual t       STDMETHODCALLTYPE m
#  define PURE               = 0
#  define THIS_
#  define THIS               void
#  define DECLARE_INTERFACE(i)        interface i
#  define DECLARE_INTERFACE_(i, base) interface i : public base

   /* IUnknown - the root of everything. */
   DECLARE_INTERFACE(IUnknown)
   {
       STDMETHOD(QueryInterface)(THIS_ REFIID riid, PVOID *ppvObject) PURE;
       STDMETHOD_(ULONG, AddRef)(THIS) PURE;
       STDMETHOD_(ULONG, Release)(THIS) PURE;
   };
   typedef IUnknown *PUNKNOWN;
#endif

/* Portable GUID compare + DEFINE_GUID, identical on all targets. */
HRESULT  PcomIsEqualGUID(PCGUID a, PCGUID b);  /* or inline */
```

Notes:
- **Real macros, portable bodies.** `DECLARE_INTERFACE_`, `STDMETHOD`, `THIS_`,
  `PURE` are spelled exactly as in `objbase.h`; on non-Windows they expand to
  portable C++ virtual-method declarations. A C-only path (function-pointer
  vtables) is also possible for OpenWatcom/pure-C builds — provide a `CINTERFACE`
  variant exactly as the SDK does.
- **No `CoCreateInstance` dependency.** Provide a tiny portable activation layer
  (a `IClassFactory` registry keyed by `CLSID`) instead of linking the COM
  runtime — backends self-register a factory at load. Equivalent to UEFI's
  "install protocol interface."
- **House style.** `HRESULT`/`SCODE` returns everywhere; `P`/`PC` typedefs at one
  level only; `CONST` uppercase; `UINT64_C` for the 64-bit GUID/option constants.

---

## 3. Decoupling frontend from backend — the abstract emitter  **[BUILD]**

To get multiple backends, the frontend must emit to an **abstract builder**, not
to LLVM. Two designs; we recommend (B) as the end state, reached via (A).

### (A) Opaque-handle emitter (mechanical migration of today's helpers)

Mirror the existing `frontend.h` helper set as an interface returning **opaque
value handles** (`ICpuValue *`) and **block handles** (`ICpuBlock *`). The
frontend stops calling `new LoadInst(...)` and instead calls methods on an
`ICpuEmitter` it is handed.

```c
/*
 * ICpuEmitter - architecture-neutral IR construction. One implementation per
 * backend (LLVM, interpreter, WASM). Mirrors today's frontend.h helpers.
 */
#undef  INTERFACE
#define INTERFACE ICpuEmitter
DECLARE_INTERFACE_(ICpuEmitter, IUnknown)
{
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, PVOID *ppvObject) PURE;
    STDMETHOD_(ULONG, AddRef)(THIS) PURE;
    STDMETHOD_(ULONG, Release)(THIS) PURE;

    /* values */
    STDMETHOD(ConstInt)(THIS_ UINT32 bits, UINT64 value, ICpuValue **ppv) PURE;
    STDMETHOD(GetReg)(THIS_ UINT32 index, UINT32 bits, ICpuValue **ppv) PURE;
    STDMETHOD(PutReg)(THIS_ UINT32 index, ICpuValue *pv, UINT32 bits, BOOL sext) PURE;
    STDMETHOD(Load)(THIS_ ICpuValue *pAddr, UINT32 bits, ICpuValue **ppv) PURE;
    STDMETHOD(Store)(THIS_ ICpuValue *pVal, ICpuValue *pAddr, UINT32 bits) PURE;
    STDMETHOD(Adc)(THIS_ ICpuValue *dst, ICpuValue *src, ICpuValue *carry,
                   BOOL plusCarry, BOOL plusOne, ICpuValue **ppv) PURE;
    /* control flow */
    STDMETHOD(Branch)(THIS_ ICpuBlock *pTarget) PURE;
    STDMETHOD(CondBranch)(THIS_ ICpuValue *pCond, ICpuBlock *t, ICpuBlock *f) PURE;
    /* ...the full set the frontends use today... */
};
```

The LLVM emitter wraps `Value*`/`BasicBlock*`; `ICpuValue`/`ICpuBlock` are thin
handles. This is a large but **mechanical** refactor of each `*_translate.cpp`
and is fully behaviour-preserving for the LLVM path.

### (B) Micro-op IR (QEMU TCG-style) — the clean end state

Have the frontend emit a small, typed, backend-neutral **micro-op stream** (à la
QEMU's TCG ops or a tiny SSA). Backends then *consume* the stream:

- the **interpreter** executes ops directly,
- the **LLVM/JIT** backends lower ops to their IR,
- the **WASM** backend lowers ops to WASM.

This fully decouples N frontends from M backends (write each once), and — crucial
for §6 — the micro-op stream is **the natural unit for on-disk caching** because
it is portable across backends and host machines.

> Migration: implement (A) first (keeps LLVM working, no frontend semantics
> change), then introduce the micro-op buffer as one `ICpuEmitter` implementation
> that records ops; backends read from it. Frontends never change again.

---

## 4. The interface decomposition

```
                         IUnknown
                            ▲
   ┌───────────────┬────────┼─────────┬──────────────┬───────────────┐
ICpuArchitecture ICpuEmitter ICpuBackend ICpuCodeCache ICpuAddressSpace ICpuProfiler
 (was arch_func_t) (§3)       (§5)        (§6)          (memory/MMU,     (tiering, §5)
   │  QI for:                  │           │           system-emu doc)
   ├ ICpuDisassembler          ├ produces ICpuCode (executable artifact)
   ├ ICpuTagger                ├ QI for ICpuBackendOptimizing / ...Baseline
   ├ ICpuSystemMode            └ GetTier() -> TIER_INTERP/BASELINE/OPTIMIZING
   └ ICpuDebug

   ICpuExecutionEngine  (tier manager): owns profiler + cache + backends,
                         decides what runs interpreted vs compiled, when to
                         promote, and persists/loads the cache.
```

- **`ICpuArchitecture`** replaces `arch_func_t`; optional capabilities are
  discovered by `QueryInterface` instead of NULL function pointers.
- **`ICpuBackend`** turns emitted IR into an **`ICpuCode`** (an executable block
  with an `Execute(ICpuState*)` method returning an `HRESULT`-coded run status,
  the moral equivalent of today's `JIT_RETURN_*`).
- **`ICpuCodeCache`** maps `(arch, code-hash, tier, options) → ICpuCode`,
  in-memory and on-disk (§6).
- **`ICpuExecutionEngine`** is the HotSpot-like coordinator (§5).
- **`ICpuAddressSpace`** is the memory/MMU seam from the system-emulation doc —
  COM-ifying it now means the MMU and devices become interfaces too.

---

## 5. HotSpot-style tiered execution  **[BUILD]**

Multiple backends are only worthwhile if something decides *which* to use. Model
the classic three tiers:

| Tier | Backend | Role | Compile cost | Run speed |
|------|---------|------|--------------|-----------|
| **0 — Interpreter** | `ICpuBackend(TIER_INTERP)` | run immediately, **profile**; handles cold & one-shot code, self-modifying code, fallback | none | slow |
| **1 — Baseline JIT** | template/fast codegen (AsmJit/DynASM/Lightning/MIR) | quick native code for warm code | low | medium |
| **2 — Optimizing JIT** | LLVM (or libgccjit/Cranelift) | heavy optimization for hot code | high | fast |

### Mechanism
- **Profiling.** The interpreter (and baseline code) bump an `ICpuProfiler`
  counter per block/edge. Reuse the *tag* infrastructure to know block
  boundaries (`is_start_of_basicblock`, `tag.cpp`).
- **Promotion.** When a block's count crosses a threshold, the
  `ICpuExecutionEngine` enqueues it for the next tier, ideally compiled on a
  **background thread** so execution doesn't stall. On completion the cache entry
  is swapped (`AddRef`/`Release` makes the handoff safe).
- **On-stack replacement (OSR)** — the hard part. Entering optimized code in the
  middle of a hot loop requires reconstructing tier-2 state from tier-0/1 state.
  *Defer it:* first cut promotes at block **entry** only (next iteration uses the
  faster code). OSR is a later optimization.
- **Deoptimization.** A CPU emulator needs far less speculative deopt than a
  language VM, but it **does** need invalidation for **self-modifying code** and
  page remaps (see `docs/system-emulation.md` §8): a write to a translated page
  drops the affected `ICpuCode` and falls back to the interpreter, which is
  always correct. The interpreter tier is thus also the **deopt safety net**.

This directly subsumes the userspace JIT-only model and the AOT model: AOT is
simply "pre-populate the on-disk cache at tier 2 for the whole binary."

---

## 6. On-disk caching  **[BUILD]**

### Key derivation — reuse what already exists
`tag.cpp init_tagging` already SHA-1-hashes the code region into
`cpu->code_digest`. The cache key is:

```
key = SHA1(code-bytes) ⊕ arch-id ⊕ backend-id ⊕ backend-version ⊕ tier ⊕ option-flags ⊕ host-triple
```

The existing `/tmp/libcpu-<sha1>.entries` *entry-point* cache is the seed: it
already persists discovered indirect targets across runs — extend the same idea
from "entries" to "compiled blocks."

### What to persist, per backend
- **Optimizing JIT (LLVM):** the emitted **object/bitcode** (this is exactly the
  `cpu_emit_object` capability proposed in `docs/aot-userspace-binaries.md` §5) —
  reload + relink instead of re-optimizing.
- **WASM:** the compiled **`.wasm` module** (and/or the engine's AOT artifact).
- **Baseline JIT:** raw code bytes + relocations (position-dependent — must record
  host triple and re-relocate, or store position-independent).
- **Interpreter:** the **micro-op stream** (§3B) — portable across hosts; saves
  re-decoding/re-tagging even when native code isn't reusable.

### Container — real COM streaming types
Use COM's own persistence shapes so the format is principled and portable:
- **`IStream` / `ISequentialStream`** for byte blobs, and optionally
  **`IStorage`** (structured/compound storage) for the multi-stream container
  (manifest + per-block streams). These are *real* COM interfaces; provide
  portable implementations over a file in `pcom.h`'s spirit (no `ole32`).
- A backend serializes/loads via **`IPersistStream`** (`Load`/`Save` taking an
  `IStream`), the genuine COM persistence contract.

### Invalidation & safety
- Version every component (`backend-version`, IR-format-version) in the key;
  refuse stale entries.
- **SMC / paging:** never trust a cached block for a page that has since been
  written (ties to system-emu §8).
- **Security:** cached native code is executable input — sign or integrity-check
  entries (HMAC over the blob) before mapping executable, especially if caches
  are shared between users.

---

## 7. Candidate backends besides LLVM

| Backend | Lang/API | Tier fit | Notes / trade-offs |
|---------|----------|----------|--------------------|
| **Custom interpreter** | C/C++ | **0** | Mandatory tier-0; also the deopt/SMC safety net. Threaded/computed-goto or micro-op walker. |
| **AsmJit** | C++ | **1** | Runtime assembler, x86/ARM64. Excellent fast baseline; no IR/optimizer. |
| **DynASM** (LuaJIT) | C + Lua preproc | **1** | Template assembler; extremely fast emit; many host arches; build-time tooling. |
| **GNU Lightning** | C | **1** | Portable JIT across many hosts; tiny; minimal optimization. Great portability for a baseline tier. |
| **MIR** (Makarov) | C | **1–2** | Light, fast SSA JIT with a real (light) optimizer; designed for exactly this; can also interpret its IR. Strong candidate to span baseline+mid. |
| **libgccjit** | C (GCC) | **2** | Optimizing, GCC-grade codegen; heavier dependency; alternative to LLVM. |
| **Cranelift** (Wasmtime) | Rust (C FFI) | **1–2** | Fast SSA codegen, good speed/quality balance; Rust FFI to integrate. |
| **B3/Air** (JSC) | C++ | **2** | High-quality optimizing backend; embedding outside WebKit is involved. |
| **QBE** | C | **1–2** | Tiny SSA compiler backend; modest quality; very small footprint. |
| **TCC (TinyCC)** | C | **1** | Emit **C** (libcpu's *original* static recompiler emitted C — see `TODO`!) and compile with TCC for near-instant builds. Nostalgic and pragmatic. |
| **NanoJIT** (historical) | C++ | **1–2** | Mozilla/Adobe trace JIT backend; mostly of historical interest. |
| **WASM engines** (wasmtime / WAMR / wasm3) | C API | **2 / portable** | Run an emitted `.wasm` (see §8) — or use as a *sandboxed* execution substrate. |

A sensible initial set: **interpreter (T0) + MIR or AsmJit (T1) + LLVM (T2)**,
with **WASM** as an experimental portable/sandboxed target.

---

## 8. A WASM backend — *entertained, IFF feasible*

**Question:** can a backend emit WebAssembly so guest code runs on a WASM engine
(wasmtime/WAMR/wasm3) or in a browser? **Answer: yes, feasibly — and libcpu is
unusually well-suited to it.** Caveats below.

### Why it fits libcpu specifically
WASM has **structured control flow only** — no arbitrary `goto`. Translating
arbitrary machine code (which has arbitrary jumps) to WASM is normally the hard
part, requiring the **Relooper/Stackifier** algorithm to recover structure, and
irreducible control flow can't be expressed directly.

**But libcpu already routes all non-trivial control flow through a single PC
dispatch switch** (`translate_all.cpp`: one `loop { switch (PC) { case... } }`).
That "giant jump table" — the very thing `docs/improving-dispatch.md` wants to
remove for *native* performance — is **exactly the idiomatic shape for WASM**:

```wat
(loop $dispatch
  (block ... (br_table $b0 $b1 ... (i32 PC)))   ;; switch on PC
  ;; block bodies; indirect transfers set PC and (br $dispatch)
)
```

So the existing model maps onto WASM `loop` + `br_table` + (for indirect calls)
`call_indirect` over a function table, **without** needing a relooper. The
dispatch-improvement work and the WASM target are therefore in mild tension:
keep the switch for WASM, chain/inline for native — the abstract emitter lets
each backend choose.

### Memory model
- Guest RAM ↦ a **WASM linear memory**; guest loads/stores ↦ `i32.load`/`store`
  etc. This is clean and fast.
- **Address width:** wasm32 linear memory is 32-bit (max 4 GiB). Guests with
  >32-bit address spaces need the **memory64** proposal (now widely available in
  wasmtime/V8). Record this as a capability the WASM backend `QueryInterface`-es.
- The system-mode MMU (`docs/system-emulation.md`) becomes WASM functions over
  linear memory; MMIO becomes imported host calls.

### Costs & honest caveats
- **Double compilation:** guest → WASM (our backend) → native (the WASM engine's
  own JIT). Acceptable for a *portable/optimizing* or *sandboxed* tier, **not** a
  fast baseline. Place WASM at tier 2-portable, not tier 1.
- **Floating point:** WASM has f32/f64 only; **fp80/fp128** guests (already a
  special case in libcpu via `CPU_FLAG_FP80/FP128`) need software emulation.
- **Flags/precise traps:** the exploded N/V/Z/C model and exception vectoring
  (system mode) must be modeled explicitly; WASM traps are limited.
- **On-disk cache:** the compiled `.wasm` is itself a portable cache artifact
  (§6) — attractive, since it's host-independent.

### Use cases that justify it
- **In-browser** emulation (libcpu compiled to WASM running a WASM-target guest,
  or serving `.wasm` blocks).
- **Sandboxed execution** of translated guest code (capability-confined memory).
- **Maximum portability** tier where no native backend exists for the host.

**Verdict:** feasible and recommended as an **experimental backend**, gated on
memory64 for wide-address guests and accepting the double-compile cost. The
existing dispatch-switch structure removes the usual hardest obstacle.

---

## 9. Roadmap

```
Phase 1  pcom.h substrate: portable COM types/macros, IUnknown, refcount base,
         IClassFactory registry, IStream/IStorage portable impls.        (§2,§6)
Phase 2  Abstract emitter (A): ICpuEmitter mirroring frontend.h; LLVM emitter
         as first impl; migrate frontends mechanically (no semantic change). (§3A)
Phase 3  COM-ify the seams: ICpuArchitecture (from arch_func_t), ICpuBackend,
         ICpuCode, ICpuCodeCache, ICpuExecutionEngine.                    (§4)
Phase 4  Interpreter backend (tier 0) + ICpuProfiler.                     (§5)
Phase 5  On-disk cache: key from existing SHA-1; persist micro-ops + LLVM
         objects via IPersistStream/IStream; versioning + integrity.      (§6)
Phase 6  Micro-op IR (B): record-and-replay emitter; LLVM & interpreter both
         consume it; frontends now backend-agnostic for good.            (§3B)
Phase 7  Baseline JIT (tier 1): MIR or AsmJit backend; wire tiered promotion,
         background compilation, SMC deopt to interpreter.               (§5)
Phase 8  WASM backend (experimental): reuse dispatch-switch shape; memory64
         capability; .wasm as cache artifact.                            (§8)
```

Phases 1–3 are pure refactor (LLVM stays the only backend, behaviour
preserved). Real plurality starts at Phase 4.

---

## 10. Risks & correctness

- [ ] **Frontend migration is large.** Every `*_translate.cpp` moves off LLVM
      types. Do it behavior-preserving with the LLVM emitter as the oracle;
      diff IR before/after.
- [ ] **ABI stability of `pcom.h`.** Once backends ship separately, vtable layout
      and IIDs are contracts — version interfaces, never reorder methods.
- [ ] **Refcount discipline.** `AddRef`/`Release` bugs are the classic COM
      failure mode; consider smart-pointer helpers (`comptr<T>`).
- [ ] **C vs C++ interface ABI.** Provide the `CINTERFACE` (function-pointer
      vtable) path for OpenWatcom/pure-C; keep method order identical.
- [ ] **Cache safety.** Integrity-check executable cache blobs; invalidate on
      SMC/paging; version by backend + host triple.
- [ ] **Tier coherence.** A block may exist at multiple tiers simultaneously;
      the cache/engine must pick atomically and retire old `ICpuCode` safely.
- [ ] **WASM gating.** Don't expose 64-bit guests on a wasm32 engine; advertise
      via capability QI, fail closed.

---

## 11. A design decision worth your input

The pivot of the whole effort is **the granularity and shape of the abstract
emitter** (§3) — it determines how invasive the frontend migration is, how
cleanly backends plug in, and what the portable on-disk cache stores. Two
positions:

```c
/*
 * Two shapes for the frontend↔backend boundary. The choice fixes the cache
 * format (§6), the WASM mapping (§8), and the size of the frontend rewrite.
 */

/* (A) Opaque-handle builder: methods construct values/blocks; backend owns repr. */
STDMETHOD(Adc)(THIS_ ICpuValue *dst, ICpuValue *src, ICpuValue *carry,
               BOOL plusCarry, BOOL plusOne, ICpuValue **ppResult) PURE;
/*   + smallest change from today's frontend.h; LLVM path nearly unchanged.
 *   - each backend reimplements the full helper surface; cache is per-backend. */

/* (B) Micro-op stream: frontend appends typed ops; every backend consumes them. */
STDMETHOD(Emit)(THIS_ MICRO_OP op, CPU_OPND CONST *opnds, UINT32 count) PURE;
/*   + write each frontend & backend once; ops are a portable, cacheable IR;
 *     interpreter/LLVM/WASM all read the same buffer.
 *   - must design a complete, stable micro-op ISA up front (the real work). */
```

The trade-off: **(A)** ships a working interpreter+LLVM split fastest with the
least frontend churn, but couples the cache to each backend and duplicates the
op surface; **(B)** is more initial design (defining the micro-op ISA) but yields
true N×M decoupling, a backend-independent on-disk cache, and the cleanest WASM
lowering. The recommendation in §3 is **A→B** (ship A, evolve to B), but if the
on-disk cache and WASM target are priorities from day one, starting at **B** may
be worth the heavier design phase. Which constraint dominates — *time to first
alternate backend*, or *a portable cache + WASM from the outset*?

---

## 12. References / prior art

- **Microsoft COM** — `IUnknown`, `QueryInterface`, `HRESULT`, `IStream`/`IStorage`,
  `IPersistStream`, `IClassFactory` (the shapes we reuse).
- **Wine** — portable COM headers/macros (`DECLARE_INTERFACE_`, `STDMETHOD`).
- **Mozilla XPCOM** — cross-platform COM in practice (`nsISupports`/`nsresult`).
- **UEFI Driver Model** — GUID + vtable protocols without a COM runtime.
- **QEMU TCG** — micro-op IR with multiple host backends (the §3B model).
- **HotSpot / V8 (Ignition+TurboFan) / LuaJIT** — tiered interpret→JIT execution,
  OSR, deopt (the §5 model).
- **Backends:** AsmJit, DynASM, GNU Lightning, MIR, libgccjit, Cranelift, QBE,
  B3/Air, TinyCC; **WASM:** wasmtime, WAMR, wasm3, Binaryen/Relooper, the
  memory64 proposal.
```

---

### One-paragraph summary

Re-base libcpu on a **portable COM substrate** (`pcom.h`: the genuine `IUnknown`,
`HRESULT`, GUID, `STDMETHOD`, `IStream` shapes with no `ole32` dependency,
matching the house NT/UEFI style), then express frontends, backends, code caches,
memory, and the profiler as **interfaces** discovered via `QueryInterface`. The
one prerequisite that unblocks everything is **abstracting IR emission** away from
LLVM (`ICpuEmitter`, evolving to a QEMU-TCG-style micro-op stream), since today's
frontends emit LLVM `Value*`/`BasicBlock*` directly. With that seam in place,
multiple `ICpuBackend`s coexist — an **interpreter** (tier 0, also the SMC/deopt
safety net), a **baseline JIT** (MIR/AsmJit/Lightning, tier 1), and an
**optimizing JIT** (LLVM/libgccjit/Cranelift, tier 2) — coordinated by a
HotSpot-style `ICpuExecutionEngine` that profiles, promotes hot code on a
background thread, and persists compiled artifacts in an **on-disk cache** keyed
off the SHA-1 digest libcpu already computes. A **WASM backend** is feasible and
recommended as an experimental portable/sandboxed tier precisely because
libcpu's existing PC-dispatch-switch maps directly onto WASM's structured
`loop`+`br_table`, sidestepping the usual relooper problem — gated on memory64
for wide-address guests and accepting a double-compile cost.
