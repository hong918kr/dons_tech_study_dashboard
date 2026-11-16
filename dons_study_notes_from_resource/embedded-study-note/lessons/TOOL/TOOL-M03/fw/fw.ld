/* TOOL-M03 펌웨어 데모 링커 스크립트 — qemu-system-arm -M mps2-an385 (Cortex-M3).
 * 앞쪽 4 MB SSRAM 을 "FLASH" 로 취급 (BM-B08 과 같은 배치). 문법 자세히는 TOOL-M01, 심화는 TOOL-A07.
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text       : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH

    /* 명령 테이블: 아무도 이름으로 참조하지 않으니 KEEP 이 없으면 --gc-sections 가 지운다 */
    .shellcmds : {
        . = ALIGN(4);
        __shellcmds_start = .;
        KEEP(*(.shellcmds))
        __shellcmds_end = .;
    } > FLASH

    /* initcall: 레벨 번호가 섹션 이름에 들어 있다 → SORT 가 이름순(= 레벨순)으로 놓는다 */
    .initcalls : {
        . = ALIGN(4);
        __initcall_start = .;
        KEEP(*(SORT(.initcall.*)))
        __initcall_end = .;
    } > FLASH

    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
ASSERT((__shellcmds_end - __shellcmds_start) % 12 == 0, "TOOL-M03: .shellcmds stride is not sizeof(struct shell_cmd)")
