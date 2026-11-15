/* CPP-A10 fw.ld — Cortex-M4 최소 링커 스크립트 (STM32F4 류 주소 대표값: FLASH 0x08000000, RAM 0x20000000) */
ENTRY(Reset_Handler)
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text       : { *(.text .text.*) } > FLASH
  .rodata     : { . = ALIGN(4); *(.rodata .rodata.*) . = ALIGN(4); } > FLASH
  .ARM.exidx  : { __exidx_start = .; *(.ARM.exidx .ARM.exidx.*) __exidx_end = .; } > FLASH   /* 언와인더가 끌려오면 필요 */
  .data       : { _sdata = .; *(.data .data.*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
  _sidata = LOADADDR(.data);
  .bss (NOLOAD) : { _sbss = .; *(.bss .bss.* COMMON) . = ALIGN(4); _ebss = .; } > RAM
  PROVIDE(end = _ebss);   /* newlib _sbrk 의 힙 시작 — 이 이미지는 힙을 쓰지 않는다 (budget.sh 가 확인) */
}
