/* ENV: qemu */
/* BM-B08 링커 스크립트 — qemu-system-arm -M mps2-an385 (Cortex-M3).
 * QEMU 보드: ZBT SSRAM1 0x00000000 (4 MB) / SSRAM2&3 0x20000000 (4 MB) — 둘 다 RAM 이지만
 * 이 레슨은 진짜 MCU 처럼 앞쪽을 "FLASH"(읽기 전용 취급), 뒤쪽을 "RAM" 으로 쓴다.
 * 크기는 STM32F4 소형 파트 흉내: FLASH 256 KB, RAM 64 KB.
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);     /* 0x20010000: 스택은 RAM 끝에서 아래로 */
_Min_Stack_Size = 0x800;                 /* 2 KB 는 반드시 남아야 한다 (ASSERT 로 검사) */

ENTRY(Reset_Handler)                     /* ELF e_entry — gdb/로더용. 하드웨어는 벡터 [1] 을 본다 */

SECTIONS
{
    .isr_vector : {                      /* VTOR = 0 → 반드시 FLASH 맨 앞 */
        . = ALIGN(4);
        KEEP(*(.isr_vector))             /* 아무도 참조 안 해도 --gc-sections 에 지워지지 않게 */
        . = ALIGN(4);
    } > FLASH

    .text : {
        *(.text*)
        *(.rodata*)                      /* const 데이터: FLASH 에만 산다 (복사 없음) */
        . = ALIGN(4);
    } > FLASH

    .init_array : {                      /* 생성자 포인터 표: 우선순위 있는 것 먼저 */
        . = ALIGN(4);
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    } > FLASH

    _sidata = LOADADDR(.data);           /* .data 초기값이 FLASH 에 놓인 주소 (LMA) */
    .data : {
        . = ALIGN(4);
        _sdata = .;                      /* RAM 에서의 실행 주소 (VMA) */
        *(.data*)
        . = ALIGN(4);                    /* 워드 복사 루프가 꼬리 바이트를 신경 안 써도 되게 */
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}

ASSERT(_ebss + _Min_Stack_Size <= _estack, "BM-B08: .data + .bss + stack do not fit in RAM")
ASSERT(_sidata + (_edata - _sdata) <= ORIGIN(FLASH) + LENGTH(FLASH), "BM-B08: image does not fit in FLASH")
