#ifndef __obsd79_m88k_types_h
#define __obsd79_m88k_types_h

#define OBSD79_MACHINE_NAME "m88k"

typedef int32_t  obsd79_time_t;

typedef int32_t  obsd79_long_t;
typedef uint32_t obsd79_ulong_t;

typedef int32_t  obsd79_intptr_t;
typedef uint32_t obsd79_uintptr_t;

#ifdef __GNUC__
#define __obsd79_guest_alignment __attribute__((aligned(4)))
#else
#define __obsd79_guest_alignment
#endif

#endif  /* !__obsd79_m88k_types_h */
