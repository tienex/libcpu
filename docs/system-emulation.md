# Moving libcpu to System-Level (Full-System) Emulation

> What it takes to grow libcpu from a userspace recompiler into a full-system
> emulator: a software MMU, guest-vectored exceptions, asynchronous interrupts,
> privilege levels and control registers, and a translation cache that survives
> paging and self-modifying code. Read `ARCHITECTURE.md` (translation model) and
> `docs/aot-userspace-binaries.md` (the userspace trap model this replaces)
> first.

---

## 0. Status & scope

The README states the intent — *"libcpu is supposed to be able to do user mode
and system emulation"* — but **only userspace is implemented today.** In
userspace mode the host services guest syscalls (`JIT_RETURN_TRAP ⇒
xec_us_syscall_dispatch`, see the AOT doc). System mode is fundamentally
different: there is **no host kernel behind the guest**. The guest runs its own
kernel, so the emulator must model the privileged machine itself — address
translation, traps that vector *into guest code*, interrupts, and devices.

Items below are tagged **[EXISTS]** (reusable today) and **[BUILD]** (new work).

### The defining difference, in one line

> **Userspace:** a trap *leaves* the guest and is serviced by the host.
> **System:** a trap/fault/interrupt *stays inside* the guest and vectors to the
> guest's own handler, after the CPU model saves state and changes privilege.

---

## 1. Gap analysis — what exists vs. what's missing

| Capability | Today | Needed for system emulation |
|------------|-------|-----------------------------|
| **Memory access** | Flat host buffer, **direct indexing**: `GetElementPtrInst::CreateInBounds(cpu->ptr_RAM, addr)` in `frontend.cpp`. No translation, no MMIO. Byte/half access synthesized from word ops. | Software MMU: virtual→physical translation, RAM vs. MMIO dispatch, alignment/permission faults. |
| **Address space** | One flat `RAM` with `code_start/code_end`. | Physical address space with regions + per-context virtual mapping (page tables / TLB). |
| **Traps** | `JIT_RETURN_TRAP` returns to host. | Synchronous **exceptions** that vector to guest handlers (save PC/PSR, switch mode, jump to vector). |
| **Interrupts** | None. | Asynchronous IRQ delivery at safe points; pending-interrupt checks. |
| **Privilege** | PSR has mode bits but core never checks them; privileged ops untranslated. | User/supervisor modes; privileged-instruction faults; mode switches. |
| **Control/system registers** | Not modeled in core (MIPS CP0/TLB exist only in the **disassembler**). | System register file + frontend hooks (CP0, m88k control regs, ARM CPSR modes, x86 CR/segments). |
| **Translation cache** | Tags one static `[code_start,code_end)` **physical** region once; assumes code never changes. | Cache keyed by physical addr (or virtual+ASID); invalidation on page remap, TLB flush, icache flush, and SMC. |
| **Devices/platform** | None. | Interrupt controller, timer, UART, board model. |
| **Timing** | Wall-clock timers only (`timings.cpp`). | Instruction/cycle budget to bound execution between interrupt checks. |
| **Page-size metadata** | `min/max/default_page_size` fields exist in `cpu_archinfo_t` **[EXISTS]** but are unused. | Drive the MMU page granularity. |

The single largest change is **memory** (§2); everything else composes around it.

---

## 2. Subsystem 1 — the software MMU (the big one)  **[BUILD]**

Today every guest load/store compiles to a direct index into the host `RAM`
pointer. For system emulation, *every* access must (a) translate a virtual
address to physical via the guest's paging, (b) check permissions and alignment
(raising a fault if violated), and (c) route physical accesses either to RAM or
to a device (MMIO).

### 2.1 The access path

```
guest VA ─► [TLB lookup] ─hit─► host ptr ─► load/store
                │ miss
                ▼
          page-table walk (arch-specific) ─fault─► raise exception (§3)
                │ ok
                ▼
          fill TLB; classify PA: RAM ─► host ptr | MMIO ─► device callback (§6)
```

### 2.2 Two implementation strategies — *the key performance decision* (see §11)

- **Inline software-TLB fast path (QEMU "softmmu" style).** Emit IR that probes
  a small per-CPU TLB array inline; on hit (the overwhelmingly common case) the
  access is a few instructions; on miss it calls a helper. Fast, but every
  `arch_load*`/`arch_store*` becomes a multi-block sequence with a slow-path
  branch, enlarging generated code and complicating IR generation.
- **Helper call-out per access.** Every load/store calls a C runtime function
  `mmu_read(cpu, va, size, &fault)` / `mmu_write(...)`. Simple to implement and
  to make correct; significantly slower (call overhead on the hottest path).

A common path is **call-out first for correctness, then add the inline TLB** as
an optimization once the rest of the system works.

### 2.3 Where it changes in the tree

- `libcpu/frontend.cpp` — `arch_load8/16/32`, `arch_store8/16/32`,
  `arch_load32_aligned`, `arch_store32_aligned` are rewritten to go through the
  MMU instead of `CreateInBounds(cpu->ptr_RAM, ...)`. Today byte/half accesses
  are emulated via aligned word load + mask (lines ~230–260); softmmu needs
  genuine sized accesses (and must fault on the actual access width/alignment).
- `libcpu/libcpu.h` — `cpu_t` gains a `tlb`, a physical memory map, and an MMU
  vtable; `cpu_archinfo_t` page-size fields finally get used.
- New `arch_func_t` hooks for the arch-specific page-table walk and TLB-managing
  instructions (§5).

### 2.4 New client API  **[BUILD]**
```c
// register a physical memory region (RAM or device-backed MMIO)
API_FUNC void cpu_map_ram   (cpu_t*, paddr_t base, size_t len, uint8_t *host);
API_FUNC void cpu_map_mmio  (cpu_t*, paddr_t base, size_t len, mmio_ops_t *ops);
```
`cpu_set_ram()` **[EXISTS]** becomes a special case of `cpu_map_ram`.

---

## 3. Subsystem 2 — exceptions that vector into the guest  **[BUILD]**

In system mode a fault (page fault, illegal/privileged instruction, alignment,
arithmetic, syscall) must **not** return to the host. The CPU model must:

1. Save the faulting PC (and sometimes next PC) and the cause/fault info into
   the architected exception registers.
2. Save and update the PSR (enter supervisor mode, mask interrupts as the ISA
   dictates).
3. Set PC to the architected **exception vector** and continue executing — now
   inside the guest's handler.

### 3.1 The hard part: faults are *mid-instruction*

A load can fault after the instruction has already updated some state. The
exception model must present a precise architectural state (the ISA's exception
semantics — restart vs. continue). Practically this means:

- Memory-access helpers signal a fault to generated code, which must branch to
  an **exception-entry block** rather than completing.
- This is a new generic control-flow target analogous to `bb_trap` in
  `function.cpp` (`cpu_create_function` already builds `bb_ret`/`bb_trap`) — but
  instead of returning, it runs an arch-emitted "enter exception" sequence and
  re-enters dispatch at the vector.

### 3.2 Replaces the userspace trap path

`TAG_TRAP` (`tag.h`) currently means "return to host." In system mode the same
instruction class **vectors internally**. Keep `JIT_RETURN_TRAP` only for
emulator escapes (debugger, host-assisted I/O); ordinary guest syscalls become
just another vectored exception handled by the guest kernel.

### 3.3 New `arch_func_t` hook
```c
// emit IR that enters the guest exception with cause/vector, updating PSR/PC.
typedef void (*fp_emit_take_exception)(cpu_t*, BasicBlock*, exc_cause_t, ...);
```

---

## 4. Subsystem 3 — asynchronous interrupts  **[BUILD]**

Devices and timers raise IRQs that must be delivered when the guest is
interruptible (interrupts enabled in PSR, at an instruction boundary). The
recompiler can't preempt mid-block, so:

- Deliver interrupts at **block boundaries** (the natural safe points). Generated
  code checks an `interrupt_pending` flag at block entry (or the run loop checks
  between block executions and re-vectors).
- Add an instruction/cycle **budget** (§7) so long-running guest loops still
  reach a check point.
- Provide a client API to raise/lower IRQ lines:
```c
API_FUNC void cpu_raise_irq(cpu_t*, int line);
API_FUNC void cpu_lower_irq(cpu_t*, int line);
```
Delivery reuses the exception-entry machinery (§3) with an async cause.

---

## 5. Subsystem 4 — privilege levels & system registers  **[BUILD]**

- **Modes.** Model at least user/supervisor. Track current mode in the PSR; the
  MMU and instruction decoder consult it.
- **Privileged-instruction faults.** Privileged ops executed in user mode must
  raise an exception (§3) rather than execute. Frontends currently translate
  unconditionally; they need a mode check or a generic "privileged" tag.
- **System register file.** CP0 (MIPS), control registers (m88k), CPSR/SPSR and
  banked regs (ARM), CRx/segment machinery (x86). These do not fit the
  GPR/FPR/VR/XR model cleanly — extend the register layout (`cpu_register_layout_t`
  is the newer, flexible model **[EXISTS]**) and add hooks for reads/writes that
  have side effects (e.g. writing a TLB index, flushing caches).
- **TLB-managing instructions.** MIPS `TLBR/TLBWI/TLBWR/TLBP` are recognized by
  the disassembler **[EXISTS]** but not translated; they must become real
  operations on the MMU's TLB.

### New `arch_func_t` hooks
```c
typedef int  (*fp_translate_priv_instr)(cpu_t*, addr_t, BasicBlock*); // privileged ops
typedef bool (*fp_mmu_translate)(cpu_t*, vaddr_t, int rw, int mode, paddr_t*, exc_cause_t*);
typedef void (*fp_mmu_flush)(cpu_t*, int scope);                      // TLB/asid flush
```

---

## 6. Subsystem 5 — devices, MMIO & a platform/board model  **[BUILD]**

System emulation needs at least an interrupt controller, a timer, and a console
UART to boot anything. The core's job is only the **MMIO seam**; the board is a
client (like QEMU machine models):

```c
typedef struct {
    uint64_t (*read )(void *opaque, paddr_t off, unsigned size);
    void     (*write)(void *opaque, paddr_t off, unsigned size, uint64_t val);
    void     *opaque;
} mmio_ops_t;
```
The physical memory map (§2.4) dispatches RAM accesses to host memory and MMIO
ranges to `mmio_ops_t`. Device IRQs feed `cpu_raise_irq` (§4). Keep devices
*outside* `libcpu/` (e.g. a new `system/` or `board/` tree) so the core stays
architecture/runtime-agnostic.

---

## 7. Subsystem 6 — execution budget & the run loop  **[BUILD]**

`cpu_run` (`interface.cpp:355`) currently loops "call FP → read PC → translate
on miss" with no notion of bounded execution. System mode needs:

- An **instruction/cycle budget** decremented per block; when it hits zero,
  return to the loop so timers advance and interrupts are checked.
- **Interrupt check** before re-entering translated code; if pending and
  enabled, vector (§3/§4).
- **Device/timer servicing** between budget windows.

Sketch of the extended loop:
```c
for (;;) {
    if (irq_pending(cpu) && irqs_enabled(cpu))   take_interrupt(cpu);   // §4 → §3
    set_icount_budget(cpu, QUANTUM);
    int rc = run_translated(cpu);                                       // existing FP call
    switch (rc) {
      case JIT_RETURN_FUNCNOTFOUND: translate_at(cpu, pc); break;       // on-demand (paged code)
      case JIT_RETURN_TRAP:         host_escape(cpu);      break;        // debugger/host I/O only
      case JIT_RETURN_ICOUNT_EXPIRED: /* fall through to service devices */ break;
    }
    run_devices_and_timers(cpu);
}
```

---

## 8. Subsystem 7 — translation cache for paged & self-modifying code  **[BUILD]**

This is where the current model breaks hardest. Today tagging assumes a single
static physical code region discovered once (`tag.cpp init_tagging` over
`[code_start, code_end)`), and translated functions are never invalidated. In a
real system:

- **Code lives anywhere and is paged in/out.** Translate **on demand** as the
  guest executes new pages (the `FUNCNOTFOUND` path already supports lazy
  discovery — extend it to arbitrary physical pages, not one region).
- **Key the cache correctly.** The same virtual address means different code in
  different address spaces. Either:
  - translate **physical-address-keyed** blocks and handle virtual aliasing, or
  - key blocks by **virtual address + ASID / page-table base**.
  `func_bb` is `map<addr_t, BasicBlock*>` per function **[EXISTS]** — the keying
  scheme must encode context, and `is_inside_code_area` (`tag.cpp`) must generalize beyond one region.
- **Invalidation.** Flush/relink translated code on: page remap or unmap, TLB
  flush, explicit icache flush, and **self-modifying code** (a write to a page
  that has been translated). SMC detection needs write-protection of translated
  pages (or coarse "dirty page ⇒ flush its blocks").
- **`cpu_flush` is too coarse** (`interface.cpp:398` drops the *current*
  function). System mode needs **per-page / per-block** invalidation; pairs well
  with per-region or per-subroutine functions (see `docs/improving-dispatch.md`
  §3.2).

> ⚠️ Note the 32-bit assumption in the entry-point cache file format
> (`tag.cpp`) — irrelevant for in-memory system caches but don't reuse that
> format for 64-bit guests.

---

## 9. Putting it together — roadmap

```
Phase 1  Memory     : softmmu via helper call-outs (§2 call-out path); physical
                      map + cpu_map_ram/mmio; alignment/permission checks.
Phase 2  Exceptions : exception-entry block + fp_emit_take_exception (§3);
                      convert faults to guest vectoring.
Phase 3  Privilege  : modes, privileged-instr faults, system register file,
                      MMU translate/flush hooks; wire one ISA's page tables (§5).
Phase 4  Interrupts : irq lines, pending check at block boundaries, icount
                      budget + extended run loop (§4, §7).
Phase 5  Cache      : on-demand paged translation, context-keyed cache,
                      invalidation incl. SMC (§8).
Phase 6  Platform   : interrupt controller + timer + UART; boot a minimal guest.
Phase 7  Perf       : inline software-TLB fast path (§2 inline path); block
                      chaining (improving-dispatch.md) to cut dispatch cost.
```

Pick **one architecture** to bring up first. **MIPS** is the natural choice: its
software-managed TLB and CP0 are already partly described in the disassembler
**[EXISTS]**, and a software TLB is simpler to model than hardware page-table
walkers. m88k is second-best given the existing OpenBSD/m88k userspace work.

---

## 10. Correctness checklist

- [ ] **Precise exceptions.** Faulting instructions present the architecturally
      correct state (restart vs. resume per ISA).
- [ ] **Atomicity of mode/PSR updates** on exception entry/exit (no half-updated
      privilege state observable by the handler).
- [ ] **MMU permission + alignment faults** match the ISA, including access size
      and read/write/execute distinctions.
- [ ] **TLB/ASID coherence.** Cache invalidation covers remap, flush, and ASID
      reuse; no stale virtual→physical or stale translated blocks.
- [ ] **SMC.** Writes to translated pages invalidate their blocks before the
      next fetch.
- [ ] **Interrupt masking & priority** honor the PSR and any interrupt
      controller priority; no delivery while masked.
- [ ] **Endianness/width** respect `address_size`/`word_size`; no 32-bit
      assumptions leak into PA/VA types.
- [ ] **Userspace mode still works.** Gate system behavior behind a flag (e.g.
      `CPU_FLAG_SYSTEM`) so the existing userspace path is untouched.

---

## 11. A design decision worth your input

The **memory-access strategy** (§2.2) is the single choice that most defines
performance, code size, and implementation complexity for the entire system
port — and it lives in one place: how `arch_load*`/`arch_store*` emit IR.

A natural seam is a generic MMU emitter that the per-arch helpers call instead
of indexing `ptr_RAM` directly:

```cpp
// libcpu/frontend.cpp  [BUILD]
// Emit IR for a guest memory access. Returns the loaded Value (for reads) or
// NULL (for writes). Faults must branch to the exception-entry block (§3).
static Value *
arch_emit_mem_access(cpu_t *cpu, Value *vaddr, Value *store_val,
                     unsigned size, bool is_write, BasicBlock *&bb)
{
    // TODO(design): choose the access model.
    //  (A) CALL-OUT : emit a call to a C helper mmu_rw(cpu, vaddr, size, ...);
    //                 simple + obviously correct + slow (call on every access).
    //  (B) INLINE TLB: emit an inline TLB probe (index = (vaddr>>page)&mask),
    //                 fast-path host load/store, slow-path helper on miss;
    //                 fast + large IR + intricate (faults from the slow path).
    //  Consider starting with (A), then layering (B) once §3–§8 work.
    return /* ... */;
}
```

The trade-offs to weigh: **(A) call-out** gets you a *correct* full-system
emulator fastest and keeps IR small, at a real per-access speed cost; **(B)
inline TLB** is how production emulators (QEMU softmmu) get acceptable speed but
multiplies the IR per access and entangles the fault path with every load/store.
Whether you can defer (B) depends on your performance target for the first
bootable guest.

---

## 12. References / prior art

- **QEMU softmmu** — inline TLB + helper slow path; the canonical design for §2.
- **QEMU exception/`cpu_loop_exit`** — mid-instruction fault unwinding for §3.
- **Self-modifying-code handling** (QEMU page-protection / DBT dirty tracking) — §8.
- **libcpu's own model** — `function.cpp` (`bb_trap` as the template for an
  exception-entry block), `tag.cpp` (lazy discovery to generalize for paging),
  and `docs/improving-dispatch.md` (block chaining / per-function split that
  per-page invalidation depends on).
```

---

### One-paragraph summary

Userspace mode lets the host stand in for the guest kernel; **system mode
removes that crutch**, so libcpu must model the privileged machine itself. The
work, in dependency order: replace flat direct-`RAM` indexing with a **software
MMU** (`frontend.cpp` memory helpers → translate/permission/MMIO, §2); add an
**exception-entry path** so faults *vector into the guest* instead of returning
to the host (§3); model **privilege, system registers, and TLB instructions**
(§5); deliver **asynchronous interrupts** at block boundaries under an
instruction budget with an extended run loop (§4, §7); make the **translation
cache** survive paging, context switches, and self-modifying code with proper
invalidation (§8); and add a **device/board layer** for MMIO and IRQs (§6). The
pivotal design choice is the memory-access model — call-out (simple, correct,
slower) vs. inline software-TLB (fast, complex) — concentrated in one emitter
(§11). Bring it up on **MIPS** first.
