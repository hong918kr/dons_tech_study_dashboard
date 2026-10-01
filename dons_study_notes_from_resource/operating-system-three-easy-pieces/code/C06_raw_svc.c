// C06_raw_svc.c — libc 없이 "trap 명령어"를 직접 실행해 시스템 콜을 한다 (macOS arm64).
// Darwin arm64 규약: x16 = 시스템 콜 번호, x0..x5 = 인자, 'svc #0x80' 으로 커널 진입.
// 리턴: x0 = 결과, 에러면 carry 플래그가 1이고 x0 = errno.
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#if defined(__APPLE__) && defined(__aarch64__)
static long raw_syscall3(long num, long a0, long a1, long a2, int *err) {
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x16 __asm__("x16") = num;
    long carry;
    __asm__ volatile(
        "svc #0x80\n"          // ← 이것이 trap 명령어. 여기서 EL0 → EL1(커널)로 넘어간다
        "cset %1, cs\n"        // carry set 이면 에러
        : "+r"(x0), "=r"(carry)
        : "r"(x1), "r"(x2), "r"(x16)
        : "memory", "cc");
    *err = (int)carry;
    return x0;
}

int main(void) {
    int err;
    setvbuf(stdout, NULL, _IONBF, 0);   // 마지막에 SIGSYS로 죽어도 출력이 남도록 버퍼링 끔
    const char *msg = "hello from svc #0x80 (SYS_write = 4)\n";
    long n = raw_syscall3(4, 1, (long)msg, (long)strlen(msg), &err);   // write(1, msg, len)
    printf("write returned %ld, error flag %d\n", n, err);

    long pid = raw_syscall3(20, 0, 0, 0, &err);                        // getpid()
    printf("raw getpid() = %ld, libc getpid() = %d\n", pid, (int)getpid());

    n = raw_syscall3(4, 99, (long)msg, 5, &err);                       // write(99, ...) 잘못된 fd
    printf("write(99, ...) returned %ld, error flag %d (EBADF = 9)\n", n, err);

    printf("now trapping with nonexistent syscall #9999 ...\n");
    n = raw_syscall3(9999, 0, 0, 0, &err);                             // 없는 시스템 콜 번호
    printf("syscall #9999 returned (%ld, %d)\n", n, err);              // 보통 여기 안 옴: SIGSYS
    return 0;
}
#else
int main(void) { puts("this demo needs macOS on arm64"); return 0; }
#endif
