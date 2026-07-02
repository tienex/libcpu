#ifndef __openbsd_sparc_types_h
#define __openbsd_sparc_types_h

#define OPENBSD_MACHINE_NAME "sparc32"

typedef int32_t  openbsd_time_t;

typedef int32_t  openbsd_long_t;
typedef uint32_t openbsd_ulong_t;

typedef int32_t  openbsd_intptr_t;
typedef uint32_t openbsd_uintptr_t;

#ifdef __GNUC__
#define __openbsd_guest_alignment __attribute__ ((aligned (4)))
#else
#define __openbsd_guest_alignment
#endif

#endif  /* !__openbsd_sparc_types_h */
