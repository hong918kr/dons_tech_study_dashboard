/* ENV: qemu */
/* ARM-A01 — qemu-system-arm -M mps2-an385 (Cortex-M3) / mps2-an386 (Cortex-M4F, 같은 메모리 맵).
 * ARM-M09 arm_m09.ld 축약판. ZBT SSRAM1 0x00000000 = "FLASH"(코드·벡터), SSRAM2&3 0x20000000 = "RAM".
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);     /* 0x20010000: 초기 MSP (벡터 [0]) = main + 모든 핸들러의 스택 */
ENTRY(Reset_Handler)
SECTIONS
{
    .text : {
        KEEP(*(.vectors))                /* 벡터 테이블은 주소 0 (VTOR 리셋값) */
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH
    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
ASSERT(_ebss + 0x1000 <= _estack, "ARM-A01: .data + .bss + 4KB MSP stack do not fit in RAM")
