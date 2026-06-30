# DEC PDP-1 UPCL Frontend — Design

**Goal:** Turn `upcl/examples/pdp1.upcl` from a no-`encode` parse-only stub into a complete
decoding / disassembling / executing DEC PDP-1 frontend, capable of running the game
**Spacewar! 3.1** through `lcx`.

**Status:** approved scope — full ISA, Spacewar-capable (Type 30 display + control input modelled),
hardware multiply/divide gated as a cpu variant, and *both* a RIM paper-tape loader and a raw
word-image loader. The IOT/display/control host handler lives in its own `Pdp1Io.cpp`.

---

## 1. Background and oracle

The PDP-1 (1959) is an 18-bit, **word-addressed**, **one's-complement** machine: 5-bit opcode
(bits 0–4, DEC numbers bit 0 = MSB), one indirect bit (bit 5), and a 12-bit address Y (bits
6–17) selecting one of 4096 words in a memory field. A single accumulator (AC) and in-out
register (IO), a 12/16-bit program counter, an Overflow flip-flop, six program flags, six sense
switches, and an 18-bit test-word register make up the programmer-visible state.

**Encoding + semantics oracle:** the SIMH `PDP1/` simulator (`pdp1_defs.h`, `pdp1_cpu.c`,
`pdp1_sys.c`, `pdp1_stddev.c`, `pdp1_dpy.c`), cross-checked against the masswerk "Inside
Spacewar!" notes and the 1962 DEC maintenance manual. Every octal opcode, mask, and arithmetic
rule in this document is quoted from those SIMH source lines (see the research report in the
session record). The PDP-1's specs are historically "sketchy or contradictory" (SIMH's own
header note); where SIMH and DEC docs disagree, **SIMH governs**.

**Engine reuse (already on `feat/com-core`):** word-addressing (`byte_size == word_size != 8`
auto-classifies the 18-bit machine as word-addressed), the `@ea`-style data-dependent indirect
chain as a body macro, `cpu … extends …` feature unions, `PromoteLoopLocals` (loop-carried
scratch), and `@trap(...)` → host syscall-vector dispatch. The PDP-1 needs **no new
word-addressing engine work**; it is data (the `.upcl`) plus a host-side I/O personality.

---

## 2. Architecture — the LibCPU seam

The work splits across the frontend/backend boundary exactly like the PDP-11 UNIX `sys`-trap and
the M24's INT-vector devices:

```
  guest PDP-1 program (Spacewar)
        │  decode + execute (pure CPU semantics)
        ▼
  upcl/examples/pdp1.upcl   ── IOT instruction ──▶  @trap(IOT_VECTOR)
        │                                                  │  State.SyscallVector / TrapPc
        ▼                                                  ▼
  CPU_STATE (AC, IO, PC, OV, core memory)        Pdp1Io.cpp  (host I/O personality)
                                                   ├─ Type 30 display  → point buffer / terminal
                                                   ├─ control input    ← keyboard (IKeyboardSink)
                                                   ├─ typewriter / paper tape
                                                   └─ HLT / sequence-break
```

- **The `.upcl` file describes only the CPU.** IOT decodes to a host trap; the CPU body never
  models a device. This keeps the frontend a pure ISA description and makes the I/O independently
  testable.
- **`Pdp1Io.cpp` is the host personality.** The `lcx run` loop already checks
  `State.SyscallVector != CPU_NO_SYSCALL` after each run window and dispatches to a personality
  handler (today `Pdp11UnixSyscall`, `ObsdM88kSyscall`). A new `Pdp1IoTrap(...)` handler joins
  that dispatch, keyed on the PDP-1 IOT vector. It reads the trapping IOT instruction word from
  guest core at `TrapPc − 1` to recover the device code (low 6 bits) and pulse field — the same
  technique `Pdp11UnixSyscall` uses (`M16(Resume − 2)`).

### Host access to guest registers

`Pdp1Io` must read/write the guest's AC, IO, PC, Overflow and core memory. It does so through the
same register-file / `CPU_STATE` accessor that the PDP-10 `.SAV` run-to-HALT test uses to assert
final AC state (see `LoadSav` + the pdp10 execution test in `LibCPU/test/lcx.cpp` and
`LibCPU/CMakeLists.txt`). The exact accessor (named `CPU_STATE` field vs. register-file index) is
confirmed against that existing code in the host-handler task; it is **not** a new mechanism.

---

## 3. The CPU description — `upcl/examples/pdp1.upcl`

### 3.1 arch header and register file

```
arch "pdp1" {
    name "DEC PDP-1";
    byte_size 18; word_size 18; psr_size 18; address_size 16;   // 16-bit extended addr space
    register_file {
        group R { [ #i18 ac ], [ #i18 io ] }
        group S { [ #i16 pc -> %PC ], [ #i1 ov ], [ #i6 pf ], [ #i6 ss ] }
        group USER { [ #i18 tw ] }   // test-word toggle switches
    }
}
```

- `address_size 16` reflects the Type-15 memory-extension address space (`ASIZE=16`,
  `MAXMEMSIZE=65536`); the standard machine uses one 4096-word field (`DAMASK=07777`).
- Sign bit is **bit 0** (`SIGN = 0400000`), word mask `DMASK = 0777777`.
- `pf` (6 program flags 1..6) and `ss` (6 sense switches 1..6) back the SKP/OPR flag tests.

### 3.2 constants (octal, from SIMH)

```
const SIGN   = 0o400000;   const DMASK  = 0o777777;
const IA     = 0o010000;   // indirect bit (bit 5)
const DAMASK = 0o007777;   // 12-bit address field
const EPCMASK= 0o170000;   // field bits of PC (high address)
const AMASK  = 0o177777;   // 16-bit extended address
// opcodes — the FULL 18-bit prototype value (5-bit op in bits 0-4):
const OP_AND=0o020000; OP_IOR=0o040000; OP_XOR=0o060000; OP_XCT=0o100000;
      OP_CALJDA=0o160000; OP_LAC=0o200000; OP_LIO=0o220000; OP_DAC=0o240000;
      OP_DAP=0o260000; OP_DIP=0o300000; OP_DIO=0o320000; OP_DZM=0o340000;
      OP_ADD=0o400000; OP_SUB=0o420000; OP_IDX=0o440000; OP_ISP=0o460000;
      OP_SAD=0o500000; OP_SAS=0o520000; OP_MUL=0o540000; OP_DIV=0o560000;
      OP_JMP=0o600000; OP_JSP=0o620000; OP_SKP=0o640000; OP_SFT=0o660000;
      OP_LAW=0o700000; OP_IOT=0o720000; OP_OPR=0o760000;
// OPR micro-op masks (within the low 13 bits): CLA=0200 CLI=04000 CMA=01000
//   LAP=0100 LAT=02000 HLT=0400 ; flag set/clear: STF bit=010, flag# = low 3 bits
// SKP masks: SZA=0100 SPA=0200 SMA=0400 SZO=01000 SPI=02000 ; SZSn=bits12-14 SZFn=bits15-17 ;
//   invert 'i' = IA (0010000)
// SFT sub-op = (f>>9)&017 ; count = popcount(f & 0777)
// IOT: dev = f & 077 ; pulse = (f>>6)&077 ; IO_WAIT=0010000 IO_CPLS=0004000
```

These exact octals are asserted in the decode tests.

### 3.3 effective address — `@ea` macro

PDP-1 indirect addressing is **multi-level / chained** in normal mode and **single-level** in
extend mode (`pdp1_cpu.c` `Ea()`). Like the PDP-10's `@ea`, the data-dependent chain lives in a
body macro:

```
macro ea ( i, y ) {
    result = ( %PC & EPCMASK ) | ( y & DAMASK );      // direct EA within current field
    #i1 defer = i;
    while ( defer != 0 ) {                              // chained indirect (normal mode)
        #i18 w = %M[ result ];
        result = ( %PC & EPCMASK ) | ( w & DAMASK );
        defer  = ( w & IA ) ? 1 : 0;
    }
}
```

(Extend-mode single-level indirect — `MA = MB & AMASK` — is gated under a later `feature extend`;
the base machine and Spacewar use normal-mode chaining. SIMH caps the chain at 16 then raises
`STOP_IND`; the engine's loop promotion handles the data-dependent count.)

### 3.4 one's-complement arithmetic helpers

```
// ADD: end-around carry, overflow on same-sign operands → opposite-sign result, then -0 → +0.
macro add1c ( a, b ) {
    #i32 s = a + b;
    #i18 r = s & DMASK;
    r = ( s > DMASK ) ? ( ( r + 1 ) & DMASK ) : r;          // end-around carry
    ov = ( ( ( ~a ^ b ) & ( a ^ r ) ) & SIGN ) ? 1 : ov;    // overflow (sticky)
    result = ( r == DMASK ) ? 0 : r;                         // -0 cleanup (ADD only)
}
// SUB: complement, add with end-around carry, overflow test, recomplement — NO -0 cleanup.
macro sub1c ( a, b ) { ... a^DMASK ... ; result = r ^ DMASK; }
```

The −0 asymmetry (ADD corrects `0777777`→`+0`; SUB does not) is a deliberate, SIMH-faithful
quirk and is asserted by a test.

### 3.5 instruction families

| Family | Members | Notes |
|---|---|---|
| Logical (mem-ref) | AND IOR XOR | `ac op= %M[@ea(i,y)]` |
| Load/store (mem-ref) | LAC LIO DAC DAP DIP DIO DZM | DAP deposits bits 6–17 only; DIP bits 0–5 only |
| Arithmetic (mem-ref) | ADD SUB IDX ISP | `@add1c`/`@sub1c`; IDX/ISP increment with end-around carry; ISP skips if AC ≥ 0 |
| Compare/skip (mem-ref) | SAD SAS | conditional `pc += 1` |
| Control transfer | JMP JSP CAL JDA XCT | JSP saves OV·extm·PC word in AC; CAL/JDA share op 007, split by bit 5; XCT executes M[Y] as an instruction |
| Operate | OPR (`op 037`) | one insn; body tests CLA/CLI/CMA/LAP/LAT/HLT/CLF/STF bits in evaluation order |
| Skip | SKP (`op 032`) | one insn; OR of SZA/SPA/SMA/SZO/SPI + sense-switch/flag tests, `i` inverts; SZO also clears OV |
| Shift/rotate | SFT (`op 033`) | one insn; sub-op selects RAL/RAR/RCL/RCR/RIL/RIR/SAL/SAR/SCL/SCR/SIL/SIR; count = # of 1-bits in low 9 (loop one step per set bit) |
| Immediate | LAW (`op 034`) | `ac = y ^ (i ? DMASK : 0)` |
| I/O | IOT (`op 035`) | `@trap(IOT_VECTOR)`; host decodes dev/pulse |
| Multiply/divide | MUS DIS (base) · MUL DIV (variant) | gated, see §3.6 |

**Augmented-group encode shape** (uniform): `encode #i18 ( op:5 = OP_x, f:13 -> f )`, body tests
`f & mask`. **Memory-ref shape:** `encode #i18 ( op:5 = OP_x, i:1 -> i, y:12 -> y )`. CAL/JDA: two
encodes both `op:5 = 007` with fixed `i:1 = 0` (CAL → EA 0100) and `i:1 = 1` (JDA → EA Y).

**Disassembly:** memory-ref and LAW/JMP/etc. print their mnemonic + operands. The combinable
groups (OPR/SKP/SFT/IOT) print the group mnemonic plus the octal `f` operand (e.g. `opr 0o1200`);
synthesising decomposed mnemonics (`cla cma`) is **out of scope** — execution correctness and
decode-to-group correctness are what the tests assert.

### 3.6 cpu variants

```
feature muldiv_step { insn mus … ; insn dis … ; }   // base PDP-1 multiply-step / divide-step
feature muldiv_hw   { insn mul … ; insn div … ; }   // Type-10 hardware option (SIMH-modelled)
cpu "pdp1"    { feature muldiv_step; }
cpu "pdp1mul" { feature muldiv_hw;   }
```

Common instructions are **ungated** (always decoded). The opcodes 026/027 are defined *only*
inside the two mutually-exclusive features, so each cpu has exactly one definition — no `extends`
union conflict. `mul`/`div` semantics come straight from SIMH (`pdp1_cpu.c`). `mus`/`dis`
(multiply-step / divide-step) semantics come from the 1962 DEC maintenance manual; if a step's
exact micro-behaviour cannot be confirmed from manual + SIMH, the implementer flags it (it is not
on the Spacewar path).

**Spacewar runs on `pdp1mul`** (it calls hardware `mul`/`div`): `lcx run … upcl:pdp1.upcl@pdp1mul`.

---

## 4. Host I/O personality — `LibCPU/test/Pdp1Io.{h,cpp}`

A single `Pdp1IoTrap(CPU_STATE *, UINT8 *Ram, UINT64 RamSize)` entry, dispatched from the run
loop. It re-reads the IOT word at `TrapPc − 1`, extracts `dev = word & 077`, and switches:

| dev (octal) | IOT | Action |
|---|---|---|
| 007 | dpy | plot point: `x=(AC>>8)&01777`, `y=(IO>>8)&01777` (one's-comp center origin), intensity from pulse bits → push to point buffer; render on terminal in `--console` mode |
| 011 | spacewar controls | load the 18-bit control word (from keyboard state) into IO |
| 003 | tyo | print FIO-DEC char from IO to host stdout |
| 004 | tyi | read host key → IO, set program flag / IOS_TTI |
| 001/002/030 | rpa/rpb/rrb | paper-tape reader (assemble frames from attached tape → IO) |
| 005/006 | ppa/ppb | paper-tape punch |
| 054/055/056 | lsm/esm/cbs | sequence-break enable/disable/clear — **Spacewar 3.1 does not require these** (pure polling loop); modelled as no-ops unless `--console` enables them |
| (HLT, via OPR) | — | a distinct `@trap(HLT_VECTOR)` stops the machine (run-to-HALT tests) |

**Point buffer:** `Pdp1Io` owns a list/grid of plotted `(x,y,intensity)`. Headless tests query it
(`count`, "was a point near (px,py) plotted"); `--console` renders it as ASCII/braille to the
terminal each frame.

**Control word:** the four-bits-per-player layout (P1 = bits 0–3 = masks `0400000 0200000
0100000 0040000`; P2 = bits 14–17 = masks `010 04 02 01`; rotate-left + rotate-right pressed
together = hyperspace). Default host key map documented in `Pdp1Io.cpp`; overridable via
`--keys`.

---

## 5. Loaders — in `LibCPU/test/lcx.cpp`

- **`LoadRim(bytes, len, Ram, RamSize, &start)`** — the RIM (read-in mode) paper-tape loader.
  Frames: channel 8 = `0200` marks a data frame; three 6-bit data frames assemble one 18-bit word
  MSB-first; non-data frames skipped. Stream is `(DIO Y)`/`(DAC Y)` control word + datum pairs
  storing `M[Y]`, terminated by a `JMP Y` that sets `start = Y`. (`OP_DIO=0320000`,
  `OP_DAC=0240000`, `OP_JMP=0600000`.) Returns the JMP target as the start PC. *(A "RIM"
  distribution tape may be a small RIM bootstrap that then reads BIN/MACRO checksummed blocks; the
  loader handles the RIM layer, and the bootstrap-then-blocks case is exercised by `spacewar.rim`.)*
- **Raw word-image loader** — load a flat sequence of 18-bit words into core from a chosen origin
  and run from a given start (parallel to the PDP-10 `.SAV` path), for the hand-assembled
  arithmetic/decode programs.
- Selected by `lcx run … upcl:pdp1.upcl[@pdp1mul]` plus a file extension / flag (`.rim` → RIM,
  raw otherwise).

---

## 6. Tests (ctest, real octal asserted — SIMH-cross-checked)

| Test | Asserts |
|---|---|
| `upcl.check-pdp1` | `lcx upcl check pdp1.upcl` parses + lowers clean (both cpus) |
| `upcl.decode-pdp1` | real octal words disassemble: e.g. `0o200000\|Y` → `lac`, `0o400000\|Y` → `add`, `0o160100`→`cal`/`0o170000\|Y`→`jda`, OPR combo `0o761200` (CLA CMA), SKP `0o640100` (SZA) + invert `0o650100`, SFT `0o661000` (RAL), IOT `0o720007` (dpy), LAW `0o700000\|Y`; each via `lcx upcl decode`, `FAIL_REGULAR_EXPRESSION "no encoding matched"` |
| `upcl.pdp1-arith` | hand-assembled raw image: one's-complement ADD end-around carry, SUB −0 quirk, OV set/clear, IDX/ISP skip — run to HLT, assert AC/IO/OV |
| `upcl.pdp1-shift` | SFT count = popcount: e.g. `ral 3s` rotates AC left 3, run-to-HLT assert |
| `upcl.pdp1-rim` | `LoadRim` loads a tiny hand-built RIM tape, deposits the right words, returns the right start; run to HLT |
| `upcl.pdp1-spacewar` | `lcx run upcl:pdp1.upcl@pdp1mul spacewar.rim`: run N cycles headless, assert the Type 30 display received plotted points (the two ships / the central star appear) and the program reaches its main loop without an illegal-instruction stop |

Interactive play (`lcx run … spacewar.rim@pdp1mul --console [--keys]`) is wired and manually
runnable, **not** an automated test (an interactive game cannot be asserted headlessly beyond the
point-plot check above).

**`spacewar.rim`** is fetched from the SIMH distribution
(`raw.githubusercontent.com/simh/simh/master/PDP1/spacewar1/spacewar.rim`, RIM format) into a test
fixture path during the loader/Spacewar task; its size and start are verified at fetch time
(`wc -c`; start = the tape's terminating JMP target).

---

## 7. File structure

| File | Change |
|---|---|
| `upcl/examples/pdp1.upcl` | rewritten: arch header, constants, `@ea`, `@add1c`/`@sub1c`, all families, OPR/SKP/SFT/IOT/LAW, `mus`/`dis`/`mul`/`div` features, two cpus |
| `LibCPU/test/Pdp1Io.h` | host I/O personality interface (`Pdp1IoTrap`, point-buffer query, key-map) |
| `LibCPU/test/Pdp1Io.cpp` | display / control / typewriter / paper-tape / sequence-break + HLT |
| `LibCPU/test/lcx.cpp` | `LoadRim` + raw loader; wire `Pdp1IoTrap` into the run-loop trap dispatch; `--console` control plumbing for pdp1 |
| `LibCPU/CMakeLists.txt` | add `Pdp1Io.cpp` to the `lcx` target; add the six ctest entries |
| `LibCPU/test/fixtures/pdp1/*.rim` | hand-built RIM + fetched `spacewar.rim` |

---

## 8. Phasing (≈13 SDD tasks, each independently testable)

1. **arch header + register file** (AC/IO/PC/OV/PF/SS/TW) + constants + `check-pdp1`.
2. **`@ea` macro** (chained indirect) + LAC/DAC + `decode-pdp1` (mem-ref subset).
3. **one's-complement arithmetic** (`@add1c`/`@sub1c`) + ADD/SUB/IDX/ISP + `pdp1-arith`.
4. **logical + load/store** (AND/IOR/XOR/LIO/DAP/DIP/DIO/DZM/SAD/SAS).
5. **control transfer** (JMP/JSP/CAL/JDA/XCT).
6. **OPR** operate group (all micro-ops, evaluation order, HLT trap).
7. **SKP** skip group (OR of conditions, sense/flag fields, `i` invert, SZO clears OV).
8. **SFT** shift/rotate group (sub-op decode, count = popcount via one-step-per-bit loop) + `pdp1-shift`.
9. **LAW** + **MUS/DIS** (base) + **MUL/DIV** (variant) + the two cpus + variant decode test.
10. **IOT decode** → `@trap(IOT_VECTOR)`, and the **`Pdp1Io` skeleton** (typewriter + HLT) wired into the run loop.
11. **loaders**: `LoadRim` + raw word-image + `pdp1-rim`.
12. **Type 30 display + control input** in `Pdp1Io` (point buffer, key map) + a headless display smoke test.
13. **Spacewar integration**: fetch `spacewar.rim`, headless run, `pdp1-spacewar` point-plot assertion; wire interactive `--console`.

---

## 9. Out of scope (YAGNI)

- PDP-1D extensions (LCH/DCH character ops, TAD/Link arithmetic, SPC Link micro-ops, extend-mode
  as default) beyond the `feature extend` hook — only what Spacewar / the base ISA needs.
- Decomposed multi-mnemonic disassembly of OPR/SKP/SFT combinations.
- Faithful cycle timing of the Type 30 / paper tape (Spacewar's loop self-times on the display).
- A windowed GUI display — the Type 30 renders to the terminal (ASCII/braille) or a headless
  point buffer.
- The sequence-break (interrupt) system as a driver of execution — modelled as no-op IOTs, since
  Spacewar 3.1 is a pure polling loop.
