/* ARM-A09 demo_elf.S — ELF(Linux aarch64) 전용 데모: 리로케이션 7종이 한 함수에 다 나온다.
 * long use_all(long x) = ext_tail(ext_func(x + g_val + g_tab[1] + g_half + ext_val))
 *                      = x + 7 + 20 + 0xBEEF + 42 + 1 + 2 = x + 48951
 * 빌드는 demo_elf.sh (VM 안에서 gcc/ld.bfd/ld.lld) — 맥에선 조립하지 않는다.
 */
    .text
    .globl  use_all
    .type   use_all, %function
    .p2align 2
use_all:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    adrp    x9, g_val                       // R_AARCH64_ADR_PREL_PG_HI21
    add     x9, x9, :lo12:g_val             // R_AARCH64_ADD_ABS_LO12_NC
    ldr     x10, [x9]
    add     x0, x0, x10
    adrp    x9, g_tab                       // ADR_PREL_PG_HI21
    ldr     x10, [x9, :lo12:g_tab + 8]      // R_AARCH64_LDST64_ABS_LO12_NC, addend 8
    add     x0, x0, x10
    adrp    x9, g_half                      // ADR_PREL_PG_HI21
    ldrh    w10, [x9, :lo12:g_half]         // R_AARCH64_LDST16_ABS_LO12_NC
    add     x0, x0, x10
    adrp    x9, :got:ext_val                // R_AARCH64_ADR_GOT_PAGE
    ldr     x9, [x9, :got_lo12:ext_val]     // R_AARCH64_LD64_GOT_LO12_NC
    ldr     x10, [x9]
    add     x0, x0, x10
    bl      ext_func                        // R_AARCH64_CALL26
    ldp     x29, x30, [sp], #16
    b       ext_tail                        // R_AARCH64_JUMP26 (꼬리 호출)
    .size   use_all, . - use_all

    .data
    .p2align 3
    .globl  g_val, g_tab, g_half, g_ptr
g_val:  .quad   7
g_tab:  .quad   10, 20, 30
g_half: .hword  0xBEEF
    .p2align 3
g_ptr:  .quad   g_val                       // R_AARCH64_ABS64 → PIE 에서는 R_AARCH64_RELATIVE
    .section .note.GNU-stack, "", %progbits
