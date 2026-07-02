#ifndef __netbsd_m88k_types_h
#define __netbsd_m88k_types_h

#define NETBSD_MACHINE_NAME "m88k"

typedef int32_t  netbsd_time_t;

typedef int32_t  netbsd_long_t;
typedef uint32_t netbsd_ulong_t;

typedef int32_t  netbsd_intptr_t;
typedef uint32_t netbsd_uintptr_t;

#ifdef __GNUC__
#define __netbsd_guest_alignment __attribute__((aligned(4)))
#else
#define __netbsd_guest_alignment
#endif

#endif  /* !__netbsd_m88k_types_h */
