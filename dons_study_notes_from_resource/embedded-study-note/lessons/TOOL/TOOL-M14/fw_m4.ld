/* TOOL-M14: minimal Cortex-M4 layout (linker scripts in depth = TOOL-M01) */
ENTRY(Reset_Handler)
MEMORY { FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 512K
         RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 128K }
SECTIONS {
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .note.gnu.build-id : { KEEP(*(.note.gnu.build-id)) } > FLASH
    .text   : { *(.text*) *(.rodata*) } > FLASH
    .data   : { *(.data*) } > RAM AT > FLASH
    .bss    : { *(.bss*) *(COMMON) } > RAM
    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
