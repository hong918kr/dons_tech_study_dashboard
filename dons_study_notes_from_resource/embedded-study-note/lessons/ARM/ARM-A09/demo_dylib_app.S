/* ARM-A09 demo_dylib_app.S (Mach-O 전용) — 실행 파일 쪽 asm. dylib 심볼은 GOT, 내 심볼은 @PAGE.
 * long scaled(long raw) = sensor_read(raw) + sensor_gain + local_bias
 */
    .text
    .globl  _scaled
    .p2align 2
_scaled:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    bl      _sensor_read                    // ARM64_RELOC_BRANCH26 → __stubs → __got (bind)
    adrp    x8, _sensor_gain@GOTPAGE        // ARM64_RELOC_GOT_LOAD_PAGE21
    ldr     x8, [x8, _sensor_gain@GOTPAGEOFF]   // ARM64_RELOC_GOT_LOAD_PAGEOFF12 : x8 = &sensor_gain
    ldr     x8, [x8]
    add     x0, x0, x8
    adrp    x9, _local_bias@PAGE            // ARM64_RELOC_PAGE21
    ldr     x9, [x9, _local_bias@PAGEOFF]   // ARM64_RELOC_PAGEOFF12 (8 B LDR → 오프셋/8)
    add     x0, x0, x9
    ldp     x29, x30, [sp], #16
    ret

    .data
    .p2align 3
    .globl  _local_bias, _ptr_to_bias, _ptr_to_gain
_local_bias:  .quad 100
_ptr_to_bias: .quad _local_bias             // ARM64_RELOC_UNSIGNED (내 심볼)  → rebase 픽스업
_ptr_to_gain: .quad _sensor_gain            // ARM64_RELOC_UNSIGNED (dylib)    → bind 픽스업
