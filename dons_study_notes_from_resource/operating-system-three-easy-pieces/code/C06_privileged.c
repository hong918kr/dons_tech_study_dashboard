// C06_privileged.c — 유저 모드(EL0)에서 "제한된 연산"을 시도하면? → CPU 예외 → 커널이 프로세스를 죽인다.
// 각 시도는 fork()한 자식에서 하고, 부모가 wait()로 사인을 확인한다 (OSTEP: "adios, offending program").
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__) && defined(__aarch64__)
static void read_current_el(void) {      // 현재 예외 레벨 읽기: EL1 이상에서만 허용
    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    printf("CurrentEL = %llu\n", (unsigned long long)(el >> 2));
}
static void mask_irqs(void) {            // 인터럽트 끄기 (DAIF.I = 1): 커널 전용
    __asm__ volatile("msr daifset, #2");
}
static void read_vbar(void) {            // 예외 벡터 테이블 주소 = OSTEP의 "trap table" 위치
    uint64_t v;
    __asm__ volatile("mrs %0, VBAR_EL1" : "=r"(v));
    printf("VBAR_EL1 = %#llx\n", (unsigned long long)v);
}
static void touch_kernel_memory(void) {  // 커널 주소 공간(상위 절반) 읽기
    volatile uint64_t *k = (uint64_t *)0xfffffe0000000000ULL;
    printf("%llu\n", (unsigned long long)*k);
}
static void read_cntvct(void) {          // 비교용: 유저에게 허용된 시스템 레지스터 (가상 카운터)
    uint64_t v;
    __asm__ volatile("mrs %0, CNTVCT_EL0" : "=r"(v));
    printf("    (child) CNTVCT_EL0 = %llu  <- allowed in user mode\n", (unsigned long long)v);
    fflush(stdout);                       // 자식은 _exit()로 끝나므로 직접 flush
}

static void attempt(const char *what, void (*fn)(void)) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        fn();
        _exit(0);
    }
    int st;
    waitpid(pid, &st, 0);
    if (WIFSIGNALED(st))
        printf("%-34s -> killed by signal %d (%s)\n", what, WTERMSIG(st), strsignal(WTERMSIG(st)));
    else
        printf("%-34s -> exited normally (%d)\n", what, WEXITSTATUS(st));
}

int main(void) {
    attempt("mrs x, CNTVCT_EL0 (user timer)", read_cntvct);
    attempt("mrs x, CurrentEL", read_current_el);
    attempt("msr daifset, #2 (disable IRQ)", mask_irqs);
    attempt("mrs x, VBAR_EL1 (trap table)", read_vbar);
    attempt("load from kernel address", touch_kernel_memory);
    return 0;
}
#else
int main(void) { puts("this demo needs macOS on arm64"); return 0; }
#endif
