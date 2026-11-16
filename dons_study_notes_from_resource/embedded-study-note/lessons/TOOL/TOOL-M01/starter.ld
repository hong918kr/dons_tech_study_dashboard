/* ENV: cross — arm-none-eabi-gcc -T 로만 쓴다 (check.sh). */
/* TOOL-M01 링커 스크립트 (starter: TODO 4곳) — 가상의 STM32F4 급 부품: FLASH 512 KB @0x08000000, RAM 128 KB @0x20000000. */

ENTRY(Reset_Handler)                     /* ELF e_entry + --gc-sections 의 뿌리(root) */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);     /* 0x20020000: 스택은 RAM 끝에서 아래로 */

SECTIONS
{
    .isr_vector :                        /* 벡터 표는 FLASH 맨 앞 (VTOR 리셋 값 = 부트 주소) */
    {
        . = ALIGN(4);
        *(.isr_vector)                   /* TODO 1: --gc-sections 뒤에도 살아남게 */
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text .text.*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > FLASH

    .data :                              /* VMA = RAM (실행 주소), LMA = FLASH (초기값 사본) */
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data .data.*)
                                         /* TODO 3: 워드 복사 루프가 넘치지 않게 */
        _edata = .;
    } > RAM                              /* TODO 2: 초기값은 어디에 저장되나? */
    _sidata = ADDR(.data);               /* TODO 4: 스타트업이 복사할 원본 */

    .bss :                               /* 입력이 전부 NOBITS → 출력도 NOBITS (파일에 0 바이트) */
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
                                         /* TODO 3: 워드 0-채우기 루프가 넘치지 않게 */
        _ebss = .;
    } > RAM
}
