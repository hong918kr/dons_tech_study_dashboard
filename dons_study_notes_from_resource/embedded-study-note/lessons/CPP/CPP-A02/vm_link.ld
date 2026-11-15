/* CPP-A02 vm_link.ld — 데모 링크용 최소 링커 스크립트 (STM32F4 류 주소, 대표값) */
MEMORY {
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}
ENTRY(Reset_Handler)
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH      /* KEEP: 아무도 참조 안 해도 gc 되지 않게 */
  .text       : { *(.text*) *(.rodata*) } > FLASH
  .ARM.exidx  : { *(.ARM.exidx*) } > FLASH
  .data       : { *(.data*) } > RAM AT > FLASH
  .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM
  _estack = ORIGIN(RAM) + LENGTH(RAM);
}
