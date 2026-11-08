/* ARM-M04 demo_broken.c — inline asm 원자 연산의 "그럴듯하지만 틀린" 4가지를 실제로 돌려 본다.
 *
 *   ./db status     STLXR 상태(Ws)를 무시 → 실패한 store 를 성공으로 착각 → 증가 유실
 *   ./db plain      LDAXR/STXR 대신 일반 LDR/STR 로 test-and-set → 두 코어가 동시에 "잡음"
 *   ./db noclobber  "memory" clobber 없는 lock/unlock → 컴파일러가 임계 구역 접근을 락 밖으로 뺀다
 *   ./db earlyclob  출력 제약에 & (early-clobber) 없음 → LDAXR 가 주소 레지스터를 덮어씀 → 크래시
 *
 * build: cc -std=c11 -O2 -pthread demo_broken.c -o db
 *        cc -std=c11 -O2 -S demo_broken.c -o -      (earlyclob 의 레지스터 배정을 눈으로 확인)
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define NT 4
#define ITERS 1000000u

static uint32_t g_cnt;
static uint32_t g_lock;
static uint64_t g_a, g_b;          /* 일반 변수, 락으로 보호한다고 "생각" 한 것 */
static volatile uint32_t g_inside, g_viol;

/* 1) status 무시 */
static void add_ignore_status(uint32_t *p) {
    uint32_t old, sum, st;
    __asm__ volatile(
        "   ldaxr  %w[old], %[mem]\n"
        "   add    %w[sum], %w[old], #1\n"
        "   stlxr  %w[st], %w[sum], %[mem]\n"     /* st 가 1 이어도 그냥 지나간다 */
        : [old] "=&r"(old), [sum] "=&r"(sum), [st] "=&r"(st), [mem] "+Q"(*p) : : "memory");
}
/* 2) 일반 LDR/STR test-and-set */
static void lock_plain(uint32_t *l) {
    uint32_t old;
    __asm__ volatile(
        "1: ldr    %w[old], %[mem]\n"
        "   cbnz   %w[old], 1b\n"
        "   str    %w[one], %[mem]\n"
        "   dmb    ish\n"
        : [old] "=&r"(old), [mem] "+Q"(*l) : [one] "r"(1u) : "memory");
}
/* 3) 올바른 명령, 그러나 "memory" clobber 없음 */
static void lock_noclob(uint32_t *l) {
    uint32_t old, st;
    __asm__ volatile(
        "1: ldaxr  %w[old], %[mem]\n"
        "   cbnz   %w[old], 1b\n"
        "   stxr   %w[st], %w[one], %[mem]\n"
        "   cbnz   %w[st], 1b\n"
        : [old] "=&r"(old), [st] "=&r"(st), [mem] "+Q"(*l) : [one] "r"(1u));
}
static void unlock_noclob(uint32_t *l) { __asm__ volatile("stlr wzr, %[mem]" : [mem] "=Q"(*l)); }
static void unlock(uint32_t *l) { __asm__ volatile("stlr wzr, %[mem]" : [mem] "=Q"(*l) : : "memory"); }
/* 4) early-clobber 없음 */
__attribute__((noinline)) static uint32_t add_no_earlyclobber(uint32_t *p, uint32_t v) {
    uint32_t old, sum, st;
    __asm__ volatile(
        "1: ldaxr  %w[old], %[mem]\n"
        "   add    %w[sum], %w[old], %w[v]\n"
        "   stlxr  %w[st], %w[sum], %[mem]\n"
        "   cbnz   %w[st], 1b\n"
        : [old] "=r"(old), [sum] "=r"(sum), [st] "=r"(st), [mem] "+Q"(*p)
        : [v] "r"(v) : "memory");
    return old;
}

static int g_mode;
static void *worker(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < ITERS; i++) {
        switch (g_mode) {
        case 0: add_ignore_status(&g_cnt); break;
        case 1:
            lock_plain(&g_lock);
            if (++g_inside != 1u) g_viol++;
            g_a += 1u; g_b += 3u;
            g_inside--;
            unlock(&g_lock);
            break;
        case 2:
            lock_noclob(&g_lock);
            g_a += 1u; g_b += 3u;
            unlock_noclob(&g_lock);
            break;
        }
    }
    return NULL;
}
static void run(void) {
    pthread_t th[NT];
    for (int i = 0; i < NT; i++) pthread_create(&th[i], NULL, worker, NULL);
    for (int i = 0; i < NT; i++) pthread_join(th[i], NULL);
}

int main(int argc, char **argv) {
    const char *m = argc > 1 ? argv[1] : "status";
    unsigned long long want = (unsigned long long)NT * ITERS;
    if (!strcmp(m, "status")) {
        g_mode = 0; run();
        printf("status   : counter=%u (expect %llu, lost %llu)\n", g_cnt, want, want - g_cnt);
    } else if (!strcmp(m, "plain")) {
        g_mode = 1; run();
        printf("plain    : a=%llu b=%llu (expect a=%llu, b=3a) overlap_seen=%u\n",
               (unsigned long long)g_a, (unsigned long long)g_b, want, g_viol);
    } else if (!strcmp(m, "noclobber")) {
        g_mode = 2; run();
        printf("noclobber: a=%llu b=%llu (expect a=%llu)\n",
               (unsigned long long)g_a, (unsigned long long)g_b, want);
    } else if (!strcmp(m, "earlyclob")) {
        pid_t pid = fork();
        if (pid == 0) {
            uint32_t x = 5u;
            uint32_t r = add_no_earlyclobber(&x, 3u);
            printf("earlyclob: returned %u, x=%u\n", r, x);
            _exit(0);
        }
        int stt = 0;
        waitpid(pid, &stt, 0);
        if (WIFSIGNALED(stt)) printf("earlyclob: child killed by signal %d (%s)\n", WTERMSIG(stt), strsignal(WTERMSIG(stt)));
        else printf("earlyclob: child exited %d\n", WEXITSTATUS(stt));
    }
    return 0;
}
