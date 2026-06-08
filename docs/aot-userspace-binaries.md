# Creating AOT Binaries with libcpu for Userspace Programs

> How to turn a guest userspace executable into a **self-contained native
> binary** by translating its code ahead-of-time with libcpu and linking it
> against **libnix** (the Unix syscall/runtime personality) and **libloader**
> (the executable parser). Read `ARCHITECTURE.md` first for the JIT model this
> builds on, and `docs/improving-dispatch.md` for why offline translation lets
> you afford heavier optimization.

---

## 0. Status & honesty up front

libcpu today is **JIT-only**. The translation backend ends in:

```cpp
// libcpu/interface.cpp — cpu_translate_function()
auto fp = reinterpret_cast<void*>(cpu->exec_engine->getFunctionAddress(func_name));
```

There is **no object-file or bitcode emission path** in the tree (verified: no
`TargetMachine::addPassesToEmitFile`, `WriteBitcode`, etc.). So "AOT" is not a
flag you flip — it is a small amount of new plumbing on top of the existing
frontend/IR pipeline plus a runtime built from the existing `test/` components.

This document specifies that plumbing. Throughout, items are tagged:

- **[EXISTS]** — present in the tree today, reusable as-is.
- **[BUILD]** — new code/changes this design requires.

The reference for the runtime behaviour is `test/m88k/run88.cpp` (the
OpenBSD/m88k userspace harness) together with `test/libnix` and
`test/libloader`.

---

## 1. What "AOT" means here

Two flavors, in increasing difficulty:

| Flavor | Idea | Indirect-branch story |
|--------|------|-----------------------|
| **A. Frozen-JIT / embedded translation** *(recommended)* | Run libcpu's tag→IR pipeline offline over the whole binary, emit the LLVM `Module` as a native object, and link it with a static runtime + libnix into an executable. | Keep libcpu's dispatch switch + a runtime fallback for targets discovered late. |
| **B. Pure static recompilation** | Resolve everything statically; emit straight-line native code with no dispatch. | Generally undecidable for real binaries (computed jumps, returns) — needs a fallback anyway. This is the historical goal in `TODO`, never completed. |

**This document targets Flavor A.** It reuses *all* of the existing frontend,
tagging, IR generation, register-file, and syscall machinery; the only genuinely
new core capability is "emit the module to a `.o` instead of JITing it."

```
            ┌──────────────────────── BUILD TIME (host) ─────────────────────────┐
guest ELF ─►│ libloader ─► guest image + entry/tstart/tsize                      │
            │ libcpu: cpu_tag(full discovery) ─► cpu_translate ─► LLVM Module     │
            │ [BUILD] emit Module ─► guest_code.o                                 │
            └────────────────────────────────────────────────────────────────────┘
                          │ link
                          ▼
            ┌──────────────────────── RUNTIME (native exe) ──────────────────────┐
            │ guest_code.o  +  libcpu runtime stub  +  libnix  +  libloader       │
            │   loop: call jitmain(); on TRAP ─► libnix syscall; on miss ─► fall  │
            └────────────────────────────────────────────────────────────────────┘
```

---

## 2. The runtime contract you are preserving

AOT works because the translated function already has a **stable, C-callable
ABI**, defined in `libcpu/function.cpp` (`cpu_create_function`):

```c
// signature of the translated function ("jitmain")
typedef int (*fp_t)(uint8_t *RAM, void *grf, void *frf, debug_function_t dbg);
```

It returns one of the `JIT_RETURN_*` codes (`libcpu/libcpu.h`):

| Return | Meaning | Runtime action |
|--------|---------|----------------|
| `JIT_RETURN_NOERR` | guest asked to stop | exit |
| `JIT_RETURN_FUNCNOTFOUND` | jumped to untranslated PC | translate/fallback (see §7) |
| `JIT_RETURN_TRAP` | guest executed a trap/syscall | **dispatch via libnix** (§6) |
| `JIT_RETURN_SINGLESTEP` | debug stepping | n/a for AOT |

The JIT run loop that drives this is `cpu_run` (`interface.cpp:355`) and the
trap-handling switch is in `run88.cpp`. **The AOT runtime stub is essentially
`run88.cpp`'s loop with the JIT-translation calls removed** (because the code is
already compiled into the binary).

---

## 3. The three moving parts and their seams

### 3.1 libcpu core — code → native object  **[EXISTS pipeline, BUILD emit]**

Tagging (`tag.cpp`), basic-block formation and IR emission (`translate_all.cpp`,
`translate.cpp`), register decode/spill (`function.cpp`), and optimization
(`optimize.cpp`) are all reused unchanged. Only the *sink* changes: instead of
`getFunctionAddress`, write `cpu->mod` to an object file (§5).

### 3.2 libloader — guest executable → memory image  **[EXISTS]**

`loader_init()` + `loader_load(mem_if, path)` parse the guest binary (ELF and
a.out personalities live under `test/libloader/elf` and the m88k loader) and
populate guest RAM through the `xec_mem_if_t` bridge, exposing entry point and
text start/size (`g_ahdr.entry`, `tstart`, `tsize` in `run88.cpp`). Used at
**build time** to know what to translate, and at **runtime** to lay out memory.

### 3.3 libnix + xec-compat — the syscall/runtime personality  **[EXISTS]**

This is the "intermediate layer" the task asks about. It is what makes the AOT
binary behave like a Unix process without a real guest kernel:

- **`xec_mem_if_t`** (`xec-compat/.../xec-mem-if`) — guest↔host address
  translation vtable. In `run88.cpp` it is a trivial `RAM + addr` mapping
  (`run88_mem_gtoh`/`htog`).
- **`xec_monitor_t`** (`xec_monitor_create`) — wraps the guest register file
  (`cpu->rf.grf`) + mem-if so syscall code can read/write guest registers and
  memory uniformly.
- **`xec_us_syscall_if_t`** — the userspace syscall dispatch interface;
  `xec_us_syscall_dispatch(us_syscall, monitor)` decodes the syscall number/args
  from guest state and services it.
- **Personality** — e.g. `obsd41_us_syscall_create(mem_if)` (OpenBSD 4.1) builds
  the syscall table and an env via `nix_env_create(mem_if)`. The `nix/` tree
  implements the actual POSIX/BSD/Linux calls on the host; `obsd41/` maps guest
  syscall numbers + errno to it.

The seam is one call: **`JIT_RETURN_TRAP` ⇒ `xec_us_syscall_dispatch(...)`**.
Nothing about that depends on JIT vs AOT.

---

## 4. The AOT build pipeline (step by step)

### Step 1 — Load the guest binary  **[EXISTS]**
```c
loader_init();
mem_if = make_mem_if();              // RAM + addr bridge
loader_load(mem_if, "guest.elf");    // fills RAM, yields entry/tstart/tsize
```

### Step 2 — Configure the CPU for *complete* discovery  **[EXISTS]**
For AOT you want to discover as much code as possible offline, so **turn the
DFS limit OFF** (the opposite of `run88.cpp`, which uses `TAG_LIMIT` for lazy
JIT):
```c
cpu = cpu_new(CPU_ARCH_M88K, CPU_FLAG_ENDIAN_BIG, 0);
cpu_set_flags_codegen(cpu, CPU_CODEGEN_OPTIMIZE);   // NOTE: no CPU_CODEGEN_TAG_LIMIT
cpu_set_flags_hint(cpu, CPU_HINT_TRAP_RETURNS_TWICE);
cpu_set_ram(cpu, RAM);
cpu->code_start = tstart; cpu->code_end = tstart + tsize; cpu->code_entry = entry;
```
With `TAG_LIMIT` off, libcpu also activates the **entry-point cache**
(`tag.cpp init_tagging`): `/tmp/libcpu-<sha1>.entries`. You can pre-seed this
file from a profiling run to capture indirect-branch targets that static
discovery misses (see §7).

### Step 3 — Tag all reachable code  **[EXISTS]**
```c
cpu_tag(cpu, cpu->code_entry);
// plus any known extra entry points (exported symbols from libloader,
// signal trampolines, profiled indirect targets)
```

### Step 4 — Generate IR for everything  **[EXISTS]**
```c
cpu_translate(cpu);   // builds the LLVM Module (frontend + optimize)
```
At this point `cpu->mod` contains the translated function(s). Because this runs
offline, you can afford aggressive optimization — and this is exactly where the
"giant dispatch switch" cost (see `docs/improving-dispatch.md`) is paid by the
*build*, not by every run.

### Step 5 — Emit a native object  **[BUILD]** (see §5)
```c
emit_object(cpu->mod, "guest_code.o");   // new function
```

### Step 6 — Link into a native executable  **[BUILD]**
```
cc guest_code.o aot_runtime.o -lcpu_rt -lnix -lloader -o guest_native
```
where `aot_runtime.o` is the static run loop (§6) and the libs are the existing
`test/libnix` / `test/libloader` built as static archives.

---

## 5. The one new core capability: object emission  **[BUILD]**

Add an AOT sibling to `cpu_translate_function`. Instead of JIT-compiling, run
the standard codegen passes into an object file via LLVM's `TargetMachine`:

```cpp
// libcpu/aot.cpp  [BUILD] — sketch (LLVM 8 API)
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/LegacyPassManager.h"

bool cpu_emit_object(cpu_t *cpu, const char *path) {
    auto triple = sys::getDefaultTargetTriple();   // host triple: AOT runs natively
    cpu->mod->setTargetTriple(triple);
    std::string err;
    auto target = TargetRegistry::lookupTarget(triple, err);
    TargetOptions opt;
    auto tm = target->createTargetMachine(triple, "generic", "", opt,
                                          Optional<Reloc::Model>(Reloc::PIC_));
    cpu->mod->setDataLayout(tm->createDataLayout());

    std::error_code ec;
    raw_fd_ostream dest(path, ec, sys::fs::F_None);
    legacy::PassManager pm;
    if (tm->addPassesToEmitFile(pm, dest, nullptr, TargetMachine::CGFT_ObjectFile))
        return false;          // target can't emit objects
    pm.run(*cpu->mod);
    dest.flush();
    return true;
}
```

Implementation notes / changes required:

- **Stable export symbol.** `cpu_create_function` hard-codes the name
  `"jitmain"` and caps functions at `func[1024]`. For AOT, give the entry
  function a fixed external name (e.g. `guest_main`) so the runtime can link to
  it; if you split per subroutine (per `docs/improving-dispatch.md` §3.2), emit
  unique external names and an exported entry table.
- **Don't build the JIT.** `cpu_new` currently constructs an `ExecutionEngine`
  to discover the data layout (FP80/FP128, endianness — `interface.cpp:188`).
  For an AOT build you still need a data layout; obtain it from the
  `TargetMachine` instead of the JIT so the object matches the host ABI.
- **PC/register storage.** `emit_decode_reg` bakes the *host address* of the
  register file into the IR as a constant (`ptr_PC` via `getIntToPtr`,
  `function.cpp:179`). That is fine for JIT (same process) but **wrong for an
  object file** that runs in a different process image. For AOT the register
  file must be passed in (it already is, as the `grf`/`frf` args) — audit and
  remove any baked-in host-pointer constants so the object is position- and
  process-independent.
- **Optimization.** Keep `CPU_CODEGEN_OPTIMIZE`; optionally run a heavier pass
  pipeline than the interactive JIT uses.

---

## 6. The static runtime stub  **[BUILD]** (mostly copied from `run88.cpp`)

The AOT executable needs a `main()` that sets up memory + libnix and then calls
the translated entry repeatedly, servicing traps. It is `run88.cpp`'s tail with
the `cpu_tag`/`cpu_translate` calls deleted (code is already compiled in):

```c
extern "C" int guest_main(uint8_t *RAM, void *grf, void *frf, debug_function_t);

int main(int argc, char **argv, char **envp) {
    xec_init(); obsd41_init(); loader_init();

    RAM     = malloc(RAM_SIZE);
    mem_if  = make_mem_if(RAM);
    us_sc   = obsd41_us_syscall_create(mem_if);     // libnix personality  [EXISTS]
    env     = nix_env_create(mem_if);               //                     [EXISTS]
    loader_load(mem_if, argv[1]);                    // lay out guest image [EXISTS]

    // guest register file: same struct the frontend's arch_*_init built
    grf = make_guest_regs();                         // PC=entry, SP, argv frame
    openbsd_m88k_setup_uframe(...);                  // argv/envp/stack     [EXISTS]
    monitor = xec_monitor_create(&guest_info, mem_if, grf, NULL);

    for (;;) {
        int rc = guest_main(RAM, grf, frf, NULL);    // <-- AOT code, no JIT
        switch (rc) {
        case JIT_RETURN_NOERR:        goto done;
        case JIT_RETURN_TRAP:
            if (is_exit_syscall(grf)) goto done;
            xec_us_syscall_dispatch(us_sc, monitor); // <-- libnix services it [EXISTS]
            break;
        case JIT_RETURN_FUNCNOTFOUND: handle_miss(grf); break;   // §7
        default:                      die("bad rc");
        }
    }
done:
    return guest_exit_code(grf);
}
```

Everything marked `[EXISTS]` here is reused verbatim from `test/libnix` /
`test/libloader`. The libnix layer is therefore the *intermediate layer*
between AOT-compiled guest code and the host OS: guest traps surface as
`JIT_RETURN_TRAP`, and `xec_us_syscall_dispatch` translates them into real host
syscalls with guest-correct argument marshalling and errno mapping.

---

## 7. The hard part: incomplete discovery & indirect branches

Static tagging cannot find every reachable instruction (computed jumps, returns
to dynamic addresses, jump tables, dlopen-style code). At AOT runtime a guest
jump to untranslated code returns `JIT_RETURN_FUNCNOTFOUND`. You must choose a
**fallback policy** — this is the central design decision (see §10):

1. **Hybrid AOT + JIT.** Keep a real `ExecutionEngine` in the runtime; on a
   miss, fall back to the existing `cpu_tag`/`cpu_translate`/`cpu_run` path for
   the new region. Largest binary, fully correct, reuses all current code.
2. **AOT + interpreter fallback.** Ship a small single-step interpreter (the
   `CPU_DEBUG_SINGLESTEP` translators already model "one instruction at a time")
   for cold/rare code; keep AOT for the hot statically-known code.
3. **Profile-guided closed-world.** Run the program once under the JIT with the
   **entry-point cache** enabled (`tag.cpp` writes `libcpu-<sha1>.entries`),
   capturing the indirect targets actually taken; feed that file into the AOT
   tag phase (§4 Step 2/3) so discovery is "complete enough." Misses then abort
   rather than translate. Smallest, fastest binary; not sound for inputs that
   reach new code paths.

The entry-point cache mechanism that makes option 3 possible **[EXISTS]** — it
was designed for exactly this (persisting discovered entries across runs). Note
its current 32-bit address assumption in the cache file format if you target a
64-bit guest.

---

## 8. Memory image: embed vs. load-at-startup

- **Load at startup (simplest):** keep `loader_load` in the runtime; the AOT
  binary takes the guest ELF as `argv[1]`, exactly like `run88.cpp`. The binary
  is "native code for this guest program's logic" but still reads the original
  file for data. Good first milestone.
- **Embed (truly standalone):** at build time, snapshot the loaded guest image
  (text+data segments) and emit it as an initialized array linked into the
  binary; the runtime `memcpy`s it into `RAM` at startup. Produces a single
  self-contained executable with no external guest file. This is a `[BUILD]`
  extension of Step 1.

---

## 9. Worked milestone plan

```
M1  Object emission           : implement cpu_emit_object (§5); verify a tiny
                                 m88k function round-trips to a .o and links.
M2  Static runtime            : port run88.cpp loop to aot_runtime (§6), linking
                                 libnix + libloader as static archives.
M3  Pointer-independence audit: remove baked-in host pointers from IR (§5) so
                                 the .o is process-independent.
M4  End-to-end (load-at-start): AOT-translate fib/sieve guest, run it natively,
                                 service write()/exit() through libnix.
M5  Fallback policy           : pick + implement one of §7 (start with hybrid).
M6  Embedded image            : optional, for fully standalone binaries (§8).
```

Each milestone is independently testable against the existing `test/m88k`
programs and `test/scripts/m88k_fib.sh`.

---

## 10. A design decision worth your input

The fallback for **untranslated indirect targets** (§7) is the choice that
defines what an AOT libcpu binary *is* — a fast-but-sound hybrid, a small-but-
risky closed-world image, or something in between. A natural seam is the
runtime's miss handler:

```c
// aot_runtime.c  [BUILD]
// Called when guest_main() returns JIT_RETURN_FUNCNOTFOUND: the guest jumped to
// a PC we did not translate ahead of time. Decide how a "native" libcpu binary
// behaves when it meets code it has never seen.
static void handle_miss(cpu_t *cpu /* or raw grf */, addr_t pc) {
    // TODO(design): choose ONE policy (see §7):
    //   (1) HYBRID   : cpu_tag(cpu, pc); cpu_flush(cpu); cpu_translate(cpu);  // keep a JIT engine alive
    //   (2) INTERP   : run a single-step interpreter until we re-enter known code
    //   (3) CLOSED   : fprintf(stderr, "unreached code @%#llx\n", pc); abort();
    // Trade-offs: (1) correct + big + needs LLVM at runtime; (2) correct + slow
    //             cold paths + no LLVM; (3) tiny + fast + unsound for new inputs.
}
```

The trade-offs to weigh: do you want these binaries to be **fully sound** for
any input (hybrid/interpreter, larger, LLVM or interpreter in the runtime), or
**small and fast** for a known workload (closed-world, profile-seeded entry
cache, aborts on surprise)? That single choice also decides whether the AOT
runtime must link LLVM at all.

---

## 11. Summary

- libcpu's frontend/tagging/IR pipeline is reusable as-is; AOT needs **one new
  sink** (`cpu_emit_object`, §5) plus a **pointer-independence audit** of the
  generated IR.
- **libloader** [EXISTS] supplies the code region and memory image; **libnix +
  xec-compat** [EXISTS] are the intermediate runtime layer that services guest
  syscalls, joined at the single `JIT_RETURN_TRAP ⇒ xec_us_syscall_dispatch`
  seam.
- The **static runtime stub** is `run88.cpp`'s loop minus the JIT calls.
- The **fallback for untranslated indirect targets** (§7/§10) is the key design
  decision and determines soundness vs. size.
- Practical path: Flavor A (frozen-JIT/embedded), milestones M1–M6 in §9.
