# libcpu — Architecture & Codebase Guide

> A developer-oriented map of how libcpu is organized and how its pieces fit
> together. Pair this with `README.md` (build/usage) and `TODO` (historical
> design notes). Line references point at the source as of this writing and may
> drift; treat them as starting points, not contracts.

---

## 1. What libcpu is

libcpu is a library that **emulates several CPU architectures by recompiling
guest machine code into native code at runtime**. It does this by:

1. Providing a small per-architecture **frontend** that knows how to *decode*,
   *classify*, and *translate* one guest instruction into **LLVM IR**.
2. Handing that IR to **LLVM**, which acts as the **backend** — running
   optimization passes and JIT-compiling the IR to host-native machine code.

The result is a dynamic recompiler: guest code is discovered, translated to a
native function, and executed; unknown code is translated lazily on demand.

The project is designed to support, in principle:

- **User-mode** and **system** emulation,
- **Dynamic** recompilation (JIT, the working path), and
- **Static** recompilation (ahead-of-time; historical, see `TODO`).

License: 2-clause BSD. Origin: the "libcpu developers", ~2009–2010, with
later maintenance commits to keep it building against modern LLVM (currently
LLVM 8.x).

---

## 2. Top-level layout

```
libcpu/
├── libcpu/          Core engine — architecture-agnostic (~5,200 LOC C++)
├── arch/            Guest CPU frontends (one subdir per architecture)
│   ├── 6502/        MOS 6502        (simplest; best place to learn the pattern)
│   ├── arm/         ARM
│   ├── m68k/        Motorola 68000
│   ├── m88k/        Motorola 88000
│   ├── mips/        MIPS
│   ├── x86/         x86 (internally "8086")
│   └── fapra/       A teaching/demo architecture
├── upcl/            "Universal Processor Crafting Language" compiler
│                    (declarative .def → frontend generator; DISABLED in build)
├── test/            Per-arch test programs + runtime support
│   ├── libloader/   Guest binary/image loader used by tests
│   ├── libnix/      Minimal syscall/runtime shim for user-mode guests
│   └── <arch>/      Architecture-specific test programs
├── CMake/           LLVM discovery, config.h generation, Apple universal build
├── images/          Logo asset
├── CMakeLists.txt   Top-level build
├── README.md / TODO / LICENSE.txt
```

### Build entry points

- `CMakeLists.txt` (root) selects which guest architectures to build via
  `GUEST_ARCHITECTURES` and pulls in LLVM through `CMake/FindLLVM.cmake`.
- Each arch builds only if its `arch/<name>/` directory exists; tests build
  only if `test/<name>/` exists.
- `upcl` is intentionally **not built** (`ADD_SUBDIRECTORY(upcl)` is commented
  out near the bottom of the root `CMakeLists.txt`).
- MSVC has a dedicated include shim at `libcpu/win32/msvc`.

---

## 3. The two-layer design: frontend vs. backend

The central idea is a clean split:

| Layer | Responsibility | Lives in |
|-------|----------------|----------|
| **Frontend** (per arch) | Decode one instruction; say what kind of control flow it is; emit LLVM IR for its semantics | `arch/<name>/*` |
| **Core** (generic) | Discover reachable code, cut it into basic blocks, drive translation, run the JIT, manage state | `libcpu/*` |
| **Backend** | Optimize IR, generate native code, execute it | LLVM (external) |

A frontend plugs into the core by filling in an `arch_func_t` — a struct of
function pointers (see `libcpu/libcpu.h:49`). Each architecture exports one
global instance, e.g. `arch_func_6502`, which `cpu_new()` selects
(`libcpu/interface.cpp:98`).

### The `arch_func_t` contract

```c
typedef struct {
    fp_init            init;            // set up arch info + register file
    fp_done            done;            // tear down
    fp_get_pc          get_pc;          // read guest PC from register file   [deprecated path]
    fp_emit_decode_reg emit_decode_reg; // emit IR to load regs into SSA values
    fp_spill_reg_state spill_reg_state; // emit IR to write SSA values back to memory
    fp_tag_instr       tag_instr;       // classify instruction (see §5)
    fp_disasm_instr    disasm_instr;    // human-readable disassembly (debug/logging)
    fp_translate_cond  translate_cond;  // emit IR computing a branch condition
    fp_translate_instr translate_instr; // emit IR for the instruction's semantics
    // idbg (interactive debugger) support: get_psr / get_reg / get_fp_reg  [deprecated path]
} arch_func_t;
```

A new architecture is, in essence, *implementing these ~11 callbacks*.

### Anatomy of a frontend (using 6502)

Each `arch/<name>/` directory typically contains:

- `*_arch.cpp` — `init`/`done`, register-file allocation, fills `cpu_archinfo_t`
  (name, endianness, byte/word/address sizes, register counts, flag layout).
  See `arch/6502/6502_arch.cpp`.
- `*_translate.cpp` — `translate_instr` / `translate_cond`: the actual IR
  emission for each opcode, written against the frontend helper API (§6).
- `*_tag.cpp` — `tag_instr`: control-flow classification (some arches fold this
  elsewhere).
- `*_disasm.cpp` — disassembler for logging and the debugger.
- `*_isa.h` / `*_internal.h` / `*_types.h` — opcode tables, register structs,
  shift constants.

---

## 4. The core data structure: `cpu_t`

Everything hangs off `cpu_t` (`libcpu/libcpu.h:209`). Key fields:

- **`info` (`cpu_archinfo_t`)** — static description of the guest: name,
  endianness, `byte_size` / `word_size` / `address_size` / `psr_size`,
  register layout, and flag layout.
- **`rf` (`cpu_archrf_t`)** — pointers to the live guest register file
  (`grf`/`frf`/`vrf` and a generic `storage`).
- **`f` (`arch_func_t`)** — the frontend callbacks.
- **LLVM handles** — `ctx` (LLVMContext), `mod` (Module), `exec_engine`
  (ExecutionEngine / JIT), and the current function `cur_func`.
- **Translated-code tables** — `func[1024]` (LLVM `Function*`) and `fp[1024]`
  (the JIT-compiled native pointers). Note the **fixed cap of 1024**.
- **`func_bb` (`funcbb_map`)** — `Function* → (guest addr → BasicBlock*)`; the
  fast basic-block lookup cache.
- **`tag`** — the per-address tag array produced by the tagging pass (§5).
- **Exploded register/flag pointers** — `ptr_gpr`, `ptr_fpr`, `ptr_xr`, and the
  individual flag pointers `ptr_N` / `ptr_V` / `ptr_Z` / `ptr_C`. During
  translation, status flags are kept as separate 1-bit SSA values so the
  optimizer can treat each independently, and are only re-packed into the PSR
  when guest code reads it.

> **Note — dual register models.** The code is mid-migration. A *deprecated*
> model uses four fixed register classes (`CPU_REG_GPR/FPR/VR/XR`, with
> `register_count[4]` / `register_size[4]`). A *newer* model uses a flat
> `cpu_register_layout_t` table (`register_layout` + `register_count2`). Both
> coexist, bracketed by `// @@@BEGIN_DEPRECATION` / `// @@@END_DEPRECATION`
> markers. Frontends today still populate the deprecated fields.

---

## 5. Code discovery — the tagging pass (`libcpu/tag.cpp`)

Before any IR is emitted, libcpu must figure out *where the code is* and *how
control flows through it*. This is the **tagging** pass: a recursive
depth-first walk over reachable instructions, annotating each address with a
bitmask `tag_t` (defined in `libcpu/tag.h`).

### Tag bits (selected)

Returned by a frontend's `tag_instr`:

| Tag | Meaning |
|-----|---------|
| `TAG_CONTINUE` | does not affect control flow |
| `TAG_CALL` | subroutine call |
| `TAG_RET` | return from subroutine |
| `TAG_BRANCH` | jump/branch |
| `TAG_TRAP` | software trap / syscall |
| `TAG_CONDITIONAL` | predicated (combine with the above) |
| `TAG_DELAY_SLOT` | has a delay slot (MIPS/SPARC-style) |

Derived/internal tags set by the core:

| Tag | Meaning |
|-----|---------|
| `TAG_CODE` | a reachable instruction lives here |
| `TAG_BRANCH_TARGET` | something branches here |
| `TAG_SUBROUTINE` | something calls here |
| `TAG_AFTER_CALL` / `TAG_AFTER_COND` / `TAG_AFTER_TRAP` | fall-through points |
| `TAG_ENTRY` | client-requested entry point |
| `TAG_TRANSLATED` | already lowered to IR |

### How it walks (`tag_recursive`, `tag.cpp:116`)

Starting from a PC, it loops instruction-by-instruction. For each, it calls
`f.tag_instr` to learn the instruction's class and its `new_pc` (branch/call
target) and `next_pc` (fall-through). It then:

- marks calls' targets `TAG_SUBROUTINE` and recurses into them,
- marks branch targets `TAG_BRANCH_TARGET` and recurses,
- stops a straight-line run at unconditional branches and returns,
- handles traps specially based on client *hints* (`CPU_HINT_TRAP_RETURNS`,
  `..._TWICE`) — important for user-mode syscalls that return to the next
  instruction (and, for OpenBSD/M88K, sometimes the one after that).

### Two operating modes

1. **Tag-limit mode** (`CPU_CODEGEN_TAG_LIMIT`): bounds the DFS depth
   (`LIMIT_TAGGING_DFS`) so only a window of code is discovered ahead of
   execution — the rest is tagged/translated lazily when first reached.
2. **Entry-cache mode** (default): SHA-1–hashes the code region
   (`sha1.cpp`), and persists discovered entry points to
   `/tmp/libcpu-<digest>.entries`. On the next run, those entries are replayed,
   recovering indirect-jump targets that static analysis can't find. (Windows
   uses `GetTempPathA`.)

> **Caveat:** the cache format hardwires **4-byte** addresses
> (`tag.cpp` read/write loops), so guest address spaces wider than 32 bits are
> not represented faithfully by the cache.

---

## 6. Translation — turning tagged code into LLVM IR

### Whole-region translation (`libcpu/translate_all.cpp`)

`cpu_translate_all` runs in two phases:

1. **Basic-block creation.** Scan `[code_start, code_end)`; wherever
   `is_start_of_basicblock()` is true (a branch/call target, a fall-through
   after a call/cond/trap, or a client entry — see `basicblock.cpp:18`) and the
   block isn't already translated, create an LLVM `BasicBlock` and register it
   in `func_bb`.
2. **The dispatch switch.** Create a `dispatch` block that loads the guest PC
   and `switch`es on it, with one `case` per basic block. This is the
   linchpin: native code cannot use guest PCs as native jump targets, so any
   control transfer whose destination isn't statically known stores the guest
   PC and branches to `dispatch`, which maps PC → the right `BasicBlock`.
3. For each basic block, walk its instructions and call `translate_instr`
   (below), linking fall-through edges where needed.

### Single-instruction translation (`libcpu/translate.cpp`)

`translate_instr` is the generic glue that wires one instruction's IR into the
CFG, calling the frontend's `translate_cond` / `translate_instr` and handling:

- **Plain instructions** — emit semantics into the current block; if it's a
  branch/call/ret, branch to `bb_target`; if a trap, branch to `bb_trap`.
- **Conditional instructions** — emit `if (cond) → taken else → not-taken`
  using an internal `BB_TYPE_COND` block.
- **Delay slots** — the tricky case: the delay-slot instruction must execute
  on *both* the taken and not-taken paths, so the code emits the branch
  instruction and its delay-slot instruction into the correct blocks in the
  right order (see `translate.cpp:36`).

It returns the block where execution continues (for the caller to link), or
`NULL` if the instruction always transfers control away.

### Basic-block helpers (`libcpu/basicblock.cpp`)

- `create_basicblock` names blocks `<type><hexaddr>` and caches `NORMAL` blocks
  in `func_bb`.
- `lookup_basicblock` finds a block by PC; if it's *not* in the current
  function, it synthesizes an "external" block that **stores the PC and returns
  to the dispatcher's return path** — this is how execution exits a translated
  function when it jumps to not-yet-translated code (the `cpu_run` loop then
  tags and translates that target).

### Other translation modes

`cpu_translate_function` (`interface.cpp:284`) chooses among:

- `cpu_translate_all` — normal whole-region translation,
- `cpu_translate_singlestep` — one instruction at a time (`CPU_DEBUG_SINGLESTEP`),
- `cpu_translate_singlestep_bb` — one basic block at a time
  (`CPU_DEBUG_SINGLESTEP_BB`),

then verifies the function, optionally optimizes (`optimize.cpp`), and JIT
compiles it via `exec_engine->getFunctionAddress()`.

---

## 7. The frontend helper API (`libcpu/frontend.h` / `frontend.cpp`)

Writing raw LLVM IR is verbose, so frontends are written against a layer of
helpers and macros that read almost like assembly:

- **Register access:** `arch_get_reg` / `arch_put_reg`,
  `arch_load_fp_reg` / `arch_store_fp_reg`.
- **Memory:** `arch_load8/16/32`, `arch_store8/16/32` (alignment- and
  endianness-aware).
- **Arithmetic & flags:** `arch_adc` (add-with-carry that also produces flag
  values), `arch_shiftrotate`, `arch_bswap`, `arch_ctlz` / `arch_cttz`.
- **Flag packing:** `arch_flags_encode` / `arch_flags_decode` convert between
  the exploded per-bit flag SSA values and the packed PSR representation, using
  the architecture's `cpu_flags_layout_t` table.
- **FP:** `arch_cast_fp32/64/80/128`, `arch_sqrt`.
- **Convenience macros:** `LOAD` / `STORE`, `CONST8/16/32/64`, `TRUNC*`,
  `ZEXT*`, `SIZE`, and `_CTX()` for the LLVMContext.

FP80/FP128 support is detected at runtime by inspecting the JIT's data layout
(`interface.cpp:196`); memory swapping (`CPU_FLAG_SWAPMEM`) is enabled when host
and guest endianness differ.

---

## 8. Execution — the run loop (`libcpu/interface.cpp`)

The public API (declared in `libcpu/libcpu.h`):

```c
cpu_t *cpu_new(arch, flags, arch_flags);   // allocate, init frontend, init LLVM
void   cpu_set_ram(cpu, RAM);              // point at guest memory
void   cpu_set_flags_codegen/debug/hint(); // tune behavior
void   cpu_tag(cpu, pc);                   // discover code from an entry point
void   cpu_translate(cpu);                 // force translation of dirty tags
int    cpu_run(cpu, debug_function);       // tag + translate + execute, looping
void   cpu_flush(cpu);                     // drop translated code
int    cpu_debugger(cpu, debug_function);  // interactive debugger (idbg.cpp)
void   cpu_print_statistics(cpu);          // dump phase timers
```

### `cpu_run` (`interface.cpp:355`)

The loop:

1. Translate any dirty code, read the current guest PC.
2. Try each compiled function `fp[i]` in turn, calling it as
   `int fn(uint8_t *RAM, void *grf, void *frf, debug_function_t)`.
3. A function returns one of `JIT_RETURN_*`:
   - `NOERR` / `SINGLESTEP` / `TRAP` → return to caller.
   - `FUNCNOTFOUND` → the guest jumped to code we haven't translated; if PC is
     inside the code area and advanced, re-loop; otherwise tag the new PC and
     translate it.

Phase timers (`TIMER_TAG/FE/BE/RUN`) are accumulated around each stage and
dumped by `cpu_print_statistics`.

> **Known sharp edge:** if tagging a new PC yields no translatable progress and
> the PC never changes, the loop has no explicit progress guard and can spin.
> Worth keeping in mind when bringing up a new frontend.

---

## 9. Supporting modules in `libcpu/`

| File | Purpose |
|------|---------|
| `function.cpp` | Builds the standard JIT function skeleton (`jitmain`) and its entry / return / trap blocks; emits register decode/spill. |
| `optimize.cpp` | Runs LLVM optimization passes over the module before codegen. |
| `fp.cpp` / `fp_types.h` | Floating-point helpers and type selection. |
| `sha1.cpp` | Code-region hashing for the entry-point cache. |
| `disasm.cpp` | Generic disassembly entry that calls the frontend's `disasm_instr`. |
| `idbg.cpp` | Interactive debugger (largest single file): inspect registers, step, etc. |
| `stat.cpp` / `timings.cpp` | Statistics and phase timing. |
| `tag.cpp` | Code discovery (see §5). |
| `platform.h` / `types.h` / `defines.h` | Portability shims, `addr_t`/`tag_t`, common macros. |
| `win32/msvc/` | MSVC compatibility headers. |

---

## 10. The `upcl` subproject (declarative frontends)

`upcl/` is a separate compiler for a domain-specific language that *describes* a
CPU declaratively, with the goal of **generating** a frontend instead of
hand-writing one. Compare `arch/6502/6502_arch.cpp` (hand-written register/flag
setup) with `upcl/examples/6502.def`:

```
arch "6502" {
    name "MOS 6502";
    endian little;
    byte_size 8;  word_size 8;  psr_size 8;  address_size 16;
    register_file {
        group R { [ #i8 A ], [ #i8 X ], [ #i8 Y ], [ #i8 S ] }
        group S {
            [ #i16 pc -> %PC ],
            [ #i8 P -> %PSR #i1 explicit ( N->%N:V->%V:0:B:D:I:Z->%Z:C->%C ) ]
        }
    }
}
```

`upcl/` has its own AST (`ast/`), semantic analysis (`sema/`, `c/`), code
generation (`cg/`), a lex/yacc grammar, an EBNF spec (`docs/ebnf.txt`), and
example definitions for 6502, 8086/80386, m68k, m88k, mips32/64, pdp1, sparc.

**Status:** it is **not part of the default build** (commented out in the root
`CMakeLists.txt`). Treat it as an experimental / aspirational generator path,
not a supported production component.

---

## 11. Testing

- `test/<arch>/` holds guest programs per architecture.
- `test/libloader/` loads guest binaries/images; `test/libnix/` provides a
  minimal runtime/syscall shim for user-mode guests. Both build on non-MSVC
  platforms (root `CMakeLists.txt`).
- `test/scripts/` holds runnable scripts; e.g. `README.md` points to
  `./test/scripts/8086.sh` for the x86 front-end.
- Extra cross-cutting tests are selected via `GUEST_EXTRA_TESTS`
  (`multi`, plus `next68k` on Apple).

---

## 12. Practical notes & gotchas

- **LLVM API churn is the dominant maintenance cost.** Recent history is almost
  entirely "build against LLVM 8.x" fixes (module lifetime via
  `unique_ptr` + `EngineBuilder`, `getFunctionAddress()` instead of
  `getPointerToFunction()`, asm-printer init). Expect to touch
  `interface.cpp` / `optimize.cpp` when moving LLVM versions.
- **Fixed capacities.** `func[1024]` / `fp[1024]` cap the number of translated
  functions; overflow trips an `assert`.
- **`exit(1)` inside library code.** `cpu_new`'s arch switch and the
  entry-cache open path call `exit(1)` on failure — inconvenient for an
  embeddable library; callers can't recover.
- **`x86` ≡ "8086" internally** — the enum is `CPU_ARCH_8086`, dispatched via
  `arch_func_8086`.
- **Old toolchain floor.** `CMAKE_MINIMUM_REQUIRED(VERSION 2.8)` plus LLVM 8.
- **Endianness/FP capabilities are runtime-detected** from the JIT data layout,
  not assumed.

---

## 13. Where to start reading, by goal

| Goal | Start here |
|------|-----------|
| Understand the public API & lifecycle | `libcpu/libcpu.h`, then `libcpu/interface.cpp` |
| Understand code discovery | `libcpu/tag.h`, then `libcpu/tag.cpp` |
| Understand IR generation flow | `libcpu/translate_all.cpp` → `libcpu/translate.cpp` → `libcpu/basicblock.cpp` |
| Learn the frontend pattern | `arch/6502/6502_arch.cpp` + `arch/6502/6502_translate.cpp`, with `libcpu/frontend.h` open alongside |
| Add a new architecture | Implement `arch_func_t`; copy the 6502 frontend as a skeleton |
| Explore declarative frontends | `upcl/examples/*.def` + `upcl/docs/ebnf.txt` |

---

### One-paragraph summary

libcpu is a **dynamic recompiler**: per-architecture **frontends** (`arch/`)
decode and classify guest instructions and emit **LLVM IR** through a shared
helper layer (`libcpu/frontend.*`); the **generic core** (`libcpu/`) discovers
reachable code by **tagging** (`tag.cpp`), cuts it into LLVM basic blocks routed
through a **PC dispatch switch** (`translate_all.cpp`), JIT-compiles each
function, and executes it in a **tag-translate-run loop** (`interface.cpp`),
falling back to on-demand translation whenever the guest jumps somewhere not yet
compiled. An experimental, currently-disabled **`upcl`** compiler aims to
generate those frontends from declarative `.def` CPU descriptions.
