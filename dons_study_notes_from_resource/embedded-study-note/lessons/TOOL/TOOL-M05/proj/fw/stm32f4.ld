/* stm32f4.ld — given linker script (syntax = TOOL-M01, boot = BM-B08).
 * Imaginary STM32F4-class part: FLASH 512 KB @0x08000000, RAM 128 KB @0x20000000. */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
    .text       : { *(.text*) . = ALIGN(4); } > FLASH
    .rodata     : { . = ALIGN(4); *(.rodata*) . = ALIGN(4); } > FLASH

    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
    .bss  : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
}
