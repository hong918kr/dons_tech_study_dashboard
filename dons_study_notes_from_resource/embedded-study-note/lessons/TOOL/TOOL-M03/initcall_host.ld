/* TOOL-M03: 호스트(Linux ELF) 실행 파일의 "기본 링커 스크립트" 에 섹션 하나를 끼워 넣는 조각.
 *   gcc ... -Wl,-T,initcall_host.ld   (INSERT 가 있으면 -T 는 기본 스크립트를 바꾸지 않고 덧붙인다)
 * Linux 커널 include/asm-generic/vmlinux.lds.h 의 INIT_CALLS 와 같은 아이디어 (커널은 레벨별 매크로로 나열).
 */
SECTIONS
{
    .initcalls : {
        __initcall_start = .;
        KEEP(*(SORT(.initcall.*)))      /* SORT = SORT_BY_NAME: .initcall.0 < .initcall.1 < ... */
        __initcall_end = .;
    }
}
INSERT AFTER .data;
