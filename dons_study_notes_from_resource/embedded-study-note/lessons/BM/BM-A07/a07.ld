/* ENV: qemu */
/* BM-A07 링커 스크립트 — qemu-system-arm -M mps2-an385 (Cortex-M3). BM-B08 의 bm_b08.ld 축약판 + 스택 구역.
 * RAM 배치 (낮은 주소 → 높은 주소):
 *   .data | .bss ... [g_neighbor 256 B = .bss 의 마지막 객체] | .stack_guard 32 B (카나리) | .stack 2 KB | (빈 RAM)
 * 스택은 _estack 에서 아래로 자란다 → 넘치면 가드(카나리)를 먼저 밟고, 그 다음 .bss 의 이웃 변수를 밟는다.
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

STACK_SIZE = 2048;          /* 메인 스택(MSP) 예약량: 체크리스트의 "reserved" */
GUARD_SIZE = 32;            /* 카나리 8 워드. 32 B 정렬 → 나중에 MPU 가드(BM-A08)로 바꾸기 쉬움 */

ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH

    .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH

    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(32);
        _sneighbor = .;
        KEEP(*(.neighbor))                /* 일부러 .bss 의 맨 끝 = 스택 가드 바로 아래 */
        _eneighbor = .;
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    .stack_guard (NOLOAD) : ALIGN(32) { _sguard = .; . += GUARD_SIZE; _eguard = .; } > RAM
    .stack (NOLOAD) : ALIGN(8) { _sstack = .; . += STACK_SIZE; _estack = .; } > RAM

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}

ASSERT(_eneighbor == _sguard, "BM-A07: g_neighbor must sit right below the stack guard")
ASSERT(_eguard == _sstack, "BM-A07: guard must sit right below the stack limit")
ASSERT(_estack <= ORIGIN(RAM) + LENGTH(RAM), "BM-A07: stack does not fit in RAM")
