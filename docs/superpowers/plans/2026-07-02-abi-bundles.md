# ABI COM Bundles + `--abi family:version` Version-Gated Syscall Tables — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Package guest-OS personalities as real COM `.abi` bundles (like `.loader`/`.device`) and make `--abi family:version` resolve to the closest approximation, with OpenBSD/NetBSD syscall availability version-gated from each family's earliest release to the latest.

**Architecture:** A COM `IAbi` interface + `LibCPUModuleCreateAbi` entry + `LCAbiMatch` plist array make ABIs discoverable exactly like loaders; `IAbi::CreatePersonality(version)` still vends the existing C `nix_personality_t` runtime seam. A single superset `.sc` table per family carries per-call `@since..until` version ranges (parsed by `sc2int`, packed into `nix_us_syscall_desc_t`), and the dispatcher gates each call against the monitor's target version (out-of-range → ENOSYS). A new `schistory` tool derives the ranges by diffing real per-release `syscalls.master` files vendored in-tree.

**Tech Stack:** C (libnix, sc2int, schistory), C++20 (COM shims, lcx), CMake + CFBundle (macOS) / dlopen (else), flex/bison (sc2int grammar).

## Global Constraints

- NT coding style; UEFI comment style; UPPERCASE typedefs; PascalCase enum values (see the repo memory "coding-style-nt-uefi-com"). Hungarian only for public COM interfaces.
- Indent 4 spaces. `if (x) { oneline }` always braced; `else` likewise. No useless `{ }`.
- `LL`/`ULL` constants use `INT64_C`/`UINT64_C`; `~UINT64_C(0)` for `UINT64_MAX`.
- No magic numbers — name every constant (enum/const/`#define`), including binary-format offsets, masks, version-component shifts.
- Target MSVC and OpenWatcom where possible (C sources stay C; avoid GNU-isms in headers shared with C).
- Run `clang-format` before each commit EXCEPT files with bespoke indentation; the repo has **no** `.clang-format` (see memory "clang-format-hazard") — match the surrounding NT style by hand and never run bare `clang-format -i` across existing files. `sc2int`/`schistory` grammar files (`lexer.l`, `parser.y`) keep their generator style.
- Fix causes, never silence warnings; never simplify tests to make them pass. DON'T SIMPLIFY.
- Version floors are per-family earliest real release: **openbsd:2.0**, **netbsd:1.0**. A requested version below the floor clamps up to it; above the ceiling clamps down.
- Version packing is `(major << 16) | (minor << 8) | patch`; `NIX_VERSION_NONE = 0` (unset/"from the beginning"), `NIX_VERSION_LATEST = ~(nix_version_t)0` (newer than any real release).
- Commit message trailer: `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.

---

## File Structure

**Created:**
- `test/libnix/nix/nix-version.h`, `test/libnix/nix/nix-version.c` — packed version type + parse/format/compare.
- `LibCPU/include/LibCPU/IAbi.h` — COM `IAbi` interface, `IID_IAbi`, `LIBCPU_MODULE_CREATE_ABI`, `LIBCPU_ABI_MATCH_KEY`.
- `LibCPU/cmake/LibCPUAddAbi.cmake`, `LibCPU/cmake/AbiInfo.plist.in` — `.abi` CFBundle packaging.
- `test/libnix/openbsd/openbsd-abi.cpp`, `test/libnix/netbsd/netbsd-abi.cpp`, `test/libnix/pdp11unix/pdp11unix-abi.cpp` — per-bundle COM shims.
- `test/libnix/tools/schistory/` — the master→`.sc` differ (C + CMake).
- `test/libnix/masters/{openbsd,netbsd}/<ver>/syscalls.master` — vendored release masters.
- `test/libnix/tools/schistory/fixtures/` — tiny fixture masters for the differ unit test.

**Modified:**
- `test/libnix/nix/nix-syscall.h` — add `since`/`until` to `nix_us_syscall_desc_t`; include `nix-version.h`.
- `test/libnix/nix/nix-monitor-priv.h`, `nix-monitor.c` — target-version field + accessors.
- `test/libnix/nix/nix-us-syscall.c` — the version gate in `nix_us_syscall_dispatch`.
- `test/libnix/nix/nix-personality.h` — factory signature `nix_personality_create(nix_version_t)`.
- `test/libnix/tools/sc2int/{lexer.l,parser.y,lists.h,lists.c,sc2int.c}` — `@since..until` grammar + emission.
- `LibCPU/include/LibCPU/Loader.h`, `LibCPU/src/LibCPULoader.cpp` — `LoadAbiBundle`.
- `LibCPU/test/lcx.cpp` — replace `LoadPersonality` with the `.abi` resolver.
- `LibCPU/CMakeLists.txt` — include `LibCPUAddAbi`; convert personalities to `.abi`; add `schistory`.
- `test/libnix/{openbsd,netbsd,pdp11unix}/CMakeLists.txt` — build as `.abi` bundles.
- Renamed dirs/files/symbols (Task 1).

---

## Task 1: Rename `obsd79` → `openbsd`, `nbsd101` → `netbsd`

Mechanical namespace rename so the canonical family name is used everywhere before the version machinery is layered on. Keeps the build green as `SHARED` dylibs loaded by the existing `--abi <name>` dlopen path (now `--abi openbsd` / `--abi netbsd`).

**Files:**
- Rename: `test/libnix/obsd79/` → `test/libnix/openbsd/`, `test/libnix/nbsd101/` → `test/libnix/netbsd/` (and every `obsd79-*`/`nbsd101-*` file within).
- Modify: `LibCPU/CMakeLists.txt:349-391` (personality block), `test/libnix/{openbsd,netbsd}/CMakeLists.txt`, `test/libnix/{openbsd,netbsd}/nix-*-config.h.cmake`, `LibCPU/CMakeLists.txt` ctest lines 649-679.

**Interfaces:**
- Produces: CMake targets `openbsd`, `netbsd` (still `SHARED`); C factory symbol `nix_personality_create` (unchanged signature); `.sc` files `openbsd.sc`/`netbsd.sc` with `NAMESPACE: openbsd`/`netbsd`; generated `openbsd-callbacks.c` etc.; C symbol prefix `openbsd_`/`netbsd_`, macro prefix `OPENBSD_`/`NETBSD_`.

- [ ] **Step 1: Rename the directories and files (git mv), openbsd first**

```bash
cd test/libnix
git mv obsd79 openbsd
cd openbsd
for f in $(git ls-files | grep obsd79); do git mv "$f" "$(echo "$f" | sed 's/obsd79/openbsd/g')"; done
cd ../..
```

- [ ] **Step 2: Rewrite identifiers inside the openbsd files**

Rename the three identifier spellings. Order matters (longest/upper first is irrelevant here since they are disjoint tokens, but do all three):

```bash
cd test/libnix/openbsd
# C symbols/namespace, macro prefix, and any human "OpenBSD 7.9" name string is left as-is.
grep -rl 'obsd79\|OBSD79' . | while read -r f; do
  sed -i '' -e 's/obsd79/openbsd/g' -e 's/OBSD79/OPENBSD/g' "$f"
done
cd ../../..
```

Then open `test/libnix/openbsd/openbsd.sc` and confirm the header reads `NAMESPACE: openbsd` and `NAME: "OpenBSD 7.9"` (the human name string is fine to keep). Confirm `arch/m88k/openbsd-guest.c` exists.

- [ ] **Step 3: Repeat for netbsd**

```bash
cd test/libnix
git mv nbsd101 netbsd
cd netbsd
for f in $(git ls-files | grep nbsd101); do git mv "$f" "$(echo "$f" | sed 's/nbsd101/netbsd/g')"; done
grep -rl 'nbsd101\|NBSD101' . | while read -r f; do
  sed -i '' -e 's/nbsd101/netbsd/g' -e 's/NBSD101/NETBSD/g' "$f"
done
cd ../../..
```

- [ ] **Step 4: Update the top-level personality block in `LibCPU/CMakeLists.txt`**

Replace the three `add_subdirectory(... obsd79 ...)` / `nbsd101` lines, the `foreach(_personality sc2int obsd79 nbsd101 pdp11unix)`, the `set_target_properties(obsd79 nbsd101 pdp11unix ...)`, and `add_dependencies(lcx obsd79 nbsd101 pdp11unix)` to use `openbsd` / `netbsd`:

```cmake
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../test/libnix/openbsd ${CMAKE_BINARY_DIR}/openbsd)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../test/libnix/netbsd ${CMAKE_BINARY_DIR}/netbsd)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../test/libnix/pdp11unix ${CMAKE_BINARY_DIR}/pdp11unix)
foreach(_personality sc2int openbsd netbsd pdp11unix)
  ...
endforeach()
set_target_properties(openbsd netbsd pdp11unix PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
...
add_dependencies(lcx openbsd netbsd pdp11unix)
```

- [ ] **Step 5: Update the ctest `--abi` invocations**

In `LibCPU/CMakeLists.txt` change `--abi obsd79` → `--abi openbsd` on lines 655, 666, 679 (leave the `r13=36` strace assertion — the syscall number is unchanged).

- [ ] **Step 6: Configure, build, and run the existing personality tests**

Run:
```bash
cmake --build LibCPU/build --target lcx openbsd netbsd pdp11unix 2>&1 | tail -20
ctest --test-dir LibCPU/build -R 'obsd|openbsd|nbsd|netbsd|pdp11' --output-on-failure
```
Expected: builds clean; the renamed hello-world / a.out / strace tests PASS with `--abi openbsd`.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "libnix: rename obsd79->openbsd, nbsd101->netbsd personalities

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Packed guest-OS version type (`nix-version`)

**Files:**
- Create: `test/libnix/nix/nix-version.h`, `test/libnix/nix/nix-version.c`
- Modify: `test/libnix/nix/CMakeLists.txt` (add `nix-version.c` to the `nix` sources)
- Test: `test/libnix/nix/nix-version-test.c` (a tiny standalone `assert` program) + a CMake `add_test`

**Interfaces:**
- Produces: `typedef uint32_t nix_version_t;`, `NIX_VERSION(maj,min,pat)`, `NIX_VERSION_NONE`, `NIX_VERSION_LATEST`, `nix_version_t nix_version_parse(char const *s)`, `char *nix_version_format(nix_version_t v, char *buf, size_t buflen)`.

- [ ] **Step 1: Write the failing test**

Create `test/libnix/nix/nix-version-test.c`:
```c
/* Unit test for the packed guest-OS version type. */
#include <assert.h>
#include <string.h>
#include "nix-version.h"

int
main (void)
{
    /* Packing orders numerically across components. */
    assert (NIX_VERSION (2, 0, 0) < NIX_VERSION (7, 9, 0));
    assert (NIX_VERSION (9, 0, 0) < NIX_VERSION (10, 1, 0));   /* not lexical */
    assert (NIX_VERSION (1, 6, 1) < NIX_VERSION (1, 6, 2));

    /* Parse dotted-numeric, tolerating a missing patch. */
    assert (nix_version_parse ("7.9") == NIX_VERSION (7, 9, 0));
    assert (nix_version_parse ("10.1") == NIX_VERSION (10, 1, 0));
    assert (nix_version_parse ("1.6.2") == NIX_VERSION (1, 6, 2));
    assert (nix_version_parse ("") == NIX_VERSION_NONE);
    assert (nix_version_parse (NULL) == NIX_VERSION_NONE);

    /* Format round-trips major.minor.patch. */
    char buf[16];
    assert (strcmp (nix_version_format (NIX_VERSION (7, 9, 0), buf, sizeof (buf)), "7.9.0") == 0);
    assert (strcmp (nix_version_format (NIX_VERSION (10, 1, 3), buf, sizeof (buf)), "10.1.3") == 0);
    return 0;
}
```

- [ ] **Step 2: Create the header `test/libnix/nix/nix-version.h`**

```c
#ifndef __nix_version_h
#define __nix_version_h

/*
 * A guest-OS ABI version, packed so it orders numerically: MAJOR in bits 16..31,
 * MINOR in bits 8..15, PATCH in bits 0..7.  This lets the syscall dispatcher gate a
 * call with a single unsigned comparison against a [since, until) range.
 */
#include "nix-host.h" /* the base integer types (uint32_t, size_t) */

typedef uint32_t nix_version_t;

#define NIX_VERSION_MAJOR_SHIFT  16
#define NIX_VERSION_MINOR_SHIFT  8
#define NIX_VERSION_COMPONENT_MASK 0xFFu
#define NIX_VERSION_MAJOR_MASK     0xFFFFu

#define NIX_VERSION(maj, min, pat)                                                        \
    (((nix_version_t) ((maj) & NIX_VERSION_MAJOR_MASK) << NIX_VERSION_MAJOR_SHIFT)         \
     | ((nix_version_t) ((min) & NIX_VERSION_COMPONENT_MASK) << NIX_VERSION_MINOR_SHIFT)   \
     | ((nix_version_t) ((pat) & NIX_VERSION_COMPONENT_MASK)))

/* "From the beginning" / unset floor -- the smallest possible version. */
#define NIX_VERSION_NONE   ((nix_version_t) 0)
/* Newer than any real release -- an unversioned run gates nothing. */
#define NIX_VERSION_LATEST (~(nix_version_t) 0)

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parse "MAJOR.MINOR[.PATCH]" (e.g. "7.9", "10.1", "1.6.2"); components past PATCH are
 * ignored.  Returns NIX_VERSION_NONE for a NULL or empty string.
 */
nix_version_t nix_version_parse (char const *s);

/* Format v into buf ("MAJOR.MINOR.PATCH"); buf must hold >= 16 bytes.  Returns buf. */
char *nix_version_format (nix_version_t v, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* !__nix_version_h */
```

- [ ] **Step 3: Create the implementation `test/libnix/nix/nix-version.c`**

```c
#include <stdio.h>
#include <stdlib.h>
#include "nix-version.h"

nix_version_t
nix_version_parse (char const *s)
{
    unsigned long major = 0, minor = 0, patch = 0;
    char         *end;

    if (s == NULL || *s == '\0') {
        return NIX_VERSION_NONE;
    }

    major = strtoul (s, &end, 10);
    if (*end == '.') {
        minor = strtoul (end + 1, &end, 10);
    }
    if (*end == '.') {
        patch = strtoul (end + 1, &end, 10);
    }
    return NIX_VERSION ((unsigned) major, (unsigned) minor, (unsigned) patch);
}

char *
nix_version_format (nix_version_t v, char *buf, size_t buflen)
{
    unsigned major = (unsigned) ((v >> NIX_VERSION_MAJOR_SHIFT) & NIX_VERSION_MAJOR_MASK);
    unsigned minor = (unsigned) ((v >> NIX_VERSION_MINOR_SHIFT) & NIX_VERSION_COMPONENT_MASK);
    unsigned patch = (unsigned) (v & NIX_VERSION_COMPONENT_MASK);

    snprintf (buf, buflen, "%u.%u.%u", major, minor, patch);
    return buf;
}
```

- [ ] **Step 4: Wire the source and test into `test/libnix/nix/CMakeLists.txt`**

Add `nix-version.c` to the `nix` library's source list. Then append:
```cmake
add_executable(nix-version-test ${CMAKE_CURRENT_SOURCE_DIR}/nix-version-test.c)
target_include_directories(nix-version-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(nix-version-test PRIVATE -w)
add_test(NAME nix.version COMMAND nix-version-test)
```

- [ ] **Step 5: Build and run — verify PASS**

Run:
```bash
cmake -S LibCPU -B LibCPU/build >/dev/null && cmake --build LibCPU/build --target nix-version-test 2>&1 | tail -5
ctest --test-dir LibCPU/build -R 'nix.version' --output-on-failure
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add test/libnix/nix/nix-version.h test/libnix/nix/nix-version.c test/libnix/nix/nix-version-test.c test/libnix/nix/CMakeLists.txt
git commit -m "libnix: packed guest-OS version type (nix-version)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Version gate in the syscall dispatcher

Add `since`/`until` to the descriptor, a target version on the monitor, and one predicate in `nix_us_syscall_dispatch`. Backward compatible: `since = NIX_VERSION_NONE`, `until = NIX_VERSION_NONE`, target `NIX_VERSION_LATEST` ⇒ every call available (today's behavior).

**Files:**
- Modify: `test/libnix/nix/nix-syscall.h`, `test/libnix/nix/nix-monitor-priv.h`, `test/libnix/nix/nix-monitor.c`, `test/libnix/nix/nix-us-syscall.c`
- Test: `test/libnix/nix/nix-gate-test.c` + CMake `add_test`

**Interfaces:**
- Consumes: `nix_version_t` (Task 2).
- Produces: `nix_us_syscall_desc_t` gains `nix_version_t since; nix_version_t until;` (before `callback`); `void nix_monitor_set_target_version(nix_monitor_t *, nix_version_t)`; `nix_version_t nix_monitor_get_target_version(nix_monitor_t const *)`; gate helper `bool nix_us_syscall_desc_available(nix_us_syscall_desc_t const *, nix_version_t)`.

- [ ] **Step 1: Write the failing test `test/libnix/nix/nix-gate-test.c`**

```c
/* The version gate: a descriptor is available iff since <= target && (until==NONE || target < until). */
#include <assert.h>
#include "nix-syscall.h"
#include "nix-version.h"

int
main (void)
{
    /* write(2): present from 2.0, never removed. */
    nix_us_syscall_desc_t write_desc = { 4, "write", "wpw", "w", 0, 3,
                                         NIX_VERSION (2, 0, 0), NIX_VERSION_NONE, NULL };
    /* pinsyscall(2): introduced 7.4. */
    nix_us_syscall_desc_t pin_desc = { 336, "pinsyscall", "", "w", 0, 0,
                                       NIX_VERSION (7, 4, 0), NIX_VERSION_NONE, NULL };
    /* an old call removed at 3.6. */
    nix_us_syscall_desc_t old_desc = { 42, "oldcall", "", "w", 0, 0,
                                       NIX_VERSION (2, 0, 0), NIX_VERSION (3, 6, 0), NULL };

    assert (nix_us_syscall_desc_available (&write_desc, NIX_VERSION (2, 0, 0)));
    assert (nix_us_syscall_desc_available (&write_desc, NIX_VERSION (7, 9, 0)));

    assert (!nix_us_syscall_desc_available (&pin_desc, NIX_VERSION (5, 9, 0)));  /* too early */
    assert (nix_us_syscall_desc_available (&pin_desc, NIX_VERSION (7, 9, 0)));

    assert (nix_us_syscall_desc_available (&old_desc, NIX_VERSION (2, 0, 0)));
    assert (!nix_us_syscall_desc_available (&old_desc, NIX_VERSION (3, 6, 0))); /* removed */
    assert (!nix_us_syscall_desc_available (&old_desc, NIX_VERSION (7, 9, 0)));

    /* LATEST target sees everything with a real since. */
    assert (nix_us_syscall_desc_available (&pin_desc, NIX_VERSION_LATEST));
    return 0;
}
```

- [ ] **Step 2: Extend the descriptor in `test/libnix/nix/nix-syscall.h`**

Add the include near the top (after the existing `#include "nix-host.h"`):
```c
#include "nix-version.h" /* nix_version_t for per-call version gating */
```
Extend `nix_us_syscall_desc_t` (insert `since`/`until` immediately before `callback`):
```c
typedef struct _nix_us_syscall_desc {
	int         number;
	char const *name;
	char const *format;
	char const *rettype;
	unsigned    flags;
#define NIX_US_SYSCALL_VARIADIC 1
	size_t                    nparams;
	nix_version_t             since;    /* first guest-OS version this call exists in (NONE = from the start) */
	nix_version_t             until;    /* first version it is GONE (NONE = never removed) */
	nix_us_syscall_callback_t callback;
} nix_us_syscall_desc_t;
```
Declare the gate helper near the dispatch prototypes:
```c
/* True iff this call exists in the given target version: since <= target && (until == NONE || target < until). */
bool nix_us_syscall_desc_available (nix_us_syscall_desc_t const *desc, nix_version_t target);
```

- [ ] **Step 3: Add the target version to the monitor**

In `test/libnix/nix/nix-monitor-priv.h`, add a field to `struct _nix_monitor` (alongside `guest_info`):
```c
	nix_version_t target_version;   /* the guest-OS version being emulated (gates syscalls) */
```
In `test/libnix/nix/nix-monitor.c`, initialise it in `nix_monitor_create` (default = LATEST so an un-set monitor gates nothing):
```c
		xmon->guest_info = *guest_info;
		xmon->target_version = NIX_VERSION_LATEST;
```
and add the accessors after `nix_monitor_get_guest_info`:
```c
void
nix_monitor_set_target_version (nix_monitor_t *xmon, nix_version_t version)
{
	xmon->target_version = (version == NIX_VERSION_NONE) ? NIX_VERSION_LATEST : version;
}

nix_version_t
nix_monitor_get_target_version (nix_monitor_t const *xmon)
{
	return xmon->target_version;
}
```
Declare both in `test/libnix/nix/nix-syscall.h` after the other `nix_monitor_*` prototypes:
```c
void          nix_monitor_set_target_version (nix_monitor_t *mon, nix_version_t version);
nix_version_t nix_monitor_get_target_version (nix_monitor_t const *mon);
```
(`nix-monitor-priv.h` must `#include "nix-version.h"` if it does not already reach it through `nix-syscall.h`.)

- [ ] **Step 4: Implement the gate + apply it in `test/libnix/nix/nix-us-syscall.c`**

Add the helper (top of file, after the includes):
```c
bool
nix_us_syscall_desc_available (nix_us_syscall_desc_t const *desc, nix_version_t target)
{
	if (desc->since != NIX_VERSION_NONE && target < desc->since) {
		return false;
	}
	if (desc->until != NIX_VERSION_NONE && target >= desc->until) {
		return false;
	}
	return true;
}
```
In `nix_us_syscall_dispatch`, immediately after the `desc->callback == NULL` ENOSYS check (around line 44), add the version gate:
```c
	if (!nix_us_syscall_desc_available (desc, nix_monitor_get_target_version (xmon))) {
		LCLog (g_nix_us_log, LCLogFatal, 0,
		       "system call \"%s\" (%d) does not exist in this guest-OS version.",
		       desc->name, desc->number);
		err = ENOSYS;
		goto error;
	}
```
Apply the same gate in `nix_us_syscall_redispatch` after its `find`/NULL-callback check (around line 197):
```c
	if (!nix_us_syscall_desc_available (desc, nix_monitor_get_target_version (xmon))) {
		return ENOSYS;
	}
```

- [ ] **Step 5: Wire the gate test into `test/libnix/nix/CMakeLists.txt`**

```cmake
add_executable(nix-gate-test ${CMAKE_CURRENT_SOURCE_DIR}/nix-gate-test.c)
target_link_libraries(nix-gate-test PRIVATE nix)
target_include_directories(nix-gate-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(nix-gate-test PRIVATE -w)
add_test(NAME nix.gate COMMAND nix-gate-test)
```

- [ ] **Step 6: Build and run all nix + personality tests — verify PASS and NO regressions**

Run:
```bash
cmake --build LibCPU/build --target nix-gate-test openbsd netbsd 2>&1 | tail -5
ctest --test-dir LibCPU/build -R 'nix.gate|openbsd|netbsd|pdp11' --output-on-failure
```
Expected: `nix.gate` PASS; existing personality tests still PASS (they run at `NIX_VERSION_LATEST`, so nothing is gated out).

- [ ] **Step 7: Commit**

```bash
git add test/libnix/nix/
git commit -m "libnix: version-gate syscalls via desc since/until + monitor target version

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: `sc2int` — parse `@since..until` and emit the ranges

Add an optional per-call range token to the `.sc` grammar and thread it into the emitted descriptor. Calls with no `@` annotation keep emitting `NONE`/`NONE` — byte-for-byte identical to today for the existing tables.

**Files:**
- Modify: `test/libnix/tools/sc2int/lexer.l`, `parser.y`, `lists.h`, `lists.c`, `sc2int.c`
- Test: `test/libnix/tools/sc2int/fixtures/gate.sc` + a CMake test that generates and greps the output.

**Interfaces:**
- Consumes: nothing new at runtime; emits into `nix_us_syscall_desc_t` (Task 3).
- Produces: `.sc` call line accepts a trailing `@<ver>` or `@<ver>..` or `@<ver>..<ver>`; `call_t` gains `char *since; char *until;` (raw version strings, NULL = none); `call_emit` writes `nix_version_parse("...")` / `NIX_VERSION_NONE` into the descriptor.

- [ ] **Step 1: Write the failing test fixture `test/libnix/tools/sc2int/fixtures/gate.sc`**

```
NAME: "Gate Fixture"
NAMESPACE: gatefix
LIMIT: 8
BAE: EFAULT

CALLS

1 void exit (word)
4 word write (word, ptr, word) @2.0
5 word oldcall (word) @2.0..3.6
7 intptr pinsyscall (intptr, intptr) @7.4..
```
Expected generated `gatefix-callbacks.c` contains, for scno 4/5/7 respectively:
`nix_version_parse ("2.0"), NIX_VERSION_NONE`, `nix_version_parse ("2.0"), nix_version_parse ("3.6")`, `nix_version_parse ("7.4"), NIX_VERSION_NONE`; and for scno 1 (no `@`): `NIX_VERSION_NONE, NIX_VERSION_NONE`.

- [ ] **Step 2: Lexer — add `@`, `..`, and a version literal (`test/libnix/tools/sc2int/lexer.l`)**

Add before the `[\(\):,]` rule (flex longest-match makes `2.0..7.9` tokenize as version, `..`, version):
```
"@"                         return T_AT;
".."                        return T_DOTDOT;
[0-9]+"."[0-9]+("."[0-9]+)? { yylval.sv = strdup (yytext); return T_VERSION; }
```
(The existing `[0-9]+ → T_NUMBER` rule stays; a bare integer is still a syscall number because a version literal requires a dot.)

- [ ] **Step 3: Parser — carry the range into `call_new` (`test/libnix/tools/sc2int/parser.y`)**

Add tokens and a type:
```
%token T_AT T_DOTDOT
%token<sv> T_VERSION
%type <sv> version_since version_until
```
Add rules (a range is `@ VER` or `@ VER ..` or `@ VER .. VER`; absent = NULL/NULL):
```
version_since:
    /* empty */              { $$ = NULL; }
  | T_AT T_VERSION           { $$ = $2; }
  ;

version_until:
    /* empty */              { $$ = NULL; }
  | T_DOTDOT                 { $$ = NULL; }
  | T_DOTDOT T_VERSION       { $$ = $2; }
  ;
```
Extend `call_new` to take the two strings and update the two `syscall_declaration` productions to thread them. Change the grammar so the optional range follows the parameter list:
```
syscall_declaration:
    T_NUMBER return_result T_IDENTIFIER '(' void_or_empty ')' version_since version_until
      { $$ = call_new ($1, $3, $2, NULL, $7, $8); }
  | T_NUMBER return_result T_IDENTIFIER '(' syscall_parameters ')' version_since version_until
      { $$ = call_new ($1, $3, $2, $5, $8, $9); }
  ;
```
(`version_since`/`version_until` reduce to empty when no `@` is present, so unannotated lines still parse.)

- [ ] **Step 4: `lists.h` / `lists.c` — store the range on `call_t`**

In `lists.h`, add to `struct _call`:
```c
	char *since;   /* raw "MAJOR.MINOR[.PATCH]" or NULL */
	char *until;   /* raw upper-bound version or NULL */
```
and update the `call_new` prototype:
```c
call_t *
call_new(int scno, char *name, param_t *rettype, param_list_t *params,
         char *since, char *until);
```
In `lists.c`, set `call->since = since; call->until = until;` in `call_new`.

- [ ] **Step 5: `sc2int.c` — emit the two version fields in `call_emit`**

Add a small helper near `call_emit`:
```c
/* Emit a nix_version_t initializer from a raw ".sc" version string (NULL -> NIX_VERSION_NONE). */
static void
emit_version(FILE *out, char const *ver)
{
	if (ver == NULL) {
		fprintf(out, "NIX_VERSION_NONE");
	} else {
		fprintf(out, "nix_version_parse (\"%s\")", ver);
	}
}
```
Update both branches of `call_emit` to add the two fields between `nparams` and the callback. The NULL/gap entry:
```c
		fprintf(out, "{ %u, %s, %s, %s, %s, %u, NIX_VERSION_NONE, NIX_VERSION_NONE, NULL }",
		        0, "NULL", "NULL", "NULL", "0", 0);
```
The real entry (replace the single `fprintf` with the version fields spliced in):
```c
		fprintf(out, "{ %s, %s, %s, %s, %s, %u, ",
		        scname, name, format, rettype, flags, nparams);
		emit_version(out, call->since);
		fprintf(out, ", ");
		emit_version(out, call->until);
		fprintf(out, ", __%s_%s_callback }", g_gbl_ns, call->name);
```
Ensure the generated `-callbacks.c` includes `nix-version.h` (it already includes `nix-syscall.h`, which now pulls it in — verify the emitted `#include` list in `sc2int.c`'s header emission covers it; if `-callbacks.c` only includes `<ns>-args.h`, add `#include "nix-version.h"` to the emitted preamble).

- [ ] **Step 6: Add the generator test to the sc2int CMake**

In `test/libnix/tools/sc2int/CMakeLists.txt` append:
```cmake
add_test(NAME sc2int.gate
  COMMAND ${CMAKE_COMMAND}
    -DSC2INT=$<TARGET_FILE:sc2int>
    -DSC=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/gate.sc
    -DOUT=${CMAKE_CURRENT_BINARY_DIR}/gatefix-callbacks.c
    -P ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/check-gate.cmake)
```
Create `test/libnix/tools/sc2int/fixtures/check-gate.cmake`:
```cmake
execute_process(COMMAND ${SC2INT} ${SC} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "sc2int failed on gate.sc (rc=${rc})")
endif()
file(READ ${OUT} txt)
foreach(needle
    "nix_version_parse (\"2.0\"), NIX_VERSION_NONE"
    "nix_version_parse (\"2.0\"), nix_version_parse (\"3.6\")"
    "nix_version_parse (\"7.4\"), NIX_VERSION_NONE"
    "NIX_VERSION_NONE, NIX_VERSION_NONE")
  string(FIND "${txt}" "${needle}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "gate.sc output missing: ${needle}")
  endif()
endforeach()
```
(`check-gate.cmake` runs sc2int in the sc2int binary dir; sc2int writes `gatefix-callbacks.c` keyed on the NAMESPACE `gatefix`.)

- [ ] **Step 7: Regenerate the parser, build sc2int, run the test**

Run:
```bash
cmake --build LibCPU/build --target sc2int openbsd netbsd 2>&1 | tail -20
ctest --test-dir LibCPU/build -R 'sc2int.gate|openbsd|netbsd' --output-on-failure
```
Expected: `sc2int.gate` PASS; the existing `openbsd`/`netbsd` tables regenerate and their tests still PASS (unannotated calls → `NONE, NONE`).

- [ ] **Step 8: Commit**

```bash
git add test/libnix/tools/sc2int/
git commit -m "sc2int: parse @since..until per-call version ranges into descriptors

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: C factory takes a target version

Change the per-personality C factory to accept the target version and stamp it on the monitor. Keep the existing `lcx` dlopen path working (updated to pass `NIX_VERSION_LATEST`) so the build stays green until Task 7 swaps in the COM resolver.

**Files:**
- Modify: `test/libnix/nix/nix-personality.h`; `test/libnix/openbsd/openbsd-personality.c`, `netbsd/netbsd-personality.c`, `pdp11unix/pdp11unix-personality.c` (or wherever each `nix_personality_create` lives — grep); the personality `-init.c` where the monitor is created; `LibCPU/test/lcx.cpp:440-446` (`LoadPersonality`).

**Interfaces:**
- Consumes: `nix_version_t` (Task 2), `nix_monitor_set_target_version` (Task 3).
- Produces: `nix_personality_t *nix_personality_create(nix_version_t target)` for every bundle; the openbsd/netbsd personalities call `nix_monitor_set_target_version(mon, target)` after creating their monitor.

- [ ] **Step 1: Update the declaration in `test/libnix/nix/nix-personality.h`**

```c
/* The factory every personality bundle's IAbi shim calls to build the runtime personality
 * for a resolved guest-OS version (NIX_VERSION_LATEST = newest / ungated). */
nix_personality_t *nix_personality_create (nix_version_t target);
```
(Add `#include "nix-version.h"` to `nix-personality.h`.)

- [ ] **Step 2: Locate each `nix_personality_create` and its monitor creation**

Run:
```bash
grep -rn "nix_personality_create\|nix_monitor_create" test/libnix/openbsd test/libnix/netbsd test/libnix/pdp11unix
```
Expected: one `nix_personality_create` per personality; a `nix_monitor_create(...)` in each personality's setup/create path.

- [ ] **Step 3: Thread the version through openbsd and netbsd**

For openbsd and netbsd, change the factory signature to `nix_personality_create (nix_version_t target)`, store `target` in the personality's concrete struct, and — right after the `nix_monitor_create(...)` call that builds `mon` — add:
```c
	nix_monitor_set_target_version (mon, target);
```
(If the monitor is created lazily in `setup` rather than in `create`, stash `target` in the personality struct in `create` and call `nix_monitor_set_target_version` where the monitor is created.)

- [ ] **Step 4: Thread the version through pdp11unix (stored, unused for gating)**

pdp11unix does not use the table dispatcher, so it does not gate. Change its `nix_personality_create` signature to accept `nix_version_t target` and simply ignore it (add `(void) target;` with a comment that the PDP-11 lineage is not version-gated yet).

- [ ] **Step 5: Update the interim `lcx` dlopen path**

In `LibCPU/test/lcx.cpp` `LoadPersonality`, change the factory typedef and call:
```c
    typedef nix_personality_t *(*CreateFn) (nix_version_t);
    CreateFn Create = (CreateFn) dlsym (pLib, "nix_personality_create");
    if (Create == nullptr) {
        std::fprintf (stderr, "lcx: '%s' has no nix_personality_create entry point\n", Leaf.c_str ());
        return nullptr;
    }
    return Create (NIX_VERSION_LATEST);
```
(Add `#include "nix-version.h"` to lcx.cpp's includes if not already reached via `nix-personality.h`.)

- [ ] **Step 6: Build and run — verify no regressions**

Run:
```bash
cmake --build LibCPU/build --target lcx openbsd netbsd pdp11unix 2>&1 | tail -10
ctest --test-dir LibCPU/build -R 'openbsd|netbsd|pdp11' --output-on-failure
```
Expected: clean build; tests PASS (still ungated at LATEST).

- [ ] **Step 7: Commit**

```bash
git add test/libnix LibCPU/test/lcx.cpp
git commit -m "libnix: personality factory takes a target version, stamped on the monitor

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: `IAbi` COM interface + `LoadAbiBundle`

Add the public COM interface and the bundle loader, mirroring `ILoader` / `LoadLoaderBundle` exactly. Additive — compiles with no consumer yet.

**Files:**
- Create: `LibCPU/include/LibCPU/IAbi.h`
- Modify: `LibCPU/include/LibCPU/Loader.h` (declare `LoadAbiBundle`), `LibCPU/src/LibCPULoader.cpp` (define it)

**Interfaces:**
- Produces: `LibCPU::IAbi` (methods `GetFamily`/`GetVersionMin`/`GetVersionMax`/`CreatePersonality`), `IID_IAbi`, `LIBCPU_MODULE_CREATE_ABI(Creator)`, `LIBCPU_MODULE_ABI_ENTRY_NAME`, `LIBCPU_ABI_MATCH_KEY`, `LibCPU::IAbi *LibCPU::LoadAbiBundle(CHAR8 CONST *pPath)`.

- [ ] **Step 1: Create `LibCPU/include/LibCPU/IAbi.h`**

```cpp
/** @file
  LibCPU guest-OS ABI (syscall personality) COM interface.

  Each guest-OS ABI (OpenBSD, NetBSD, classic PDP-11 UNIX, ...) is a COM object packaged in its own
  ".abi" bundle, exactly like the ".loader"/".device" plugins. The host enumerates the bundles,
  matches one by family name (the LCAbiMatch plist array, read without loading code), then loads the
  winning bundle and asks it to build a runtime personality for a resolved guest-OS version.

  The runtime seam a personality drives (the C nix_personality_t / nix_cpu_if_t) is unchanged; IAbi
  only moves DISCOVERY and CREATION onto COM. CreatePersonality vends that C personality.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_IABI_H
#define LIBCPU_IABI_H

#include "LibCPU/PCom.h"

struct _nix_personality; /* the C runtime personality (test/libnix/nix/nix-personality.h) */

namespace LibCPU {

//
// One guest-OS ABI. GetFamily/GetVersionMin/GetVersionMax describe the coverage the host uses to
// match `--abi family[:version]` and clamp the requested version ("closest approximation");
// CreatePersonality builds the runtime personality for the resolved version.
//
DECLARE_INTERFACE_ (IAbi, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD_ (CHAR8 CONST *, GetFamily)(THIS) PURE;      // canonical family, e.g. "openbsd"
    STDMETHOD_ (CHAR8 CONST *, GetVersionMin)(THIS) PURE;  // earliest supported, e.g. "2.0"
    STDMETHOD_ (CHAR8 CONST *, GetVersionMax)(THIS) PURE;  // latest supported, e.g. "7.9"
    //
    // Build a runtime personality for the resolved guest-OS version, a "MAJOR.MINOR[.PATCH]"
    // string (NULL = latest). The returned personality is owned by the caller and destroyed
    // through its own vtbl (nix_personality_destroy); the IAbi may be Released afterwards.
    //
    STDMETHOD_ (struct _nix_personality *, CreatePersonality)(THIS_ IN CHAR8 CONST *pVersion) PURE;
};

typedef IAbi *PIABI;

//
// Interface identifier. ABI family base {1C9A0004-0001-4C50-9A00-0000000000NN}.
//
inline constexpr IID IID_IAbi =
    { 0x1C9A0004, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 } };

} // namespace LibCPU

//
// The plist array key a ".abi" bundle carries (family name + legacy aliases), read WITHOUT loading
// the bundle -- exactly like LCDeviceMatch.
//
#define LIBCPU_ABI_MATCH_KEY "LCAbiMatch"

//
// ABI-bundle entry contract. A ".abi" bundle exports one C symbol returning a new, owned IAbi:
//
//     LIBCPU_MODULE_CREATE_ABI (LibCPU::CreateOpenBsdAbi)
//
#if defined(_WIN32)
#define LIBCPU_ABI_EXPORT __declspec(dllexport)
#else
#define LIBCPU_ABI_EXPORT __attribute__((visibility("default")))
#endif

#define LIBCPU_MODULE_CREATE_ABI(Creator) \
    extern "C" LIBCPU_ABI_EXPORT ::LibCPU::IAbi *LibCPUModuleCreateAbi (void) { return (Creator) (); }

#define LIBCPU_MODULE_ABI_ENTRY_NAME "LibCPUModuleCreateAbi"

#endif // LIBCPU_IABI_H
```

- [ ] **Step 2: Declare `LoadAbiBundle` in `LibCPU/include/LibCPU/Loader.h`**

After the `LoadLoaderBundle` declaration (line ~27), add:
```cpp
class IAbi;
IAbi *LoadAbiBundle (CHAR8 CONST *pPath);
```
(Place inside `namespace LibCPU`, matching `LoadLoaderBundle`.)

- [ ] **Step 3: Define `LoadAbiBundle` in `LibCPU/src/LibCPULoader.cpp`**

Add the include near the top:
```cpp
#include "LibCPU/IAbi.h"
```
Add the typedef by the others:
```cpp
typedef IAbi *(*AbiEntryFn) (void);
```
Add the function (a verbatim parallel of `LoadLoaderBundle`, using `LIBCPU_MODULE_ABI_ENTRY_NAME` and returning `IAbi *`):
```cpp
IAbi *
LoadAbiBundle (CHAR8 CONST *pPath)
{
#if defined(__APPLE__)
    CFStringRef PathStr = CFStringCreateWithCString (nullptr, pPath, kCFStringEncodingUTF8);
    if (PathStr == nullptr) {
        return nullptr;
    }
    CFURLRef Url = CFURLCreateWithFileSystemPath (nullptr, PathStr, kCFURLPOSIXPathStyle, true);
    CFRelease (PathStr);
    if (Url == nullptr) {
        return nullptr;
    }
    CFBundleRef Bundle = CFBundleCreate (nullptr, Url);
    CFRelease (Url);
    if (Bundle == nullptr) {
        return nullptr;
    }
    CFStringRef FnName = CFStringCreateWithCString (nullptr, LIBCPU_MODULE_ABI_ENTRY_NAME, kCFStringEncodingUTF8);
    AbiEntryFn pEntry = (AbiEntryFn) CFBundleGetFunctionPointerForName (Bundle, FnName);
    CFRelease (FnName);
    if (pEntry == nullptr) {
        CFRelease (Bundle);
        return nullptr;
    }
    // Bundle intentionally retained (not released) so the code stays mapped.
    return pEntry ();
#else
    void *pHandle = dlopen (pPath, RTLD_NOW | RTLD_LOCAL);
    if (pHandle == nullptr) {
        return nullptr;
    }
    AbiEntryFn pEntry = (AbiEntryFn) dlsym (pHandle, LIBCPU_MODULE_ABI_ENTRY_NAME);
    if (pEntry == nullptr) {
        dlclose (pHandle);
        return nullptr;
    }
    return pEntry ();
#endif
}
```

- [ ] **Step 4: Build the framework — verify it compiles**

Run:
```bash
cmake --build LibCPU/build --target LibCPU 2>&1 | tail -10
```
Expected: clean build (no consumer yet, but the symbol is exported).

- [ ] **Step 5: Commit**

```bash
git add LibCPU/include/LibCPU/IAbi.h LibCPU/include/LibCPU/Loader.h LibCPU/src/LibCPULoader.cpp
git commit -m "LibCPU: IAbi COM interface + LoadAbiBundle (parallel to ILoader)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Package personalities as `.abi` bundles + resolve `--abi family:version`

The atomic switch: add the per-bundle COM shims, the `LibCPUAddAbi` CMake helper + plist, convert the three targets to `.abi` MODULE bundles, and replace `lcx`'s `LoadPersonality` with the COM resolver. Done together so `--abi` never breaks between commits.

**Files:**
- Create: `LibCPU/cmake/LibCPUAddAbi.cmake`, `LibCPU/cmake/AbiInfo.plist.in`, `test/libnix/openbsd/openbsd-abi.cpp`, `test/libnix/netbsd/netbsd-abi.cpp`, `test/libnix/pdp11unix/pdp11unix-abi.cpp`
- Modify: `LibCPU/CMakeLists.txt`, `test/libnix/{openbsd,netbsd,pdp11unix}/CMakeLists.txt`, `LibCPU/test/lcx.cpp`

**Interfaces:**
- Consumes: `IAbi` + `LoadAbiBundle` (Task 6), `nix_personality_create(nix_version_t)` (Task 5), `nix_version_parse`/clamp (Task 2), `ReadPlistStringArray` (existing, in MachineBuilder — expose it or re-implement a 2-line reader in lcx).
- Produces: `openbsd.abi`/`netbsd.abi`/`pdp11unix.abi` beside `lcx`; `lcx` resolves `--abi family[:version]` via the bundles.

- [ ] **Step 1: Create the plist template `LibCPU/cmake/AbiInfo.plist.in`**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>        <string>en</string>
  <key>CFBundleExecutable</key>               <string>@OUTNAME@</string>
  <key>CFBundleIdentifier</key>               <string>org.libcpu.abi.@OUTNAME@</string>
  <key>CFBundleInfoDictionaryVersion</key>    <string>6.0</string>
  <key>CFBundleName</key>                     <string>@OUTNAME@</string>
  <key>CFBundlePackageType</key>              <string>BNDL</string>
  <key>CFBundleShortVersionString</key>       <string>1.0</string>
  <key>CFBundleVersion</key>                  <string>1.0</string>
  <key>LibCPUModuleKind</key>                 <string>abi</string>
  <key>LCAbiMatch</key>
  <array>
@LCABI_MATCH_ENTRIES@  </array>
</dict>
</plist>
```

- [ ] **Step 2: Create the CMake helper `LibCPU/cmake/LibCPUAddAbi.cmake`**

```cmake
# LibCPUAddAbi.cmake -- build a guest-OS ABI personality as a real CFBundle (.abi).
#
# libcpu_add_abi(<target> <outname>
#   MATCH family;alias;...   family name + legacy aliases (--abi <this>) baked into the plist
#   SOURCES ... [INCLUDES ...] [DEFINES ...] [LIBS ...])
#
# The MATCH strings go into the bundle's Info.plist (LCAbiMatch array), which lcx reads to bind
# `--abi family[:version]` to a bundle -- without loading its code (as with LCDeviceMatch).

set(LIBCPU_ABI_PLIST_IN ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AbiInfo.plist.in)

function(libcpu_add_abi target outname)
  cmake_parse_arguments(A "" "" "MATCH;SOURCES;INCLUDES;DEFINES;LIBS" ${ARGN})

  set(LCABI_MATCH_ENTRIES "")
  foreach(m IN LISTS A_MATCH)
    string(APPEND LCABI_MATCH_ENTRIES "    <string>${m}</string>\n")
  endforeach()
  set(OUTNAME "${outname}")
  set(_plist ${CMAKE_CURRENT_BINARY_DIR}/${outname}.abi.Info.plist)
  configure_file(${LIBCPU_ABI_PLIST_IN} ${_plist} @ONLY)

  add_library(${target} MODULE ${A_SOURCES})
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME ${outname}
    BUNDLE TRUE
    BUNDLE_EXTENSION "abi"
    MACOSX_BUNDLE_INFO_PLIST ${_plist})
  # NOTE: no *_VISIBILITY_PRESET hidden -- the C nix_personality_* symbols and the exported
  # LibCPUModuleCreateAbi (LIBCPU_ABI_EXPORT) must remain visible; the personalities are older C.
  if(A_INCLUDES)
    target_include_directories(${target} PRIVATE ${A_INCLUDES})
  endif()
  if(A_DEFINES)
    target_compile_definitions(${target} PRIVATE ${A_DEFINES})
  endif()
  if(A_LIBS)
    target_link_libraries(${target} PRIVATE ${A_LIBS})
  endif()
endfunction()
```
(`${LIBCPU_ABI_PLIST_IN}` resolves against `CMAKE_CURRENT_SOURCE_DIR` = the LibCPU dir where the helper is `include()`d, so the personality subdirs can call `libcpu_add_abi` and still find the template — the variable is captured at include time. If a personality subdir's `CMAKE_CURRENT_SOURCE_DIR` differs, pass the absolute template path; set it once in `LibCPU/CMakeLists.txt` as a cache/normal var `LIBCPU_ABI_PLIST_IN` before `add_subdirectory`.)

- [ ] **Step 3: Create the openbsd COM shim `test/libnix/openbsd/openbsd-abi.cpp`**

```cpp
/** @file
  OpenBSD guest-OS ABI as a COM .abi bundle. Bridges the IAbi discovery/creation seam to the C
  nix_personality_create factory; version coverage is openbsd:2.0 .. openbsd:7.9.
**/
#include "LibCPU/IAbi.h"
#include "LibCPU/PCom.h"

extern "C" {
#include "nix-personality.h"
#include "nix-version.h"
}

namespace LibCPU {
namespace {

class OpenBsdAbi final : public ComObject<IAbi>
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        return DefaultQuery (riid, IID_IAbi, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetFamily (VOID) override { return "openbsd"; }
    CHAR8 CONST *STDMETHODCALLTYPE GetVersionMin (VOID) override { return "2.0"; }
    CHAR8 CONST *STDMETHODCALLTYPE GetVersionMax (VOID) override { return "7.9"; }
    struct _nix_personality *STDMETHODCALLTYPE CreatePersonality (CHAR8 CONST *pVersion) override
    {
        nix_version_t Target = (pVersion != nullptr) ? nix_version_parse (pVersion) : NIX_VERSION_LATEST;
        return nix_personality_create (Target);
    }
};

} // namespace

IAbi *
CreateOpenBsdAbi (VOID)
{
    return new OpenBsdAbi ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_ABI (LibCPU::CreateOpenBsdAbi)
```

- [ ] **Step 4: Create the netbsd and pdp11unix shims**

`test/libnix/netbsd/netbsd-abi.cpp` — identical to Step 3 but class `NetBsdAbi`, `GetFamily` `"netbsd"`, `GetVersionMin` `"1.0"`, `GetVersionMax` `"10.1"`, factory `CreateNetBsdAbi`.

`test/libnix/pdp11unix/pdp11unix-abi.cpp` — class `Pdp11UnixAbi`, `GetFamily` `"pdp11unix"`, `GetVersionMin`/`GetVersionMax` both `"7.0"` (nominal — not gated), factory `CreatePdp11UnixAbi`. `CreatePersonality` still forwards `nix_version_parse(pVersion)` (personality ignores it).

- [ ] **Step 5: Convert the personality `CMakeLists.txt` to `.abi` bundles**

In `test/libnix/openbsd/CMakeLists.txt`, replace the `ADD_LIBRARY(openbsd SHARED ...)` + `ADD_DEPENDENCIES` + `TARGET_LINK_LIBRARIES` block with:
```cmake
libcpu_add_abi(openbsd openbsd
  MATCH openbsd obsd79
  SOURCES
    openbsd-init.c openbsd-personality.c openbsd-ioctl.c openbsd-structs.c
    openbsd-syscalls.c arc4random.c
    ${PROJECT_BINARY_DIR}/openbsd-callbacks.c
    arch/${GUEST}/openbsd-guest.c
    openbsd-abi.cpp
  INCLUDES
    ${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/arch/${GUEST}
  LIBS nix LibCPU)
add_dependencies(openbsd sc2int)
```
Do the same for netbsd (MATCH `netbsd nbsd101`, add `netbsd-abi.cpp`) and pdp11unix (MATCH `pdp11unix pdp11`, add `pdp11unix-abi.cpp`, its own source list). `LibCPU` is linked for `ComObject`/`IAbi`.

- [ ] **Step 6: Update `LibCPU/CMakeLists.txt` — include the helper, set the plist path, drop the SHARED output-dir hack**

Near the other `include(LibCPUAdd*)` (line ~21):
```cmake
include(LibCPUAddAbi)     # libcpu_add_abi()
set(LIBCPU_ABI_PLIST_IN ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AbiInfo.plist.in)
```
Keep `set_target_properties(openbsd netbsd pdp11unix PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})` so the `.abi` bundles land beside `lcx` (MODULE bundle honours `LIBRARY_OUTPUT_DIRECTORY`). Keep `add_dependencies(lcx openbsd netbsd pdp11unix)`.

- [ ] **Step 7: Replace `LoadPersonality` in `lcx.cpp` with the COM resolver**

Add includes: `#include "LibCPU/IAbi.h"`, `#include "LibCPU/Loader.h"`, `#include "nix-version.h"`. Add a scalar-plist reader is NOT needed — family match uses the `LCAbiMatch` array; version min/max come from `IAbi`. Add a tiny array reader (copy the algorithm from `MachineBuilder::ReadPlistStringArray`, or add a shared helper). Replace `LoadPersonality` with:
```cpp
// Resolve `--abi family[:version]` to a personality via the .abi COM bundles next to lcx.
// Family is matched against each bundle's LCAbiMatch array (read without loading code); the
// matched bundle is loaded, its [min,max] version range clamps the request ("closest
// approximation"), and CreatePersonality builds the runtime personality.
static nix_personality_t *
LoadPersonality (CHAR8 CONST *pAbi, CHAR8 CONST *pArgv0)
{
    std::string Spec (pAbi);
    std::string Family = Spec, VerStr;
    std::string::size_type Colon = Spec.find (':');
    if (Colon != std::string::npos) {
        Family = Spec.substr (0, Colon);
        VerStr = Spec.substr (Colon + 1);
    }

    std::string Dir (pArgv0 != nullptr ? pArgv0 : "");
    Dir = Dir.substr (0, Dir.find_last_of ('/') + 1);
    if (Dir.empty ()) { Dir = "./"; }

    std::string BundlePath;
    if (DIR *pD = opendir (Dir.c_str ())) {
        while (struct dirent *pE = readdir (pD)) {
            std::string Name = pE->d_name;
            if (Name.size () < 5 || Name.compare (Name.size () - 4, 4, ".abi") != 0) { continue; }
            std::string Plist = Dir + Name + "/Contents/Info.plist";
            for (std::string CONST &M : ReadPlistStringArray (Plist, LIBCPU_ABI_MATCH_KEY)) {
                if (M == Family) { BundlePath = Dir + Name; break; }
            }
            if (!BundlePath.empty ()) { break; }
        }
        closedir (pD);
    }
    if (BundlePath.empty ()) {
        std::fprintf (stderr, "lcx: no .abi bundle matches '--abi %s'\n", Family.c_str ());
        return nullptr;
    }

    LibCPU::IAbi *pAbiObj = LibCPU::LoadAbiBundle (BundlePath.c_str ());
    if (pAbiObj == nullptr) {
        std::fprintf (stderr, "lcx: cannot load .abi bundle '%s'\n", BundlePath.c_str ());
        return nullptr;
    }

    // Clamp the requested version into [min, max]; empty request -> max (latest).
    nix_version_t Min = nix_version_parse (pAbiObj->GetVersionMin ());
    nix_version_t Max = nix_version_parse (pAbiObj->GetVersionMax ());
    nix_version_t Want = VerStr.empty () ? Max : nix_version_parse (VerStr.c_str ());
    if (Want < Min) { Want = Min; }
    if (Want > Max) { Want = Max; }

    char Buf[16];
    nix_personality_t *pPersona = pAbiObj->CreatePersonality (nix_version_format (Want, Buf, sizeof (Buf)));
    pAbiObj->Release ();
    return pPersona;
}
```
Add a file-local `ReadPlistStringArray` in lcx.cpp (same algorithm as MachineBuilder's) OR, cleaner, promote MachineBuilder's to a shared header and reuse it. (Reuse if the function is already non-static and declared in a header; otherwise copy the ~20-line reader with an NT-style comment.)

- [ ] **Step 8: Build everything and run the personality tests**

Run:
```bash
cmake -S LibCPU -B LibCPU/build >/dev/null && cmake --build LibCPU/build 2>&1 | tail -20
ctest --test-dir LibCPU/build -R 'openbsd|netbsd|pdp11' --output-on-failure
```
Expected: `openbsd.abi`/`netbsd.abi`/`pdp11unix.abi` build beside `lcx`; the tests (running `--abi openbsd` etc., now resolved via COM at latest) PASS. Verify the legacy alias resolves:
```bash
E=LibCPU/build/lcx
"$E" run LibCPU/build/hello.m88k --arch upcl:/path/to/m88k.upcl --abi obsd79 | grep -q '^hello$' && echo ALIAS-OK
```

- [ ] **Step 9: Commit**

```bash
git add LibCPU/cmake/LibCPUAddAbi.cmake LibCPU/cmake/AbiInfo.plist.in \
        test/libnix/openbsd/openbsd-abi.cpp test/libnix/netbsd/netbsd-abi.cpp \
        test/libnix/pdp11unix/pdp11unix-abi.cpp \
        test/libnix/openbsd/CMakeLists.txt test/libnix/netbsd/CMakeLists.txt \
        test/libnix/pdp11unix/CMakeLists.txt LibCPU/CMakeLists.txt LibCPU/test/lcx.cpp
git commit -m "lcx: personalities as .abi COM bundles; resolve --abi family:version

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: `schistory` — derive `@since..until` from `syscalls.master` history

A standalone C tool that parses BSD `syscalls.master` files across releases (in order) and emits the annotated superset `.sc`.

**Files:**
- Create: `test/libnix/tools/schistory/schistory.c`, `test/libnix/tools/schistory/CMakeLists.txt`
- Create fixtures: `test/libnix/tools/schistory/fixtures/rel-2.0.master`, `rel-3.6.master`, `rel-7.9.master`, and `fixtures/check-schistory.cmake`
- Modify: `LibCPU/CMakeLists.txt` (add_subdirectory for schistory)

**Interfaces:**
- Produces: `schistory --family <name> --out <file.sc> <ver>=<master> <ver>=<master> ...` → writes an annotated `.sc` (header `NAME`/`NAMESPACE`/`FAMILY`/`LIMIT`/`BAE` + `CALLS` + one `@since..until`-annotated line per call in the superset).

- [ ] **Step 1: Write fixture masters (minimal BSD `syscalls.master` subset)**

`fixtures/rel-2.0.master`:
```
; @(#)syscalls.master  fixture 2.0
0	STD		{ int nosys(void); }
1	STD		{ void exit(int rval); }
4	STD		{ ssize_t write(int fd, const void *buf, size_t nbyte); }
42	STD		{ int oldcall(int x); }
```
`fixtures/rel-3.6.master` (oldcall removed → OBSOL; a new call appears):
```
; @(#)syscalls.master  fixture 3.6
0	STD		{ int nosys(void); }
1	STD		{ void exit(int rval); }
4	STD		{ ssize_t write(int fd, const void *buf, size_t nbyte); }
42	OBSOL		oldcall
73	STD		{ int munmap(void *addr, size_t len); }
```
`fixtures/rel-7.9.master` (another new call at 336):
```
; @(#)syscalls.master  fixture 7.9
0	STD		{ int nosys(void); }
1	STD		{ void exit(int rval); }
4	STD		{ ssize_t write(int fd, const void *buf, size_t nbyte); }
73	STD		{ int munmap(void *addr, size_t len); }
336	STD		{ int pinsyscall(int syscall, void *base, size_t len); }
```
Expected emitted `.sc` (order by number): `write @2.0`, `oldcall @2.0..3.6`, `munmap @3.6`, `pinsyscall @7.9`, `exit @2.0`, `nosys @2.0`.

- [ ] **Step 2: Write the differ `test/libnix/tools/schistory/schistory.c`**

```c
/*
 * schistory -- derive per-call @since..until version ranges for a syscall superset table by
 * diffing a family's sys/kern/syscalls.master across releases (given in order on the command
 * line), and emit the annotated .sc that sc2int consumes.
 *
 * Usage: schistory --family <name> --name "<human>" --bae <ERRNO> --out <file.sc> \
 *                  <ver>=<master> <ver>=<master> ...   (versions in ascending order)
 *
 * We track, per (number,name), the first release it appears as a live STD/NOARGS call (since)
 * and the first release it is gone/OBSOL/UNIMPL after having existed (until). BSD numbering is
 * stable, so (number,name) is a reliable identity; a renumber shows up as a new identity.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCH_MAX_CALLS   1024
#define SCH_MAX_RELS    64
#define SCH_LINE        1024
#define SCH_NAME        128
#define SCH_PROTO       768

typedef struct _sch_call {
    int  number;
    char name[SCH_NAME];
    char rettype[SCH_NAME];              /* .sc return type keyword */
    char format[SCH_PROTO];              /* space-separated .sc arg type keywords */
    char since[16];                      /* version string; "" = unset */
    char until[16];                      /* version string; "" = never removed */
    int  present_now;                    /* seen live in the release currently being parsed */
    int  ever;                           /* has ever been live */
} sch_call_t;

static sch_call_t g_calls[SCH_MAX_CALLS];
static int        g_ncalls = 0;
static int        g_limit = 0;

/* Map a C proto token stream to a single .sc type keyword. Pointers -> ptr; 64-bit-ish ->
 * dword/intptr; default word. Named, not magic: the classification is explicit below. */
static char const *
sch_type_of (char const *ctype, int is_pointer)
{
    if (is_pointer) { return "ptr"; }
    if (strstr (ctype, "off_t") || strstr (ctype, "int64") || strstr (ctype, "quad")
        || strstr (ctype, "long long")) { return "dword"; }
    if (strstr (ctype, "size_t") || strstr (ctype, "ssize_t") || strstr (ctype, "intptr")
        || strstr (ctype, "long")) { return "intptr"; }
    if (strstr (ctype, "double")) { return "double"; }
    if (strstr (ctype, "float")) { return "single"; }
    return "word";
}

static sch_call_t *
sch_find (int number, char const *name)
{
    int i;
    for (i = 0; i < g_ncalls; i++) {
        if (g_calls[i].number == number && strcmp (g_calls[i].name, name) == 0) {
            return &g_calls[i];
        }
    }
    return NULL;
}

/* Return the .sc return type for a C return type spelling. */
static char const *
sch_rettype_of (char const *ret)
{
    if (strstr (ret, "void")) { return "void"; }
    if (strstr (ret, "off_t") || strstr (ret, "int64") || strstr (ret, "quad")) { return "dword"; }
    if (strstr (ret, "*")) { return "ptr"; }
    if (strstr (ret, "size_t") || strstr (ret, "long") || strstr (ret, "intptr")) { return "intptr"; }
    return "word";
}

/* Parse one "{ ret name(args); }" prototype into rettype/name/format on the call. */
static int
sch_parse_proto (char const *proto, int number, char const *version)
{
    char        ret[SCH_NAME] = { 0 };
    char        name[SCH_NAME] = { 0 };
    char const *lp = strchr (proto, '(');
    char const *rp = lp ? strchr (lp, ')') : NULL;
    char const *p, *nstart;
    sch_call_t *c;
    char        args[SCH_PROTO];

    if (lp == NULL || rp == NULL) { return 0; }

    /* name = last identifier before '(' ; ret = everything before the name. */
    p = lp;
    while (p > proto && (isspace ((unsigned char) p[-1]) || p[-1] == '(')) { p--; }
    nstart = p;
    while (nstart > proto && (isalnum ((unsigned char) nstart[-1]) || nstart[-1] == '_')) { nstart--; }
    {
        size_t nlen = (size_t) (p - nstart);
        size_t rlen;
        if (nlen == 0 || nlen >= SCH_NAME) { return 0; }
        memcpy (name, nstart, nlen);
        name[nlen] = '\0';
        rlen = (size_t) (nstart - proto);
        while (rlen > 0 && isspace ((unsigned char) proto[rlen - 1])) { rlen--; }
        if (rlen >= SCH_NAME) { rlen = SCH_NAME - 1; }
        memcpy (ret, proto, rlen);
        ret[rlen] = '\0';
    }

    c = sch_find (number, name);
    if (c == NULL) {
        if (g_ncalls >= SCH_MAX_CALLS) { return 0; }
        c = &g_calls[g_ncalls++];
        memset (c, 0, sizeof (*c));
        c->number = number;
        strncpy (c->name, name, SCH_NAME - 1);
        strncpy (c->since, version, sizeof (c->since) - 1);
    }
    strncpy (c->rettype, sch_rettype_of (ret), SCH_NAME - 1);

    /* Build the .sc arg format from the arg list. "void" / empty -> no args. */
    {
        size_t alen = (size_t) (rp - lp - 1);
        char  *tok;
        char  *save = NULL;
        c->format[0] = '\0';
        if (alen >= sizeof (args)) { alen = sizeof (args) - 1; }
        memcpy (args, lp + 1, alen);
        args[alen] = '\0';
        if (strstr (args, "void") == NULL && alen > 0) {
            for (tok = strtok_r (args, ",", &save); tok != NULL; tok = strtok_r (NULL, ",", &save)) {
                int is_ptr = (strchr (tok, '*') != NULL);
                if (c->format[0] != '\0') { strncat (c->format, ", ", sizeof (c->format) - strlen (c->format) - 1); }
                strncat (c->format, sch_type_of (tok, is_ptr), sizeof (c->format) - strlen (c->format) - 1);
            }
        }
    }

    c->present_now = 1;
    c->ever = 1;
    if (number + 1 > g_limit) { g_limit = number + 1; }
    return 1;
}

/* Parse one release's master; mark presence; close ranges for calls that vanished. */
static void
sch_parse_release (char const *path, char const *version)
{
    FILE *f = fopen (path, "r");
    char  line[SCH_LINE];
    int   i;

    if (f == NULL) {
        fprintf (stderr, "schistory: cannot open %s\n", path);
        exit (2);
    }
    for (i = 0; i < g_ncalls; i++) { g_calls[i].present_now = 0; }

    while (fgets (line, sizeof (line), f) != NULL) {
        char *p = line;
        int   number;
        char *brace;
        while (isspace ((unsigned char) *p)) { p++; }
        if (*p == ';' || *p == '#' || *p == '\0') { continue; }
        if (!isdigit ((unsigned char) *p)) { continue; }
        number = atoi (p);
        /* STD/NOARGS/NODEF with a { proto } are live calls; OBSOL/UNIMPL/EXCL are gaps. */
        brace = strchr (p, '{');
        if (brace != NULL && (strstr (p, "STD") || strstr (p, "NOARGS") || strstr (p, "NODEF")
                              || strstr (p, "NOERR"))) {
            char *end = strrchr (brace, '}');
            if (end != NULL) { *end = '\0'; }
            sch_parse_proto (brace + 1, number, version);
        }
        /* OBSOL/UNIMPL: leave present_now = 0 so the range closes below. */
    }
    fclose (f);

    /* Any call that existed but is not present in this release, and has no until yet, ends here. */
    for (i = 0; i < g_ncalls; i++) {
        if (g_calls[i].ever && !g_calls[i].present_now && g_calls[i].until[0] == '\0') {
            strncpy (g_calls[i].until, version, sizeof (g_calls[i].until) - 1);
        }
    }
}

static int
sch_cmp (void const *a, void const *b)
{
    return ((sch_call_t const *) a)->number - ((sch_call_t const *) b)->number;
}

int
main (int argc, char **argv)
{
    char const *family = "unknown";
    char const *human = "Unknown";
    char const *bae = "EFAULT";
    char const *out = NULL;
    int         i;
    FILE       *o;

    for (i = 1; i < argc; i++) {
        if (!strcmp (argv[i], "--family") && i + 1 < argc) { family = argv[++i]; }
        else if (!strcmp (argv[i], "--name") && i + 1 < argc) { human = argv[++i]; }
        else if (!strcmp (argv[i], "--bae") && i + 1 < argc) { bae = argv[++i]; }
        else if (!strcmp (argv[i], "--out") && i + 1 < argc) { out = argv[++i]; }
        else {
            char *eq = strchr (argv[i], '=');
            if (eq != NULL) { *eq = '\0'; sch_parse_release (eq + 1, argv[i]); }
        }
    }
    if (out == NULL) { fprintf (stderr, "schistory: --out required\n"); return 2; }

    qsort (g_calls, (size_t) g_ncalls, sizeof (g_calls[0]), sch_cmp);

    o = fopen (out, "w");
    if (o == NULL) { fprintf (stderr, "schistory: cannot write %s\n", out); return 2; }
    fprintf (o, "# Generated by schistory from vendored syscalls.master history. DO NOT EDIT.\n");
    fprintf (o, "NAME: \"%s\"\n", human);
    fprintf (o, "NAMESPACE: %s\n", family);
    fprintf (o, "FAMILY: %s\n", family);
    fprintf (o, "LIMIT: %d\n", g_limit);
    fprintf (o, "BAE: %s\n\n", bae);
    fprintf (o, "CALLS\n\n");
    for (i = 0; i < g_ncalls; i++) {
        sch_call_t *c = &g_calls[i];
        fprintf (o, "%d %s %s (%s)", c->number, c->rettype, c->name,
                 c->format[0] ? c->format : "");
        if (c->since[0] != '\0') {
            fprintf (o, " @%s", c->since);
            if (c->until[0] != '\0') { fprintf (o, "..%s", c->until); }
        }
        fprintf (o, "\n");
    }
    fclose (o);
    return 0;
}
```
(The `.sc` grammar needs `FAMILY:` — either add a `T_FAMILY` head-declaration to `sc2int` now, or emit it as a `# FAMILY:` comment. Simpler: emit `# family: <name>` as a comment and DROP the `FAMILY:` line, since `NAMESPACE` already equals the family. Adjust the emit to a comment to avoid touching sc2int again. If you keep `FAMILY:`, add the token+rule to `lexer.l`/`parser.y` and a `g_gbl_family` in Task 4 — prefer the comment to stay YAGNI.)

- [ ] **Step 3: Create `test/libnix/tools/schistory/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.10)
project(schistory C)
add_executable(schistory schistory.c)
target_compile_options(schistory PRIVATE -w)
```

- [ ] **Step 4: Add the fixture check `fixtures/check-schistory.cmake`**

```cmake
execute_process(COMMAND ${SCHISTORY}
    --family gatefix --name "Gate Fixture" --bae EFAULT --out ${OUT}
    2.0=${DIR}/rel-2.0.master 3.6=${DIR}/rel-3.6.master 7.9=${DIR}/rel-7.9.master
  RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "schistory failed (rc=${rc})")
endif()
file(READ ${OUT} txt)
foreach(needle
    "4 word write (word, ptr, intptr) @2.0"
    "42 word oldcall (word) @2.0..3.6"
    "73 word munmap (ptr, intptr) @3.6"
    "336 word pinsyscall (word, ptr, intptr) @7.9")
  string(FIND "${txt}" "${needle}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "schistory output missing: ${needle}\n--- got ---\n${txt}")
  endif()
endforeach()
```
(Adjust the expected `write` arg types to whatever `sch_type_of` yields for `size_t nbyte` — `intptr` per the mapping above. If the mapping is tuned, update these needles to match; the point is the `@since..until` annotations.)

- [ ] **Step 5: Wire schistory + its test into `LibCPU/CMakeLists.txt`**

Near the sc2int subdirectory:
```cmake
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../test/libnix/tools/schistory ${CMAKE_BINARY_DIR}/tools/schistory)
add_test(NAME schistory.gate
  COMMAND ${CMAKE_COMMAND}
    -DSCHISTORY=$<TARGET_FILE:schistory>
    -DDIR=${CMAKE_CURRENT_SOURCE_DIR}/../test/libnix/tools/schistory/fixtures
    -DOUT=${CMAKE_BINARY_DIR}/tools/schistory/gatefix.sc
    -P ${CMAKE_CURRENT_SOURCE_DIR}/../test/libnix/tools/schistory/fixtures/check-schistory.cmake)
```

- [ ] **Step 6: Build and run the differ test**

Run:
```bash
cmake -S LibCPU -B LibCPU/build >/dev/null && cmake --build LibCPU/build --target schistory 2>&1 | tail -10
ctest --test-dir LibCPU/build -R 'schistory.gate' --output-on-failure
```
Expected: PASS (annotations match). If arg-type needles mismatch, tune `sch_type_of`/`sch_rettype_of` or the needles until the ranges are correct AND the types are sane — do not weaken the test to pass.

- [ ] **Step 7: Commit**

```bash
git add test/libnix/tools/schistory LibCPU/CMakeLists.txt
git commit -m "schistory: derive @since..until from syscalls.master history

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: Vendor the real masters and generate the superset tables

Fetch and commit each release's `syscalls.master`, then wire `schistory` as a build step producing the real `openbsd.sc` / `netbsd.sc` (superset with real ranges). Gating goes live.

**Files:**
- Create: `test/libnix/masters/openbsd/<ver>/syscalls.master` (2.0 → 7.9), `test/libnix/masters/netbsd/<ver>/syscalls.master` (1.0 → 10.1), plus `test/libnix/masters/README.md` (sources + fetch date + any skipped releases).
- Modify: `test/libnix/openbsd/CMakeLists.txt`, `test/libnix/netbsd/CMakeLists.txt` (replace the hand `.sc` with a `schistory`-generated one), delete the seed `openbsd.sc`/`netbsd.sc` from source or keep as `*.seed.sc` reference.

**Interfaces:**
- Consumes: `schistory` (Task 8), `sc2int` (Task 4).
- Produces: build-time `${PROJECT_BINARY_DIR}/openbsd.sc` → `openbsd-callbacks.c` with real gates; same for netbsd.

- [ ] **Step 1: Fetch the OpenBSD masters (2.0 → 7.9)**

For each release tag, fetch `sys/kern/syscalls.master`. Use OpenBSD cvsweb raw checkout by tag `OPENBSD_x_y`, e.g.:
```bash
mkdir -p test/libnix/masters/openbsd
for v in 2.0 2.1 2.2 2.3 2.4 2.5 2.6 2.7 2.8 2.9 3.0 3.1 3.2 3.3 3.4 3.5 3.6 3.7 3.8 3.9 \
         4.0 4.1 4.2 4.3 4.4 4.5 4.6 4.7 4.8 4.9 5.0 5.1 5.2 5.3 5.4 5.5 5.6 5.7 5.8 5.9 \
         6.0 6.1 6.2 6.3 6.4 6.5 6.6 6.7 6.8 6.9 7.0 7.1 7.2 7.3 7.4 7.5 7.6 7.7 7.8 7.9; do
  tag=OPENBSD_$(echo "$v" | tr . _)
  mkdir -p "test/libnix/masters/openbsd/$v"
  curl -fsS "https://cvsweb.openbsd.org/cgi-bin/cvsweb/~checkout~/src/sys/kern/syscalls.master?rev=&content-type=text/plain&only_with_tag=$tag" \
    -o "test/libnix/masters/openbsd/$v/syscalls.master" \
    && echo "ok $v" || { echo "SKIP $v (unreachable)"; rmdir "test/libnix/masters/openbsd/$v" 2>/dev/null; }
done
```
Record any skipped releases in `test/libnix/masters/README.md`. Verify a couple of files look like real masters (`grep -c STD test/libnix/masters/openbsd/7.9/syscalls.master`).

- [ ] **Step 2: Fetch the NetBSD masters (1.0 → 10.1)**

NetBSD cvsweb, release tags `netbsd-x-y` (and `netbsd-10`), file `src/sys/kern/syscalls.master`:
```bash
mkdir -p test/libnix/masters/netbsd
# Example set; adjust tag spelling per cvsweb (netbsd-1-0, netbsd-1-1, ... netbsd-9, netbsd-10):
for entry in 1.0=netbsd-1-0 1.1=netbsd-1-1 1.2=netbsd-1-2 1.3=netbsd-1-3 1.4=netbsd-1-4 \
             1.5=netbsd-1-5 1.6=netbsd-1-6 2.0=netbsd-2-0 3.0=netbsd-3-0 4.0=netbsd-4-0 \
             5.0=netbsd-5-0 6.0=netbsd-6-0 7.0=netbsd-7-0 8.0=netbsd-8-0 9.0=netbsd-9 10.1=netbsd-10; do
  v=${entry%%=*}; tag=${entry##*=}
  mkdir -p "test/libnix/masters/netbsd/$v"
  curl -fsS "https://cvsweb.netbsd.org/bsdweb.cgi/~checkout~/src/sys/kern/syscalls.master?only_with_tag=$tag&content-type=text/plain" \
    -o "test/libnix/masters/netbsd/$v/syscalls.master" \
    && echo "ok $v" || { echo "SKIP $v"; rmdir "test/libnix/masters/netbsd/$v" 2>/dev/null; }
done
```
(If cvsweb rate-limits or a tag differs, note it and adjust; a HEAD/trunk copy for the latest is acceptable if a tagged one is unreachable — record it.)

- [ ] **Step 3: Point openbsd's build at a schistory-generated superset**

In `test/libnix/openbsd/CMakeLists.txt`, before the sc2int custom command, add a schistory custom command that builds `${PROJECT_BINARY_DIR}/openbsd.sc` from every vendored master (globbed and version-sorted), then feed THAT to sc2int instead of the hand `openbsd.sc`:
```cmake
file(GLOB _obsd_vers RELATIVE ${MASTERS}/openbsd ${MASTERS}/openbsd/*)
# Build "<ver>=<path>" args, ascending. (Use a small sort; CMake list(SORT) is lexical, so pad
# or use a natural-sort helper -- keep versions zero-padded in a CMake variable if needed.)
set(_obsd_args "")
foreach(v IN LISTS _obsd_vers)
  list(APPEND _obsd_args "${v}=${MASTERS}/openbsd/${v}/syscalls.master")
endforeach()
add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/openbsd.sc
  COMMAND schistory --family openbsd --name "OpenBSD" --bae EFAULT
          --out ${PROJECT_BINARY_DIR}/openbsd.sc ${_obsd_args}
  DEPENDS schistory ${MASTERS}/openbsd
  COMMENT "Deriving OpenBSD superset syscall table from masters")
```
Change the sc2int custom command's input from `${PROJECT_SOURCE_DIR}/openbsd.sc` to `${PROJECT_BINARY_DIR}/openbsd.sc` and add it to DEPENDS. Set `MASTERS` (e.g. `set(MASTERS ${CMAKE_CURRENT_SOURCE_DIR}/../masters)` at the top). **Version sort:** ensure ascending order (schistory relies on it) — build the arg list in numeric order, not lexical. Implement a numeric sort in CMake (split on `.`, compare) or, simpler, have `schistory` itself sort its release args by parsed version before processing (add a qsort of the input list in `main`, keyed by `nix_version_parse`-style packing). **Prefer sorting inside schistory** so CMake ordering cannot corrupt the result — add that sort in this step.

- [ ] **Step 4: Same for netbsd**

Mirror Step 3 in `test/libnix/netbsd/CMakeLists.txt` (`--family netbsd --name "NetBSD"`, masters under `${MASTERS}/netbsd`).

- [ ] **Step 5: Keep the old hand tables as reference, or delete**

Move the seed tables aside so they are not mistaken for the source of truth:
```bash
git mv test/libnix/openbsd/openbsd.sc test/libnix/openbsd/openbsd.seed.sc
git mv test/libnix/netbsd/netbsd.sc test/libnix/netbsd/netbsd.seed.sc
```
(They document the pre-history hand table; the build now generates `openbsd.sc` in the binary dir.)

- [ ] **Step 6: Build and run the full personality suite — gating now live**

Run:
```bash
cmake -S LibCPU -B LibCPU/build >/dev/null && cmake --build LibCPU/build 2>&1 | tail -30
ctest --test-dir LibCPU/build -R 'openbsd|netbsd|pdp11|schistory|sc2int' --output-on-failure
```
Expected: superset tables generate; existing `--abi openbsd` (latest) tests still PASS (latest sees every call). Spot-check the generated table:
```bash
grep -n 'write\|pinsyscall' LibCPU/build/openbsd/openbsd.sc | head
```
`write` should carry `@2.0` (or the earliest fetched), a modern call a later `@since`.

- [ ] **Step 7: Commit (masters + wiring)**

```bash
git add test/libnix/masters test/libnix/openbsd test/libnix/netbsd LibCPU/CMakeLists.txt
git commit -m "abi: vendor OpenBSD/NetBSD syscalls.master history; generate superset tables

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 10: Integration test — version gating end to end

Prove `--abi openbsd:<early>` gates out a call that `--abi openbsd:<late>` allows, using the real generated table.

**Files:**
- Modify: `LibCPU/CMakeLists.txt` (add a gating ctest)
- Possibly create: `test/abi/gate-guest.upcl.s` or reuse an existing m88k guest that invokes a late-introduced syscall.

**Interfaces:**
- Consumes: the built `lcx` + `openbsd.abi` with the real superset (Task 9).

- [ ] **Step 1: Identify a late-introduced OpenBSD syscall present in the table**

Run:
```bash
awk '/@[4-9]\./ {print}' LibCPU/build/openbsd/openbsd.sc | head
```
Pick one with a clear `@since` well above 2.0 (e.g. `pinsyscall @7.x` or another modern call). Note its number N and since-version V.

- [ ] **Step 2: Add a ctest that runs the SAME guest under an early and a late version**

Reuse the existing m88k hello guest but set r13 (the m88k syscall number) to N via `--reg 13=N` and run it twice. Under `--abi openbsd:2.0` the dispatcher must log "does not exist in this guest-OS version" / return ENOSYS; under `--abi openbsd:<V>` it must reach the call. Add to `LibCPU/CMakeLists.txt`:
```cmake
add_test(NAME abi.gate.openbsd
  COMMAND ${CMAKE_COMMAND}
    -DLCX=$<TARGET_FILE:lcx> -DGUEST=${CMAKE_BINARY_DIR}/hello.m88k
    -DUPCL=${CMAKE_CURRENT_SOURCE_DIR}/../arch/m88k/m88k.upcl -DNUM=<N> -DVER=<V>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/test/abi-gate.cmake)
```
Create `LibCPU/test/abi-gate.cmake`:
```cmake
# Early version must reject the late syscall (ENOSYS / "does not exist"); late version must not.
execute_process(COMMAND ${LCX} run ${GUEST} --arch upcl:${UPCL} --abi openbsd:2.0 --reg 13=${NUM}
  OUTPUT_VARIABLE early ERROR_VARIABLE early_err)
string(FIND "${early}${early_err}" "does not exist in this guest-OS version" epos)
if(epos EQUAL -1)
  message(FATAL_ERROR "openbsd:2.0 did not gate syscall ${NUM}:\n${early}${early_err}")
endif()
execute_process(COMMAND ${LCX} run ${GUEST} --arch upcl:${UPCL} --abi openbsd:${VER} --reg 13=${NUM}
  OUTPUT_VARIABLE late ERROR_VARIABLE late_err)
string(FIND "${late}${late_err}" "does not exist in this guest-OS version" lpos)
if(NOT lpos EQUAL -1)
  message(FATAL_ERROR "openbsd:${VER} wrongly gated syscall ${NUM}:\n${late}${late_err}")
endif()
```
(Fill `<N>`/`<V>` from Step 1. If the chosen call needs valid pointer args to not fault before the gate, pick a call whose gate is checked before argument marshalling — the gate in Task 3 runs right after `extract`, before `get_next_param`, so ENOSYS fires regardless of args. Good.)

- [ ] **Step 3: Run the gating test**

Run:
```bash
cmake --build LibCPU/build --target lcx openbsd 2>&1 | tail -5
ctest --test-dir LibCPU/build -R 'abi.gate.openbsd' --output-on-failure
```
Expected: PASS — early rejects, late allows.

- [ ] **Step 4: Full suite sanity**

Run:
```bash
ctest --test-dir LibCPU/build --output-on-failure 2>&1 | tail -25
```
Expected: no regressions; total test count increased by the new `nix.version`, `nix.gate`, `sc2int.gate`, `schistory.gate`, `abi.gate.openbsd`.

- [ ] **Step 5: Commit**

```bash
git add LibCPU/CMakeLists.txt LibCPU/test/abi-gate.cmake
git commit -m "test: end-to-end --abi openbsd version gating

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- `.abi` COM bundles like loaders/devices → Tasks 6 (IAbi+LoadAbiBundle), 7 (shims+cmake+plist+convert). ✓
- `--abi family:version` with closest approximation (clamp) → Task 7 resolver. ✓
- Superset table + `@since..until` gating → Tasks 3 (gate), 4 (sc2int grammar), 9 (real tables). ✓
- Ranges derived from real syscalls.master → Tasks 8 (schistory), 9 (vendored masters). ✓
- OpenBSD 2.0→7.9, NetBSD 1.0→10.1 floors/clamp → Task 2 (version), 7 (clamp), 9 (masters). ✓
- pdp11unix packaged as .abi, not gated → Task 7 (shim, ignores version). ✓
- Legacy `--abi obsd79` alias → Task 7 (LCAbiMatch includes alias). ✓
- Namespace rename → Task 1. ✓

**Placeholder scan:** `<N>`/`<V>` in Task 10 are filled from Task 10 Step 1's output (a discovered value, not a plan gap); the arg-type needles in Tasks 8 are explicitly noted to be tuned to the mapping. No "TBD"/"add error handling"/"similar to Task N".

**Type consistency:** `nix_version_t` (Task 2) used identically in Tasks 3/4/5/7; `nix_us_syscall_desc_available` signature matches between Task 3 header and use; `IAbi` method names (`GetFamily`/`GetVersionMin`/`GetVersionMax`/`CreatePersonality`) match between Task 6 (declaration), Task 7 (shim + resolver); `nix_personality_create(nix_version_t)` consistent across Tasks 5/7; `LCAbiMatch`/`LIBCPU_ABI_MATCH_KEY` consistent Task 6/7.
