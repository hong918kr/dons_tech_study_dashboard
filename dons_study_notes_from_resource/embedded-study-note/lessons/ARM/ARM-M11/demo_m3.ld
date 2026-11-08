/* ENV: qemu */
/* ARM-M11 — mps2-an385 (QEMU hw/arm/mps2.c): ZBT SSRAM1 0x00000000 4MB (코드·벡터), SSRAM2&3 0x20000000 4MB (데이터·스택) */
MEMORY
{
    CODE (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
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
    } > CODE
    _sidata = LOADADDR(.data);
    .data : { _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > CODE
    .bss (NOLOAD) : { _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.comment) }
}
