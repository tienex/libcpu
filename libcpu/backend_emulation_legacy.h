/*
 * libcpu Backend Emulation - Legacy and Specialized Architecture Operations
 *
 * Covers operations from historical and niche architectures:
 * VAX, ALPHA, PARISC, IA-64, CRAY, PDP series, 68K, Z80, 6502, SPARC VIS,
 * DSPs (TMS320, 56000, C6X, Blackfin), SuperH, M88K, Hexagon, SPU, etc.
 *
 * These operations represent decades of CPU design and are useful for:
 * 1. Emulating legacy architectures
 * 2. Algorithm implementation
 * 3. Specialized computing patterns
 */

#ifndef __LIBCPU_BACKEND_EMULATION_LEGACY_H__
#define __LIBCPU_BACKEND_EMULATION_LEGACY_H__

#include "backend_emulation_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************
 * BCD and Packed Decimal Operations (VAX, 6502, Z80, 68K, x86)
 ***************************************************************************/

/* BCD (Binary Coded Decimal) - 2 digits per byte */
typedef struct {
	uint8_t *data;      /* BCD digits */
	size_t num_bytes;   /* Number of bytes */
	int sign;           /* 0=positive, 1=negative */
} bcd_t;

/* Packed decimal - VAX style (variable length) */
typedef struct {
	uint8_t *data;      /* Packed digits */
	size_t num_digits;  /* Number of decimal digits */
	int sign;
} packed_decimal_t;

/* BCD operations */
void bcd_add(bcd_t *result, const bcd_t *a, const bcd_t *b);
void bcd_sub(bcd_t *result, const bcd_t *a, const bcd_t *b);
void bcd_mul(bcd_t *result, const bcd_t *a, const bcd_t *b);
void bcd_div(bcd_t *quotient, bcd_t *remainder, const bcd_t *dividend, const bcd_t *divisor);
void bcd_adjust_add(uint8_t *byte);   /* DAA - Decimal Adjust After Addition */
void bcd_adjust_sub(uint8_t *byte);   /* DAS - Decimal Adjust After Subtraction */

/* Packed decimal operations (VAX ASHP, MOVP, etc.) */
void packed_add(packed_decimal_t *result, const packed_decimal_t *a, const packed_decimal_t *b);
void packed_sub(packed_decimal_t *result, const packed_decimal_t *a, const packed_decimal_t *b);
void packed_mul(packed_decimal_t *result, const packed_decimal_t *a, const packed_decimal_t *b);
void packed_div(packed_decimal_t *quotient, const packed_decimal_t *dividend, const packed_decimal_t *divisor);
void packed_to_binary(int64_t *result, const packed_decimal_t *packed);
void binary_to_packed(packed_decimal_t *result, int64_t value);

/***************************************************************************
 * Bit Field Operations (68K, M88K, MIPS, IA-64)
 ***************************************************************************/

/* Extract bit field */
uint64_t bitfield_extract(uint64_t value, uint32_t offset, uint32_t width);
uint64_t bitfield_extract_signed(uint64_t value, uint32_t offset, uint32_t width);

/* Insert bit field */
uint64_t bitfield_insert(uint64_t dest, uint64_t src, uint32_t offset, uint32_t width);

/* Find first set bit in field */
int bitfield_ffs(uint64_t value, uint32_t offset, uint32_t width);

/* Bit field test and set/clear */
int bitfield_test(uint64_t value, uint32_t offset);
uint64_t bitfield_set(uint64_t value, uint32_t offset);
uint64_t bitfield_clear(uint64_t value, uint32_t offset);
uint64_t bitfield_toggle(uint64_t value, uint32_t offset);

/* 68K-style bit field operations */
void bfextu(uint32_t *dest, const void *base, int32_t offset, uint32_t width);  /* Unsigned extract */
void bfexts(int32_t *dest, const void *base, int32_t offset, uint32_t width);   /* Signed extract */
void bfins(void *base, uint32_t value, int32_t offset, uint32_t width);         /* Insert */
void bfset(void *base, int32_t offset, uint32_t width);                         /* Set bits */
void bfclr(void *base, int32_t offset, uint32_t width);                         /* Clear bits */
void bfchg(void *base, int32_t offset, uint32_t width);                         /* Toggle bits */
int bftst(const void *base, int32_t offset, uint32_t width);                    /* Test bits */

/***************************************************************************
 * String Operations (VAX, x86, POWER)
 ***************************************************************************/

/* VAX-style string operations */
void movc3(void *dst, const void *src, size_t count);           /* Move characters */
void movc5(void *dst, size_t dstlen, uint8_t fill,             /* Move with fill */
           const void *src, size_t srclen);
int cmpc3(const void *s1, const void *s2, size_t count);        /* Compare */
int cmpc5(const void *s1, size_t len1,                          /* Compare with fill */
          const void *s2, size_t len2, uint8_t fill);
void *locc(uint8_t chr, size_t count, const void *str);         /* Locate character */
void *skpc(uint8_t chr, size_t count, const void *str);         /* Skip character */
int spanc(uint8_t table[256], size_t count, const void *str);   /* Span characters */
int scanc(uint8_t table[256], size_t count, const void *str);   /* Scan characters */

/* x86-style string operations */
void rep_movs(void *dst, const void *src, size_t count, int width);     /* REP MOVSB/W/D/Q */
void rep_stos(void *dst, uint64_t value, size_t count, int width);      /* REP STOSB/W/D/Q */
void rep_lods(uint64_t *value, const void *src, size_t count, int width); /* REP LODSB/W/D/Q */
void *rep_scas(const void *str, uint64_t value, size_t count, int width); /* REP SCASB/W/D/Q */
void *rep_cmps(const void *s1, const void *s2, size_t count, int width);  /* REP CMPSB/W/D/Q */

/* POWER string operations */
void power_lswi(uint32_t *regs, const void *mem, uint32_t num_bytes);   /* Load string word immediate */
void power_stswi(void *mem, const uint32_t *regs, uint32_t num_bytes);  /* Store string word immediate */

/***************************************************************************
 * Block Operations (Z80, 68K)
 ***************************************************************************/

/* Z80 block operations */
void z80_ldir(void *dst, const void *src, uint16_t *bc);        /* Load, increment, repeat */
void z80_lddr(void *dst, const void *src, uint16_t *bc);        /* Load, decrement, repeat */
void z80_cpir(const void *mem, uint8_t value, uint16_t *bc);    /* Compare, increment, repeat */
void z80_cpdr(const void *mem, uint8_t value, uint16_t *bc);    /* Compare, decrement, repeat */
void z80_inir(void *dst, uint16_t port, uint16_t *bc);          /* Input, increment, repeat */
void z80_indr(void *dst, uint16_t port, uint16_t *bc);          /* Input, decrement, repeat */
void z80_otir(uint16_t port, const void *src, uint16_t *bc);    /* Output, increment, repeat */
void z80_otdr(uint16_t port, const void *src, uint16_t *bc);    /* Output, decrement, repeat */

/***************************************************************************
 * Fixed-Point Arithmetic (DSPs: TMS320, 56000, C6X, Blackfin)
 ***************************************************************************/

/* Q format fixed-point (Qm.n where m+n+1=total bits) */
typedef int32_t q15_t;   /* Q0.15 (1 sign, 15 fractional) */
typedef int32_t q31_t;   /* Q0.31 (1 sign, 31 fractional) */
typedef int16_t q7_t;    /* Q0.7 */

/* Fixed-point operations */
q31_t q31_add(q31_t a, q31_t b);
q31_t q31_sub(q31_t a, q31_t b);
q31_t q31_mul(q31_t a, q31_t b);           /* With scaling */
q31_t q31_div(q31_t a, q31_t b);
q31_t q31_abs(q31_t a);
q31_t q31_negate(q31_t a);

/* Q15 operations */
q15_t q15_add(q15_t a, q15_t b);
q15_t q15_sub(q15_t a, q15_t b);
q15_t q15_mul(q15_t a, q15_t b);

/* Saturating fixed-point */
q31_t q31_add_sat(q31_t a, q31_t b);
q31_t q31_sub_sat(q31_t a, q31_t b);
q31_t q31_mul_sat(q31_t a, q31_t b);

/* Float to/from fixed-point */
q31_t float_to_q31(float f);
float q31_to_float(q31_t q);
q15_t float_to_q15(float f);
float q15_to_float(q15_t q);

/***************************************************************************
 * DSP-Specific Operations
 ***************************************************************************/

/* MAC (Multiply-Accumulate) - fundamental DSP operation */
int64_t mac_i32(int64_t acc, int32_t a, int32_t b);             /* acc += a * b */
int64_t msu_i32(int64_t acc, int32_t a, int32_t b);             /* acc -= a * b */
int64_t mac_q31(int64_t acc, q31_t a, q31_t b);                 /* Fixed-point MAC */

/* Saturating MAC */
int64_t mac_sat(int64_t acc, int32_t a, int32_t b, int64_t max, int64_t min);

/* Complex multiply (DSP) */
void complex_mul_q15(q15_t *real_out, q15_t *imag_out,
                     q15_t a_real, q15_t a_imag,
                     q15_t b_real, q15_t b_imag);

/* FFT butterfly operations */
void fft_butterfly_radix2(q31_t *x0, q31_t *x1, q31_t w_real, q31_t w_imag);
void fft_butterfly_radix4(q31_t *x0, q31_t *x1, q31_t *x2, q31_t *x3,
                          q31_t w1_real, q31_t w1_imag,
                          q31_t w2_real, q31_t w2_imag,
                          q31_t w3_real, q31_t w3_imag);

/* Bit-reversed addressing (DSP) */
uint32_t bit_reverse_address(uint32_t addr, uint32_t bits);
void bit_reverse_copy(void *dst, const void *src, size_t count, size_t element_size, uint32_t bits);

/* Circular buffer operations (DSP) */
typedef struct {
	void *base;
	size_t size;
	size_t offset;
} circular_buffer_t;

void circular_buffer_write(circular_buffer_t *buf, const void *data, size_t size);
void circular_buffer_read(void *data, circular_buffer_t *buf, size_t size);

/***************************************************************************
 * Predication and Speculation (IA-64, PARISC, ARM)
 ***************************************************************************/

/* IA-64 style predication */
typedef struct {
	uint64_t predicates;   /* 64 predicate registers */
} predicate_state_t;

/* Set predicate based on comparison */
void pred_cmp_eq(predicate_state_t *ps, uint32_t p1, uint32_t p2, uint64_t a, uint64_t b);
void pred_cmp_lt(predicate_state_t *ps, uint32_t p1, uint32_t p2, uint64_t a, uint64_t b);
void pred_cmp_le(predicate_state_t *ps, uint32_t p1, uint32_t p2, uint64_t a, uint64_t b);

/* Predicated operations */
uint64_t pred_add(predicate_state_t *ps, uint32_t pred, uint64_t a, uint64_t b);
uint64_t pred_sub(predicate_state_t *ps, uint32_t pred, uint64_t a, uint64_t b);
void pred_store(predicate_state_t *ps, uint32_t pred, void *addr, uint64_t value);
uint64_t pred_load(predicate_state_t *ps, uint32_t pred, const void *addr);

/* PARISC nullification */
typedef struct {
	int nullify_next;     /* Nullify next instruction */
} nullify_state_t;

void nullify_set(nullify_state_t *ns, int condition);
int nullify_check(nullify_state_t *ns);

/* IA-64 speculation */
typedef struct {
	uint64_t nat_bits;    /* NaT (Not a Thing) bits */
	uint64_t alat[64];    /* Advanced Load Address Table */
} speculation_state_t;

uint64_t speculative_load(speculation_state_t *ss, uint32_t reg, const void *addr);
int check_speculation(speculation_state_t *ss, uint32_t reg);
void advance_speculation(speculation_state_t *ss);

/***************************************************************************
 * Register Windows (SPARC, AM29000)
 ***************************************************************************/

typedef struct {
	uint32_t regs[512];   /* Register file */
	uint32_t cwp;         /* Current window pointer */
	uint32_t wim;         /* Window invalid mask */
	uint32_t nwindows;    /* Number of windows */
} register_window_state_t;

/* SPARC register window operations */
void sparc_save(register_window_state_t *rws);     /* Save window */
void sparc_restore(register_window_state_t *rws);  /* Restore window */
int sparc_window_overflow(register_window_state_t *rws);
int sparc_window_underflow(register_window_state_t *rws);

/***************************************************************************
 * Conditional Moves (ALPHA, MIPS, ARM, x86-64)
 ***************************************************************************/

/* Conditional move operations */
uint64_t cmov_eq(uint64_t dst, uint64_t src, uint64_t a, uint64_t b);   /* Move if equal */
uint64_t cmov_ne(uint64_t dst, uint64_t src, uint64_t a, uint64_t b);
uint64_t cmov_lt(uint64_t dst, uint64_t src, uint64_t a, uint64_t b);
uint64_t cmov_le(uint64_t dst, uint64_t src, uint64_t a, uint64_t b);
uint64_t cmov_gt(uint64_t dst, uint64_t src, uint64_t a, uint64_t b);
uint64_t cmov_ge(uint64_t dst, uint64_t src, uint64_t a, uint64_t b);

/* ALPHA-style conditional move */
uint64_t alpha_cmoveq(uint64_t ra, uint64_t rb, uint64_t rc);  /* rc = ra==0 ? rb : rc */
uint64_t alpha_cmovne(uint64_t ra, uint64_t rb, uint64_t rc);
uint64_t alpha_cmovlt(uint64_t ra, uint64_t rb, uint64_t rc);
uint64_t alpha_cmovle(uint64_t ra, uint64_t rb, uint64_t rc);
uint64_t alpha_cmovgt(uint64_t ra, uint64_t rb, uint64_t rc);
uint64_t alpha_cmovge(uint64_t ra, uint64_t rb, uint64_t rc);

/***************************************************************************
 * VAX Special Operations
 ***************************************************************************/

/* Queue operations */
typedef struct queue_entry {
	struct queue_entry *flink;  /* Forward link */
	struct queue_entry *blink;  /* Backward link */
} queue_entry_t;

typedef struct {
	queue_entry_t *head;
} queue_t;

/* VAX queue instructions */
void vax_insque(queue_entry_t *entry, queue_entry_t *pred);    /* Insert into queue */
void vax_remque(queue_entry_t *entry, queue_entry_t **removed); /* Remove from queue */

/* Polynomial evaluation - POLY instruction */
double vax_poly(double arg, int degree, const double *coeffs);

/* Index computation - INDEX instruction */
uint32_t vax_index(uint32_t subscript, uint32_t low, uint32_t high, uint32_t size);

/* CRC operations */
uint32_t vax_crc(uint32_t initial_crc, const void *table, const void *stream, size_t len);

/* Edit packed string - EDITPC */
void vax_editpc(const char *pattern, const packed_decimal_t *src, char *dst);

/***************************************************************************
 * SPARC VIS (Visual Instruction Set) Operations
 ***************************************************************************/

/* Pixel operations */
uint64_t vis_fpack16(uint64_t src);         /* Pack 4x16-bit to 4x8-bit */
uint64_t vis_fpack32(uint64_t src1, uint64_t src2); /* Pack 2x32-bit to 2x16-bit */
uint64_t vis_fexpand(uint64_t src);         /* Expand 4x8-bit to 4x16-bit */

/* Pixel compare */
uint64_t vis_fcmpgt8(uint64_t a, uint64_t b);  /* Compare 8x8-bit */
uint64_t vis_fcmpgt16(uint64_t a, uint64_t b); /* Compare 4x16-bit */
uint64_t vis_fcmpgt32(uint64_t a, uint64_t b); /* Compare 2x32-bit */

/* Alignment operations */
uint64_t vis_faligndata(uint64_t a, uint64_t b, uint32_t offset);

/* Pixel distance */
uint64_t vis_pdist(uint64_t a, uint64_t b, uint64_t acc);

/***************************************************************************
 * i860 Graphics Operations
 ***************************************************************************/

/* Pixel operations (i860) */
uint64_t i860_pix_add(uint64_t a, uint64_t b);     /* Pixel add (4x16-bit) */
uint64_t i860_pix_sub(uint64_t a, uint64_t b);     /* Pixel subtract */
uint64_t i860_pix_cmp(uint64_t a, uint64_t b);     /* Pixel compare */

/* Dual-operation mode */
typedef struct {
	float adder_result;
	float multiplier_result;
} i860_dual_result_t;

i860_dual_result_t i860_dual_fmul_fadd(float m1, float m2, float a1, float a2);

/***************************************************************************
 * CRAY Vector Operations
 ***************************************************************************/

/* CRAY-style vector operations (classic vector supercomputer) */
void cray_vector_add(double *result, const double *a, const double *b, size_t length);
void cray_vector_mul(double *result, const double *a, const double *b, size_t length);
double cray_vector_reduce_sum(const double *vec, size_t length);
double cray_vector_reduce_max(const double *vec, size_t length);

/* CRAY-style population count (early implementation) */
uint64_t cray_popcnt(uint64_t x);

/* CRAY vector mask operations */
void cray_vector_where(double *result, const double *a, const double *b, const uint64_t *mask, size_t length);

/***************************************************************************
 * Hexagon (Qualcomm) Special Operations
 ***************************************************************************/

/* Hardware loops */
typedef struct {
	uint32_t start_addr;
	uint32_t end_addr;
	uint32_t count;
	int active;
} hardware_loop_t;

void hexagon_loop_setup(hardware_loop_t *loop, uint32_t start, uint32_t end, uint32_t count);
int hexagon_loop_check(hardware_loop_t *loop, uint32_t pc);

/* Predicated operations (Hexagon style) */
uint32_t hexagon_add_pred(uint32_t a, uint32_t b, int pred);
void hexagon_store_pred(void *addr, uint32_t value, int pred);

/***************************************************************************
 * Cell SPU Operations
 ***************************************************************************/

/* SPU (Synergistic Processing Unit) operations */
void spu_shuffle(vector_t *result, const vector_t *a, const vector_t *b, const vector_t *pattern);
void spu_selb(vector_t *result, const vector_t *a, const vector_t *b, const vector_t *mask);
void spu_gather_bits(uint32_t *result, const vector_t *src);
void spu_splat(vector_t *result, uint32_t value);

/***************************************************************************
 * Endian Conversion Operations
 ***************************************************************************/

/* Byte swapping - common across many architectures */
uint16_t swap16(uint16_t x);
uint32_t swap32(uint32_t x);
uint64_t swap64(uint64_t x);

/* Load/store with endian conversion */
uint32_t load_be32(const void *ptr);
uint32_t load_le32(const void *ptr);
void store_be32(void *ptr, uint32_t value);
void store_le32(void *ptr, uint32_t value);

/* POWER load/store with byte reversal */
uint32_t power_lwbrx(const void *ptr);
void power_stwbrx(void *ptr, uint32_t value);

/***************************************************************************
 * Atomic Operations (Various architectures)
 ***************************************************************************/

/* SPARC CAS variants */
uint32_t sparc_cas(uint32_t *ptr, uint32_t expected, uint32_t desired);
uint64_t sparc_casx(uint64_t *ptr, uint64_t expected, uint64_t desired);

/* 68K CAS2 - double compare-and-swap */
int m68k_cas2(uint32_t *ptr1, uint32_t expect1, uint32_t update1,
              uint32_t *ptr2, uint32_t expect2, uint32_t update2);

/* LL/SC (Load-Linked/Store-Conditional) - MIPS, ALPHA, POWER, RISC-V */
typedef struct {
	void *ll_addr;
	uint64_t ll_value;
	int ll_active;
} ll_sc_state_t;

uint64_t ll(ll_sc_state_t *state, const void *ptr);
int sc(ll_sc_state_t *state, void *ptr, uint64_t value);

/***************************************************************************
 * Misc Legacy Operations
 ***************************************************************************/

/* Skip instructions (PDP-11, others) */
int skip_if_zero(uint32_t value);
int skip_if_negative(uint32_t value);
int skip_if_overflow(uint32_t a, uint32_t b);

/* 6502 BCD mode */
typedef struct {
	int decimal_mode;    /* D flag */
} cpu_6502_state_t;

uint8_t m6502_adc_bcd(cpu_6502_state_t *cpu, uint8_t a, uint8_t b, int *carry);
uint8_t m6502_sbc_bcd(cpu_6502_state_t *cpu, uint8_t a, uint8_t b, int *carry);

/* Z8000 special operations */
void z8000_ldk(uint32_t *reg, uint32_t constant);  /* Load constant (16 values) */
void z8000_ldctl(uint32_t *reg, uint32_t control_reg);

#ifdef __cplusplus
}
#endif

#endif /* __LIBCPU_BACKEND_EMULATION_LEGACY_H__ */
