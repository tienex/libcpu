# Task 3 Report: run-loop CPU_EXEC_ONE handler + PDP-1 XCT execution tests

## Changes made

### `LibCPU/test/lcx.cpp`

1. Added `int XctDepth = 0;` immediately before the `for (int I = 0; I < 100000; I++)` interpreter loop.

2. Added the `CPU_EXEC_ONE` handler inside `if (State.SyscallVector != CPU_NO_SYSCALL)`, before the existing arch-specific handlers. The handler is arch-neutral, gated only by `State.SyscallVector == CPU_EXEC_ONE`.

### `LibCPU/CMakeLists.txt`

Added `upcl.pdp1-xct` test with three sub-programs: fall-through, skip, nesting. Fixed the nesting test's data constant from `0o000011` (= 9 decimal) to `0o21` (= 17 decimal = 0x11 hex) to match the grep pattern `0x00011`.

---

## Handler implementation: iterative, not recursive via `continue`

The brief described a `continue`-based approach where nested XCTs re-enter the loop. This does NOT work with the existing loop structure because `GenerateAotCfg(Pc, End)` at the top of the loop always re-translates from `Pc` (the original outer XCT address), re-triggering the outer XCT trap indefinitely and causing `XctDepth` overflow.

The working implementation uses an **iterative inner `while` loop** within the handler:

```cpp
std::vector<CPU_ADDR> RetStack;
RetStack.push_back ((CPU_ADDR) State.TrapPc);  // outermost XCT+1
while (State.SyscallVector == CPU_EXEC_ONE) {
    if ((int) RetStack.size () > 16) { /* nesting too deep */ break; }
    IExecPc = (CPU_ADDR) State.DispPc;
    A.pArch->TagInstr (IExecPc, &ITag, &INewPc, &INextPc);
    State.Reg[ArchInfo.PcRegIndex] = IExecPc;
    /* GenerateAotCfg(IExecPc, IExecPc+1) + Execute */
    if (State.SyscallVector == CPU_EXEC_ONE) {
        RetStack.push_back ((CPU_ADDR) State.TrapPc);  // nested RetPc
    }
}
// Determine final Pc from RetStack[0] + skip delta
```

---

## Fall-through vs skip predicate: observed interpreter behavior

Both fall-through (LAC) and skip-taken (SAS condition true) exit the single-instruction region `[ExecPc, ExecPc+1)` via `pExit` without setting `State.TrapPc` → both leave `State.TrapPc = CPU_SMC_NO_TRAP`.

The PC register (`State.Reg[PcRegIndex]`) is also identical: both = `IExecPc` (set before execute; not updated by either the branch or the non-branch path in the single-instruction region).

**Differentiation via `TagInstr` + skip-detection by exclusion:**

- `TagInstr(ExecPc)` is called before executing to get `ITag`, `INewPc` (skip target), `INextPc` (fall-through = `ExecPc + D.Length`).
- For `TagConditional` (skip instructions like SAS): the not-taken path loops WITHIN the single-instruction region forever. If `Execute` returns at all, the taken path (skip) was taken. This is detection by exclusion.
- `INewPc` from `TagInstr` uses `NextPc = ExecPc + D.Length` as the value of `pc` in UPCL's folding. For PDP-1 (D.Length=1): `INewPc = (ExecPc + 1) + skip_count`. The skip delta relative to the instruction's "next" is `INewPc - INextPc`.

**Final Pc resolution:**

```cpp
CPU_ADDR Target = (CPU_ADDR) State.TrapPc;   // CPU_SMC_NO_TRAP for non-jump exits
if (Target != (CPU_ADDR) CPU_SMC_NO_TRAP) {
    Pc = Target;                             // computed/indirect jump (TrapPc set by OpIndirect)
} else if (ITag & TagConditional) {
    Pc = OuterRetPc + (INewPc - INextPc);   // skip: XCT+1 + delta
} else {
    Pc = OuterRetPc;                         // fall-through
}
```

**Why `INewPc - INextPc` (not `INewPc - IExecPc`):**

`TagInstr` folds `pc` as `NextPc = ExecPc + D.Length` (not `ExecPc`). For SAS at ExecPc=24:
- `INextPc = 25`, `INewPc = 26` (pc+1 = NextPc+1 = 26).
- `INewPc - IExecPc = 2` → Pc = RetPc+2 = 6 (HLT) — WRONG.
- `INewPc - INextPc = 1` → Pc = RetPc+1 = 5 (DAC 0o32) — CORRECT.

The delta `INewPc - INextPc` correctly extracts the skip count (1 for PDP-1 single skips) regardless of the UPCL `pc` folding convention.

---

## Brief's predicate vs this implementation

The brief's predicate `Target == CPU_SMC_NO_TRAP || Target == ExecPc` is NOT used because the interpreter does not distinguish fall-through from skip via `State.TrapPc` — both produce `CPU_SMC_NO_TRAP`. The `TagInstr`-based approach is the correct replacement.

**The skip test assertion is NOT weakened**: the skip lands at `RetPc + 1 = 5` = DAC 0o32, M[0o32]=5 is confirmed.

---

## Brief's octal constant fix

The brief wrote `M[0o32]=0o000011` (= 9 decimal = 0x9 hex) for the nesting test, but the grep checks `0x00011` (= 17 decimal). This is a typo: `0o21` (= 17 = 0x11) was intended. Fixed in `CMakeLists.txt`.

---

## Sub-test results

- **fall-through:** M[0o32] = 0x00007 = 7 ✓ (XCT of LAC, AC=7, DAC runs at XCT+1)
- **skip:** M[0o32] = 0x00005 = 5 ✓ (XCT of SAS-that-skips, LAW 1 skipped, AC stays 5, DAC runs at XCT+2)
- **nesting:** M[0o33] = 0x00011 = 17 ✓ (XCT of XCT of LAC, AC=17, DAC runs at outermost XCT+1)

---

## Regression result

`ctest -R "upcl.pdp1|upcl.pdp10"` — 15/15 PASS. No regressions.

---

## Deviations from brief

1. **Handler structure**: iterative inner `while` loop instead of `continue`-based re-dispatch. The `continue`-based approach doesn't work because the outer loop re-translates from `Pc` on every iteration, causing infinite outer-XCT re-entry for nesting. The iterative approach achieves identical semantics with correct nesting.

2. **Fall-through/skip predicate**: `TagInstr`-based detection instead of `Target ∈ {CPU_SMC_NO_TRAP, ExecPc}`. The interpreter does not signal skip vs fall-through via `TrapPc`; detection-by-exclusion via `TagConditional` is the correct approach.

3. **Skip delta**: `INewPc - INextPc` instead of `Target - ExecPc`. Necessary because `TagInstr` returns `INewPc` relative to `NextPc = ExecPc + D.Length`, not to `ExecPc`.

4. **Nesting test data constant**: `0o21` (17) instead of `0o000011` (9) to match the grep's `0x00011`. The brief has a typo.

5. **`XctDepth` usage**: The declared `int XctDepth = 0` is present per brief, but depth is also tracked via `RetStack.size()` in the handler. `XctDepth` is reset to 0 after each complete chain; `RetStack.size()` guards the depth cap within the chain.

---

## Fix: XCT continuation correctness

### Mechanism chosen: `GenerateAotExecOne` + handler restructure

Three correctness bugs were present in the committed `CPU_EXEC_ONE` handler (commit a055291):

**Bug 1 — skip-not-taken misclassified.** The handler relied on "detection by exclusion" for
`TagConditional` instructions: it assumed that if execution returned at all, the skip was taken
(claiming the not-taken path loops forever inside the single-instruction region). This is WRONG.
For PDP-1 SAS/SAD with `pc = (cond) ? (pc+1) : pc`:
- Taken: `pc = ExecPc+1` — outside the `[IExecPc, IExecPc+1)` window → exits via pExit.
- NOT taken: `pc = pc = IExecPc` — ALSO outside the region boundary per the
  `GenerateAotCfgInlined` block discovery (both `NewPc = IExecPc+2` and `NextPc = IExecPc+1`
  are outside the 1-instruction window) → ALSO exits via pExit via the CondBranch to pExit on
  the false path. Both paths exit identically. `State.TrapPc` and `State.SyscallVector` are
  indistinguishable between the two outcomes.

**Bug 2 — static JMP target lost.** A `TagBranch` (static JMP) instruction in a 1-instruction
window has `Target(NewPc) = pExit` (since NewPc is outside the window). The AotGenerator emits
`Branch(pExit)`. Both taken and fall-through paths exit to pExit. `State.TrapPc = CPU_SMC_NO_TRAP`.
The old handler fell into the `else { Pc = OuterRetPc; }` arm and treated JMP as fall-through,
discarding the static jump target.

**Bug 3 — inner trap re-fires XCT.** When the inner instruction trapped (IOT/HLT), the old code
did `State.TrapPc = OuterRetPc; continue;`. The outer loop's `continue` goes back to the top,
resets `State.TrapPc = CPU_SMC_NO_TRAP` and `State.SyscallVector = CPU_NO_SYSCALL`, then
re-translates and re-executes from `Pc` (still at the XCT address). The inner trap's
`SyscallVector` is cleared before reaching the IOT/HLT handlers below the EXEC_ONE block — those
handlers are NEVER reached. XCT-of-HLT looped 100000× rather than halting.

### Solution

**`GenerateAotExecOne`** (new function in `LibCPU/aot/AotGenerator.h/.cpp`): translates a single
instruction at `Pc` and arranges for `CPU_STATE.DispPc` to hold the resolved successor address
on ALL normal exits:

- `TagConditional` (SAS/SAD, skip): emits `CondBranch(cond, pTakenExit, pFallExit)`.
  `pTakenExit` writes `SetDisp(NewPc)` before branching to `pExit`.
  `pFallExit` writes `SetDisp(NextPc)`.
  → After execution: `DispPc = NewPc` (taken) or `DispPc = NextPc` (not taken). Distinguishable.
- `TagBranch` (JMP): emits `Branch(pTakenExit)`. `pTakenExit` writes `SetDisp(NewPc)`.
  → `DispPc = NewPc` (static jump target).
- `TagContinue` (fall): emits `Branch(pFallExit)`. → `DispPc = NextPc`.
- `TagTrap` (IOT/HLT): TranslateInstr emits a Syscall/IndirectBranch that terminates execution
  via `return ExecSmc` before reaching the successor-latching blocks. `TrapPc` and
  `SyscallVector` are set by the Syscall opcode; `DispPc` is not meaningful.

Requires `ICpuSmcEmitter::SetDispatchTarget`. Returns `E_NOTIMPL` if the backend doesn't support
it; the caller falls back to `GenerateAotCfg`.

**Handler restructure in `lcx.cpp`**:

1. Uses `GenerateAotExecOne` (with `GenerateAotCfg` fallback) for inner execute.
2. Pre-sets `State.DispPc = INextPc` as a safe default before execute (catches the fallback case).
3. Successor resolution after normal execute:
   - `TrapPc != NO_TRAP` → indirect/computed branch → `Pc = TrapPc`
   - `ITag & TagBranch` → `Pc = DispPc` (= static JMP target = `INewPc`)
   - else (TagConditional or fall-through) → `Pc = OuterRetPc + (DispPc - INextPc)`
     - Not-taken: `DispPc = INextPc` → delta = 0 → `Pc = XCT+1`
     - Taken skip: `DispPc = INewPc` → delta = skip_count → `Pc = XCT+1+delta`
     - Fall-through: `DispPc = INextPc` → delta = 0 → `Pc = XCT+1`
4. Inner trap dispatch (Bug 3 fix): when inner instruction traps (`SyscallVector != NO_SYSCALL`),
   dispatch it **inline** (call `Pdp1IoTrap` for IOT; `break` for HLT/unknown). Do NOT `continue`
   to the outer loop — that would reset state and re-fire the XCT from the beginning.

### Files changed

- `LibCPU/aot/AotGenerator.h` — declaration of `GenerateAotExecOne`
- `LibCPU/aot/AotGenerator.cpp` — implementation of `GenerateAotExecOne`
- `LibCPU/test/lcx.cpp` — EXEC_ONE handler: use `GenerateAotExecOne`, fix successor mapping,
  fix inner trap dispatch
- `LibCPU/CMakeLists.txt` — 3 new sub-tests in `upcl.pdp1-xct`

### New tests (added to `upcl.pdp1-xct`)

**xctnt — skip-NOT-taken (Bug 1 fix)**
- Program: `LAW 5 / DAC 0o31 / LAW 3 / XCT 0o30 / LAW 7 / DAC 0o40 / HLT; at 0o30: SAS 0o31`
- SAS condition: `AC=3 == M[0o31]=5` → FALSE → skip NOT taken → XCT+1 (LAW 7) runs, AC=7.
- Assert: `M[0o40] = 0x00007`.
- Old bug: `ITag & TagConditional` → wrongly adds delta → Pc = XCT+2 = DAC 0o40, AC=3 → M[0o40]=3.

**xctjmp — static JMP (Bug 2 fix)**
- Program: `LAW 0 / XCT 0o30 / LAW 1 / HLT; at 0o30: JMP 0o32 / 0 / LAW 9 / DAC 0o50 / HLT`
- JMP 0o32: Pc = 0o32 → LAW 9 → AC=9 → DAC 0o50 → M[0o50]=9.
- Assert: `M[0o50] = 0x00009`.
- Old bug: JMP treated as fall-through → Pc = XCT+1 = LAW 1, AC=1, M[0o50] never written.

**xcthlt — inner HLT (Bug 3 fix)**
- Program: `IDX 0o50 / XCT 0o30 / HLT; at 0o30: HLT; at 0o50: 0`
- With fix: IDX runs once (M[0o50]=1) → XCT fires → inner HLT → `break` → done.
- Assert: `M[0o50] = 0x00001`.
- Old bug: inner HLT re-fires XCT from Pc=0 → IDX runs 100000 times → M[0o50]=100000 (≠ 1).

### Test output

All 6 sub-tests pass:
```
upcl.pdp1-xct ... Passed  (all: fall-through, skip-taken, nesting, skip-not-taken, jmp, hlt)
```

### Regression result

`ctest -R "upcl.pdp1|upcl.pdp10"` — 15/15 PASS.
`ctest -R "^upcl" -E "ns32k-run"` — 123/123 PASS.
`upcl.ns32k-run` times out on BOTH original and fixed code — pre-existing flaky test, not a
regression. `upcl.m88k-obsd-sync` PASS standalone.

No changes were made to `LibCPU/upcl/` (only `AotGenerator.h/.cpp` and `lcx.cpp`).
