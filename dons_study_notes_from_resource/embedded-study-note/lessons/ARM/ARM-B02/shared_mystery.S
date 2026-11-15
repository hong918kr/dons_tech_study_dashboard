/* ARM-B02 shared_mystery.S — "what does this asm do?" (6 functions)
 *
 * REAL Apple clang 21 -O2 output (arm64-apple-darwin) of 6 hidden C functions:
 *     clang -S -O2 -fno-vectorize -fno-slp-vectorize -fno-asynchronous-unwind-tables
 * Mechanical cleanup only (check_mystery_sync.sh proves the instructions are identical):
 *   - dropped .cfi_* / .build_version, clang ";" comments
 *   - symbol names through SYM() so the same file assembles on Linux/ELF
 *   - per-instruction comments describe REGISTER effects only (the meaning is your job)
 * Instruction lines are byte-for-byte what clang emitted.
 */
#ifdef __APPLE__
#  define SYM(n)  _##n              /* Mach-O: C name foo -> asm symbol _foo */
#  define TYPE(n)                   /* Mach-O has no .type / .size          */
#  define SIZE(n)
#else
#  define SYM(n)  n                 /* ELF: no underscore prefix            */
#  define TYPE(n) .type n, %function
#  define SIZE(n) .size n, . - n
#endif

    .text

// ---------------------------------------------------------------- mystery1
// C prototype:  uint32_t mystery1(int32_t a, int32_t b);
    .globl  SYM(mystery1)
    TYPE(mystery1)
    .p2align 2
SYM(mystery1):
	sub	w8, w1, w0              // w8 = w1 - w0          (flags untouched)
	subs	w9, w0, w1              // w9 = w0 - w1, set NZCV from w0 - w1
	csel	w0, w9, w8, gt          // w0 = GT (signed >) ? w9 : w8
	ret                             // return w0
    SIZE(mystery1)

// ---------------------------------------------------------------- mystery2
// C prototype:  long mystery2(long x, long lo, long hi);
    .globl  SYM(mystery2)
    TYPE(mystery2)
    .p2align 2
SYM(mystery2):
	cmp	x0, x2                  // flags from x0 - x2
	csel	x8, x0, x2, lt          // x8 = LT (signed <) ? x0 : x2
	cmp	x0, x1                  // flags from x0 - x1
	csel	x0, x1, x8, lt          // x0 = LT ? x1 : x8
	ret
    SIZE(mystery2)

// ---------------------------------------------------------------- mystery3
// C prototype:  int mystery3(uint32_t x);
    .globl  SYM(mystery3)
    TYPE(mystery3)
    .p2align 2
SYM(mystery3):
	cbz	w0, LBB2_3              // if w0 == 0 goto LBB2_3 (returns w0 = 0)
	mov	x8, x0                  // x8 = x0
	mov	w0, #0                  // w0 = 0
LBB2_2:
	and	w9, w8, #0x1            // w9 = w8 & 1
	add	w0, w0, w9              // w0 += w9
	cmp	w8, #3                  // flags from w8 - 3   (OLD w8)
	lsr	w8, w8, #2              // w8 = w8 >> 2 (logical)   (lsr does not touch flags)
	b.hi	LBB2_2                  // loop while OLD w8 > 3 (unsigned HI)
LBB2_3:
	ret
    SIZE(mystery3)

// ---------------------------------------------------------------- mystery4
// C prototype:  size_t mystery4(const char *s);
    .globl  SYM(mystery4)
    TYPE(mystery4)
    .p2align 2
SYM(mystery4):
	mov	x8, #0                  // x8 = 0
LBB3_1:
	ldrsb	w9, [x0, x8]            // w9 = sign-extended byte at x0 + x8
	add	x8, x8, #1              // x8 += 1
	cmp	w9, #32                 // flags from w9 - 32
	b.gt	LBB3_1                  // loop while w9 > 32 (signed GT)
	sub	x0, x8, #1              // x0 = x8 - 1
	ret
    SIZE(mystery4)

// ---------------------------------------------------------------- mystery5
// C prototype:  int32_t mystery5(const struct sensor_tab *t, uint16_t id);
//   struct sensor_tab { uint32_t n; uint16_t id[16]; int16_t temp[16]; };
    .globl  SYM(mystery5)
    TYPE(mystery5)
    .p2align 2
SYM(mystery5):
	ldr	w8, [x0]                // w8 = 32-bit load from [x0 + 0]
	cbz	w8, LBB4_4              // if w8 == 0 goto LBB4_4
	add	x9, x0, #36             // x9 = x0 + 36
LBB4_2:
	ldurh	w10, [x9, #-32]         // w10 = zero-extended halfword at x9 - 32
	cmp	w10, w1                 // flags from w10 - w1
	b.eq	LBB4_5                  // if equal goto LBB4_5
	add	x9, x9, #2              // x9 += 2
	subs	x8, x8, #1              // x8 -= 1, set flags
	b.ne	LBB4_2                  // loop while x8 != 0
LBB4_4:
	mov	w0, #-2147483648        // w0 = 0x80000000
	ret
LBB4_5:
	ldrsh	w8, [x9]                // w8 = sign-extended halfword at x9
	add	w8, w8, w8, lsl #2      // w8 = w8 + (w8 << 2)
	add	w8, w8, w8, lsr #31     // w8 = w8 + (w8 >> 31 logical)  -> +1 if w8 < 0
	asr	w0, w8, #1              // w0 = w8 >> 1 (arithmetic)
	ret
    SIZE(mystery5)

// ---------------------------------------------------------------- mystery6
// C prototype:  uint32_t mystery6(uint32_t head, uint32_t tail);
    .globl  SYM(mystery6)
    TYPE(mystery6)
    .p2align 2
SYM(mystery6):
	sub	w8, w0, w1              // w8 = w0 - w1   (mod 2^32)
	add	w8, w8, #24             // w8 += 24       (mod 2^32)
	mov	w9, #43691              // w9 = 0x0000aaab
	movk	w9, #43690, lsl #16     // w9 = 0xaaaaaaab   (movk keeps the other halfwords)
	umull	x9, w8, w9              // x9 = (u64)w8 * (u64)w9
	lsr	x9, x9, #36             // x9 = x9 >> 36
	mov	w10, #24                // w10 = 24
	msub	w0, w9, w10, w8         // w0 = w8 - w9 * w10
	ret
    SIZE(mystery6)

#ifdef __APPLE__
    .subsections_via_symbols
#else
    .section .note.GNU-stack, "", %progbits   // non-executable stack marker
#endif
