/* ARM-A10 demo_fork_keys.c — PAC 키는 fork 로 물려받고 exec 로 새로 뽑히나? (같은 포인터·modifier 를 pacia)
 * VM:  gcc -O2 demo_fork_keys.c -o fk && ./fk          Mac: cc -O2 -arch arm64e demo_fork_keys.c -o fk && ./fk */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#ifdef __APPLE__
#define T __attribute__((target("v8.3a")))
#else
#define T __attribute__((target("arch=armv8.3-a")))
#endif
T static uint64_t sign_ia(uint64_t p, uint64_t m) { __asm__ volatile("pacia %0, %1" : "+r"(p) : "r"(m)); return p; }
T static uint64_t sign_ib(uint64_t p, uint64_t m) { __asm__ volatile("pacib %0, %1" : "+r"(p) : "r"(m)); return p; }
int main(int argc, char **argv) {
    const uint64_t p = 0x0000000100004000ull, m = 0x16fdff000ull;
    printf("%-7s pid %6d: pacia=%#018llx pacib=%#018llx\n", argc > 1 ? argv[1] : "parent", (int)getpid(),
           (unsigned long long)sign_ia(p, m), (unsigned long long)sign_ib(p, m));
    fflush(stdout);
    if (argc > 1) return 0;
    pid_t c = fork();
    if (c == 0) {                                 /* 자식: 같은 주소 공간 사본 */
        printf("%-7s pid %6d: pacia=%#018llx pacib=%#018llx\n", "fork", (int)getpid(),
               (unsigned long long)sign_ia(p, m), (unsigned long long)sign_ib(p, m));
        fflush(stdout);
        execl(argv[0], argv[0], "exec", (char *)0);   /* 새 프로그램 이미지 */
        _exit(1);
    }
    waitpid(c, 0, 0);
    return 0;
}
