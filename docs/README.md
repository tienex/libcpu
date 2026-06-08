# libcpu Design Notes

A set of design documents for libcpu's architecture and its planned evolution.
They are written to be read in order but each stands alone; they cross-reference
each other heavily. Every document is grounded in the actual source and tags
proposals as **[BUILD]/[PROPOSED]** versus what **[EXISTS]** today.

## Reading order

### Track 1 — The engine (as-is, then forward)

1. **[../ARCHITECTURE.md](../ARCHITECTURE.md)** — How libcpu works today: the
   frontend/backend split, the `cpu_t` state, the tag → translate → run pipeline,
   the PC-dispatch switch, and the LLVM JIT. *Start here.*
2. **[improving-dispatch.md](improving-dispatch.md)** — Removing the monolithic
   PC-dispatch "giant jump table": indirect-target gating, per-subroutine
   functions, return prediction, IBTC, traces.
3. **[aot-userspace-binaries.md](aot-userspace-binaries.md)** — Ahead-of-time
   compilation of userspace guests into native binaries, with **libnix** as the
   syscall/runtime layer and **libloader** as the image loader.
4. **[system-emulation.md](system-emulation.md)** — Growing libcpu into a
   full-system emulator: software MMU, guest-vectored exceptions, interrupts,
   privilege/system registers, paged/SMC-safe translation cache, devices.
5. **[com-architecture-and-backends.md](com-architecture-and-backends.md)** — A
   portable-COM (no `ole32`) interface architecture enabling pluggable backends
   (interpreter, JITs, WASM), HotSpot-style tiered execution, and on-disk
   caching. The `ICpuEmitter` seam here is the keystone the UPCL "dynamic" path
   depends on.
6. **[dynamic-modules.md](dynamic-modules.md)** — Packaging frontends and backends
   as independently-loadable shared libraries (`.so`/`.dll`/`.dylib`) over the COM
   model, so any guest pairs with any codegen target for either AOT or JIT — an
   any-to-any plugin matrix. Covers the module ABI, discovery/registry, and the
   cross-boundary stability rules. Builds on doc 5.

### Track 2 — UPCL (the CPU-description language)

7. **[upcl-reference-manual.md](upcl-reference-manual.md)** — The programmer's
   reference for UPCL, derived from the real grammar/lexer/IR/examples. Part I is
   the language; Part II is a cookbook (SPARC/IA-64/AM29K/MMIX/APX register files,
   condition codes, NaT, ARM predication, MIPS delay slots, PPC XER, M88K compare
   compression, real-vs-virtual memory, SMM, VM, SIMD/matrix, PSX1 MDEC, one's
   complement). *Start the UPCL track here.*
8. **[upcl-cpu-fpu-simd.md](upcl-cpu-fpu-simd.md)** — Making UPCL generate
   complete frontends (Mode A) and using it *dynamically* (Mode B) to implement
   IEEE FPU and SIMD with shared lowerings across backends.
9. **[upcl-instruction-decoders.md](upcl-instruction-decoders.md)** — A
   CPU-agnostic instruction-decoder sub-language spanning MMIX → x86/VAX: fetch
   models, bit-field match, opcode tables, decode-time state + continuation
   (the agnostic basis for "prefixes"), and recursive operand decoders.
10. **[upcl-complex-cpu-ia64.md](upcl-complex-cpu-ia64.md)** — The capstone:
   describing IA-64/EPIC (bundles, predication, register stacking/rotation, NaT,
   speculation/ALAT, extended FP) — the stress test that exercises every
   capability above.

## Dependency notes

- The **`ICpuEmitter`** abstraction (doc 5, §3) is the single prerequisite that
  unblocks multiple backends, the interpreter, WASM, UPCL's dynamic lowering
  (docs 8–10), and loadable frontend/backend modules (doc 6).
- **Context-keyed translation caching** appears in three places — system-mode
  ASIDs (doc 4 §8), the on-disk cache (doc 5 §6), and IA-64 CFM/RRB specialization
  (doc 10 §5) — and is the same mechanism each time.
- The **PC-dispatch switch** that doc 2 wants to remove for native speed is the
  same structure that makes a **WASM** backend feasible (doc 5 §8) — the abstract
  emitter lets each backend choose.
- **AOT vs JIT is not a code path but a backend capability** (doc 6 §6): the same
  emitted IR yields a native object or callable code depending only on which
  backend module is loaded.

## Status of the codebase (as of writing)

- libcpu builds against **LLVM 8**, JIT-only; the userspace test harnesses run.
- **UPCL** builds (after a one-line fix in `sema/register_file_builder.cpp`) and
  **already generates** libcpu frontend files from a `.def`: `<arch>_arch.{h,cpp}`
  (register layout, init, and a `translate_instr`), `_regfile.h` (the packed
  `reg_<arch>_t` struct), `_opc.h`, `_tcond.cpp`, and a `_tag_stub.cpp`. Remaining
  gaps: it is disabled in the root build (not wired/regenerated); instruction
  **encoding/decode** is unspecified in the `.def`s, so `tag` generation is a stub
  (this is what the decoder DSL, docs 8–10, addresses); and the generated code is
  unverified against the engine (compile-testing it needs LLVM). UPCL itself needs
  only flex/bison + a C++ compiler — no LLVM — so it is the most readily buildable
  starting point. The grammar currently reports many shift/reduce + reduce/reduce
  conflicts (the instruction grammar is ambiguous/partial).

These documents are design intent, not implemented features. Treat line/file
references as navigation aids that may drift.
