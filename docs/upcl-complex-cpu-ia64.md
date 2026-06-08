# Making UPCL Able to Describe a Complex CPU: the IA-64 (Itanium / EPIC) Case

> IA-64 is the hardest mainstream ISA to emulate, and therefore the best stress
> test for an architecture-description language. This document walks through
> *every* IA-64 feature that breaks the assumptions currently baked into UPCL
> **and** into libcpu's core, and proposes the language extensions, IR additions,
> lowerings, and core changes needed to express it.
>
> Read `docs/upcl-cpu-fpu-simd.md` (UPCL pipeline & dynamic lowering),
> `docs/com-architecture-and-backends.md` (`ICpuEmitter`/intrinsics),
> `docs/system-emulation.md` (auxiliary hardware structures), and
> `docs/improving-dispatch.md` (basic-block formation) first — this is the
> capstone that combines all of them.

---

## 0. Why IA-64 is the right torture test

libcpu and UPCL both quietly assume a **RISC-ish world**: fixed or simple
instruction encodings, fetch-and-execute one instruction at a time, a *static*
register file at fixed offsets, unconditional execution, and a single linear
program counter. IA-64 (EPIC — *Explicitly Parallel Instruction Computing*)
violates **all** of these on purpose. If UPCL can describe IA-64, it can describe
practically anything.

The point of this document is not "Itanium is exotic" — it is that each Itanium
feature names a *general capability* (bundle decode, predication, dynamic
register naming, register poison, auxiliary HW tables) that a serious ADL must
have. IA-64 just demands all of them at once.

Items are tagged **[UPCL]** (language/IR/codegen change), **[CORE]** (libcpu
engine change), **[RT]** (runtime helper/state), **[EXISTS]** (reusable today).

---

## 1. The assumptions IA-64 breaks

| IA-64 feature | Assumption it breaks | Where it hurts |
|---------------|----------------------|----------------|
| **128-bit bundles**: 3×41-bit slots + 5-bit template | "one instruction = N contiguous bytes; decode sequentially" | UPCL decoder grammar; libcpu byte-granular `tag.cpp`, `tag_instr` advancing by bytes |
| **Instruction groups & stops** | "instructions execute strictly sequentially with visible intermediate state" | execution/commit model; basic-block formation |
| **Pervasive predication** (64 predicate regs) | "instructions are unconditional; only branches have conditions" | every instruction's semantics; BB edges |
| **Register stack + rotation** (r32–r127, CFM/RRB, RSE) | "register name → fixed offset, statically" | UPCL static `register_file`; all register access lowering |
| **NaT bits** (65-bit GPRs) | "a register is just its value" | UPCL register types; ALU lowering (poison propagation) |
| **Speculation + ALAT** (`ld.s/.a`, `chk`) | "loads are simple; no auxiliary disambiguation HW" | new runtime structure + intrinsics |
| **82-bit extended FP**, parallel FP/integer | "host has a matching FP type; ops are scalar" | FPU/SIMD lowering (ties to the fpu-simd doc) |
| **IP + slot as PC**, bundle-aligned branches | "PC is a flat byte address" | libcpu `ptr_PC`, branch targets, tagging keys |
| **Predicate-writing parallel compares** | "a compare writes one flag word" | compare-type vocabulary; intra-group predicate semantics |

Everything below addresses one row.

---

## 2. Bundles, templates, and group decoding  **[UPCL] [CORE]**

### The problem
An IA-64 fetch unit is a **128-bit bundle**: three 41-bit instruction slots plus a
5-bit **template** that (a) maps each slot to an execution-unit type (M/I/F/B/L+X)
and (b) marks **stops** (instruction-group boundaries). `movl` even consumes two
slots for a 64-bit immediate (L+X). So "decode the instruction at `pc`" is
meaningless without the bundle and template.

### UPCL extension — a bundle/template description
Add a fetch-unit concept above the per-instruction encoding (proposed syntax):

```
bundle {
    size 128;                 // bits
    slots 3;                  // 41-bit instruction slots
    template field [4:0];     // 5-bit template selector
}

template 0x00 = ( M, I, I        );          // slot→unit assignment
template 0x01 = ( M, I, I  stop  );          // 'stop' = group boundary after
template 0x02 = ( M, I  stop, I  );
/* ...all 32 templates, incl. L+X (long immediate) and reserved... */
```

Each `insn` then declares which **unit type(s)** it may occupy, and decoding is:
fetch bundle → read template → for each slot, decode per its unit type. The
`decoder_operand_def` machinery **[EXISTS]** (including `CONSTANT` for immediates)
extends naturally to slot-relative bit fields.

### libcpu core change
`tag.cpp` is byte-granular ("one tag per byte of code"); `tag_instr` advances by
instruction bytes and `ptr_PC` is one address. For IA-64:
- The **tagging unit becomes the bundle**; the *slot* is a sub-index.
- `tag_instr` must return "advance one bundle (16 bytes) / next slot," and tagging
  keys become `(bundle_addr, slot)`.
- This generalizes the same way fixed-width ISAs would benefit (the `tag.cpp`
  TODO already laments per-byte waste): a `fetch_granule` notion in
  `cpu_archinfo_t`.

---

## 3. Pervasive predication  **[UPCL] [CORE]**

### The problem
Almost every IA-64 instruction carries a 6-bit **qualifying predicate** (qp):
`(p5) add r1 = r2, r3` executes only if `p5` is true (`p0` is hardwired true).
Predication is the norm, not the exception.

### UPCL extension — a default predicate operand
Let an `insn` (or a whole instruction class) declare it is predicated, with the qp
as an implicit operand:

```
class alu predicated by qp:6 -> %PRED;     // qp indexes the predicate reg file

insn "add" : alu {
    encoding ( ... qp:6, r1:7, r2:7, r3:7 );
    semantics { r1 <- r2 + r3; }            // implicitly guarded by (qp)
}
```

### Lowering
The predicate guard wraps the instruction's **writes**:
- **JIT/LLVM backend:** either a conditional block (`if (qp) { ...writes... }`) or,
  for single-assignment instructions, a `select` (`r1 = qp ? r2+r3 : r1`). The
  `select` form keeps basic blocks large (good for `docs/improving-dispatch.md`)
  by avoiding a branch per predicated op.
- **Interpreter:** a runtime `if (PRED[qp])` around the effects.

### Core interaction (basic-block formation)
Predicated *non-branch* instructions must **not** split basic blocks — they are
straight-line. Only predicated **branches** create conditional CFG edges
(reusing `TAG_CONDITIONAL`). So `tag_instr` reports predication separately from
control flow; most predicated ops stay `TAG_CONTINUE`.

---

## 4. Instruction groups, stops, and the commit model  **[UPCL] [CORE]**

### The problem
Within an instruction group (between stops), the architecture guarantees the
*compiler* placed no disallowed dependencies, and certain effects are defined as
if **all reads observe the group-entry state**. Naive sequential emulation is
correct for dependency-free groups, but **parallel compares** and multiple
predicate writes in one group have ordering-sensitive semantics.

### Design choice — execution/commit model (see §12)
Two models:
- **Sequential (optimistic):** execute slots in order, trusting the no-dependency
  guarantee. Simplest; correct for the vast majority of valid code; matches how
  most IA-64 emulators actually run.
- **Transactional (read-snapshot/commit):** within a group, read from a snapshot
  of group-entry state and commit writes at the stop. Required only for the
  precise parallel-compare/predicate cases; expensive if applied everywhere.

A practical hybrid: **sequential by default**, with UPCL marking the few op
classes (parallel compares) that need snapshot-read semantics so only they defer
their commit.

### UPCL extension
A `stop` is already decodable from the template (§2). UPCL exposes a `group`
boundary tag so `tag_instr` can mark group ends, and a per-op
`reads group-entry` / `commit at stop` attribute for the transactional cases.

---

## 5. Dynamic register naming: stacking & rotation  **[UPCL] [CORE] [RT]**

### The problem — *this is the crux*
IA-64 GP registers r32–r127 form a **register stack**: `alloc` carves a frame; the
**RSE** (Register Stack Engine) transparently spills/fills frames to a backing
store. Software-pipelined loops **rotate** registers: the **RRB** (register rotation
base) makes register *names* map to different physical registers each
`br.ctop`/`br.wtop`. So `r35` is not a fixed offset — it is
`phys = rename(name, CFM, RRB)`. The static `register_file` model
(`upcl/examples/*.def`, fixed offsets) cannot express this.

### UPCL extension — renaming bindings
Allow a register group to declare a **renaming function** over architectural
state registers (CFM, RRB), generalizing the existing `binding_expression`:

```
register_file {
    group GR {
        [ #i64+nat r0?:128 ],                       // 128 GPRs, with NaT (see §6)
        rename stacked  (32..127) by  rse_rename(name, %CFM);   // [UPCL] dynamic naming
        rename rotating (32..127) by  rrb_rotate(name, %RRB);   // software-pipelining
    }
    group FR { [ #f82 f0?:128 ], rename rotating (32..127) by rrb_rotate(name, %RRB); }
    group PR { [ #i1  p0?:64  ], rename rotating (16..63)  by rrb_rotate(name, %RRB); }
}
```

### Lowering — the performance decision (see §12)
Register access `R(name)` no longer compiles to a fixed offset:
- **Runtime-indirected:** `R(name)` lowers to `phys_regfile[ rename(name, CFM, RRB) ]`
  — a computed GEP. Always correct, costs an index computation per access, and
  defeats keeping registers in host registers.
- **Specialized (per-frame/RRB) translation:** translate a block *for a known
  CFM/RRB*, baking the renaming into constants; re-translate (or pick a cached
  variant) when frame/rotation state changes. Fast code, but the translation
  cache must be **keyed by CFM/RRB** (ties to the on-disk-cache keying in
  `docs/com-architecture-and-backends.md` §6, and to context-keyed caching in
  `docs/system-emulation.md` §8).

### RSE as a runtime state machine  **[RT]**
`alloc`, `br.ret`, `flushrs`, and automatic spill/fill are **side-effecting**:
model them as **intrinsics** (`@rse_alloc`, `@rse_flush`, …) backed by a runtime
helper that manages the backing store and CFM — exactly the
intrinsic-with-runtime-helper pattern from the backends doc. UPCL describes the
*interface*; the RSE *mechanism* lives once in C.

---

## 6. NaT bits — registers as 65-bit poisoned values  **[UPCL]**

### The problem
Each GPR has an associated **NaT** ("Not a Thing") bit for control speculation: a
speculative load that would fault sets NaT instead; any ALU op consuming a NaT
operand **propagates** NaT to its result; `chk.s` detects it. So a GPR is
effectively 65 bits, and poison propagation is automatic.

### UPCL extension — a poison-carrying type + auto-propagation
Add a register-type attribute `+nat` (a parallel poison bit), and define
propagation as an automatic lowering rule (like flag computation is automatic in
libcpu today):

```
type #i64+nat                                    // 64-bit value + 1 NaT bit  [UPCL]
// auto-rule: result.nat = OR(any source operand .nat) for ALU ops;
//            speculative loads set .nat on deferred fault; chk.s reads it.
```

### Lowering
- Track the NaT bit as a **parallel 1-bit value** alongside each GPR's value (a
  second register file, like libcpu's exploded N/V/Z/C flags **[EXISTS pattern]**).
- ALU emitters OR the source NaT bits into the destination automatically; only ops
  that explicitly clear/set NaT (e.g. `mov`, `chk`) override it.

---

## 7. Speculation & the ALAT  **[UPCL] [RT]**

### The problem
**Data speculation**: `ld.a` (advanced load) records the load in the **ALAT**
(Advanced Load Address Table); a later `chk.a`/`ld.c` checks whether an
intervening store invalidated it. **Control speculation**: `ld.s`/`chk.s` (NaT,
§6). The ALAT is an associative hardware table.

### UPCL extension + runtime
Model the ALAT as an **auxiliary hardware structure** accessed via intrinsics —
the same pattern the system-emulation doc uses for the TLB:

```
hw_table ALAT { entries 32; key (addr, size, reg); }     // [UPCL] declares the structure
// ops lower to intrinsics:
//   ld.a   -> @alat_insert(reg, addr, size); ld value
//   chk.a  -> if (!@alat_present(reg)) branch recovery
//   st     -> @alat_invalidate(addr, size)        // stores prune the ALAT
```

The ALAT *mechanism* is one runtime helper **[RT]**; UPCL only declares the table
and which ops touch it. Stores must call `@alat_invalidate`, so UPCL's store
lowering grows an arch-conditional hook.

---

## 8. Extended (82-bit) and parallel floating point  **[UPCL]** (ties to fpu-simd doc)

- IA-64 FP registers are **82-bit** (17-bit exponent, 64-bit significand);
  `f0=+0.0`, `f1=+1.0` are hardwired. libcpu already special-cases fp80/fp128
  (`CPU_FLAG_FP80/FP128`); **82-bit is a third soft-float width** — handled by the
  shared soft-float library from `docs/upcl-cpu-fpu-simd.md` §4, not a host type.
- **Parallel FP** (two single-precision in one register) and **parallel integer**
  (MMX-like on GPRs) are SIMD — use the `lanes`/`reduce` vocabulary from the
  fpu-simd doc §5.
- FP registers **rotate** (§5), so FP access also goes through renaming.
- `f0`/`f1` hardwiring uses UPCL's existing `value_hardwired` (`<- (expr)`)
  binding **[EXISTS]**.

This is where the **intrinsic-vs-decomposed** decision from the fpu-simd doc §9
recurs: keep `fma`/parallel-FP as intrinsics so backends use native FP, decompose
only the genuinely odd ops.

---

## 9. IP + slot program counter, bundle-aligned branches  **[CORE]**

The architectural IP addresses a **bundle** (16-byte aligned); the executing
**slot** (0–2) is part of state. Branch targets are bundle addresses; there is no
"branch to slot 1 of a bundle" in the usual case (branches go to slot 0 of a
target bundle). Core changes:
- `ptr_PC` becomes `(bundle_ip, slot)`; `address_size` covers the bundle IP, with
  the slot tracked alongside (or encoded in low bits).
- Tagging/basic-block keys (`func_bb`, `tag.cpp`) use `(bundle_ip, slot)`.
- Branch registers `b0–b7` are an indirect-branch source — feed the IBTC/return
  prediction from `docs/improving-dispatch.md` §3.3–3.4.

---

## 10. Predicate-writing parallel compares  **[UPCL]**

`cmp`/`cmp4`/`tbit`/etc. write a **pair** of predicates and support **compare
types** — `none`, `unc` (unconditional), `and`, `or`, `and.orcm`, `or.andcm`,
`de`, … — that define *how* the two target predicates are updated, enabling fast
if-conversion. This needs a **compare-type vocabulary** in UPCL:

```
insn "cmp.eq.and" {
    encoding ( ... qp:6, p1:6, p2:6, r2:7, r3:7 );
    semantics {
        cmp_pair p1, p2 = (r2 == r3) type and;     // [UPCL] compare-type aware
    }
}
```

The `and`/`or` types only *clear*/*set* predicates (never both) and have defined
behavior when `qp` is false — semantics the lowering encodes once. These interact
with the group commit model (§4): parallel compares in one group are the main
reason the transactional path exists.

---

## 11. What this means for the libcpu core (summary)

UPCL changes alone are insufficient; IA-64 forces **core** generalizations that
benefit other ISAs too:

1. **Fetch granule = bundle** (not byte/instruction): `tag.cpp`, `tag_instr`,
   `cpu_archinfo_t` (§2).
2. **`(addr, slot)` PC and cache keys**: `ptr_PC`, `func_bb` (§9).
3. **Predication that doesn't split blocks**: `tag_instr` reports it orthogonally
   to control flow (§3).
4. **Context-keyed translation cache** for CFM/RRB specialization (§5) — the same
   mechanism system-mode needs for ASIDs (`docs/system-emulation.md` §8).
5. **Auxiliary HW structures as intrinsics + runtime helpers**: RSE, ALAT (§5,§7)
   — same pattern as the softmmu TLB.
6. **Poison/parallel bit register files**: NaT (§6) — same pattern as exploded
   flags.

Most of these are *generalizations*, not IA-64 special cases — which is the
argument for doing them properly.

---

## 12. Roadmap (this is large — phase aggressively)

```
P1  Bundle/template decode in UPCL + bundle-granular tagging in core.        (§2)
P2  Predication: default qp operand, select/guard lowering, BB rules.        (§3)
P3  Sequential group execution + stop tagging (defer transactional).         (§4)
P4  NaT: poison type + auto-propagation (parallel bit file).                 (§6)
P5  Register stacking/rotation: renaming bindings; START runtime-indirected,
    then add CFM/RRB-specialized translation + cache keying.                 (§5)
P6  RSE + ALAT runtime helpers + intrinsics; speculation ops.               (§5,§7)
P7  82-bit/parallel FP via shared soft-float + lanes (fpu-simd doc).         (§8)
P8  Parallel compares + transactional commit for the cases that need it.     (§10,§4)
P9  IP+slot PC, b0-b7 indirect-branch prediction.                            (§9)
```

P1–P4 get *simple, non-rotating, non-speculative* IA-64 code running. P5–P6 are
the genuinely hard middle. P7–P9 round out FP/SIMD and precise control flow.
Bring up under the **interpreter backend first** (it tolerates the runtime
indirection of §5 with no specialization), then add JIT specialization.

---

## 13. Risks & correctness

- [ ] **Register renaming correctness** across `alloc`/rotation/`br.ret`/RSE
      spill-fill — the single largest bug surface; test against HP's *Ski* /
      Intel *SoftSDV* traces.
- [ ] **NaT propagation completeness** — every ALU path must OR source NaT;
      missing one silently corrupts speculation.
- [ ] **ALAT precision** — store-to-advanced-load disambiguation must invalidate
      exactly; over- or under-invalidation breaks `chk.a` recovery.
- [ ] **Group semantics** — verify the sequential model against parallel compares
      and multiple predicate writers before trusting it; add the transactional
      path where it diverges.
- [ ] **Cache keying** — CFM/RRB-specialized blocks must be keyed by frame/rotation
      state or they alias catastrophically.
- [ ] **82-bit FP conformance** — exponent/significand handling vs. IEEE + IA-64
      extras (SoftFloat-style tests).
- [ ] **Template coverage** — handle all 32 templates incl. reserved/illegal
      (raise illegal-op), and L+X long-immediate two-slot decode.

---

## 14. A design decision worth your input

The whole IA-64 effort lives or dies on **how dynamic register naming
(stacking + rotation, §5) is lowered** — it is the most-executed operation and the
hardest to make fast.

```
/*
 * Register access R(name) under IA-64 stacking/rotation. The choice fixes
 * translation-cache keying, JIT speed, and how invasive renaming is.
 */

// (1) RUNTIME-INDIRECTED: every access computes its physical index live.
//     phys = rse_rename(name, CFM, RRB);  value = phys_regfile[phys];
//   + always correct; no re-translation on frame/rotation change; interpreter-friendly.
//   - index math on every access; registers can't stay in host registers; slow JIT.

// (2) STATE-SPECIALIZED: translate a block for a *known* (CFM, RRB); bake renaming
//     into constants; cache per (bundle_ip, slot, CFM, RRB); re-pick on change.
//   + fast code, registers can be promoted; matches the on-disk cache design.
//   - cache explosion if frame/rotation state varies widely; re-translation churn;
//     correctness hinges on precise keying & invalidation.
```

The trade-off mirrors §5 / the system-emulation context-cache problem: **(1)**
gets IA-64 *running* fastest and is the only sane interpreter path; **(2)** is the
only way to get *fast* IA-64 JIT code but multiplies cache entries by the
frame/rotation state space and demands airtight keying. The likely answer is
**(1) for the interpreter tier, (2) for the optimizing tier** (HotSpot model,
com-architecture doc §5) — but the keying granularity in (2) (full CFM/RRB? just
frame size? rotation amount modulo?) is the open question. Which dominates your
target: *time-to-first-correct-execution*, or *steady-state JIT throughput*?

---

## 15. References / prior art

- **Intel® Itanium® Architecture Software Developer's Manual** (Vols. 1–3) — the
  authoritative source for bundles, predication, RSE, NaT, ALAT, compare types.
- **HP *Ski* IA-64 simulator** and **Intel *SoftSDV*** — reference behavior/traces.
- **EPIC / VLIW literature** — bundle decode, predication, software pipelining,
  rotating registers.
- **Sibling docs** — `docs/upcl-cpu-fpu-simd.md` (soft-float/lanes for §8, the
  intrinsic-vs-decomposed decision), `docs/com-architecture-and-backends.md`
  (`ICpuEmitter`, intrinsics, tiered execution, context-keyed cache for §5),
  `docs/system-emulation.md` (auxiliary HW structures, context-keyed translation),
  `docs/improving-dispatch.md` (indirect-branch prediction for `b0–b7`, predication
  vs. basic blocks).
```

---

### One-paragraph summary

IA-64 is valuable to UPCL precisely because it breaks every simplifying assumption
at once: **128-bit bundles with templates** (UPCL needs a bundle/template decode
layer; libcpu needs bundle-granular, `(addr,slot)`-keyed tagging), **pervasive
predication** (a default qualifying-predicate operand lowered as guard/`select`
that does *not* split basic blocks), **instruction groups/stops** (a
sequential-by-default commit model with a transactional path only for parallel
compares), **register stacking & rotation** (dynamic renaming bindings over
CFM/RRB — *the* crux, lowered runtime-indirected for the interpreter and
state-specialized-and-cache-keyed for the JIT), **NaT poison bits** (a parallel
1-bit register file with automatic propagation, reusing libcpu's exploded-flag
pattern), **speculation/ALAT** (auxiliary HW structures modeled as intrinsics over
a runtime helper, like the softmmu TLB), **82-bit and parallel FP** (the shared
soft-float/lane libraries from the FPU/SIMD doc), and an **IP+slot PC with b0–b7
indirect branches** (fed by the dispatch doc's prediction). Most of these are
*general* ADL capabilities, not Itanium hacks — which is the case for building
them properly. The decisive design choice is how dynamic register naming is
lowered: runtime-indirected (correct, interpreter-friendly, slow) versus
state-specialized with CFM/RRB-keyed caching (fast JIT, cache-explosion risk) —
most likely both, split across the interpreter and optimizing tiers.
