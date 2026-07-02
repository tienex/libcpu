# CoreKVM Phase 1 — Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the protocol-agnostic, sans-I/O C90 substrate of CoreKVM — the object runtime, framebuffer (multi-screen, pixel conversion, dirty regions), input model, and the byte-buffer + event-queue primitives every protocol engine will build on — with a full CTest suite and no I/O or protocol code.

**Architecture:** A CoreFoundation-style C runtime: every object begins with a `KVMObjectHeader` (type id + retain count + dealloc), so a single `KVMRetain`/`KVMRelease` works across all types and `KVMRef` is the common opaque handle. Everything is single-instance, allocation-explicit, and performs zero I/O. The framebuffer keeps a canonical BGRA8888 surface and converts foreign pixel formats into it; a dirty-region accumulator coalesces damage. Byte buffers and an event queue are the sans-I/O plumbing later phases pump through.

**Tech Stack:** Strict C90, CMake (native static library + CTest), no external dependencies in this phase.

## Global Constraints

- **Language:** strict **C90** — no `<stdint.h>`, no `//` comments, no mixed declarations/statements (all declarations at the top of a block), no VLAs. Build flags enforce this.
- **Compilers:** must build clean on **clang/gcc** (`-std=c90 -pedantic -Wall -Werror`) and be **MSVC- and OpenWatcom-aware** (no GNU extensions).
- **Style:** NT/UEFI house style. 4-space indent. UPPERCASE-ish CF typedefs (`KVMUInt32`), PascalCase functions (`KVMFrameBufferCreate`), CF `k`-prefixed enum constants (`kKVMSuccess`). `if (x) { oneline }` — always braces, no useless braces, no `{ }` elision.
- **64-bit literals:** use `INT64_C` / `UINT64_C`; `~UINT64_C(0)` is `UINT64_MAX`.
- **C ABI shape:** CoreFoundation-style — opaque `KVM<Object>Ref` handles, `KVMRetain`/`KVMRelease`, Create returns +1 owned, Get returns borrowed pointers backed by owner storage. **No fixed-size arrays in structs** exposed across the ABI.
- **No silent failure:** every fallible function returns `KVMStatus` or a NULL/`-1` sentinel; bounds are checked on all length/offset inputs.
- **Location:** all files under `CoreKVM/` in the repository root.

---

### Task 1: Build scaffold + `KVMBase` types, status, and object runtime

**Files:**
- Create: `CoreKVM/CMakeLists.txt`
- Create: `CoreKVM/include/CoreKVM/KVMBase.h`
- Create: `CoreKVM/core/KVMRuntime.h`
- Create: `CoreKVM/core/KVMBase.c`
- Test: `CoreKVM/tests/test_base.c`

**Interfaces:**
- Consumes: nothing (first task).
- Produces:
  - Types: `KVMBool` (`KVM_TRUE`/`KVM_FALSE`), `KVMIndex` (signed `long`), `KVMUInt8/16/32`, `KVMInt8/16/32`, `KVMTypeID` (`unsigned long`), `KVMRef` (opaque `void *`-like handle).
  - `KVMStatus` enum: `kKVMSuccess=0, kKVMErrorNoMemory, kKVMErrorInvalidArgument, kKVMErrorProtocol, kKVMErrorWouldBlock, kKVMErrorClosed, kKVMErrorUnsupported`.
  - Runtime: `void *KVMRuntimeCreate(KVMTypeID typeID, KVMIndex size, KVMDeallocFn dealloc)` where `typedef void (*KVMDeallocFn)(void *object)`.
  - `KVMRef KVMRetain(KVMRef object)`, `void KVMRelease(KVMRef object)`, `KVMIndex KVMGetRetainCount(KVMRef object)`, `KVMTypeID KVMGetTypeID(KVMRef object)`.
  - Header struct `KVMObjectHeader { KVMTypeID typeID; KVMIndex retainCount; KVMDeallocFn dealloc; }` as the mandatory first member of every CoreKVM object.

- [ ] **Step 1: Write the failing test**

Create `CoreKVM/tests/test_base.c`:

```c
/*
 * Unit tests for the CoreKVM object runtime.
 */
#include "CoreKVM/KVMBase.h"
#include "KVMRuntime.h"

#include <stdio.h>

static int gTestsFailed = 0;

#define KVM_CHECK(cond)                                                        \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            gTestsFailed = 1;                                                  \
        }                                                                      \
    } while (0)

static const KVMTypeID kDummyTypeID = 0x11110001UL;
static int gDeallocCount;

typedef struct _Dummy {
    KVMObjectHeader base;
    int value;
} Dummy;

static void
DummyDealloc(void *object)
{
    (void)object;
    gDeallocCount++;
}

static void
TestRetainReleaseLifecycle(void)
{
    Dummy *dummy;

    gDeallocCount = 0;
    dummy = (Dummy *)KVMRuntimeCreate(kDummyTypeID, (KVMIndex)sizeof(Dummy),
                                      DummyDealloc);
    KVM_CHECK(dummy != NULL);
    KVM_CHECK(KVMGetRetainCount((KVMRef)dummy) == 1);
    KVM_CHECK(KVMGetTypeID((KVMRef)dummy) == kDummyTypeID);

    KVMRetain((KVMRef)dummy);
    KVM_CHECK(KVMGetRetainCount((KVMRef)dummy) == 2);

    KVMRelease((KVMRef)dummy);
    KVM_CHECK(KVMGetRetainCount((KVMRef)dummy) == 1);
    KVM_CHECK(gDeallocCount == 0);

    KVMRelease((KVMRef)dummy);
    KVM_CHECK(gDeallocCount == 1);
}

static void
TestNullSafety(void)
{
    /* Retain/Release/getters must tolerate NULL like CoreFoundation. */
    KVMRelease(NULL);
    KVM_CHECK(KVMRetain(NULL) == NULL);
    KVM_CHECK(KVMGetRetainCount(NULL) == 0);
}

int
main(void)
{
    TestRetainReleaseLifecycle();
    TestNullSafety();
    if (gTestsFailed) {
        printf("test_base: FAILED\n");
        return 1;
    }
    printf("test_base: OK\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S CoreKVM -B build/corekvm && cmake --build build/corekvm`
Expected: FAIL — build error, `KVMBase.h` / `KVMRuntime.h` not found (headers and library don't exist yet).

- [ ] **Step 3: Write the headers, runtime, and build scaffold**

Create `CoreKVM/include/CoreKVM/KVMBase.h`:

```c
/*
 * CoreKVM public base: portable fixed-width types, status codes, and the
 * CoreFoundation-style object handle + memory management contract.
 */
#ifndef COREKVM_KVMBASE_H
#define COREKVM_KVMBASE_H

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Fixed-width types. C90 has no <stdint.h>, so they are defined here and a
 * compile-time size assertion below rejects targets where they do not hold.
 */
typedef unsigned char  KVMUInt8;
typedef signed char    KVMInt8;
typedef unsigned short KVMUInt16;
typedef short          KVMInt16;
typedef unsigned int   KVMUInt32;
typedef int            KVMInt32;

typedef long           KVMIndex;   /* signed length/offset/count */
typedef int            KVMBool;
typedef unsigned long  KVMTypeID;

#define KVM_TRUE  1
#define KVM_FALSE 0

/* Compile-time width checks (illegal negative array size on mismatch). */
typedef char KVMStaticAssertUInt8[(sizeof(KVMUInt8) == 1) ? 1 : -1];
typedef char KVMStaticAssertUInt16[(sizeof(KVMUInt16) == 2) ? 1 : -1];
typedef char KVMStaticAssertUInt32[(sizeof(KVMUInt32) == 4) ? 1 : -1];

typedef enum _KVMStatus {
    kKVMSuccess = 0,
    kKVMErrorNoMemory,
    kKVMErrorInvalidArgument,
    kKVMErrorProtocol,
    kKVMErrorWouldBlock,
    kKVMErrorClosed,
    kKVMErrorUnsupported
} KVMStatus;

/* Opaque handle common to every CoreKVM object. */
typedef void *KVMRef;

KVMRef    KVMRetain(KVMRef object);
void      KVMRelease(KVMRef object);
KVMIndex  KVMGetRetainCount(KVMRef object);
KVMTypeID KVMGetTypeID(KVMRef object);

#if defined(__cplusplus)
}
#endif

#endif /* COREKVM_KVMBASE_H */
```

Create `CoreKVM/core/KVMRuntime.h`:

```c
/*
 * CoreKVM internal object runtime. Every object embeds KVMObjectHeader as its
 * first member so a KVMRef can be reinterpreted as its header.
 */
#ifndef COREKVM_KVMRUNTIME_H
#define COREKVM_KVMRUNTIME_H

#include "CoreKVM/KVMBase.h"

typedef void (*KVMDeallocFn)(void *object);

typedef struct _KVMObjectHeader {
    KVMTypeID    typeID;
    KVMIndex     retainCount;
    KVMDeallocFn dealloc;
} KVMObjectHeader;

/*
 * Allocate a zero-filled object of `size` bytes (>= sizeof(KVMObjectHeader)),
 * with retain count 1. Returns NULL on allocation failure or bad arguments.
 */
void *KVMRuntimeCreate(KVMTypeID typeID, KVMIndex size, KVMDeallocFn dealloc);

#endif /* COREKVM_KVMRUNTIME_H */
```

Create `CoreKVM/core/KVMBase.c`:

```c
/*
 * CoreKVM object runtime implementation.
 */
#include "CoreKVM/KVMBase.h"
#include "KVMRuntime.h"

#include <stdlib.h>
#include <string.h>

void *
KVMRuntimeCreate(KVMTypeID typeID, KVMIndex size, KVMDeallocFn dealloc)
{
    KVMObjectHeader *header;

    if (size < (KVMIndex)sizeof(KVMObjectHeader)) {
        return NULL;
    }

    header = (KVMObjectHeader *)malloc((size_t)size);
    if (header == NULL) {
        return NULL;
    }

    memset(header, 0, (size_t)size);
    header->typeID = typeID;
    header->retainCount = 1;
    header->dealloc = dealloc;
    return header;
}

KVMRef
KVMRetain(KVMRef object)
{
    KVMObjectHeader *header;

    if (object == NULL) {
        return NULL;
    }
    header = (KVMObjectHeader *)object;
    header->retainCount++;
    return object;
}

void
KVMRelease(KVMRef object)
{
    KVMObjectHeader *header;

    if (object == NULL) {
        return;
    }
    header = (KVMObjectHeader *)object;
    header->retainCount--;
    if (header->retainCount <= 0) {
        if (header->dealloc != NULL) {
            header->dealloc(object);
        }
        free(header);
    }
}

KVMIndex
KVMGetRetainCount(KVMRef object)
{
    if (object == NULL) {
        return 0;
    }
    return ((KVMObjectHeader *)object)->retainCount;
}

KVMTypeID
KVMGetTypeID(KVMRef object)
{
    if (object == NULL) {
        return 0;
    }
    return ((KVMObjectHeader *)object)->typeID;
}
```

Create `CoreKVM/CMakeLists.txt`:

```cmake
CMAKE_MINIMUM_REQUIRED(VERSION 3.5)
PROJECT(CoreKVM C)

SET(CMAKE_C_STANDARD 90)
SET(CMAKE_C_STANDARD_REQUIRED ON)

IF(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    ADD_COMPILE_OPTIONS(-std=c90 -pedantic -Wall -Wextra -Werror)
ELSEIF(MSVC)
    ADD_COMPILE_OPTIONS(/W4 /WX)
ENDIF()

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/include)

ADD_LIBRARY(corekvm STATIC
    core/KVMBase.c
)
TARGET_INCLUDE_DIRECTORIES(corekvm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

ENABLE_TESTING()

# Tests may reach internal headers under core/.
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/core)

ADD_EXECUTABLE(test_base tests/test_base.c)
TARGET_LINK_LIBRARIES(test_base corekvm)
ADD_TEST(NAME base COMMAND test_base)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake -S CoreKVM -B build/corekvm && cmake --build build/corekvm && ctest --test-dir build/corekvm --output-on-failure`
Expected: PASS — `test_base: OK`, `1/1 tests passed`.

- [ ] **Step 5: Commit**

```bash
git add CoreKVM/CMakeLists.txt CoreKVM/include/CoreKVM/KVMBase.h CoreKVM/core/KVMRuntime.h CoreKVM/core/KVMBase.c CoreKVM/tests/test_base.c
git commit -m "feat(corekvm): object runtime — KVMRef retain/release, status, types"
```

---

### Task 2: `KVMFrameBuffer` creation, pixel storage, and Get-rule access

**Files:**
- Create: `CoreKVM/include/CoreKVM/KVMFrameBuffer.h`
- Create: `CoreKVM/core/KVMFrameBuffer.c`
- Modify: `CoreKVM/CMakeLists.txt` (add source + test)
- Test: `CoreKVM/tests/test_framebuffer.c`

**Interfaces:**
- Consumes: `KVMRuntimeCreate`, `KVMRelease`, base types from Task 1.
- Produces:
  - `typedef struct _KVMFrameBuffer *KVMFrameBufferRef;`
  - `typedef struct _KVMRect { KVMInt32 x; KVMInt32 y; KVMInt32 width; KVMInt32 height; } KVMRect;`
  - `KVMFrameBufferRef KVMFrameBufferCreate(KVMInt32 width, KVMInt32 height)` — canonical BGRA8888, 4 bytes/pixel, zero-filled. NULL on bad size/OOM.
  - `KVMInt32 KVMFrameBufferGetWidth(KVMFrameBufferRef fb)` / `KVMFrameBufferGetHeight(...)`.
  - `const KVMUInt8 *KVMFrameBufferGetPixels(KVMFrameBufferRef fb, KVMIndex *outStride)` — borrowed pointer, `*outStride` = bytes per row (`width * 4`).

- [ ] **Step 1: Write the failing test**

Create `CoreKVM/tests/test_framebuffer.c`:

```c
/*
 * Unit tests for KVMFrameBuffer allocation and access.
 */
#include "CoreKVM/KVMFrameBuffer.h"

#include <stdio.h>

static int gTestsFailed = 0;

#define KVM_CHECK(cond)                                                        \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            gTestsFailed = 1;                                                  \
        }                                                                      \
    } while (0)

static void
TestCreateAndAccess(void)
{
    KVMFrameBufferRef fb;
    const KVMUInt8 *pixels;
    KVMIndex stride;
    KVMIndex i;
    KVMBool allZero;

    fb = KVMFrameBufferCreate(4, 3);
    KVM_CHECK(fb != NULL);
    KVM_CHECK(KVMFrameBufferGetWidth(fb) == 4);
    KVM_CHECK(KVMFrameBufferGetHeight(fb) == 3);

    stride = 0;
    pixels = KVMFrameBufferGetPixels(fb, &stride);
    KVM_CHECK(pixels != NULL);
    KVM_CHECK(stride == 4 * 4);

    allZero = KVM_TRUE;
    for (i = 0; i < stride * 3; i++) {
        if (pixels[i] != 0) {
            allZero = KVM_FALSE;
        }
    }
    KVM_CHECK(allZero == KVM_TRUE);

    KVMRelease(fb);
}

static void
TestRejectsBadSize(void)
{
    KVM_CHECK(KVMFrameBufferCreate(0, 10) == NULL);
    KVM_CHECK(KVMFrameBufferCreate(10, -1) == NULL);
}

int
main(void)
{
    TestCreateAndAccess();
    TestRejectsBadSize();
    if (gTestsFailed) {
        printf("test_framebuffer: FAILED\n");
        return 1;
    }
    printf("test_framebuffer: OK\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/corekvm`
Expected: FAIL — `KVMFrameBuffer.h` not found.

- [ ] **Step 3: Write the header and implementation**

Create `CoreKVM/include/CoreKVM/KVMFrameBuffer.h`:

```c
/*
 * CoreKVM framebuffer: a canonical BGRA8888 surface. Foreign pixel formats are
 * converted into it (Task 3); damage is tracked as dirty rectangles (Task 4).
 */
#ifndef COREKVM_KVMFRAMEBUFFER_H
#define COREKVM_KVMFRAMEBUFFER_H

#include "CoreKVM/KVMBase.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _KVMFrameBuffer *KVMFrameBufferRef;

typedef struct _KVMRect {
    KVMInt32 x;
    KVMInt32 y;
    KVMInt32 width;
    KVMInt32 height;
} KVMRect;

/* Canonical surface is always BGRA8888, 4 bytes per pixel. */
#define KVM_FRAMEBUFFER_BYTES_PER_PIXEL 4

KVMFrameBufferRef KVMFrameBufferCreate(KVMInt32 width, KVMInt32 height);
KVMInt32          KVMFrameBufferGetWidth(KVMFrameBufferRef fb);
KVMInt32          KVMFrameBufferGetHeight(KVMFrameBufferRef fb);

/*
 * Borrowed pointer to the BGRA pixel storage; *outStride receives the row
 * stride in bytes (width * 4). Storage lives as long as the framebuffer.
 */
const KVMUInt8 *KVMFrameBufferGetPixels(KVMFrameBufferRef fb, KVMIndex *outStride);

#if defined(__cplusplus)
}
#endif

#endif /* COREKVM_KVMFRAMEBUFFER_H */
```

Create `CoreKVM/core/KVMFrameBuffer.c`:

```c
/*
 * CoreKVM framebuffer implementation.
 */
#include "CoreKVM/KVMFrameBuffer.h"
#include "KVMRuntime.h"

#include <stdlib.h>

static const KVMTypeID kKVMFrameBufferTypeID = 0x4642554FUL; /* 'FBUO' */

struct _KVMFrameBuffer {
    KVMObjectHeader base;
    KVMInt32        width;
    KVMInt32        height;
    KVMIndex        stride;
    KVMUInt8       *pixels;
};

static void
KVMFrameBufferDealloc(void *object)
{
    struct _KVMFrameBuffer *fb = (struct _KVMFrameBuffer *)object;

    if (fb->pixels != NULL) {
        free(fb->pixels);
        fb->pixels = NULL;
    }
}

KVMFrameBufferRef
KVMFrameBufferCreate(KVMInt32 width, KVMInt32 height)
{
    struct _KVMFrameBuffer *fb;
    KVMIndex stride;

    if (width <= 0 || height <= 0) {
        return NULL;
    }

    fb = (struct _KVMFrameBuffer *)KVMRuntimeCreate(
        kKVMFrameBufferTypeID, (KVMIndex)sizeof(*fb), KVMFrameBufferDealloc);
    if (fb == NULL) {
        return NULL;
    }

    stride = (KVMIndex)width * KVM_FRAMEBUFFER_BYTES_PER_PIXEL;
    fb->pixels = (KVMUInt8 *)calloc((size_t)(stride * height), 1);
    if (fb->pixels == NULL) {
        KVMRelease((KVMRef)fb);
        return NULL;
    }

    fb->width = width;
    fb->height = height;
    fb->stride = stride;
    return fb;
}

KVMInt32
KVMFrameBufferGetWidth(KVMFrameBufferRef fb)
{
    return (fb != NULL) ? fb->width : 0;
}

KVMInt32
KVMFrameBufferGetHeight(KVMFrameBufferRef fb)
{
    return (fb != NULL) ? fb->height : 0;
}

const KVMUInt8 *
KVMFrameBufferGetPixels(KVMFrameBufferRef fb, KVMIndex *outStride)
{
    if (fb == NULL) {
        return NULL;
    }
    if (outStride != NULL) {
        *outStride = fb->stride;
    }
    return fb->pixels;
}
```

Modify `CoreKVM/CMakeLists.txt` — add `core/KVMFrameBuffer.c` to the `corekvm` library sources, and after the `test_base` block add:

```cmake
ADD_EXECUTABLE(test_framebuffer tests/test_framebuffer.c)
TARGET_LINK_LIBRARIES(test_framebuffer corekvm)
ADD_TEST(NAME framebuffer COMMAND test_framebuffer)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake -S CoreKVM -B build/corekvm && cmake --build build/corekvm && ctest --test-dir build/corekvm --output-on-failure`
Expected: PASS — `test_framebuffer: OK`, `2/2 tests passed`.

- [ ] **Step 5: Commit**

```bash
git add CoreKVM/include/CoreKVM/KVMFrameBuffer.h CoreKVM/core/KVMFrameBuffer.c CoreKVM/CMakeLists.txt CoreKVM/tests/test_framebuffer.c
git commit -m "feat(corekvm): KVMFrameBuffer — BGRA surface, create + Get-rule access"
```

---

### Task 3: Pixel-format conversion into the canonical surface

**Files:**
- Modify: `CoreKVM/include/CoreKVM/KVMFrameBuffer.h` (add `KVMPixelFormat` + write API)
- Modify: `CoreKVM/core/KVMFrameBuffer.c` (implement conversion)
- Modify: `CoreKVM/CMakeLists.txt` (add test)
- Test: `CoreKVM/tests/test_pixelconvert.c`

**Interfaces:**
- Consumes: `KVMFrameBufferRef`, `KVMRect`, `KVMFrameBufferGetPixels` from Task 2.
- Produces:
  - `typedef enum _KVMPixelFormat { kKVMPixelFormatBGRA8888 = 0, kKVMPixelFormatRGB888, kKVMPixelFormatRGB565 } KVMPixelFormat;`
  - `KVMStatus KVMFrameBufferWriteRect(KVMFrameBufferRef fb, KVMRect rect, const void *src, KVMPixelFormat srcFormat, KVMIndex srcStride)` — converts `src` (rows of `srcFormat`, `srcStride` bytes each) into the canonical BGRA surface at `rect`. Rejects out-of-bounds rects with `kKVMErrorInvalidArgument`.

- [ ] **Step 1: Write the failing test**

Create `CoreKVM/tests/test_pixelconvert.c`:

```c
/*
 * Unit tests for pixel-format conversion into the canonical BGRA surface.
 */
#include "CoreKVM/KVMFrameBuffer.h"

#include <stdio.h>

static int gTestsFailed = 0;

#define KVM_CHECK(cond)                                                        \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            gTestsFailed = 1;                                                  \
        }                                                                      \
    } while (0)

static void
TestWriteRGB565Red(void)
{
    KVMFrameBufferRef fb;
    const KVMUInt8 *pixels;
    KVMIndex stride;
    KVMRect rect;
    KVMUInt16 srcRow[2];
    KVMStatus status;

    /* Two RGB565 red pixels: R=0x1F, G=0, B=0 -> 0xF800. */
    srcRow[0] = (KVMUInt16)0xF800;
    srcRow[1] = (KVMUInt16)0xF800;

    fb = KVMFrameBufferCreate(2, 1);
    KVM_CHECK(fb != NULL);

    rect.x = 0;
    rect.y = 0;
    rect.width = 2;
    rect.height = 1;
    status = KVMFrameBufferWriteRect(fb, rect, srcRow, kKVMPixelFormatRGB565,
                                     (KVMIndex)sizeof(srcRow));
    KVM_CHECK(status == kKVMSuccess);

    pixels = KVMFrameBufferGetPixels(fb, &stride);
    /* BGRA: B=0, G=0, R=255, A=255. */
    KVM_CHECK(pixels[0] == 0);
    KVM_CHECK(pixels[1] == 0);
    KVM_CHECK(pixels[2] == 255);
    KVM_CHECK(pixels[3] == 255);

    KVMRelease(fb);
}

static void
TestRejectsOutOfBounds(void)
{
    KVMFrameBufferRef fb;
    KVMRect rect;
    KVMUInt8 src[4];
    KVMStatus status;

    fb = KVMFrameBufferCreate(2, 2);
    rect.x = 1;
    rect.y = 1;
    rect.width = 2; /* extends to x=3 > 2 */
    rect.height = 1;
    status = KVMFrameBufferWriteRect(fb, rect, src, kKVMPixelFormatBGRA8888, 8);
    KVM_CHECK(status == kKVMErrorInvalidArgument);
    KVMRelease(fb);
}

int
main(void)
{
    TestWriteRGB565Red();
    TestRejectsOutOfBounds();
    if (gTestsFailed) {
        printf("test_pixelconvert: FAILED\n");
        return 1;
    }
    printf("test_pixelconvert: OK\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/corekvm`
Expected: FAIL — `kKVMPixelFormatRGB565` / `KVMFrameBufferWriteRect` undeclared.

- [ ] **Step 3: Add the format enum, declaration, and implementation**

In `CoreKVM/include/CoreKVM/KVMFrameBuffer.h`, after the `KVMRect` typedef add:

```c
typedef enum _KVMPixelFormat {
    kKVMPixelFormatBGRA8888 = 0,   /* canonical */
    kKVMPixelFormatRGB888,
    kKVMPixelFormatRGB565
} KVMPixelFormat;
```

and after `KVMFrameBufferGetPixels` declaration add:

```c
/*
 * Convert `src` (rows of `srcFormat`, `srcStride` bytes per row) into the
 * canonical BGRA surface at `rect`. Returns kKVMErrorInvalidArgument if `rect`
 * lies outside the framebuffer.
 */
KVMStatus KVMFrameBufferWriteRect(KVMFrameBufferRef fb, KVMRect rect,
                                  const void *src, KVMPixelFormat srcFormat,
                                  KVMIndex srcStride);
```

In `CoreKVM/core/KVMFrameBuffer.c`, add before the end of the file:

```c
static void
KVMConvertPixel(const KVMUInt8 *src, KVMPixelFormat format, KVMUInt8 *outBgra)
{
    KVMUInt16 v;

    switch (format) {
    case kKVMPixelFormatBGRA8888:
        outBgra[0] = src[0];
        outBgra[1] = src[1];
        outBgra[2] = src[2];
        outBgra[3] = src[3];
        break;

    case kKVMPixelFormatRGB888:
        outBgra[0] = src[2]; /* B */
        outBgra[1] = src[1]; /* G */
        outBgra[2] = src[0]; /* R */
        outBgra[3] = 255;
        break;

    case kKVMPixelFormatRGB565:
    default:
        v = (KVMUInt16)(src[0] | (src[1] << 8));
        /* Expand 5/6/5 to 8 bits by replicating high bits into low. */
        outBgra[0] = (KVMUInt8)(((v & 0x001F) << 3) | ((v & 0x001F) >> 2));
        outBgra[1] = (KVMUInt8)(((v & 0x07E0) >> 3) | ((v & 0x07E0) >> 9));
        outBgra[2] = (KVMUInt8)(((v & 0xF800) >> 8) | ((v & 0xF800) >> 13));
        outBgra[3] = 255;
        break;
    }
}

static KVMIndex
KVMBytesPerSourcePixel(KVMPixelFormat format)
{
    switch (format) {
    case kKVMPixelFormatBGRA8888:
        return 4;
    case kKVMPixelFormatRGB888:
        return 3;
    case kKVMPixelFormatRGB565:
    default:
        return 2;
    }
}

KVMStatus
KVMFrameBufferWriteRect(KVMFrameBufferRef fb, KVMRect rect, const void *src,
                        KVMPixelFormat srcFormat, KVMIndex srcStride)
{
    const KVMUInt8 *srcBytes;
    KVMIndex srcBpp;
    KVMInt32 row;
    KVMInt32 col;

    if (fb == NULL || src == NULL) {
        return kKVMErrorInvalidArgument;
    }
    if (rect.x < 0 || rect.y < 0 || rect.width <= 0 || rect.height <= 0) {
        return kKVMErrorInvalidArgument;
    }
    if (rect.x + rect.width > fb->width || rect.y + rect.height > fb->height) {
        return kKVMErrorInvalidArgument;
    }

    srcBytes = (const KVMUInt8 *)src;
    srcBpp = KVMBytesPerSourcePixel(srcFormat);

    for (row = 0; row < rect.height; row++) {
        const KVMUInt8 *srcLine = srcBytes + (KVMIndex)row * srcStride;
        KVMUInt8 *dstLine =
            fb->pixels + (KVMIndex)(rect.y + row) * fb->stride +
            (KVMIndex)rect.x * KVM_FRAMEBUFFER_BYTES_PER_PIXEL;

        for (col = 0; col < rect.width; col++) {
            KVMConvertPixel(srcLine + (KVMIndex)col * srcBpp, srcFormat,
                            dstLine + (KVMIndex)col *
                                          KVM_FRAMEBUFFER_BYTES_PER_PIXEL);
        }
    }
    return kKVMSuccess;
}
```

Add to `CoreKVM/CMakeLists.txt`:

```cmake
ADD_EXECUTABLE(test_pixelconvert tests/test_pixelconvert.c)
TARGET_LINK_LIBRARIES(test_pixelconvert corekvm)
ADD_TEST(NAME pixelconvert COMMAND test_pixelconvert)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake -S CoreKVM -B build/corekvm && cmake --build build/corekvm && ctest --test-dir build/corekvm --output-on-failure`
Expected: PASS — `test_pixelconvert: OK`, `3/3 tests passed`.

- [ ] **Step 5: Commit**

```bash
git add CoreKVM/include/CoreKVM/KVMFrameBuffer.h CoreKVM/core/KVMFrameBuffer.c CoreKVM/CMakeLists.txt CoreKVM/tests/test_pixelconvert.c
git commit -m "feat(corekvm): pixel-format conversion (BGRA/RGB888/RGB565) into canonical surface"
```

---

### Task 4: Dirty-region accumulator with coalescing

**Files:**
- Create: `CoreKVM/include/CoreKVM/KVMDirty.h`
- Create: `CoreKVM/core/KVMDirty.c`
- Modify: `CoreKVM/CMakeLists.txt`
- Test: `CoreKVM/tests/test_dirty.c`

**Interfaces:**
- Consumes: `KVMRect` (Task 2), base runtime (Task 1).
- Produces:
  - `typedef struct _KVMDirty *KVMDirtyRef;`
  - `KVMDirtyRef KVMDirtyCreate(void)`.
  - `void KVMDirtyMark(KVMDirtyRef dirty, KVMRect rect)` — adds a rect, coalescing it into any existing rect it intersects or edge-touches (bounding-box union).
  - `KVMIndex KVMDirtyGetCount(KVMDirtyRef dirty)`.
  - `KVMBool KVMDirtyGetRect(KVMDirtyRef dirty, KVMIndex index, KVMRect *outRect)`.
  - `void KVMDirtyClear(KVMDirtyRef dirty)`.

- [ ] **Step 1: Write the failing test**

Create `CoreKVM/tests/test_dirty.c`:

```c
/*
 * Unit tests for the dirty-region accumulator.
 */
#include "CoreKVM/KVMDirty.h"

#include <stdio.h>

static int gTestsFailed = 0;

#define KVM_CHECK(cond)                                                        \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            gTestsFailed = 1;                                                  \
        }                                                                      \
    } while (0)

static KVMRect
MakeRect(KVMInt32 x, KVMInt32 y, KVMInt32 w, KVMInt32 h)
{
    KVMRect r;
    r.x = x;
    r.y = y;
    r.width = w;
    r.height = h;
    return r;
}

static void
TestDisjointStaySeparate(void)
{
    KVMDirtyRef dirty = KVMDirtyCreate();
    KVM_CHECK(dirty != NULL);
    KVMDirtyMark(dirty, MakeRect(0, 0, 2, 2));
    KVMDirtyMark(dirty, MakeRect(10, 10, 2, 2));
    KVM_CHECK(KVMDirtyGetCount(dirty) == 2);
    KVMRelease(dirty);
}

static void
TestOverlapCoalesces(void)
{
    KVMDirtyRef dirty = KVMDirtyCreate();
    KVMRect out;

    KVMDirtyMark(dirty, MakeRect(0, 0, 4, 4));
    KVMDirtyMark(dirty, MakeRect(2, 2, 4, 4));
    KVM_CHECK(KVMDirtyGetCount(dirty) == 1);
    KVM_CHECK(KVMDirtyGetRect(dirty, 0, &out) == KVM_TRUE);
    /* Union bounding box: (0,0) .. (6,6). */
    KVM_CHECK(out.x == 0 && out.y == 0 && out.width == 6 && out.height == 6);

    KVMDirtyClear(dirty);
    KVM_CHECK(KVMDirtyGetCount(dirty) == 0);
    KVMRelease(dirty);
}

int
main(void)
{
    TestDisjointStaySeparate();
    TestOverlapCoalesces();
    if (gTestsFailed) {
        printf("test_dirty: FAILED\n");
        return 1;
    }
    printf("test_dirty: OK\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/corekvm`
Expected: FAIL — `KVMDirty.h` not found.

- [ ] **Step 3: Write the header and implementation**

Create `CoreKVM/include/CoreKVM/KVMDirty.h`:

```c
/*
 * CoreKVM dirty-region accumulator: collects damaged rectangles and coalesces
 * overlapping or edge-touching ones by bounding-box union.
 */
#ifndef COREKVM_KVMDIRTY_H
#define COREKVM_KVMDIRTY_H

#include "CoreKVM/KVMBase.h"
#include "CoreKVM/KVMFrameBuffer.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _KVMDirty *KVMDirtyRef;

KVMDirtyRef KVMDirtyCreate(void);
void        KVMDirtyMark(KVMDirtyRef dirty, KVMRect rect);
KVMIndex    KVMDirtyGetCount(KVMDirtyRef dirty);
KVMBool     KVMDirtyGetRect(KVMDirtyRef dirty, KVMIndex index, KVMRect *outRect);
void        KVMDirtyClear(KVMDirtyRef dirty);

#if defined(__cplusplus)
}
#endif

#endif /* COREKVM_KVMDIRTY_H */
```

Create `CoreKVM/core/KVMDirty.c`:

```c
/*
 * CoreKVM dirty-region accumulator implementation.
 */
#include "CoreKVM/KVMDirty.h"
#include "KVMRuntime.h"

#include <stdlib.h>

static const KVMTypeID kKVMDirtyTypeID = 0x44495254UL; /* 'DIRT' */

#define KVM_DIRTY_INITIAL_CAPACITY 8

struct _KVMDirty {
    KVMObjectHeader base;
    KVMRect        *rects;
    KVMIndex        count;
    KVMIndex        capacity;
};

static void
KVMDirtyDealloc(void *object)
{
    struct _KVMDirty *dirty = (struct _KVMDirty *)object;

    if (dirty->rects != NULL) {
        free(dirty->rects);
        dirty->rects = NULL;
    }
}

static KVMBool
KVMRectsTouchOrOverlap(const KVMRect *a, const KVMRect *b)
{
    /* Expand `a` by one pixel so edge-adjacent rects count as touching. */
    KVMInt32 ax0 = a->x - 1;
    KVMInt32 ay0 = a->y - 1;
    KVMInt32 ax1 = a->x + a->width + 1;
    KVMInt32 ay1 = a->y + a->height + 1;
    KVMInt32 bx0 = b->x;
    KVMInt32 by0 = b->y;
    KVMInt32 bx1 = b->x + b->width;
    KVMInt32 by1 = b->y + b->height;

    if (ax1 <= bx0 || bx1 <= ax0) {
        return KVM_FALSE;
    }
    if (ay1 <= by0 || by1 <= ay0) {
        return KVM_FALSE;
    }
    return KVM_TRUE;
}

static KVMRect
KVMRectUnion(const KVMRect *a, const KVMRect *b)
{
    KVMRect out;
    KVMInt32 x0 = (a->x < b->x) ? a->x : b->x;
    KVMInt32 y0 = (a->y < b->y) ? a->y : b->y;
    KVMInt32 ax1 = a->x + a->width;
    KVMInt32 ay1 = a->y + a->height;
    KVMInt32 bx1 = b->x + b->width;
    KVMInt32 by1 = b->y + b->height;
    KVMInt32 x1 = (ax1 > bx1) ? ax1 : bx1;
    KVMInt32 y1 = (ay1 > by1) ? ay1 : by1;

    out.x = x0;
    out.y = y0;
    out.width = x1 - x0;
    out.height = y1 - y0;
    return out;
}

KVMDirtyRef
KVMDirtyCreate(void)
{
    struct _KVMDirty *dirty;

    dirty = (struct _KVMDirty *)KVMRuntimeCreate(
        kKVMDirtyTypeID, (KVMIndex)sizeof(*dirty), KVMDirtyDealloc);
    if (dirty == NULL) {
        return NULL;
    }
    dirty->rects =
        (KVMRect *)malloc(sizeof(KVMRect) * KVM_DIRTY_INITIAL_CAPACITY);
    if (dirty->rects == NULL) {
        KVMRelease((KVMRef)dirty);
        return NULL;
    }
    dirty->capacity = KVM_DIRTY_INITIAL_CAPACITY;
    dirty->count = 0;
    return dirty;
}

void
KVMDirtyMark(KVMDirtyRef dirty, KVMRect rect)
{
    KVMIndex i;

    if (dirty == NULL || rect.width <= 0 || rect.height <= 0) {
        return;
    }

    /* Coalesce into the first rect it touches; repeat until stable. */
    for (i = 0; i < dirty->count; i++) {
        if (KVMRectsTouchOrOverlap(&dirty->rects[i], &rect)) {
            rect = KVMRectUnion(&dirty->rects[i], &rect);
            dirty->rects[i] = dirty->rects[dirty->count - 1];
            dirty->count--;
            i = -1; /* restart scan; ++ makes it 0 */
        }
    }

    if (dirty->count == dirty->capacity) {
        KVMIndex newCap = dirty->capacity * 2;
        KVMRect *grown =
            (KVMRect *)realloc(dirty->rects, sizeof(KVMRect) * (size_t)newCap);
        if (grown == NULL) {
            return; /* drop the mark rather than corrupt state */
        }
        dirty->rects = grown;
        dirty->capacity = newCap;
    }
    dirty->rects[dirty->count] = rect;
    dirty->count++;
}

KVMIndex
KVMDirtyGetCount(KVMDirtyRef dirty)
{
    return (dirty != NULL) ? dirty->count : 0;
}

KVMBool
KVMDirtyGetRect(KVMDirtyRef dirty, KVMIndex index, KVMRect *outRect)
{
    if (dirty == NULL || outRect == NULL) {
        return KVM_FALSE;
    }
    if (index < 0 || index >= dirty->count) {
        return KVM_FALSE;
    }
    *outRect = dirty->rects[index];
    return KVM_TRUE;
}

void
KVMDirtyClear(KVMDirtyRef dirty)
{
    if (dirty != NULL) {
        dirty->count = 0;
    }
}
```

Add to `CoreKVM/CMakeLists.txt`: append `core/KVMDirty.c` to the `corekvm` sources, and add:

```cmake
ADD_EXECUTABLE(test_dirty tests/test_dirty.c)
TARGET_LINK_LIBRARIES(test_dirty corekvm)
ADD_TEST(NAME dirty COMMAND test_dirty)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake -S CoreKVM -B build/corekvm && cmake --build build/corekvm && ctest --test-dir build/corekvm --output-on-failure`
Expected: PASS — `test_dirty: OK`, `4/4 tests passed`.

- [ ] **Step 5: Commit**

```bash
git add CoreKVM/include/CoreKVM/KVMDirty.h CoreKVM/core/KVMDirty.c CoreKVM/CMakeLists.txt CoreKVM/tests/test_dirty.c
git commit -m "feat(corekvm): dirty-region accumulator with bounding-box coalescing"
```

---

### Task 5: Input event model

**Files:**
- Create: `CoreKVM/include/CoreKVM/KVMInput.h`
- Create: `CoreKVM/core/KVMInput.c`
- Modify: `CoreKVM/CMakeLists.txt`
- Test: `CoreKVM/tests/test_input.c`

**Interfaces:**
- Consumes: base types (Task 1).
- Produces:
  - `typedef enum _KVMInputKind { kKVMInputPointerMove=0, kKVMInputPointerButton, kKVMInputWheel, kKVMInputKey, kKVMInputRelativePointer } KVMInputKind;`
  - `typedef struct _KVMInputEvent { KVMInputKind kind; KVMInt32 x; KVMInt32 y; KVMUInt32 buttonMask; KVMInt32 wheelDelta; KVMUInt32 keysym; KVMUInt32 scancode; KVMBool pressed; } KVMInputEvent;`
  - Constructors returning a fully-initialized event: `KVMInputEvent KVMInputMakePointerMove(KVMInt32 x, KVMInt32 y, KVMUInt32 buttonMask)`, `KVMInputEvent KVMInputMakeKey(KVMUInt32 keysym, KVMUInt32 scancode, KVMBool pressed)`, `KVMInputEvent KVMInputMakeRelative(KVMInt32 dx, KVMInt32 dy, KVMUInt32 buttonMask)`.

- [ ] **Step 1: Write the failing test**

Create `CoreKVM/tests/test_input.c`:

```c
/*
 * Unit tests for the input event model constructors.
 */
#include "CoreKVM/KVMInput.h"

#include <stdio.h>

static int gTestsFailed = 0;

#define KVM_CHECK(cond)                                                        \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            gTestsFailed = 1;                                                  \
        }                                                                      \
    } while (0)

static void
TestPointerMove(void)
{
    KVMInputEvent e = KVMInputMakePointerMove(100, 50, 0x1);
    KVM_CHECK(e.kind == kKVMInputPointerMove);
    KVM_CHECK(e.x == 100);
    KVM_CHECK(e.y == 50);
    KVM_CHECK(e.buttonMask == 0x1);
    KVM_CHECK(e.pressed == KVM_FALSE);
}

static void
TestKey(void)
{
    KVMInputEvent e = KVMInputMakeKey(0x0041U, 0x001EU, KVM_TRUE);
    KVM_CHECK(e.kind == kKVMInputKey);
    KVM_CHECK(e.keysym == 0x0041U);
    KVM_CHECK(e.scancode == 0x001EU);
    KVM_CHECK(e.pressed == KVM_TRUE);
}

static void
TestRelative(void)
{
    KVMInputEvent e = KVMInputMakeRelative(-3, 4, 0x2);
    KVM_CHECK(e.kind == kKVMInputRelativePointer);
    KVM_CHECK(e.x == -3);
    KVM_CHECK(e.y == 4);
    KVM_CHECK(e.buttonMask == 0x2);
}

int
main(void)
{
    TestPointerMove();
    TestKey();
    TestRelative();
    if (gTestsFailed) {
        printf("test_input: FAILED\n");
        return 1;
    }
    printf("test_input: OK\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/corekvm`
Expected: FAIL — `KVMInput.h` not found.

- [ ] **Step 3: Write the header and implementation**

Create `CoreKVM/include/CoreKVM/KVMInput.h`:

```c
/*
 * CoreKVM unified input event model: absolute + relative pointer, wheel, and
 * key events carrying both an X keysym and a hardware scancode.
 */
#ifndef COREKVM_KVMINPUT_H
#define COREKVM_KVMINPUT_H

#include "CoreKVM/KVMBase.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef enum _KVMInputKind {
    kKVMInputPointerMove = 0,
    kKVMInputPointerButton,
    kKVMInputWheel,
    kKVMInputKey,
    kKVMInputRelativePointer
} KVMInputKind;

typedef struct _KVMInputEvent {
    KVMInputKind kind;
    KVMInt32     x;          /* absolute position, or dx for relative */
    KVMInt32     y;          /* absolute position, or dy for relative */
    KVMUInt32    buttonMask; /* one bit per pointer button */
    KVMInt32     wheelDelta;
    KVMUInt32    keysym;     /* X11 keysym */
    KVMUInt32    scancode;   /* hardware scancode (extended key events) */
    KVMBool      pressed;    /* button/key press vs release */
} KVMInputEvent;

KVMInputEvent KVMInputMakePointerMove(KVMInt32 x, KVMInt32 y,
                                      KVMUInt32 buttonMask);
KVMInputEvent KVMInputMakeKey(KVMUInt32 keysym, KVMUInt32 scancode,
                              KVMBool pressed);
KVMInputEvent KVMInputMakeRelative(KVMInt32 dx, KVMInt32 dy,
                                   KVMUInt32 buttonMask);

#if defined(__cplusplus)
}
#endif

#endif /* COREKVM_KVMINPUT_H */
```

Create `CoreKVM/core/KVMInput.c`:

```c
/*
 * CoreKVM input event constructors.
 */
#include "CoreKVM/KVMInput.h"

#include <string.h>

static KVMInputEvent
KVMInputZero(void)
{
    KVMInputEvent e;
    memset(&e, 0, sizeof(e));
    return e;
}

KVMInputEvent
KVMInputMakePointerMove(KVMInt32 x, KVMInt32 y, KVMUInt32 buttonMask)
{
    KVMInputEvent e = KVMInputZero();
    e.kind = kKVMInputPointerMove;
    e.x = x;
    e.y = y;
    e.buttonMask = buttonMask;
    return e;
}

KVMInputEvent
KVMInputMakeKey(KVMUInt32 keysym, KVMUInt32 scancode, KVMBool pressed)
{
    KVMInputEvent e = KVMInputZero();
    e.kind = kKVMInputKey;
    e.keysym = keysym;
    e.scancode = scancode;
    e.pressed = pressed;
    return e;
}

KVMInputEvent
KVMInputMakeRelative(KVMInt32 dx, KVMInt32 dy, KVMUInt32 buttonMask)
{
    KVMInputEvent e = KVMInputZero();
    e.kind = kKVMInputRelativePointer;
    e.x = dx;
    e.y = dy;
    e.buttonMask = buttonMask;
    return e;
}
```

Add to `CoreKVM/CMakeLists.txt`: append `core/KVMInput.c` to `corekvm` sources, and:

```cmake
ADD_EXECUTABLE(test_input tests/test_input.c)
TARGET_LINK_LIBRARIES(test_input corekvm)
ADD_TEST(NAME input COMMAND test_input)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake -S CoreKVM -B build/corekvm && cmake --build build/corekvm && ctest --test-dir build/corekvm --output-on-failure`
Expected: PASS — `test_input: OK`, `5/5 tests passed`.

- [ ] **Step 5: Commit**

```bash
git add CoreKVM/include/CoreKVM/KVMInput.h CoreKVM/core/KVMInput.c CoreKVM/CMakeLists.txt CoreKVM/tests/test_input.c
git commit -m "feat(corekvm): unified input event model (pointer/relative/wheel/key+scancode)"
```

---

### Task 6: Sans-I/O growable byte buffer

**Files:**
- Create: `CoreKVM/include/CoreKVM/KVMBuffer.h`
- Create: `CoreKVM/core/KVMBuffer.c`
- Modify: `CoreKVM/CMakeLists.txt`
- Test: `CoreKVM/tests/test_buffer.c`

**Interfaces:**
- Consumes: base runtime (Task 1).
- Produces:
  - `typedef struct _KVMBuffer *KVMBufferRef;`
  - `KVMBufferRef KVMBufferCreate(void)`.
  - `KVMStatus KVMBufferAppend(KVMBufferRef buffer, const void *data, KVMIndex length)`.
  - `KVMIndex KVMBufferGetLength(KVMBufferRef buffer)`.
  - `const KVMUInt8 *KVMBufferGetBytes(KVMBufferRef buffer)` — borrowed, points at the current front.
  - `KVMStatus KVMBufferConsume(KVMBufferRef buffer, KVMIndex length)` — drops `length` bytes from the front; `kKVMErrorInvalidArgument` if `length` exceeds the contents.

- [ ] **Step 1: Write the failing test**

Create `CoreKVM/tests/test_buffer.c`:

```c
/*
 * Unit tests for the sans-I/O byte buffer.
 */
#include "CoreKVM/KVMBuffer.h"

#include <stdio.h>
#include <string.h>

static int gTestsFailed = 0;

#define KVM_CHECK(cond)                                                        \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            gTestsFailed = 1;                                                  \
        }                                                                      \
    } while (0)

static void
TestAppendConsume(void)
{
    KVMBufferRef buffer = KVMBufferCreate();
    const KVMUInt8 *bytes;

    KVM_CHECK(buffer != NULL);
    KVM_CHECK(KVMBufferAppend(buffer, "hello", 5) == kKVMSuccess);
    KVM_CHECK(KVMBufferAppend(buffer, "world", 5) == kKVMSuccess);
    KVM_CHECK(KVMBufferGetLength(buffer) == 10);

    KVM_CHECK(KVMBufferConsume(buffer, 3) == kKVMSuccess);
    KVM_CHECK(KVMBufferGetLength(buffer) == 7);

    bytes = KVMBufferGetBytes(buffer);
    KVM_CHECK(memcmp(bytes, "loworld", 7) == 0);

    KVMRelease(buffer);
}

static void
TestConsumeOverrunRejected(void)
{
    KVMBufferRef buffer = KVMBufferCreate();
    KVMBufferAppend(buffer, "ab", 2);
    KVM_CHECK(KVMBufferConsume(buffer, 5) == kKVMErrorInvalidArgument);
    KVM_CHECK(KVMBufferGetLength(buffer) == 2);
    KVMRelease(buffer);
}

int
main(void)
{
    TestAppendConsume();
    TestConsumeOverrunRejected();
    if (gTestsFailed) {
        printf("test_buffer: FAILED\n");
        return 1;
    }
    printf("test_buffer: OK\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/corekvm`
Expected: FAIL — `KVMBuffer.h` not found.

- [ ] **Step 3: Write the header and implementation**

Create `CoreKVM/include/CoreKVM/KVMBuffer.h`:

```c
/*
 * CoreKVM growable byte buffer: the sans-I/O plumbing that inbound and outbound
 * protocol bytes flow through. Consume drops bytes from the front.
 */
#ifndef COREKVM_KVMBUFFER_H
#define COREKVM_KVMBUFFER_H

#include "CoreKVM/KVMBase.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _KVMBuffer *KVMBufferRef;

KVMBufferRef    KVMBufferCreate(void);
KVMStatus       KVMBufferAppend(KVMBufferRef buffer, const void *data,
                                KVMIndex length);
KVMIndex        KVMBufferGetLength(KVMBufferRef buffer);
const KVMUInt8 *KVMBufferGetBytes(KVMBufferRef buffer);
KVMStatus       KVMBufferConsume(KVMBufferRef buffer, KVMIndex length);

#if defined(__cplusplus)
}
#endif

#endif /* COREKVM_KVMBUFFER_H */
```

Create `CoreKVM/core/KVMBuffer.c`:

```c
/*
 * CoreKVM byte buffer implementation. Bytes are stored contiguously; the read
 * cursor advances on Consume and the buffer compacts when the cursor grows
 * large relative to the remaining contents.
 */
#include "CoreKVM/KVMBuffer.h"
#include "KVMRuntime.h"

#include <stdlib.h>
#include <string.h>

static const KVMTypeID kKVMBufferTypeID = 0x42554621UL; /* 'BUF!' */

#define KVM_BUFFER_INITIAL_CAPACITY 256

struct _KVMBuffer {
    KVMObjectHeader base;
    KVMUInt8       *data;
    KVMIndex        capacity;
    KVMIndex        head;   /* read cursor */
    KVMIndex        tail;   /* write cursor */
};

static void
KVMBufferDealloc(void *object)
{
    struct _KVMBuffer *buffer = (struct _KVMBuffer *)object;

    if (buffer->data != NULL) {
        free(buffer->data);
        buffer->data = NULL;
    }
}

KVMBufferRef
KVMBufferCreate(void)
{
    struct _KVMBuffer *buffer;

    buffer = (struct _KVMBuffer *)KVMRuntimeCreate(
        kKVMBufferTypeID, (KVMIndex)sizeof(*buffer), KVMBufferDealloc);
    if (buffer == NULL) {
        return NULL;
    }
    buffer->data = (KVMUInt8 *)malloc(KVM_BUFFER_INITIAL_CAPACITY);
    if (buffer->data == NULL) {
        KVMRelease((KVMRef)buffer);
        return NULL;
    }
    buffer->capacity = KVM_BUFFER_INITIAL_CAPACITY;
    buffer->head = 0;
    buffer->tail = 0;
    return buffer;
}

static KVMStatus
KVMBufferEnsureRoom(struct _KVMBuffer *buffer, KVMIndex extra)
{
    KVMIndex used = buffer->tail - buffer->head;
    KVMIndex needed;
    KVMIndex newCap;
    KVMUInt8 *grown;

    /* Compact first: slide contents to the front. */
    if (buffer->head > 0) {
        memmove(buffer->data, buffer->data + buffer->head, (size_t)used);
        buffer->head = 0;
        buffer->tail = used;
    }

    needed = used + extra;
    if (needed <= buffer->capacity) {
        return kKVMSuccess;
    }

    newCap = buffer->capacity;
    while (newCap < needed) {
        newCap *= 2;
    }
    grown = (KVMUInt8 *)realloc(buffer->data, (size_t)newCap);
    if (grown == NULL) {
        return kKVMErrorNoMemory;
    }
    buffer->data = grown;
    buffer->capacity = newCap;
    return kKVMSuccess;
}

KVMStatus
KVMBufferAppend(KVMBufferRef buffer, const void *data, KVMIndex length)
{
    KVMStatus status;

    if (buffer == NULL || (data == NULL && length > 0) || length < 0) {
        return kKVMErrorInvalidArgument;
    }
    if (length == 0) {
        return kKVMSuccess;
    }
    status = KVMBufferEnsureRoom(buffer, length);
    if (status != kKVMSuccess) {
        return status;
    }
    memcpy(buffer->data + buffer->tail, data, (size_t)length);
    buffer->tail += length;
    return kKVMSuccess;
}

KVMIndex
KVMBufferGetLength(KVMBufferRef buffer)
{
    return (buffer != NULL) ? (buffer->tail - buffer->head) : 0;
}

const KVMUInt8 *
KVMBufferGetBytes(KVMBufferRef buffer)
{
    if (buffer == NULL) {
        return NULL;
    }
    return buffer->data + buffer->head;
}

KVMStatus
KVMBufferConsume(KVMBufferRef buffer, KVMIndex length)
{
    if (buffer == NULL || length < 0) {
        return kKVMErrorInvalidArgument;
    }
    if (length > buffer->tail - buffer->head) {
        return kKVMErrorInvalidArgument;
    }
    buffer->head += length;
    if (buffer->head == buffer->tail) {
        buffer->head = 0;
        buffer->tail = 0;
    }
    return kKVMSuccess;
}
```

Add to `CoreKVM/CMakeLists.txt`: append `core/KVMBuffer.c` to `corekvm` sources, and:

```cmake
ADD_EXECUTABLE(test_buffer tests/test_buffer.c)
TARGET_LINK_LIBRARIES(test_buffer corekvm)
ADD_TEST(NAME buffer COMMAND test_buffer)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake -S CoreKVM -B build/corekvm && cmake --build build/corekvm && ctest --test-dir build/corekvm --output-on-failure`
Expected: PASS — `test_buffer: OK`, `6/6 tests passed`.

- [ ] **Step 5: Commit**

```bash
git add CoreKVM/include/CoreKVM/KVMBuffer.h CoreKVM/core/KVMBuffer.c CoreKVM/CMakeLists.txt CoreKVM/tests/test_buffer.c
git commit -m "feat(corekvm): sans-I/O growable byte buffer with compaction"
```

---

### Task 7: Emitted-event queue

**Files:**
- Create: `CoreKVM/include/CoreKVM/KVMEvent.h`
- Create: `CoreKVM/core/KVMEvent.c`
- Modify: `CoreKVM/CMakeLists.txt`
- Test: `CoreKVM/tests/test_event.c`

**Interfaces:**
- Consumes: base runtime (Task 1), `KVMRect` (Task 2), `KVMStatus` (Task 1).
- Produces:
  - `typedef enum _KVMEventKind { kKVMEventDamage=0, kKVMEventResize, kKVMEventCursor, kKVMEventChannelData, kKVMEventStatus } KVMEventKind;`
  - `typedef struct _KVMEvent { KVMEventKind kind; KVMRect rect; KVMInt32 width; KVMInt32 height; KVMStatus status; KVMUInt32 channelId; } KVMEvent;`
  - `typedef struct _KVMEventQueue *KVMEventQueueRef;`
  - `KVMEventQueueRef KVMEventQueueCreate(void)`.
  - `KVMStatus KVMEventQueuePush(KVMEventQueueRef queue, KVMEvent event)`.
  - `KVMBool KVMEventQueuePop(KVMEventQueueRef queue, KVMEvent *outEvent)` — FIFO; `KVM_FALSE` when empty.
  - `KVMIndex KVMEventQueueGetCount(KVMEventQueueRef queue)`.

- [ ] **Step 1: Write the failing test**

Create `CoreKVM/tests/test_event.c`:

```c
/*
 * Unit tests for the emitted-event queue (FIFO order).
 */
#include "CoreKVM/KVMEvent.h"

#include <stdio.h>

static int gTestsFailed = 0;

#define KVM_CHECK(cond)                                                        \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            gTestsFailed = 1;                                                  \
        }                                                                      \
    } while (0)

static void
TestFifoOrder(void)
{
    KVMEventQueueRef queue = KVMEventQueueCreate();
    KVMEvent a;
    KVMEvent b;
    KVMEvent out;

    KVM_CHECK(queue != NULL);

    a.kind = kKVMEventResize;
    a.width = 640;
    a.height = 480;
    b.kind = kKVMEventStatus;
    b.status = kKVMErrorClosed;

    KVM_CHECK(KVMEventQueuePush(queue, a) == kKVMSuccess);
    KVM_CHECK(KVMEventQueuePush(queue, b) == kKVMSuccess);
    KVM_CHECK(KVMEventQueueGetCount(queue) == 2);

    KVM_CHECK(KVMEventQueuePop(queue, &out) == KVM_TRUE);
    KVM_CHECK(out.kind == kKVMEventResize);
    KVM_CHECK(out.width == 640 && out.height == 480);

    KVM_CHECK(KVMEventQueuePop(queue, &out) == KVM_TRUE);
    KVM_CHECK(out.kind == kKVMEventStatus);
    KVM_CHECK(out.status == kKVMErrorClosed);

    KVM_CHECK(KVMEventQueuePop(queue, &out) == KVM_FALSE);
    KVMRelease(queue);
}

int
main(void)
{
    TestFifoOrder();
    if (gTestsFailed) {
        printf("test_event: FAILED\n");
        return 1;
    }
    printf("test_event: OK\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/corekvm`
Expected: FAIL — `KVMEvent.h` not found.

- [ ] **Step 3: Write the header and implementation**

Create `CoreKVM/include/CoreKVM/KVMEvent.h`:

```c
/*
 * CoreKVM emitted-event queue: protocol state machines push events here during
 * pump(); the embedder pops them to drive rendering, resizing, and channels.
 */
#ifndef COREKVM_KVMEVENT_H
#define COREKVM_KVMEVENT_H

#include "CoreKVM/KVMBase.h"
#include "CoreKVM/KVMFrameBuffer.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef enum _KVMEventKind {
    kKVMEventDamage = 0,
    kKVMEventResize,
    kKVMEventCursor,
    kKVMEventChannelData,
    kKVMEventStatus
} KVMEventKind;

typedef struct _KVMEvent {
    KVMEventKind kind;
    KVMRect      rect;      /* damage / cursor hotspot region */
    KVMInt32     width;     /* resize */
    KVMInt32     height;    /* resize */
    KVMStatus    status;    /* status change */
    KVMUInt32    channelId; /* channel data source */
} KVMEvent;

typedef struct _KVMEventQueue *KVMEventQueueRef;

KVMEventQueueRef KVMEventQueueCreate(void);
KVMStatus        KVMEventQueuePush(KVMEventQueueRef queue, KVMEvent event);
KVMBool          KVMEventQueuePop(KVMEventQueueRef queue, KVMEvent *outEvent);
KVMIndex         KVMEventQueueGetCount(KVMEventQueueRef queue);

#if defined(__cplusplus)
}
#endif

#endif /* COREKVM_KVMEVENT_H */
```

Create `CoreKVM/core/KVMEvent.c`:

```c
/*
 * CoreKVM emitted-event queue implementation: a growable ring buffer.
 */
#include "CoreKVM/KVMEvent.h"
#include "KVMRuntime.h"

#include <stdlib.h>

static const KVMTypeID kKVMEventQueueTypeID = 0x45564E54UL; /* 'EVNT' */

#define KVM_EVENT_QUEUE_INITIAL_CAPACITY 16

struct _KVMEventQueue {
    KVMObjectHeader base;
    KVMEvent       *events;
    KVMIndex        capacity;
    KVMIndex        head;
    KVMIndex        count;
};

static void
KVMEventQueueDealloc(void *object)
{
    struct _KVMEventQueue *queue = (struct _KVMEventQueue *)object;

    if (queue->events != NULL) {
        free(queue->events);
        queue->events = NULL;
    }
}

KVMEventQueueRef
KVMEventQueueCreate(void)
{
    struct _KVMEventQueue *queue;

    queue = (struct _KVMEventQueue *)KVMRuntimeCreate(
        kKVMEventQueueTypeID, (KVMIndex)sizeof(*queue), KVMEventQueueDealloc);
    if (queue == NULL) {
        return NULL;
    }
    queue->events =
        (KVMEvent *)malloc(sizeof(KVMEvent) * KVM_EVENT_QUEUE_INITIAL_CAPACITY);
    if (queue->events == NULL) {
        KVMRelease((KVMRef)queue);
        return NULL;
    }
    queue->capacity = KVM_EVENT_QUEUE_INITIAL_CAPACITY;
    queue->head = 0;
    queue->count = 0;
    return queue;
}

static KVMStatus
KVMEventQueueGrow(struct _KVMEventQueue *queue)
{
    KVMIndex newCap = queue->capacity * 2;
    KVMEvent *grown = (KVMEvent *)malloc(sizeof(KVMEvent) * (size_t)newCap);
    KVMIndex i;

    if (grown == NULL) {
        return kKVMErrorNoMemory;
    }
    /* Copy in logical order so head resets to 0. */
    for (i = 0; i < queue->count; i++) {
        grown[i] = queue->events[(queue->head + i) % queue->capacity];
    }
    free(queue->events);
    queue->events = grown;
    queue->capacity = newCap;
    queue->head = 0;
    return kKVMSuccess;
}

KVMStatus
KVMEventQueuePush(KVMEventQueueRef queue, KVMEvent event)
{
    KVMIndex tail;

    if (queue == NULL) {
        return kKVMErrorInvalidArgument;
    }
    if (queue->count == queue->capacity) {
        KVMStatus status = KVMEventQueueGrow(queue);
        if (status != kKVMSuccess) {
            return status;
        }
    }
    tail = (queue->head + queue->count) % queue->capacity;
    queue->events[tail] = event;
    queue->count++;
    return kKVMSuccess;
}

KVMBool
KVMEventQueuePop(KVMEventQueueRef queue, KVMEvent *outEvent)
{
    if (queue == NULL || outEvent == NULL || queue->count == 0) {
        return KVM_FALSE;
    }
    *outEvent = queue->events[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    return KVM_TRUE;
}

KVMIndex
KVMEventQueueGetCount(KVMEventQueueRef queue)
{
    return (queue != NULL) ? queue->count : 0;
}
```

Add to `CoreKVM/CMakeLists.txt`: append `core/KVMEvent.c` to `corekvm` sources, and:

```cmake
ADD_EXECUTABLE(test_event tests/test_event.c)
TARGET_LINK_LIBRARIES(test_event corekvm)
ADD_TEST(NAME event COMMAND test_event)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake -S CoreKVM -B build/corekvm && cmake --build build/corekvm && ctest --test-dir build/corekvm --output-on-failure`
Expected: PASS — `test_event: OK`, `7/7 tests passed`.

- [ ] **Step 5: Commit**

```bash
git add CoreKVM/include/CoreKVM/KVMEvent.h CoreKVM/core/KVMEvent.c CoreKVM/CMakeLists.txt CoreKVM/tests/test_event.c
git commit -m "feat(corekvm): emitted-event queue (FIFO ring) for the sans-I/O pump"
```

---

## Phase 1 Definition of Done

- `cmake -S CoreKVM -B build/corekvm && cmake --build build/corekvm && ctest --test-dir build/corekvm` reports **7/7 tests passing**.
- The `corekvm` static library builds clean under `-std=c90 -pedantic -Wall -Wextra -Werror`.
- The substrate is complete: object runtime, framebuffer + pixel conversion + dirty regions, input model, byte buffer, and event queue — all sans-I/O, all protocol-agnostic. Phase 2 (transports + TLS) and Phase 3 (libvnc handshake + Raw over loopback) build directly on these primitives.

## Phase roadmap (subsequent detailed plans)

2. Transports + TLS — `KVMTransport` vtable, TCP, WebSocket framing, mbedTLS BIO + TOFU pinning.
3. libvnc handshake + Raw encoding (client+server) over in-memory loopback.
4. Remaining encodings (RRE/Hextile/ZRLE/Tight-JPEG) + security (VNC-DES, VeNCrypt) + pseudo-encodings + flow control.
5. `KVMChannel` framework — clipboard + monitor wired; storage block-protocol; other channels stubbed with real framing.
6. Native reference client — headless PPM + optional SDL2.
7. UNO client shell — P/Invoke + Emscripten bindings, WriteableBitmap fallback surface, input, single session.
8. UNO per-head native surfaces + adaptive UI + multi-session + secure storage + channel drawer + monitor HUD + record/replay.
