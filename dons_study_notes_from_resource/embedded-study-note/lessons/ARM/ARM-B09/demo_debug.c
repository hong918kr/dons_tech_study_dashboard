/* ARM-B09 demo_debug.c — lldb 세션용 작은 프로그램 (테스트 아님)
 *
 *   cc -g -O0 demo_debug.c starter.S -o dbg     # 버그 있는 판
 *   ./dbg          → "abc" 와 "" 의 길이를 출력 (""가 틀린다)
 *   ./dbg crash    → "" 를 가드 페이지 바로 앞에 두고 호출 → EXC_BAD_ACCESS (SIGBUS)
 *
 * lldb -b -s demo_debug.lldb dbg       (정상 실행 경로: si/ni, register/memory read, watchpoint)
 * lldb -b -s demo_crash.lldb dbg       (크래시 경로: 멈춘 PC·주소로 원인 역추적)
 */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

extern size_t my_strlen(const char *s);

/* 두 번 호출하는 C 함수: 이 안의 `bl my_strlen` 에서 si 와 ni 의 차이를 본다 */
__attribute__((noinline)) size_t two_lengths(const char *a, const char *b) {
    size_t la = my_strlen(a);
    size_t lb = my_strlen(b);
    return la * 100 + lb;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "crash") == 0) {
        size_t pg = (size_t)sysconf(_SC_PAGESIZE);
        char *map = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (map == MAP_FAILED) return 2;
        mprotect(map + pg, pg, PROT_NONE);       /* 두 번째 페이지 = 가드 */
        char *s = map + pg - 1;                  /* "" : NUL 이 첫 페이지 마지막 바이트 */
        *s = '\0';
        printf("s=%p guard=%p\n", (void *)s, (void *)(map + pg));
        fflush(stdout);
        printf("len=%zu\n", my_strlen(s));       /* 버그 판: 여기서 죽는다 */
        return 0;
    }
    static char text[16] = "abc\0#######";      /* NUL 뒤에 쓰레기 '#' */
    static char empty[16] = "\0Zebra";           /* "" 이지만 뒤에 "Zebra" */
    printf("two_lengths = %zu (기대 300)\n", two_lengths(text, empty));
    return 0;
}
