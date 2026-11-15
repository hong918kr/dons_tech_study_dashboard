/* ENV: qemu */
/* RTOS-A01 링커 스크립트 — qemu-system-arm -M mps2-an385 (Cortex-M3).
 * BM-B08 의 bm_b08.ld 와 같은 모양 (앞쪽 4 MB SSRAM1 을 "FLASH", 0x20000000 을 "RAM" 으로 쓴다).
 * MSP 스택(_estack 아래)은 이제 main() 과 ISR 만 쓴다. 태스크 스택은 .bss 안의 정적 배열이고 PSP 로 쓴다.
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);     /* 0x20010000: MSP 초기값 = 벡터 [0] */
_Min_Stack_Size = 0x800;                 /* main + ISR 중첩용 MSP 2 KB 는 남아야 한다 */

ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector : {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text : {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    _sidata = LOADADDR(.data);
    .data : {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) : {
        . = ALIGN(8);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        _ebss = .;
    } > RAM

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.init_array*) }
}

ASSERT(_ebss + _Min_Stack_Size <= _estack, "RTOS-A01: .data + .bss (task stacks) + MSP stack do not fit in RAM")
