/* ENV: cross — arm-none-eabi-gcc -T 로만 쓴다 (check.sh). */
/* TOOL-M01 practice.ld — 같은 부품에 (1) 16 KB 부트로더 자리 (2) .noinit (3) CCM RAM 64 KB @0x10000000.
 * 부트로더(BM-B06)가 0x08000000..0x08003FFF 를 쓰고, 이 앱은 0x08004000 부터 산다 → 앱의 VTOR = 0x08004000. */

ENTRY(Reset_Handler)                     /* ELF e_entry + --gc-sections 의 뿌리(root) */

MEMORY
{
    FLASH  (rx)  : ORIGIN = 0x08000000 + 16K, LENGTH = 512K - 16K   /* 앞 16 KB = 부트로더 */
    RAM    (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    CCMRAM (rw)  : ORIGIN = 0x10000000, LENGTH = 64K           /* 코어 전용, DMA 불가 */
}

_estack = ORIGIN(RAM) + LENGTH(RAM);     /* 0x20020000: 스택은 RAM 끝에서 아래로 */

SECTIONS
{
    .isr_vector :                        /* 벡터 표는 FLASH 맨 앞 (VTOR 리셋 값 = 부트 주소) */
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))             /* 아무도 참조 안 함 → KEEP 없으면 gc 가 지운다 */
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
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> FLASH
    _sidata = LOADADDR(.data);           /* 스타트업이 복사할 원본 = LMA */

    .bss :                               /* 입력이 전부 NOBITS → 출력도 NOBITS (파일에 0 바이트) */
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    .noinit (NOLOAD) :                   /* 리셋을 넘어 값 유지: _sbss.._ebss 밖이라 0 으로 안 지워진다 */
    {
        . = ALIGN(4);
        _snoinit = .;
        *(.noinit .noinit.*)
        . = ALIGN(4);
        _enoinit = .;
    } > RAM

    .ccmram (NOLOAD) :                   /* NOLOAD 없으면 4 KB 0 이 PROGBITS → .bin 이 0x10000000 까지 늘어난다 */
    {
        . = ALIGN(4);
        *(.ccmram .ccmram.*)
        . = ALIGN(4);
    } > CCMRAM
}
