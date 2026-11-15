/* ENV: cross — demo_size.sh 가 thumbv7em(Mac clang) / arm-none-eabi-gcc(VM) 로 빌드한다. 호스트에서 실행 금지. */
/* TOOL-B06 링커 스크립트 — 이 레슨에서는 "주어진 것" 으로 쓴다 (문법은 TOOL-M01, 부팅은 BM-B08).
 * 가상의 Cortex-M4 부품: FLASH 64 KB @0x08000000, RAM 16 KB @0x20000000 (STM32 배치 흉내).
 * ._user_heap_stack 은 STM32CubeIDE 프로젝트처럼 힙/스택 최소 크기를 "자리만" 잡는 섹션.
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 16K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size  = 0x200;
_Min_Stack_Size = 0x400;
ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
    .text       : { *(.text*) *(.glue_7) *(.glue_7t) *(.eh_frame) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); } > FLASH
    .rodata     : { . = ALIGN(4); *(.rodata*) . = ALIGN(4); } > FLASH
    .ARM.extab  : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM.exidx  : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > FLASH
    .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) PROVIDE_HIDDEN(__init_array_end = .); } > FLASH
    .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array)) PROVIDE_HIDDEN(__fini_array_end = .); } > FLASH

    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH

    .bss : { . = ALIGN(4); _sbss = .; __bss_start__ = _sbss; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; __bss_end__ = _ebss; } > RAM

    ._user_heap_stack : {
        . = ALIGN(8);
        PROVIDE(end = .);            /* newlib _sbrk (nosys) 가 힙 시작으로 쓰는 심볼 */
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } > RAM
}
