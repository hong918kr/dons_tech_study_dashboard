/* ENV: qemu */
/* BM-M02 링커 스크립트 (BM-B08 bm_b08.ld 축약판) — qemu-system-arm -M mps2-an385 (Cortex-M3).
 * QEMU hw/arm/mps2.c (v10.2.1): ZBT SSRAM1 0x00000000 (4 MB) 를 FLASH 처럼, SSRAM2&3 0x20000000 를 RAM 으로 쓴다.
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Stack_Size = 0x800;
ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
    .text       : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.init_array*) }
}

ASSERT(_ebss + _Min_Stack_Size <= _estack, "BM-M02: .data + .bss + stack do not fit in RAM")
