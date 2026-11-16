/* ENV: qemu */
/* ARM-A07 — qemu-system-arm -M mps2-an386 (Cortex-M4) / mps2-an385 (Cortex-M3, m3probe). ARM-M09 arm_m09.ld 축약판.
 * ZBT SSRAM1 0x00000000 (코드·벡터) / SSRAM2&3 0x20000000 (데이터·스택). 앞쪽을 "FLASH", 뒤쪽을 "RAM" 으로 쓴다.
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
ENTRY(Reset_Handler)
SECTIONS
{
    .text : {
        KEEP(*(.vectors))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH
    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
ASSERT(_ebss + 0x1000 <= _estack, "ARM-A07: .data + .bss + 4KB stack do not fit in RAM")
