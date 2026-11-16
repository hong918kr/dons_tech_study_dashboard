/* ENV: board */
/* BM-A10 보드 펌웨어 링커 스크립트 — STM32F407xG: FLASH 1 MB @0x08000000, SRAM1+2 128 KB @0x20000000
 * (RM0090 / 데이터시트 기준 대표값 — 부품 확인 권장). CCM 64 KB 는 쓰지 않는다. */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Stack_Size = 0x800;
ENTRY(Reset_Handler)
SECTIONS
{
    .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
    .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
ASSERT(_ebss + _Min_Stack_Size <= _estack, "BM-A10: RAM overflow")
