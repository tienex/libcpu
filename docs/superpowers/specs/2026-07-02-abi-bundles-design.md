# ABI Bundles + `--abi family:version` Version-Gated Syscall Tables

Date: 2026-07-02
Branch: `worktree-abi-bundles` (off `feat/com-core`)

## Problem

Today `lcx --abi X` is an ad-hoc mechanism: it constructs `libX.dylib`, `dlopen`s it,
and calls the bespoke C factory `nix_personality_create()`. There is no versioning, no
bundle metadata, and no discovery/matching — unlike the real CFBundle framework used by
`.loader` / `.device` / `.backend`. Only three personalities exist (`obsd79`, `nbsd101`,
`pdp11unix`), each a plain `SHARED` dylib named by exact leaf.

We want:

1. **`.abi` bundles** — package personalities as proper CFBundles matched by metadata,
   like `.device`/`.loader`.
2. **`--abi family:version`** — e.g. `--abi openbsd:5.9`, with the version resolved to the
   *closest approximation*.
3. **Coverage of OpenBSD and NetBSD from each family's earliest release up to the latest**
   syscall tables.

### Factual floors

OpenBSD's first release was **2.0** (Oct 1996) — there was never an OpenBSD 1.0; it forked
from NetBSD 1.0. NetBSD's first `syscalls.master`-bearing release is **1.0** (1994). So the
real per-family floors are `openbsd:2.0` and `netbsd:1.0`. "From 1.0" is interpreted as
"from each family's earliest real release"; a request below a family's floor clamps up to it.

## Chosen approach (decided during brainstorming)

- **One superset `.sc` table per family** with per-call `@since..until` version ranges; the
  dispatcher gates each call against the requested target version (out-of-range → ENOSYS,
  reusing the existing NULL-descriptor gap path). One shared implementation set per family —
  the 4546-line implementation file stays singular.
- **Ranges derived from real `syscalls.master` history** by a new differ tool that ingests
  each release's master file and computes exact `@since`/`@until`.
- **Master files fetched now and vendored in-tree** under `test/libnix/masters/…`, committed,
  so the differ and build are offline-reproducible.

## Existing structure (verified)

- CLI parse + dispatch: `LibCPU/test/lcx.cpp` — `--abi` at `:785`, `LoadPersonality` at
  `:396-424` (dlopen by name), `nix_personality_setup` at `:800`, `nix_personality_syscall`
  at `:924-934`. Host seams `LcxFlatMem`/`LcxCpu` at `:311-393`.
- Personality interface: `test/libnix/nix/nix-personality.h` — `nix_personality_t` vtbl
  (`setup`/`syscall`/`destroy`), factory `nix_personality_create(void)`.
- Syscall engine ABI: `test/libnix/nix/nix-syscall.h` — `nix_monitor_t`, `nix_param_t`,
  `nix_us_syscall_if_t`, `nix_us_syscall_desc_t`. Driver `test/libnix/nix/nix-us-syscall.c`
  (`nix_us_syscall_dispatch`; NULL descriptor / NULL callback → ENOSYS).
- `.sc` format + generator: `test/libnix/tools/sc2int/` (flex/bison) → `<ns>-callbacks.c`,
  `<ns>-syscalls.h`, `<ns>-args.h`. Tables: `test/libnix/obsd79/obsd79.sc` (220 calls,
  LIMIT 331), `test/libnix/nbsd101/nbsd101.sc` (267 calls, LIMIT 500).
- Personalities: `test/libnix/{obsd79,nbsd101,pdp11unix}/` — each `-personality.c`,
  `-init.c`, `-syscalls.c`, `.sc`, plus `arch/<guest>/…-guest.c`.
- COM bundle framework: `LibCPU/cmake/LibCPUAdd{Loader,Device,Backend}.cmake` +
  `*Info.plist.in`; `LibCPU/include/LibCPU/{ILoader,IDevice}.h`; enumerate+match+dlopen in
  `LibCPU/core/MachineBuilder.cpp` (`AddBundleDirectory`, `ReadPlistStringArray`,
  `LoadDeviceBundle`) and `lcx.cpp` (`LoaderModules`/`RunLoader`, `:201-242`).

## Design

### 1. `.abi` bundle packaging

- New `LibCPU/cmake/LibCPUAddAbi.cmake` + `LibCPU/cmake/AbiInfo.plist.in`
  (`LibCPUModuleKind = abi`). The three personalities become CFBundle `MODULE` targets
  (`BUNDLE TRUE`) producing `openbsd.abi`, `netbsd.abi`, `pdp11unix.abi`, dropped beside
  `lcx`, replacing the `SHARED` + `LIBRARY_OUTPUT_DIRECTORY` block at
  `LibCPU/CMakeLists.txt:349-378`.
- Plist match metadata:
  - `LCAbiFamily` — e.g. `openbsd`.
  - `LCAbiVersionMin` / `LCAbiVersionMax` — e.g. `2.0` / `7.9`.
  - `LCAbiAliases` — array of legacy names (`obsd79`, `nbsd101`) so `--abi obsd79` and the
    existing ctest invocations keep working.
- Entry point: keep the C `nix_personality_create` seam but pass the **resolved target
  version** in via a new `nix_abi_config_t` (family string + packed version). Signature
  becomes `nix_personality_create(const nix_abi_config_t *)`. A convenience macro
  `LIBCPU_MODULE_CREATE_ABI(Creator)` exports the standardized entry symbol, paralleling
  `LIBCPU_MODULE_CREATE_LOADER/DEVICE`. The runtime seam (`setup`/`syscall`, `nix_cpu_if_t`,
  `nix_mem_if_t`) is unchanged.

### 2. `--abi family[:version]` resolution in `lcx`

- Replace `LoadPersonality` (dlopen `lib<name>`) with an `AddBundleDirectory`-style
  enumerator over `*.abi` next to the exe, reusing `ReadPlistStringArray` and the `.device`
  CFBundle/`dlopen` path.
- Parse `family[:version]`:
  - Match a bundle whose `LCAbiFamily` (or an `LCAbiAliases` entry) equals `family`.
  - Resolve version: parse dotted-numeric, compare **component-wise** (so `10.1 > 9.0`,
    not lexical). Use the requested version directly for gating; **clamp** to
    `[LCAbiVersionMin, LCAbiVersionMax]` when outside (this is the "closest approximation").
  - `--abi family` with no version → `LCAbiVersionMax` (latest).
  - `--abi obsd79` (legacy alias) → resolves the openbsd bundle at its declared version.
- Version packing: `(major << 16) | (minor << 8) | patch` (32-bit), enough for BSD
  major.minor[.patch].

### 3. `.sc` grammar + `sc2int` — per-call version ranges

- Grammar: optional range token per call line. Examples:
  - `4 word write (word, ptr, word) @2.0` (introduced 2.0, never removed)
  - `336 word pinsyscall (...) @7.4..` (introduced 7.4)
  - `X word oldcall (...) @2.0..3.5` (present 2.0 through <3.5, then gone)
  - Absence of `@` = always present (defaults to `[family-min, +inf)`).
  - Header gains `FAMILY: openbsd`.
- `sc2int` parses the range, packs `since`/`until` (same version encoding) into new fields
  on `nix_us_syscall_desc_t`.
- Dispatch (`nix-us-syscall.c`): a descriptor whose `[since, until)` does not cover the
  monitor's target version takes the **same path as a NULL descriptor → ENOSYS**. The
  target version is stored on `nix_monitor_t` (set at setup time from `nix_abi_config_t`).

### 4. `schistory` differ (new tool)

- New `test/libnix/tools/schistory/` (C, links `nix`, built like `sc2int`).
- Parses BSD `syscalls.master`: handles `STD`, `OBSOL`, `UNIMPL`, `EXCL`, `NODEF`,
  `COMPAT_xx` / `COMPAT`, `NOARGS`, `{ proto }` blocks, and numbering gaps/ranges.
- Ingests the vendored per-release masters **in release order**; computes per (num,name):
  - `@since` = first release the entry appears as a live call.
  - `@until` = first release it is removed / becomes `OBSOL`/`UNIMPL` / is renumbered.
- Emits the annotated superset `openbsd.sc` / `netbsd.sc`. Runs as a CMake custom command
  (regenerable), like `sc2int` today. Where the master format across families differs, the
  parser is shared but family quirks are handled behind a small table.
- Argument types: map master proto C types to `.sc` types (`int`→`word`, pointers→`ptr`,
  `long`/`off_t`→`intptr`/`dword` per the guest word size, `...`→variadic) with an explicit,
  named mapping table (no magic).

### 5. Vendored masters (fetched now, committed)

- `test/libnix/masters/{openbsd,netbsd}/<version>/syscalls.master`.
- Sources: OpenBSD via `cvsweb.openbsd.org` `OPENBSD_x_y` release tags (2.0 → 7.9); NetBSD
  via `cvsweb.netbsd.org` `netbsd-x` / release tags (1.0 → 10.1).
- Reachability verified during implementation; an unreachable release is **skipped and
  logged** (never silently omitted) — the differ notes the coverage gap in a comment header
  of the emitted `.sc`.

### 6. Migration

- Namespace rename `obsd79` → `openbsd`, `nbsd101` → `netbsd`: rename source files
  (`obsd79-syscalls.c` → `openbsd-syscalls.c`, etc.), symbol prefix `obsd79_` → `openbsd_`
  across the 4546-line implementation file + `-init.c` + `-personality.c` + guest glue +
  generated header includes + per-personality `CMakeLists.txt`. Mechanical but broad.
- The current `obsd79.sc` / `nbsd101.sc` become the **seed superset** (their calls annotated
  at 7.9 / 10.1); `schistory` then backfills real `@since`/`@until` from history and produces
  the committed `openbsd.sc` / `netbsd.sc`.
- `pdp11unix` becomes an `.abi` bundle for packaging parity but **keeps its hand-switch
  dispatch** and its own lineage; version-gating is OpenBSD/NetBSD-only in this work. Its
  plist declares `LCAbiFamily = pdp11` with a nominal version range; the PDP-11 lineage
  (V6/V7/2BSD/Venix) as gated versions is flagged future work.

## Isolation / units

- `LibCPUAddAbi.cmake` + `AbiInfo.plist.in` — packaging only; no logic.
- `schistory` — pure master→`.sc` transform; testable on fixtures with no runtime deps.
- `sc2int` range support — additive to the existing generator; NULL-range = today's behavior.
- `nix-us-syscall.c` version gate — one predicate at dispatch; target version on the monitor.
- lcx bundle resolver — enumerate/match/clamp; the `nix_personality_*` runtime seam untouched.

Each unit is independently testable and communicates through the existing typed seams.

## Testing

- **Unit**: `schistory` on a tiny 3-release fixture → expected annotated `.sc` (asserts
  `@since`/`@until`, OBSOL handling, renumbering, gaps). `sc2int` range-token parsing round-trip.
  Version parse/compare/clamp.
- **Integration**: same guest binary run under `--abi openbsd:2.0` vs `--abi openbsd:7.9` —
  a late-introduced call returns ENOSYS under 2.0 and succeeds under 7.9. `--abi openbsd`
  (no version) uses latest. Legacy `--abi obsd79` / `--abi nbsd101` still pass via aliases.
- **Regression**: existing ctest personality tests (`LibCPU/CMakeLists.txt:648,659,672,2287`)
  updated to the new bundle names but the alias path keeps them green during transition.

## Out of scope

- PDP-11 lineage version-gating (V6/V7/2BSD/Venix as `@since` ranges).
- Non-BSD families (Linux/HP-UX host handlers exist in libnix but no version-gated table).
- Multi-guest-arch personalities beyond what already exists (m88k wired; sparc present but
  not wired) — unchanged by this work.
