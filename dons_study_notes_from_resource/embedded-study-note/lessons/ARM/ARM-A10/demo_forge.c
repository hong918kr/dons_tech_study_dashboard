/* ARM-A10 demo_forge.c — 자기 프레임의 저장된 LR 을 직접 바꿔 "반환 주소 위조"를 재현한다 (장난감, 자기 프로세스 안에서만).
 * 모드:  forge   — 저장 LR 을 landing() 의 생(raw) 주소로 덮는다 (ROP 의 첫 걸음)
 *        replay  — 같은 SP 에서 서명된 LR 을 다른 함수 프레임에 재사용한다
 *        replay2 — 다른 SP(한 단계 깊은 곳)에서 재사용한다
 * 각 모드는 fork 한 자식에서 실행; 부모가 종료 코드/시그널을 한 줄로 보고한다.
 * Mac:  cc -O2 [-mbranch-protection=standard | -arch arm64e] demo_forge.c
 * VM :  gcc -O2 -fno-omit-frame-pointer -mbranch-protection={none,standard} demo_forge.c */
#define _POSIX_C_SOURCE 200809L   /* strsignal, fork (-std=c11 에서도) */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#if defined(__APPLE__) && __has_include(<ptrauth.h>)
#include <ptrauth.h>
#define RAW_CODE(f) ((uintptr_t)ptrauth_strip((void *)(f), ptrauth_key_function_pointer))
#else
#define RAW_CODE(f) ((uintptr_t)(f))
#endif

#define NOINL __attribute__((noinline))
volatile uintptr_t g_saved;            /* replay 용: 서명된 LR 보관함 */
volatile int g_pass;
volatile int g_sink;

NOINL void landing(void) {             /* 공격자가 가고 싶은 곳 */
    printf("  child: LANDED in landing()\n");
    fflush(stdout);
    _exit(42);
}

/* 프레임 레코드 = {saved x29, saved x30}, x29 가 그 시작을 가리킨다 (clang/GCC AArch64 공통, -fno-omit-frame-pointer) */
static volatile uintptr_t *lr_slot(void *fp) { return (volatile uintptr_t *)fp + 1; }

NOINL int victim_forge(int x) {
    volatile uintptr_t *slot = lr_slot(__builtin_frame_address(0));
    printf("  child: saved LR slot = %#018lx -> overwrite with raw &landing = %#018lx\n",
           (unsigned long)*slot, (unsigned long)RAW_CODE(landing));
    fflush(stdout);
    *slot = RAW_CODE(landing);          /* 서명 없는 생 주소 */
    return x + g_sink;
}

NOINL int record_lr(int x) {            /* 자기 저장 LR(서명됐을 수 있음)을 복사해 둔다 */
    volatile uintptr_t *slot = lr_slot(__builtin_frame_address(0));
    g_saved = *slot;
    printf("  child: record_lr saved LR = %#018lx\n", (unsigned long)g_saved);
    fflush(stdout);
    return x + g_sink;
}

NOINL int victim_replay(int x) {        /* 남의 서명된 LR 로 자기 LR 을 바꾼다 */
    volatile uintptr_t *slot = lr_slot(__builtin_frame_address(0));
    printf("  child: victim_replay own LR = %#018lx -> replace with recorded\n", (unsigned long)*slot);
    fflush(stdout);
    *slot = g_saved;
    return x + g_sink;
}

NOINL int deeper(int x) { return victim_replay(x) + 1; }   /* 한 프레임 더 깊다 → SP 가 다르다 */

static int run_child(const char *mode) {
    if (!strcmp(mode, "forge")) {
        victim_forge(1);
        printf("  child: returned normally (forge FAILED to redirect)\n");
        return 0;
    }
    int deep = !strcmp(mode, "replay2");
    record_lr(1);                                  /* ← 복귀점 R: replay 가 성공하면 여기로 한 번 더 온다 */
    if (++g_pass > 1) {
        printf("  child: REPLAYED — came back to record_lr's return site (pass %d)\n", g_pass);
        fflush(stdout);
        _exit(43);
    }
    if (deep) deeper(2); else victim_replay(2);
    printf("  child: returned normally (replay did not redirect)\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s forge|replay|replay2\n", argv[0]); return 2; }
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) _exit(run_child(argv[1]));
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFSIGNALED(st))
        printf("RESULT %s: signal %d (%s)\n", argv[1], WTERMSIG(st), strsignal(WTERMSIG(st)));
    else
        printf("RESULT %s: exit %d\n", argv[1], WEXITSTATUS(st));
    return 0;
}
