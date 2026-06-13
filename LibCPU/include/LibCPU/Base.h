/** @file
  LibCPU base definitions: UEFI-style basic data types, COM HRESULT/SCODE
  status codes, and GUID/IID/CLSID identity types.

  This header is shared by the public C API and the C++ (COM) implementation,
  so it must compile cleanly as both C23 and C++20.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_BASE_H
#define LIBCPU_BASE_H

#include <stdint.h>
#include <stddef.h>

//
// UEFI-style basic data types.
//
typedef uint8_t   UINT8;
typedef uint16_t  UINT16;
typedef uint32_t  UINT32;
typedef uint64_t  UINT64;
typedef int8_t    INT8;
typedef int16_t   INT16;
typedef int32_t   INT32;
typedef int64_t   INT64;
typedef uintptr_t UINTN;
typedef intptr_t  INTN;
typedef char      CHAR8;
typedef uint16_t  CHAR16;
typedef unsigned char BOOLEAN;

#ifndef VOID
#define VOID void
#endif
#ifndef CONST
#define CONST const
#endif

#ifndef TRUE
#define TRUE  ((BOOLEAN)(1))
#endif
#ifndef FALSE
#define FALSE ((BOOLEAN)(0))
#endif

//
// UEFI parameter direction annotations (documentation only).
//
#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif
#ifndef OPTIONAL
#define OPTIONAL
#endif

//
// COM result type. HRESULT/SCODE are 32-bit; the high bit indicates failure.
//
typedef INT32 HRESULT;
typedef INT32 SCODE;

#define S_OK            ((HRESULT)0)
#define S_FALSE         ((HRESULT)1)
#define E_NOTIMPL       ((HRESULT)0x80004001)
#define E_NOINTERFACE   ((HRESULT)0x80004002)
#define E_POINTER       ((HRESULT)0x80004003)
#define E_ABORT         ((HRESULT)0x80004004)
#define E_FAIL          ((HRESULT)0x80004005)
#define E_OUTOFMEMORY   ((HRESULT)0x8007000E)
#define E_INVALIDARG    ((HRESULT)0x80070057)

#define SUCCEEDED(Hr)   (((HRESULT)(Hr)) >= 0)
#define FAILED(Hr)      (((HRESULT)(Hr)) <  0)

//
// COM identity. GUID is the structure; IID and CLSID are aliases used for
// interface identifiers and class identifiers respectively.
//
typedef struct _GUID {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} GUID;

typedef GUID IID;
typedef GUID CLSID;

typedef GUID CONST *PCGUID;
typedef GUID       *PGUID;

/**
  Compare two GUIDs for equality.

  @param[in]  pA  First GUID.
  @param[in]  pB  Second GUID.

  @retval TRUE   The GUIDs are bit-for-bit identical.
  @retval FALSE  The GUIDs differ.
**/
static inline BOOLEAN
CompareGuid (
    IN PCGUID pA,
    IN PCGUID pB
    )
{
    UINTN Index;

    if (pA->Data1 != pB->Data1 || pA->Data2 != pB->Data2 ||
        pA->Data3 != pB->Data3) {
        return FALSE;
    }
    for (Index = 0; Index < 8; Index++) {
        if (pA->Data4[Index] != pB->Data4[Index]) {
            return FALSE;
        }
    }
    return TRUE;
}

#endif // LIBCPU_BASE_H
