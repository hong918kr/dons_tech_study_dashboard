/* ENV: qemu */
/* ARM-A03 arm_a03.ld — qemu-system-aarch64 -M virt 베어메탈 (RAM 0x40000000, 128 MB). (ARM-M13 arm_m13.ld 축약판)
 * 이미지 전체(.text ~ 스택)가 RAM 의 첫 2 MB 블록(0x40000000–0x401FFFFF) 안에 들어간다 → 아이덴티티 매핑의
 * 블록 0 하나만 맞으면 MMU 를 켠 순간에도 PC·스택·벡터가 그대로 산다 (5. 풀이 Part A).
 * 변환 테이블은 .bss 의 4 KB 정렬 배열 (C 코드 쪽 __attribute__((aligned(4096)))).
 */
ENTRY(_start)
SECTIONS
{
    . = 0x40080000;
    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    .bss (NOLOAD) : {
        . = ALIGN(16);
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }
    . = ALIGN(16);
    . = . + 0x10000;                 /* 64 KB 부트 스택 (SP_EL1) */
    __stack_top = .;
    __image_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
