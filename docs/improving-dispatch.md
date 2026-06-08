# Improving Dynamic Translation: Eliminating the Giant Jump Table

> A design document proposing concrete, incremental ways to remove libcpu's
> monolithic PC-dispatch switch and replace it with techniques from the dynamic
> binary translation (DBT) literature. Read `ARCHITECTURE.md` §6 first for the
> current translation model.

---

## 1. The problem, precisely

libcpu translates an entire code region into a **single LLVM function**
(`jitmain`, built by `cpu_create_function` in `function.cpp`) and routes all
non-statically-resolvable control flow through **one dispatch switch**.

In `libcpu/translate_all.cpp`:

```cpp
// create dispatch basicblock
BasicBlock* bb_dispatch = BasicBlock::Create(_CTX(), "dispatch", cpu->cur_func, 0);
Value *v_pc = new LoadInst(cpu->ptr_PC, "", false, bb_dispatch);
SwitchInst* sw = SwitchInst::Create(v_pc, bb_ret, bbs, bb_dispatch);
...
for (it = bb_addr.begin(); it != bb_addr.end(); it++) {
    ...
    // Add dispatch switch case for basic block.
    ConstantInt* c = ConstantInt::get(getIntegerType(cpu->info.address_size), pc);
    sw->addCase(c, cur_bb);          // <-- EVERY basic block becomes a case
    ...
}
```

And everything that can't resolve a target statically lands on that switch:

```cpp
if (tag & TAG_RET)
    bb_target = bb_dispatch;                 // returns
if (tag & (TAG_CALL|TAG_BRANCH)) {
    if (new_pc == NEW_PC_NONE)               // indirect / computed branches
        bb_target = bb_dispatch;
    else
        bb_target = lookup_basicblock(...);  // direct: linked with a real branch
}
```

So the switch has **one case per basic block in the whole region**, even though
the vast majority of those blocks are only ever reached by *direct* branches
(which already use a real `BranchInst` and never consult the switch).

### Why this hurts

| Symptom | Cause |
|---------|-------|
| **Slow compilation** | LLVM must build, verify, and optimize one function whose CFG has thousands of switch edges. Verifier/optimizer cost is super-linear in some passes. |
| **Large code & poor i-cache behavior** | LLVM lowers a dense switch to a jump table (memory) or a sparse one to a binary-search tree (branchy code). Either way it sits on the hot path of every indirect transfer and return. |
| **Doesn't scale / can't grow incrementally** | Adding newly discovered code means re-emitting the whole function and its switch (`cpu_flush` + retranslate), because the switch must enumerate every block. |
| **Every return/indirect call pays full dispatch** | Calls + returns dominate real programs; each `ret` does a full switch lookup instead of an O(1) prediction. |
| **One unresolved target ⇒ fall to default ⇒ host round-trip** | The switch default is `bb_ret` → `JIT_RETURN_FUNCNOTFOUND` → back into `cpu_run`'s tag/translate loop (`interface.cpp:380`), an expensive native↔C transition. |

The goal of this document: **shrink, then largely eliminate, the central
switch**, while keeping a correct slow path as a fallback.

---

## 2. Guiding principles

1. **Keep a correct slow path.** Every optimization below is a *fast path* in
   front of the existing dispatch/`FUNCNOTFOUND` mechanism. Never remove the
   slow path — it is the correctness backstop for indirect targets, newly
   discovered code, and (eventually) self-modifying code.
2. **Distinguish direct from indirect control flow.** Direct branches already
   work without the switch. The switch exists *only* for indirect transfers,
   returns, and cross-function entry. Optimize those specifically.
3. **Prefer O(1) prediction over O(log n) search** for the two hottest indirect
   transfers: **returns** and **indirect calls**.
4. **Make translation incremental.** Smaller translation units mean we add code
   without rebuilding a global table.

---

## 3. Techniques, ordered by return-on-investment

### 3.1 — Only switch-dispatch blocks that can actually be indirect targets *(cheapest, high impact)*

**Idea.** A basic block needs a `sw->addCase` entry **only if some control
transfer can reach it without a statically-known target**. Blocks reached
exclusively by direct branches never need a case — they are always entered via
`BranchInst`.

From the tag bits (`tag.h`), the set of *indirectly reachable* blocks is:

- `TAG_ENTRY` — client can enter here,
- `TAG_SUBROUTINE` — call targets (reachable by indirect calls),
- `TAG_AFTER_CALL` — return landing pads (reachable by `ret`),
- targets of **indirect/computed** branches (today these are `NEW_PC_NONE`, so
  the target set is unknown — see 3.4 for narrowing it).

**Change.** In `translate_all.cpp`, gate the `addCase` call:

```cpp
if (needs_dispatch_case(cpu, pc))   // TAG_ENTRY|TAG_SUBROUTINE|TAG_AFTER_COND-after-trap|...
    sw->addCase(c, cur_bb);
```

**Benefit.** Typically removes the large majority of cases (most blocks are
direct-branch-only), directly shrinking the jump table and the function's CFG.
Zero correctness risk: any block we *don't* case is, by construction, only
reachable by a direct branch we already emit.

**Effort/risk:** Low / Low. **This is the recommended first step.**

> ⚠️ Conservatism: if a guest can compute an address into the *middle* of a
> known direct-only block, you'd miss it. In practice indirect targets coincide
> with call/branch-target tags. Keep the slow-path default (`bb_ret`) so a
> genuinely unanticipated PC still falls through correctly rather than
> mis-dispatching.

---

### 3.2 — Split the monolithic function into per-subroutine functions *(structural, high impact)*

**Idea.** Instead of one `jitmain` containing the whole region, translate **one
LLVM function per subroutine** (boundaries are already known: `TAG_SUBROUTINE`).
Guest `call`/`ret` become real native `call`/`ret` of these functions.

**Consequences.**
- Each function's dispatch switch shrinks to *only its own* indirect targets,
  which is tiny — most intra-procedural flow is direct branches.
- **Returns stop using the switch entirely**: a guest `ret` becomes an LLVM
  `ret`, and the host CPU's own return-address prediction handles it for free.
- Translation becomes **incremental**: discover and JIT a new subroutine
  without touching others (no global `cpu_flush`).
- Smaller functions optimize and verify far faster.

**Change.** Significant: `cpu_translate_function` currently builds exactly one
function; this becomes "build/lookup the function for subroutine S". The
`func_bb` map is already keyed by `Function*`, so the data model anticipates
multiple functions. Cross-function direct calls map to LLVM `CallInst`; calls
to not-yet-translated subroutines go through a **call stub** that translates on
demand (a thin reuse of the existing `FUNCNOTFOUND` path).

**Effort/risk:** Medium–High / Medium. Highest structural payoff; pairs
naturally with 3.3.

---

### 3.3 — Return-address prediction via a shadow stack *(targets the #1 indirect transfer)*

**Idea.** Returns are the most frequent indirect transfer. Maintain a software
**return-address stack** mapping guest return-PC → host block (or host function
continuation). On a guest `call`, push (guest_return_pc, host_landing). On a
guest `ret`, pop and compare: if the guest return PC matches the prediction,
branch directly; otherwise fall back to the slow dispatch.

This is exactly what the original authors planned — see `TODO`:

> *"at every RTS, add code to lookup the 6502 return address in a table and
> compare it with `__builtin_return(0)`. if the same, use RTS"* and *"have a
> special RTS table for every RTS … performance was great."*

**Interaction with 3.2.** If subroutines become real LLVM functions (3.2), the
host return stack does this automatically for the common case, and the shadow
stack is only needed for mismatches (longjmp, tail-call tricks, hand-rolled
stacks). If you *don't* adopt 3.2, the shadow stack is the standalone win for
returns.

**Effort/risk:** Medium / Medium (correctness care around setjmp/longjmp,
signal returns, and stack manipulation that defeats the prediction — always
verify the predicted PC before committing).

---

### 3.4 — Indirect Branch Target Cache (IBTC) / inline caches *(the general indirect case)*

**Idea.** For indirect branches/calls (`NEW_PC_NONE`), instead of jumping to the
global switch, emit a small **inline cache**: a fast check against the last
resolved `(guest_pc → host_target)`, with a hash-table fallback, and only then
the slow path.

Two common forms:

- **Per-site inline cache (monomorphic/polymorphic):** at each indirect site,
  inline a compare of the computed guest PC against the last-seen value(s); on
  hit, branch directly to the cached host block.
- **Shared IBTC hash table:** one open-addressed table `guest_pc → host_ptr`,
  probed in a handful of instructions. This is the QEMU / DynamoRIO / Pin
  approach and scales to large/polymorphic target sets.

**How it maps to libcpu.** Indirect transfers currently set `bb_target =
bb_dispatch`. Replace that with a generated fast-path block that probes the
IBTC; on miss, *then* go to `bb_dispatch` (or directly to the
`FUNCNOTFOUND`/slow path). The table is a host-side array the JITed code
indexes; populate it as targets resolve.

**Benefit.** Converts the O(log n) switch (or jump-table indirection) into a
~O(1) probe that stays warm in cache, and removes indirect transfers from the
global switch's reason-to-exist.

**Effort/risk:** Medium / Medium. This is the workhorse technique once 3.1/3.2
have removed the easy cases.

---

### 3.5 — Direct block chaining with lazy patching for resolved indirect edges

**Idea.** When an indirect edge resolves to a stable target at runtime, **patch
it into a direct branch** so subsequent executions skip dispatch entirely
(classic "block linking"/"chaining").

**Caveat for libcpu specifically.** LLVM's JIT does not expose easy
post-compilation branch patching the way a hand-written code-cache JIT (QEMU
TCG, DynamoRIO) does. Practical options:
- Approximate it with the **inline cache** (3.4) — same effect, no patching.
- Or, longer-term, move the hot code cache out of one-shot LLVM functions and
  into a managed code buffer where stubs can be repointed (a much larger
  architectural change; see 3.7).

**Effort/risk:** Medium (as inline cache) / High (as true patching).

---

### 3.6 — Trace / superblock formation to reduce dispatch *frequency*

**Idea.** Rather than only making each dispatch cheaper, **dispatch less often**
by forming hot **traces** (superblocks that span the common path across multiple
basic blocks and even calls), with side-exits to the slow path for the cold
cases. Fewer control transfers reach any dispatch mechanism at all.

This complements 3.1–3.4 and is how mature DBTs (e.g. trace-based JITs) get
their headline speed. It is the most involved and should come last.

**Effort/risk:** High / Medium-High.

---

### 3.7 — (If you keep a switch at all) make it dense and ranged

If, after 3.1–3.4, a residual switch remains, help LLVM lower it well:

- **Normalize PC keys.** On fixed-width-instruction ISAs (ARM, MIPS, M88K),
  index by `pc >> log2(insn_size)` so the case values are contiguous and LLVM
  emits a tight jump table instead of a sparse tree. (`tag.cpp` already notes it
  wastes a tag per byte on fixed-width ISAs — the same observation applies
  here.)
- **Partition by address range** so dense regions get jump tables and sparse
  regions get cheap branch trees, instead of one mixed switch.

**Effort/risk:** Low / Low — but strictly a fallback; the structural fixes
matter more.

---

## 4. Recommended roadmap

```
Phase 0  (now)   : measure — count switch cases vs. blocks actually reached
                   indirectly; time TIMER_BE and TIMER_RUN per region.
Phase 1  (low)   : 3.1  gate addCase to indirect-target blocks only.   ← start here
                   3.7  normalize PC keys on fixed-width ISAs.
Phase 2  (med)   : 3.4  add an IBTC/inline cache for NEW_PC_NONE transfers.
                   3.3  add return prediction (shadow stack).
Phase 3  (high)  : 3.2  per-subroutine functions; returns/calls become native.
Phase 4  (high)  : 3.6  trace formation for hot paths.
```

Each phase is independently shippable and measurable. Phases 1–2 should remove
most of the jump-table pain with modest, low-risk changes; Phase 3 is the
architectural cleanup that makes the central switch nearly vestigial.

---

## 5. Correctness checklist (applies to every phase)

- [ ] **Slow path preserved.** Any miss/mismatch falls back to the existing
      `bb_dispatch` → `bb_ret` → `FUNCNOTFOUND` → tag/translate loop.
- [ ] **Predicted PC is verified before commit.** Never trust a cached/predicted
      target without comparing the actual computed guest PC.
- [ ] **Newly discovered code still integrates.** On-demand discovery
      (`cpu_tag` / `tags_dirty`) must keep working; caches/tables get new
      entries, not stale routing.
- [ ] **Endianness/width unchanged.** PC key transforms must respect
      `address_size` and not assume 32-bit (note the existing 32-bit assumption
      in the entry-point cache — don't propagate it).
- [ ] **Single-step/debug modes unaffected.** `CPU_DEBUG_SINGLESTEP*` use
      different translators (`translate_singlestep*.cpp`); ensure fast paths are
      bypassed there.
- [ ] **Self-modifying code (future).** If/when SMC is supported, cache and
      table entries for invalidated regions must be flushed.

---

## 6. A design decision worth your input

The cheapest, highest-leverage change (Phase 1, §3.1) hinges on a single
predicate: **"does this basic block need a dispatch-switch case?"** Getting it
right is the whole correctness story for that phase — too narrow and a real
indirect target mis-dispatches; too broad and we keep the giant switch.

A natural home is a small helper in `translate_all.cpp`:

```cpp
// Returns true iff `pc` can be reached by a transfer whose target is NOT
// statically known at translate time (and therefore must be findable via the
// dispatch switch). Blocks reachable only by direct branches return false.
static bool
needs_dispatch_case(cpu_t *cpu, addr_t pc)
{
    tag_t tag = get_tag(cpu, pc);
    // TODO(design): which tags belong here?
    //   - TAG_ENTRY        : client may start execution here          -> yes
    //   - TAG_SUBROUTINE   : reachable by (possibly indirect) calls    -> ?
    //   - TAG_AFTER_CALL   : return landing pad (reachable by `ret`)   -> ?
    //   - TAG_AFTER_TRAP   : trap-return landing pad                   -> ?
    //   - others?
    // Consider: if §3.2/§3.3 land later, TAG_SUBROUTINE/TAG_AFTER_CALL
    // may move to native call/ret and drop out of this set.
    return /* your predicate */;
}
```

The trade-offs to weigh: include `TAG_SUBROUTINE`/`TAG_AFTER_CALL` now for
safety (returns/calls still use the switch until Phase 2–3), or scope them out
immediately if you commit to the IBTC/return-stack path first. Whichever you
choose shapes how much the switch shrinks in Phase 1 and what Phase 2/3 must
take over.

---

## 7. References / prior art

- **QEMU TCG** — block chaining + indirect-branch hash lookup.
- **DynamoRIO / Pin** — IBTC and indirect-branch inline caches.
- **HotSpot / trace JITs** — trace formation and superblocks.
- **libcpu's own `TODO`** — the return-address-table idea for `RTS`, with a note
  that the old static C recompiler implemented it and "performance was great".
