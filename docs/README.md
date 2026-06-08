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

### Track 2 — UPCL (the CPU-description language)

6. **[upcl-reference-manual.md](upcl-reference-manual.md)** — The programmer's
   reference for UPCL, derived from the real grammar/lexer/IR/examples. Part I is
   the language; Part II is a cookbook (SPARC/IA-64/AM29K/MMIX/APX register files,
   condition codes, NaT, ARM predication, MIPS delay slots, PPC XER, M88K compare
   compression, real-vs-virtual memory, SMM, VM, SIMD/matrix, PSX1 MDEC, one's
   complement). *Start the UPCL track here.*
7. **[upcl-cpu-fpu-simd.md](upcl-cpu-fpu-simd.md)** — Making UPCL generate
   complete frontends (Mode A) and using it *dynamically* (Mode B) to implement
   IEEE FPU and SIMD with shared lowerings across backends.
8. **[upcl-instruction-decoders.md](upcl-instruction-decoders.md)** — A
   CPU-agnostic instruction-decoder sub-language spanning MMIX → x86/VAX: fetch
   models, bit-field match, opcode tables, decode-time state + continuation
   (the agnostic basis for "prefixes"), and recursive operand decoders.
9. **[upcl-complex-cpu-ia64.md](upcl-complex-cpu-ia64.md)** — The capstone:
   describing IA-64/EPIC (bundles, predication, register stacking/rotation, NaT,
   speculation/ALAT, extended FP) — the stress test that exercises every
   capability above.

## Dependency notes

- The **`ICpuEmitter`** abstraction (doc 5, §3) is the single prerequisite that
  unblocks multiple backends, the interpreter, WASM, and UPCL's dynamic lowering
  (docs 7–9).
- **Context-keyed translation caching** appears in three places — system-mode
  ASIDs (doc 4 §8), the on-disk cache (doc 5 §6), and IA-64 CFM/RRB specialization
  (doc 9 §5) — and is the same mechanism each time.
- The **PC-dispatch switch** that doc 2 wants to remove for native speed is the
  same structure that makes a **WASM** backend feasible (doc 5 §8) — the abstract
  emitter lets each backend choose.

## Status of the codebase (as of writing)

- libcpu builds against **LLVM 8**, JIT-only; the userspace test harnesses run.
- **UPCL** parses and runs semantic analysis but does not yet drive codegen
  (`upcl/main.cpp` stops after sema); `upcl/cg/` generators exist; the subproject
  is disabled in the root build. UPCL needs only flex/bison + a C++ compiler — no
  LLVM — so it is the most readily buildable starting point.

These documents are design intent, not implemented features. Treat line/file
references as navigation aids that may drift.
