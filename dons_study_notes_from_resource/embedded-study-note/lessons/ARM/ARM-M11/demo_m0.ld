/* ENV: qemu */
/* ARM-M11 — microbit (QEMU nRF51822, Cortex-M0): flash 0x00000000 256KB, RAM 0x20000000 16KB */
MEMORY
{
    CODE (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 16K
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
