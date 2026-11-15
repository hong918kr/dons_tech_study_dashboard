/* ENV: qemu — BM-B05 예제 펌웨어의 링커 스크립트 (STM32F411RE 급 512 KB Flash / 128 KB SRAM 가정)
 * ST CubeMX 가 만드는 STM32F4xx_FLASH.ld 의 구조를 줄여 쓴 것. 자세한 문법은 TOOL-M01.
 */
ENTRY(Reset_Handler)

_Min_Heap_Size  = 0x800;    /* 2 KB  — newlib _sbrk 가 쓸 영역 (여기서는 예약만) */
_Min_Stack_Size = 0x1000;   /* 4 KB  — "최소 이만큼은 비어 있어야 한다" 는 링크 타임 검사용 */

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);   /* 스택은 RAM 맨 위에서 아래로 자란다 */

SECTIONS
{
  .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH

  .text : {
    . = ALIGN(4);
    *(.text) *(.text*)
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  .rodata : { . = ALIGN(4); *(.rodata) *(.rodata*) . = ALIGN(4); } > FLASH

  _sidata = LOADADDR(.data);           /* .data 초기값 이미지가 Flash 에 놓인 주소 (LMA) */
  .data : {
    . = ALIGN(4);
    _sdata = .;                        /* RAM 쪽 주소 (VMA) */
    *(.data) *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT> FLASH                    /* 실행은 RAM, 저장은 Flash → 양쪽을 다 먹는다 */

  .bss : {
    . = ALIGN(4);
    _sbss = .;
    *(.bss) *(.bss*) *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  ._user_heap_stack (NOLOAD) : {      /* 공간 확인용 예약: 넘치면 ld 가 region RAM overflowed */
    . = ALIGN(8);
    PROVIDE(end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } > RAM

}
