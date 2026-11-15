/* ENV: qemu */
/* BM-A09 링커 스크립트 — qemu-system-arm -M mps2-an385 (Cortex-M3). BM-M13 m13.ld 의 확장판.
 * FLASH 256 KB @0x00000000 (QEMU 에선 SSRAM1), RAM 60 KB @0x20000000, NOINIT 4 KB @0x2000F000 (고정 주소).
 * RAM 안 순서: .data → .bss → .fault_stack (1 KB, 크래시 덤프 전용 스택) → .stack (32 B MPU 가드 + 4 KB 주 스택).
 * 주 스택이 넘치면 아래쪽의 가드(접근 금지 MPU 영역)를 먼저 친다 → .bss / fault 스택은 안전.
 */
MEMORY
{
    FLASH  (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM    (rwx) : ORIGIN = 0x20000000, LENGTH = 60K
    NOINIT (rw)  : ORIGIN = 0x2000F000, LENGTH = 4K
}

_Main_Stack_Size  = 0x1000;
_Fault_Stack_Size = 0x400;

ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH

    .text : { _stext = .; *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > FLASH

    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH

    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM

    /* fault 핸들러 전용 스택: 주 스택이 망가져도 (넘침, 잘못된 SP) 여기서 C 를 돌린다 */
    .fault_stack (NOLOAD) : { . = ALIGN(8); _sfault_stack = .; . += _Fault_Stack_Size; _fault_stack_top = .; } > RAM

    /* 주 스택: 맨 아래 32 B = MPU 가드 (32 B 정렬 필요: v7-M MPU 영역 base 는 크기에 정렬) */
    .stack (NOLOAD) : {
        . = ALIGN(32);
        _sstack_guard = .;  . += 32;  _estack_guard = .;
        . += _Main_Stack_Size;
        . = ALIGN(8);
        _estack = .;
    } > RAM

    /* 리셋을 건너 살아남는 것: reset_init 이 복사도 0 도 안 하고, ELF 에 내용도 없다 (FileSiz 0) */
    .noinit (NOLOAD) : { . = ALIGN(4); _snoinit = .; KEEP(*(.noinit*)) . = ALIGN(4); _enoinit = .; } > NOINIT

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}

ASSERT(_estack <= ORIGIN(RAM) + LENGTH(RAM), "BM-A09: .data + .bss + stacks do not fit in RAM")
ASSERT(_snoinit == ORIGIN(NOINIT), "BM-A09: .noinit must start at the fixed NOINIT address")
ASSERT((_sstack_guard & 31) == 0, "BM-A09: MPU guard must be 32-byte aligned")
