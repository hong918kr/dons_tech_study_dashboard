/* ARM-B02 practice.S — 4 more mysteries for 추가 연습 (mystery7..mystery10)
 *
 * REAL Apple clang 21 -O2 output of hidden C functions (same flags/cleanup as shared_mystery.S;
 * check_mystery_sync.sh proves the instruction lines are identical).
 * Extra cleanup here: the Mach-O-only "@PAGE/@PAGEOFF" relocation syntax and the jump-table
 * section name go through macros so the file also assembles on Linux/ELF; ".loh" (Mach-O linker
 * optimization hint) is dropped.
 */
#ifdef __APPLE__
#  define SYM(n)  _##n
#  define TYPE(n)
#  define SIZE(n)
#  define PAGE(s)    s@PAGE                  /* adrp: 4 KB page of s            */
#  define PAGEOFF(s) s@PAGEOFF               /* add : low 12 bits of s          */
#  define CONST_SECTION .section __TEXT,__const
#else
#  define SYM(n)  n
#  define TYPE(n) .type n, %function
#  define SIZE(n) .size n, . - n
#  define PAGE(s)    s
#  define PAGEOFF(s) :lo12:s
#  define CONST_SECTION .section .rodata
#endif

    .text

// ---------------------------------------------------------------- mystery7
// C prototype:  uint32_t mystery7(uint32_t x);
    .globl  SYM(mystery7)
    TYPE(mystery7)
    .p2align 2
SYM(mystery7):
	cbz	w0, LBB6_3              // w0 == 0 -> return 0
	mov	x8, x0                  // x8 = x0
	mov	w0, #0                  // w0 = 0
	mov	w9, #52429              // w9 = 0x0000cccd
	movk	w9, #52428, lsl #16     // w9 = 0xcccccccd
	mov	w10, #10                // w10 = 10
LBB6_2:
	umull	x11, w8, w9             // x11 = (u64)w8 * 0xcccccccd
	lsr	x11, x11, #35           // x11 >>= 35
	msub	w12, w11, w10, w8       // w12 = w8 - w11 * 10
	add	w0, w0, w12             // w0 += w12
	cmp	w8, #9                  // flags from OLD w8 - 9
	mov	x8, x11                 // x8 = x11  (mov does not touch flags)
	b.hi	LBB6_2                  // loop while OLD w8 > 9 (unsigned)
LBB6_3:
	ret
    SIZE(mystery7)

// ---------------------------------------------------------------- mystery8
// C prototype:  int32_t mystery8(int op, int32_t x);
    .globl  SYM(mystery8)
    TYPE(mystery8)
    .p2align 2
SYM(mystery8):
	cmp	w0, #9                  // flags from w0 - 9
	b.hi	LBB7_11                 // UNSIGNED w0 > 9 -> default (negative w0 too!)
	mov	w8, w0                  // x8 = zero-extended w0 (index)
	adrp	x9, PAGE(lJTI7_0)       // x9 = 4 KB page of the jump table
	add	x9, x9, PAGEOFF(lJTI7_0) // x9 = &table[0]
	adr	x10, LBB7_2             // x10 = address of the first case block (base)
	ldrb	w11, [x9, x8]           // w11 = table[index]   (1 byte per entry)
	add	x10, x10, x11, lsl #2   // x10 = base + entry * 4   (entry counts instructions)
	br	x10                     // indirect jump
LBB7_2:
	add	w0, w1, #7
	ret
LBB7_3:
	mov	w8, #85                 // w8 = 0x55
	eor	w0, w1, w8              // w0 = w1 ^ w8
	ret
LBB7_4:
	mvn	w0, w1                  // w0 = ~w1
	ret
LBB7_5:
	asr	w0, w1, #2              // arithmetic shift right
	ret
LBB7_6:
	neg	w0, w1                  // w0 = 0 - w1
	ret
LBB7_7:
	sub	w0, w1, #100
	ret
LBB7_8:
	add	w0, w1, w1, lsl #1      // w0 = w1 + (w1 << 1)
	ret
LBB7_9:
	add	w8, w1, #7              // w8 = w1 + 7
	cmp	w1, #0                  // flags from w1 - 0
	csel	w8, w8, w1, lt          // w8 = (w1 < 0) ? w8 : w1
	asr	w0, w8, #3              // w0 = w8 >> 3 (arithmetic)
	ret
LBB7_10:
	and	w0, w1, #0xf0
	ret
LBB7_11:
	mov	w0, #0
	ret
LBB7_12:
	lsl	w0, w1, #4
	ret
    SIZE(mystery8)

    CONST_SECTION
lJTI7_0:                                // 1-byte entries: (target - LBB7_2) / 4
	.byte	(LBB7_2-LBB7_2)>>2
	.byte	(LBB7_8-LBB7_2)>>2
	.byte	(LBB7_5-LBB7_2)>>2
	.byte	(LBB7_6-LBB7_2)>>2
	.byte	(LBB7_3-LBB7_2)>>2
	.byte	(LBB7_9-LBB7_2)>>2
	.byte	(LBB7_10-LBB7_2)>>2
	.byte	(LBB7_7-LBB7_2)>>2
	.byte	(LBB7_12-LBB7_2)>>2
	.byte	(LBB7_4-LBB7_2)>>2

    .text

// ---------------------------------------------------------------- mystery9
// C prototype:  uint32_t mystery9(uint32_t reg, uint32_t v);
    .globl  SYM(mystery9)
    TYPE(mystery9)
    .p2align 2
SYM(mystery9):
	bfi	w0, w1, #4, #7          // w0[10:4] = w1[6:0], other bits of w0 kept
	ret
    SIZE(mystery9)

// ---------------------------------------------------------------- mystery10
// C prototype:  long mystery10(const long *a, size_t n, long (*f)(long));
    .globl  SYM(mystery10)
    TYPE(mystery10)
    .p2align 2
SYM(mystery10):
	stp	x22, x21, [sp, #-48]!   // sp -= 48; [sp] = x22, [sp+8] = x21
	stp	x20, x19, [sp, #16]     // [sp+16] = x20, [sp+24] = x19
	stp	x29, x30, [sp, #32]     // [sp+32] = fp, [sp+40] = lr
	add	x29, sp, #32            // fp = sp + 32 -> frame record
	cbz	x1, LBB9_3              // x1 == 0 -> LBB9_3
	mov	x19, x2                 // keep x2 in callee-saved x19
	mov	x20, x1                 // keep x1 in x20
	mov	x21, x0                 // keep x0 in x21
	mov	x22, #0                 // x22 = 0
LBB9_2:
	ldr	x0, [x21], #8           // x0 = *x21; x21 += 8  (post-index)
	blr	x19                     // call the function pointer in x19 (lr = next insn)
	add	x22, x0, x22            // x22 += return value
	subs	x20, x20, #1            // x20 -= 1, set flags
	b.ne	LBB9_2                  // loop while x20 != 0
	b	LBB9_4
LBB9_3:
	mov	x22, #0
LBB9_4:
	mov	x0, x22                 // return x22
	ldp	x29, x30, [sp, #32]     // restore fp, lr
	ldp	x20, x19, [sp, #16]     // restore x20, x19
	ldp	x22, x21, [sp], #48     // restore x22, x21; sp += 48
	ret
    SIZE(mystery10)

#ifdef __APPLE__
    .subsections_via_symbols
#else
    .section .note.GNU-stack, "", %progbits
#endif
