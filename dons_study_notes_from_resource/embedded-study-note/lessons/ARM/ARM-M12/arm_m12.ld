/* ENV: qemu */
/* ARM-M12 — qemu-system-arm -M mps2-an385 (Cortex-M3). ARM-B14 의 arm_b14.ld 축약판.
 * QEMU 보드: SSRAM1 0x00000000 (코드·벡터, "FLASH" 처럼 사용) / SSRAM2&3 0x20000000 (데이터·스택).
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);     /* 0x20010000: 초기 MSP (벡터 [0]) */
ENTRY(Reset_Handler)
SECTIONS
{
    .text : {
        KEEP(*(.vectors))                /* 벡터 테이블은 주소 0 (VTOR 리셋값) */
        _stext = .;
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH
    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
ASSERT(_ebss + 0x800 <= _estack, "ARM-M12: .data + .bss + 2KB MSP stack do not fit in RAM")
