# Exotic Number Systems and Architectures

This document describes libcpu's support for unusual number representations and specialized architectures through the emulation layer.

## Overview

The exotic systems emulation layer (`backend_emulation_exotic.h/cpp`) provides transparent support for:

- **IEEE 754-2008 Decimal Floating Point** (DPD and BID encodings)
- **Historical Floating Point Formats** (VAX, IBM hexadecimal, Cray)
- **1's Complement Arithmetic** (PDP-1, CDC, UNIVAC)
- **Non-Power-of-2 Word Sizes** (PDP-10 36-bit, CDC 60-bit)
- **Pre-1.0 RISC-V Vector** (RVV 0.9)
- **Wireless MMX** (Intel XScale WMMX/WMMX2)
- **MIPS SIMD** (MSA2 extensions)
- **Loongson Multimedia** (LMMX)
- **ARM FPA** (Original ARM Floating Point Accelerator)

---

## IEEE 754-2008 Decimal Floating Point

### Overview

IEEE 754-2008 defines decimal floating point for financial and commercial applications where exact decimal arithmetic is required (unlike binary floating point which has rounding errors with decimal fractions).

### Formats

- **Decimal32**: 7 decimal digits precision
- **Decimal64**: 16 decimal digits precision
- **Decimal128**: 34 decimal digits precision

### Encodings

Two encoding schemes are supported:

1. **DPD (Densely Packed Decimal)**: Hardware-friendly encoding where 3 decimal digits pack into 10 bits
2. **BID (Binary Integer Decimal)**: Software-friendly encoding using binary coefficient

### Usage

```c
decimal64_t a, b, result;

// Create from double
double_to_dec64(&a, 123.456);
double_to_dec64(&b, 789.012);

// Perform exact decimal arithmetic
dec64_add(&result, &a, &b);  // No binary rounding errors

// Fused multiply-add
dec64_fma(&result, &a, &b, &c);  // result = a*b + c

// Convert between encodings
decimal64_t dpd_value;
dpd_value.format = DEC_FORMAT_DPD;
decimal64_t bid_value;
// Convert DPD to BID automatically in operations
```

### Quantum Operations

IEEE 754-2008 decimal includes quantum operations for financial rounding:

```c
// Quantize 'a' to have same exponent as 'b'
dec64_quantize(&result, &a, &b);

// Get quantum (ULP) of a value
dec64_quantum(&quantum, &a);
```

### Applications

- Financial calculations (currency, interest)
- Tax calculations
- Scientific data with decimal units
- IBM Power architecture (POWER6+)
- Intel decimal floating point instructions

---

## VAX Floating Point (1977-2000)

### Overview

Digital Equipment Corporation's VAX systems used custom floating point formats with different characteristics than IEEE 754.

### Key Differences from IEEE

- **No implicit leading bit** in mantissa (explicit mantissa)
- **Different exponent bias**
- **Different byte ordering** (PDP-11 endianness)
- **Reserved operand** instead of NaN
- **No subnormal numbers**

### Formats

| Format | Bits | Exponent | Mantissa | Bias | Range |
|--------|------|----------|----------|------|-------|
| F_floating | 32 | 8 bits | 23 bits | 128 | ~10^±38 |
| D_floating | 64 | 8 bits | 55 bits | 128 | ~10^±38 |
| G_floating | 64 | 11 bits | 52 bits | 1024 | ~10^±308 |
| H_floating | 128 | 15 bits | 112 bits | 16384 | ~10^±4932 |

### Usage

```c
vax_f_float_t vax_a, vax_b, vax_result;
float ieee_value = 3.14159f;

// Convert IEEE to VAX
ieee_float_to_vax_f(&vax_a, ieee_value);

// VAX arithmetic
vax_f_add(&vax_result, &vax_a, &vax_b);
vax_f_mul(&vax_result, &vax_a, &vax_b);

// Convert back to IEEE
float ieee_result;
vax_f_to_ieee_float(&ieee_result, &vax_result);
```

### Historical Context

- Used in VAX-11, VAX-8000, MicroVAX systems
- VMS operating system
- Many scientific applications from 1977-1990s
- Legacy data files still exist in scientific archives

---

## IBM Hexadecimal Floating Point (1960s-1990s)

### Overview

IBM mainframes (System/360, System/370, System/390) used base-16 (hexadecimal) floating point instead of base-2.

### Key Characteristics

- **Base 16 exponent** (not base 2 like IEEE)
- **Exponent bias of 64**
- **No implicit bit**
- **Not normalized** (can have leading zero nibbles)

### Formats

| Format | Bits | Exponent | Mantissa | Precision |
|--------|------|----------|----------|-----------|
| Short | 32 | 7 bits | 24 bits | ~6 decimal digits |
| Long | 64 | 7 bits | 56 bits | ~17 decimal digits |
| Extended | 128 | Two 64-bit halves | ~34 decimal digits |

### Usage

```c
ibm_long_float_t ibm_a, ibm_b, ibm_result;
double ieee_value = 2.718281828;

// Convert IEEE to IBM
ieee_double_to_ibm_long(&ibm_a, ieee_value);

// IBM arithmetic
ibm_long_add(&ibm_result, &ibm_a, &ibm_b);
ibm_long_mul(&ibm_result, &ibm_a, &ibm_b);

// Convert back
double ieee_result;
ibm_long_to_ieee_double(&ieee_result, &ibm_result);
```

### Precision Notes

- Base-16 means **3-4 bits of precision can be lost** to leading zeros
- Less precise than IEEE for same bit width
- Range is larger than IEEE for same exponent bits

### Historical Applications

- IBM mainframe FORTRAN, COBOL
- Scientific computing on System/360/370
- Legacy financial systems
- Geological survey data

---

## Cray Floating Point (1976-2000s)

### Overview

Cray supercomputers used custom 64-bit floating point optimized for vector processing.

### Format

- **64 bits total**
- **1 bit sign**
- **15 bits exponent** (base 2, bias 16384)
- **48 bits mantissa**
- **No exponent bias in original Cray-1**

### Key Differences

- Larger mantissa than IEEE double (48 vs 52 bits)
- Simpler format for fast vector operations
- No subnormals, infinities, or NaNs in original design

### Usage

```c
cray_float_t cray_a, cray_b, cray_result;

// Convert from IEEE
ieee_double_to_cray(&cray_a, 1.23456789);

// Cray operations
cray_add(&cray_result, &cray_a, &cray_b);
cray_mul(&cray_result, &cray_a, &cray_b);
cray_reciprocal(&cray_result, &cray_a);  // Fast reciprocal

// Convert to IEEE
double ieee_result;
cray_to_ieee_double(&ieee_result, &cray_result);
```

### Historical Context

- Cray-1, Cray-2, Cray X-MP, Cray Y-MP
- Supercomputing applications (weather, nuclear, CFD)
- Optimized for massive vector operations
- Legacy scientific datasets

---

## 1's Complement Arithmetic

### Overview

Early computers (PDP-1, CDC 6600, UNIVAC) used 1's complement representation instead of 2's complement.

### Key Characteristics

- **Two representations of zero**: +0 (all bits 0) and -0 (all bits 1)
- **Negation is bitwise NOT** (simpler hardware)
- **End-around carry** in addition
- **Symmetric range**: -(2^n - 1) to +(2^n - 1)

### 1's Complement vs 2's Complement

| Operation | 1's Complement | 2's Complement |
|-----------|----------------|----------------|
| Negation | Bitwise NOT | NOT + 1 |
| Zero | +0 and -0 | Only +0 |
| Addition | End-around carry | Standard |
| Range (8-bit) | -127 to +127 | -128 to +127 |

### Usage

```c
ones_comp_t a, b, result;

// Create values
twos_to_ones_comp(&a, 42);
twos_to_ones_comp(&b, -17);

// Arithmetic (handles end-around carry)
ones_comp_add(&result, &a, &b);
ones_comp_sub(&result, &a, &b);
ones_comp_mul(&result, &a, &b);

// Check for zero (must check both +0 and -0)
if (ones_comp_is_zero(&result)) {
    printf("Result is zero\n");
}

// Normalize (convert -0 to +0)
ones_comp_normalize(&result);

// Convert back to 2's complement
int64_t value = ones_comp_to_twos(&result);
```

### Historical Systems

- **PDP-1** (1959): First PDP computer
- **CDC 6600** (1964): First supercomputer
- **UNIVAC 1100** series (1962-1980s)
- **LINC** (1962): Early lab computer

---

## Non-Power-of-2 Word Sizes

### PDP-10: 36-bit Words

#### Overview

The PDP-10 (1966-1983) used 36-bit words, a common size in 1960s-1970s mainframes.

#### Characteristics

- **36-bit words** (not 32 or 64)
- **Variable-size bytes** (1-36 bits each!)
- **Byte pointers** specify position and size
- **5 x 7-bit ASCII** or **4 x 9-bit bytes** per word

#### Usage

```c
pdp10_word_t a, b, result;

a.bits = 0x123456789 & PDP10_WORD_MASK;  // Mask to 36 bits
b.bits = 0xABCDEF012 & PDP10_WORD_MASK;

// Arithmetic (auto-masks to 36 bits)
pdp10_add(&result, &a, &b);
pdp10_mul(&result, &a, &b);

pdp10_word_t quotient, remainder;
pdp10_div(&quotient, &remainder, &a, &b);

// Variable-size byte operations
pdp10_word_t byte_value;

// Load 7-bit byte at position 14
pdp10_ldb(&byte_value, &a, 14, 7);

// Deposit 9-bit byte at position 18
pdp10_dpb(&result, &byte_value, &a, 18, 9);
```

#### Byte Pointer Format

PDP-10 byte pointers encode:
- **Position** (bit offset)
- **Size** (1-36 bits)
- **Address** (memory location)

This allowed efficient character and bit-field manipulation.

#### Historical Applications

- TOPS-10, TOPS-20 operating systems
- ITS (MIT AI Lab)
- Early Lisp implementations (MacLisp)
- TECO text editor (precursor to Emacs)

### CDC 6600: 60-bit Words

#### Overview

CDC 6600 and 7600 supercomputers used 60-bit words.

#### Characteristics

- **60-bit words**
- **1's complement arithmetic**
- **15-bit, 18-bit, or 60-bit operands**
- **10 x 6-bit display code characters** per word

#### Usage

```c
cdc_word_t a, b, result;

a.bits = 0x0FEDCBA987654321 & CDC_WORD_MASK;  // Mask to 60 bits

// CDC arithmetic (1's complement with 60-bit words)
cdc_add(&result, &a, &b);  // Includes end-around carry
cdc_mul(&result, &a, &b);
```

---

## RVV 0.9 (Pre-1.0 RISC-V Vector)

### Overview

RISC-V Vector extension 0.9 was the pre-ratification specification with different semantics from the final 1.0 version.

### Key Differences from RVV 1.0

- **Different vsetvl semantics**
- **Different mask register organization**
- **Different instruction encodings**
- **Widening operation behavior changes**
- **Configuration register format differences**

### Usage

```c
rvv09_state_t vstate;
vector_t va, vb, vdest;

// Configure vector unit (RVV 0.9 semantics)
// avl=Application Vector Length, sew=8/16/32/64, lmul=1/2/4/8
rvv09_vsetvl(&vstate, 16, 8, 1);  // vl=16, SEW=8 bits, LMUL=1

// Widening add (doubles element width)
rvv09_vwadd_vv(&vdest, &va, &vb, &vstate);

// Add with carry-out (generates mask)
vector_t mask;
rvv09_vmadc(&mask, &va, &vb, &vstate);
```

### Why Support 0.9?

- Early RISC-V vector implementations (pre-2021)
- Legacy research code and simulators
- Transition period codebases
- Academic papers referencing 0.9 spec

---

## WMMX (Wireless MMX)

### Overview

Intel XScale processors included Wireless MMX for multimedia acceleration in embedded devices (PDAs, phones).

### Characteristics

- **64-bit WMMX registers** (wR0-wR15)
- **128-bit WMMX2 registers** (extension)
- **Pack/unpack operations** for multimedia codecs
- **Alignment operations** for bitstream processing

### Usage

```c
wmmx_reg_t wa, wb, wdest;

// Packed arithmetic (element_size: 8, 16, or 32 bits)
wmmx_wadd(&wdest, &wa, &wb, 8);   // Add 8 bytes
wmmx_wsub(&wdest, &wa, &wb, 16);  // Subtract 4 halfwords
wmmx_wmul(&wdest, &wa, &wb, 16);  // Multiply 4 halfwords

// Pack with saturation (for video encoding)
wmmx_wpack(&wdest, &wa, &wb, 8);  // Pack halfwords to bytes

// Unpack (for decoding)
wmmx_wunpack(&wdest, &wa, 8);  // Unpack bytes to halfwords

// Align for bitstream processing
wmmx_waligni(&wdest, &wa, &wb, 3);  // Align by 3 bytes

// Shuffle halfwords (for pixel manipulation)
wmmx_wshufh(&wdest, &wa, 0b11100100);  // Permute pattern
```

### WMMX2 Extensions

```c
wmmx2_reg_t wa2, wb2, wdest2;

// 128-bit operations
wmmx2_waddbhus(&wdest2, &wa, &wb);     // Add bytes, horizontal saturate
wmmx2_wsubaddhx(&wdest2, &wa2, &wb2);  // Subtract/add with exchange
```

### Historical Devices

- Intel XScale PXA processors (2002-2006)
- HP iPAQ PDAs
- Dell Axim handhelds
- Early smartphones (pre-ARM NEON)

---

## MIPS MSA2

### Overview

MIPS SIMD Architecture 2 (MSA2) extends MSA with additional operations for multimedia.

### Features

- **128-bit vector registers**
- **Fused multiply-add/subtract**
- **Extended shuffle operations**
- **Bit manipulation instructions**

### Usage

```c
msa2_reg_t va, vb, vc, vdest;

// Fused operations
msa2_fmadd(&vdest, &va, &vb, &vc);  // vdest = va*vb + vc
msa2_fmsub(&vdest, &va, &vb, &vc);  // vdest = va*vb - vc

// Horizontal add
msa2_hadd(&vdest, &va, &vb);

// Advanced shuffle
msa2_shf(&vdest, &va, 0xE4);

// Bit insert (left/right)
msa2_binsli(&vdest, &va, 5);  // Insert low 5 bits
msa2_binsri(&vdest, &va, 5);  // Insert high bits except 5
```

### Applications

- Video encoding/decoding (H.264, VP9)
- Image processing
- Crypto acceleration
- DSP applications

---

## Loongson LMMX

### Overview

Loongson processors (Chinese MIPS-compatible) include LMMX extensions derived from PlayStation 2's MMI instructions.

### Characteristics

- **128-bit registers** (two 64-bit halves)
- **Saturating arithmetic** for multimedia
- **Pack/unpack operations**
- **Multiply-accumulate** for signal processing

### Usage

```c
lmmx_reg_t la, lb, ldest;

// Packed add (no saturation)
lmmx_paddb(&ldest, &la, &lb);  // Add 16 bytes
lmmx_paddh(&ldest, &la, &lb);  // Add 8 halfwords
lmmx_paddw(&ldest, &la, &lb);  // Add 4 words
lmmx_paddd(&ldest, &la, &lb);  // Add 2 doublewords

// Saturating add (for image/audio processing)
lmmx_paddsb(&ldest, &la, &lb);   // Signed byte saturation
lmmx_paddsh(&ldest, &la, &lb);   // Signed halfword saturation
lmmx_paddusb(&ldest, &la, &lb);  // Unsigned byte saturation
lmmx_paddush(&ldest, &la, &lb);  // Unsigned halfword saturation

// Pack with saturation (reduce bit width)
lmmx_packsshb(&ldest, &la, &lb);  // Signed halfword -> byte
lmmx_packsswh(&ldest, &la, &lb);  // Signed word -> halfword
lmmx_packushb(&ldest, &la, &lb);  // Unsigned halfword -> byte

// Multiply operations
lmmx_pmulhh(&ldest, &la, &lb);   // Multiply, keep high 16 bits
lmmx_pmullh(&ldest, &la, &lb);   // Multiply, keep low 16 bits
lmmx_pmaddhw(&ldest, &la, &lb);  // Multiply-add adjacent pairs

// Shuffle and shift
lmmx_pshufh(&ldest, &la, 0xB1);  // Shuffle halfwords
lmmx_psllh(&ldest, &la, 3);      // Shift left logical
lmmx_psrlh(&ldest, &la, 3);      // Shift right logical
lmmx_psrah(&ldest, &la, 3);      // Shift right arithmetic
```

### Historical Context

- Loongson 2E/2F processors (2006-2010)
- Used in Lemote Yeeloong laptops
- MIPS-compatible Chinese processors
- Similar to PS2 Emotion Engine MMI

---

## ARM FPA (Floating Point Accelerator)

### Overview

The original ARM Floating Point Accelerator (1991-1996) used 80-bit extended precision similar to x87.

### Characteristics

- **80-bit extended precision** registers (F0-F7)
- **Hardware transcendental functions** (sin, cos, tan, log, exp)
- **Multiple precision modes** (single, double, extended, packed decimal)
- **Unusual instruction mnemonics** (ADF, MUF, DVF instead of ADD, MUL, DIV)

### Format

```
arm_fpa_extended_t:
  - 64-bit mantissa
  - 15-bit exponent + sign
  - type field (normal, denormal, infinity, NaN, zero)
```

### Usage

```c
arm_fpa_extended_t fa, fb, fres;

// Basic arithmetic (using FPA mnemonics)
fpa_adf(&fres, &fa, &fb);  // ADF = Add Float
fpa_suf(&fres, &fa, &fb);  // SUF = Subtract Float
fpa_muf(&fres, &fa, &fb);  // MUF = Multiply Float
fpa_dvf(&fres, &fa, &fb);  // DVF = Divide Float
fpa_rmf(&fres, &fa, &fb);  // RMF = Remainder Float

// Hardware transcendentals (rare in embedded CPUs!)
fpa_sin(&fres, &fa);       // Sine
fpa_cos(&fres, &fa);       // Cosine
fpa_tan(&fres, &fa);       // Tangent
fpa_asn(&fres, &fa);       // Arc sine (asin)
fpa_acs(&fres, &fa);       // Arc cosine (acos)
fpa_atn(&fres, &fa);       // Arc tangent (atan)
fpa_sqt(&fres, &fa);       // Square root
fpa_log(&fres, &fa);       // Log base 10
fpa_lgn(&fres, &fa);       // Log natural (ln)
fpa_exp(&fres, &fa);       // e^x
fpa_pow(&fres, &fa);       // 10^x

// Convert to/from IEEE
double ieee_val;
fpa_extended_to_ieee_double(&ieee_val, &fres);
ieee_double_to_fpa_extended(&fa, 3.14159);
```

### Historical Devices

- ARM610, ARM710 with FPA10/FPA11
- Acorn RiscPC
- Early ARM workstations
- Pre-VFP ARM architecture

### Why Hardware Transcendentals?

FPA included hardware sin/cos/tan/log/exp because:
- 1990s CPUs were slow
- Software transcendentals took thousands of cycles
- Scientific computing benefit
- Differentiation from x86

---

## Universal Floating Point Conversion

### Overview

The `fp_convert()` function provides universal conversion between all supported floating point formats.

### Supported Formats

```c
typedef enum {
    FP_FORMAT_IEEE_SINGLE,
    FP_FORMAT_IEEE_DOUBLE,
    FP_FORMAT_IEEE_QUAD,
    FP_FORMAT_VAX_F,
    FP_FORMAT_VAX_D,
    FP_FORMAT_VAX_G,
    FP_FORMAT_IBM_SHORT,
    FP_FORMAT_IBM_LONG,
    FP_FORMAT_CRAY,
    FP_FORMAT_ARM_FPA,
    FP_FORMAT_DEC32_DPD,
    FP_FORMAT_DEC64_DPD,
    FP_FORMAT_DEC32_BID,
    FP_FORMAT_DEC64_BID
} fp_format_t;
```

### Usage

```c
// Convert VAX F to IEEE single
vax_f_float_t vax_value;
float ieee_value;
fp_convert(&ieee_value, FP_FORMAT_IEEE_SINGLE,
           &vax_value, FP_FORMAT_VAX_F);

// Convert IBM long to Cray
ibm_long_float_t ibm_value;
cray_float_t cray_value;
fp_convert(&cray_value, FP_FORMAT_CRAY,
           &ibm_value, FP_FORMAT_IBM_LONG);

// Convert decimal to ARM FPA
decimal64_t dec_value;
arm_fpa_extended_t fpa_value;
fp_convert(&fpa_value, FP_FORMAT_ARM_FPA,
           &dec_value, FP_FORMAT_DEC64_BID);
```

### Conversion Path

All conversions go through IEEE double as intermediate format:
```
Source Format -> IEEE Double -> Destination Format
```

This ensures consistent behavior and simplifies implementation.

---

## Integration with Emulation Layer

### Automatic Type Detection

The emulation layer can automatically detect and convert between formats:

```c
// Backend emulation automatically handles format conversions
IValue *vax_value = builder->CreateLoad(...);
IValue *ieee_value = builder->ConvertFloatFormat(vax_value,
                                                   FP_FORMAT_VAX_F,
                                                   FP_FORMAT_IEEE_SINGLE);
```

### Transparent Operations

Operations automatically use appropriate arithmetic:

```c
// If values are in exotic formats, operations route through emulation
IValue *result = builder->CreateFAdd(vax_a, vax_b, "sum");
// Automatically: VAX->IEEE, add, IEEE->VAX
```

### Performance Considerations

- **Native format preferred**: Keep values in native format when possible
- **Batch conversions**: Convert once, operate multiple times
- **Format hints**: Specify expected format to avoid detection overhead

---

## Use Cases

### Legacy Data Files

Many scientific and commercial organizations have data files in legacy formats:

```c
// Read VAX floating point from legacy Fortran data
FILE *f = fopen("legacy_data.vax", "rb");
vax_d_float_t vax_values[1000];
fread(vax_values, sizeof(vax_d_float_t), 1000, f);

// Convert to modern IEEE format
double ieee_values[1000];
for (int i = 0; i < 1000; i++) {
    vax_d_to_ieee_double(&ieee_values[i], &vax_values[i]);
}
```

### Cross-Architecture Emulation

Emulate historical systems with correct arithmetic:

```c
// Emulating PDP-10 code
pdp10_word_t pdp_regs[16];

// Execute PDP-10 ADD instruction
pdp10_add(&pdp_regs[dst], &pdp_regs[src1], &pdp_regs[src2]);

// Execute LDB (load byte) instruction
pdp10_ldb(&pdp_regs[dst], &pdp_regs[ptr], pos, size);
```

### Financial Systems

Use decimal floating point for exact calculations:

```c
decimal64_t price, quantity, total;

double_to_dec64(&price, 19.99);
double_to_dec64(&quantity, 100.0);

// Exact multiplication (no binary rounding)
dec64_mul(&total, &price, &quantity);

// Convert back to display
double display_total;
dec64_to_double(&display_total, &total);
printf("Total: $%.2f\n", display_total);  // Exactly $1999.00
```

### Archive Processing

Process historical scientific data:

```c
// 1970s Cray supercomputer output
cray_float_t cray_data[10000];
read_cray_dataset(cray_data, 10000);

// Convert to modern format for analysis
double modern_data[10000];
for (int i = 0; i < 10000; i++) {
    cray_to_ieee_double(&modern_data[i], &cray_data[i]);
}

// Analyze with modern tools
perform_fft(modern_data, 10000);
```

---

## Implementation Notes

### Precision Loss

Some conversions may lose precision:

- **VAX to IEEE**: VAX has explicit leading bit, IEEE has implicit
- **IBM to IEEE**: IBM base-16 can waste 3 bits of mantissa
- **Decimal to Binary**: Some decimal fractions cannot be represented exactly in binary
- **Cray to IEEE double**: Both 64-bit but different mantissa sizes

### Special Values

Different formats handle special values differently:

| Format | Infinity | NaN | Denormals | -0 |
|--------|----------|-----|-----------|-----|
| IEEE | Yes | Yes | Yes | Yes |
| VAX | Reserved | Reserved | No | No |
| IBM | No | No | No | Yes |
| Cray | No | No | No | Yes |
| Decimal | Yes | Yes | Yes | No |
| 1's Comp | No | No | No | Yes (+0 and -0) |

### Byte Ordering

Some formats have unusual byte ordering:
- **VAX**: PDP-11 byte order (swapped words)
- **IBM**: Big-endian (network byte order)
- **Cray**: Word-addressable (no byte order issues)

---

## Testing

### Verification

Test conversions with known values:

```c
// Test VAX conversion
float original = 3.14159f;
vax_f_float_t vax;
float converted;

ieee_float_to_vax_f(&vax, original);
vax_f_to_ieee_float(&converted, &vax);

assert(fabsf(original - converted) < 1e-6);
```

### Edge Cases

Test special cases:
- Zero (positive and negative where applicable)
- Very small numbers (underflow behavior)
- Very large numbers (overflow behavior)
- NaN and infinity (where supported)

### Round-Trip Testing

Ensure conversions are reversible:

```c
// IEEE -> VAX -> IEEE should preserve value (within precision)
double original = 1.234567890123456;
vax_d_float_t vax;
double roundtrip;

ieee_double_to_vax_d(&vax, original);
vax_d_to_ieee_double(&roundtrip, &vax);

assert(fabs(original - roundtrip) < 1e-15);
```

---

## References

### IEEE 754-2008 Decimal
- IEEE 754-2008 Standard
- Intel Decimal Floating Point Library
- IBM DFP support in POWER processors

### VAX Floating Point
- "VAX Architecture Reference Manual" (DEC, 1987)
- "VAX Floating Point: A Solid Foundation for Numerical Computation"

### IBM Hexadecimal FP
- "IBM System/360 Principles of Operation"
- "A Comparison of IBM and IEEE Floating Point Arithmetic"

### Cray Floating Point
- "Cray-1 Computer System Hardware Reference Manual"
- "Floating Point Arithmetic on the Cray-1"

### 1's Complement
- "The CDC 6600" (Thornton, 1970)
- "PDP-1 Handbook" (DEC, 1963)

### PDP-10
- "PDP-10 Reference Handbook" (DEC, 1971)
- TOPS-20 Monitor Source Code

### RVV 0.9
- RISC-V Vector Extension 0.9 Draft Specification
- RISC-V Vector Extension 1.0 (for comparison)

### WMMX
- Intel XScale Technology Developer's Manual
- Wireless MMX Programming Guide

### ARM FPA
- ARM Architecture Reference Manual (ARMv4)
- FPA10/FPA11 Data Sheet

---

## Conclusion

The exotic systems emulation layer enables libcpu to correctly emulate historical architectures and support unusual number representations. This is essential for:

- **Legacy system emulation** (VAX, IBM mainframes, Cray supercomputers)
- **Historical data processing** (scientific archives, commercial databases)
- **Cross-platform compatibility** (reading foreign data formats)
- **Financial applications** (exact decimal arithmetic)
- **Research and education** (understanding computer architecture evolution)

By providing transparent conversions and operations, the emulation layer allows modern JIT backends to execute code that was written for exotic architectures decades ago, preserving computational heritage and enabling access to historical datasets.
