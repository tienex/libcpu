#ifndef __nbsd101_m88k_types_h
#define __nbsd101_m88k_types_h

#define NBSD101_MACHINE_NAME "m88k"

typedef int32_t  nbsd101_time_t;

typedef int32_t  nbsd101_long_t;
typedef uint32_t nbsd101_ulong_t;

typedef int32_t  nbsd101_intptr_t;
typedef uint32_t nbsd101_uintptr_t;

#ifdef __GNUC__
#define __nbsd101_guest_alignment __attribute__((aligned(4)))
#else
#define __nbsd101_guest_alignment
#endif

#endif  /* !__nbsd101_m88k_types_h */
