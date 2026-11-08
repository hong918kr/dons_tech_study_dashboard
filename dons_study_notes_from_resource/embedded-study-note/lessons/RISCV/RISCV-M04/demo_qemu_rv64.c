/* ENV: qemu */
/* RISCV-M04 demo_qemu_rv64.c — 같은 LR/SC·AMO 어셈블리를 "진짜" RISC-V 스레드로: qemu-riscv64 사용자 모드
 *
 * Lima VM elinux (aarch64 Ubuntu): RV64 리눅스 바이너리를 정적 링크해 qemu-riscv64 로 실행.
 * 게스트 스레드 = 호스트 스레드 (병렬 실행). .w 명령은 RV64 에서도 32비트 워드에 동작한다.
 *   riscv64-linux-gnu-gcc -std=c11 -O2 -Wall -Wextra -Werror -static -pthread demo_qemu_rv64.c -o dq
 *   qemu-riscv64 ./dq
 * Part 1: 스핀락(amoswap.w.aq/.rl) · CAS 증가(lr.w/sc.w) · ticket lock(amoadd.w + fence) 4 스레드 스트레스
 * Part 2: ABA 실험 — LR 후 다른 스레드가 "같은 값"을 쓰면 스펙상 SC 는 실패해야 한다. QEMU 는?
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#define NT 4
#define ITERS 100000

static int32_t g_lock, g_next, g_serve;   /* 락 워드들 (asm 으로만 접근) */
static int32_t g_cas;                      /* CAS 로만 증가 */
static long g_plain_a, g_plain_b;          /* 락이 지키는 일반 변수 */

static void spin_lock(int32_t *l) {
    int32_t one = 1, old;
    __asm__ volatile(
        "1: lw   %0, (%1)\n"
        "   bnez %0, 1b\n"
        "   amoswap.w.aq %0, %2, (%1)\n"
        "   bnez %0, 1b\n"
        : "=&r"(old) : "r"(l), "r"(one) : "memory");
}
static void spin_unlock(int32_t *l) { __asm__ volatile("amoswap.w.rl zero, zero, (%0)" :: "r"(l) : "memory"); }

/* 스펙 예제 cas: 0 = 성공 */
static int cas32(int32_t *p, int32_t expect, int32_t desired) {
    int32_t old, fail;
    __asm__ volatile(
        "1: lr.w %0, (%2)\n"
        "   bne  %0, %3, 2f\n"
        "   sc.w %1, %4, (%2)\n"
        "   bnez %1, 1b\n"
        "   li   %1, 0\n"
        "   j    3f\n"
        "2: li   %1, 1\n"
        "3:\n"
        : "=&r"(old), "=&r"(fail) : "r"(p), "r"(expect), "r"(desired) : "memory");
    return (int)fail;
}

static void ticket_lock(int32_t *my) {
    int32_t one = 1, t, s;
    __asm__ volatile(
        "   amoadd.w %0, %3, (%2)\n"
        "1: lw   %1, (%4)\n"
        "   bne  %1, %0, 1b\n"
        "   fence r, rw\n"
        : "=&r"(t), "=&r"(s) : "r"(&g_next), "r"(one), "r"(&g_serve) : "memory");
    *my = t;
}
static void ticket_unlock(int32_t my) {
    __asm__ volatile("fence rw, w\n   sw %0, (%1)" :: "r"(my + 1), "r"(&g_serve) : "memory");
}

static void *worker(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERS; i++) {
        spin_lock(&g_lock); g_plain_a++; spin_unlock(&g_lock);
        int32_t v;
        do { v = __atomic_load_n(&g_cas, __ATOMIC_RELAXED); } while (cas32(&g_cas, v, v + 1));
        int32_t my; ticket_lock(&my); g_plain_b++; ticket_unlock(my);
    }
    return NULL;
}

/* ---- Part 2: ABA ---- */
static int32_t x_word;
static atomic_int ready, done;
static int32_t b_first, b_second;           /* B 가 쓸 값 두 개 (같으면 "같은 값 다시 쓰기") */

static void *thread_b(void *arg) {
    (void)arg;
    while (!atomic_load(&ready)) { }
    __atomic_store_n(&x_word, b_first, __ATOMIC_SEQ_CST);
    __atomic_store_n(&x_word, b_second, __ATOMIC_SEQ_CST);
    atomic_store(&done, 1);
    return NULL;
}
/* A: lr.w → (ready=1, done 기다림: 제약 안 된 시퀀스!) → sc.w. 0 이면 SC 성공 */
static int32_t aba_try(int32_t newv) {
    int32_t old, rc, t, one = 1;
    __asm__ volatile(
        "   lr.w %0, (%3)\n"
        "   sw   %5, (%4)\n"            /* ready = 1 */
        "1: lw   %2, (%6)\n"            /* while (!done) */
        "   beqz %2, 1b\n"
        "   sc.w %1, %7, (%3)\n"
        : "=&r"(old), "=&r"(rc), "=&r"(t)
        : "r"(&x_word), "r"(&ready), "r"(one), "r"(&done), "r"(newv) : "memory");
    return rc;
}
static void aba_case(const char *what, int32_t first, int32_t second) {
    pthread_t b;
    x_word = 5; b_first = first; b_second = second;
    atomic_store(&ready, 0); atomic_store(&done, 0);
    pthread_create(&b, NULL, thread_b, NULL);
    int32_t rc = aba_try(99);
    pthread_join(b, NULL);
    printf("  %-34s SC rd=%d (%s), x=%d\n", what, rc, rc == 0 ? "success" : "fail", x_word);
}

int main(void) {
    pthread_t t[NT];
    for (int i = 0; i < NT; i++) pthread_create(&t[i], NULL, worker, NULL);
    for (int i = 0; i < NT; i++) pthread_join(t[i], NULL);
    long want = (long)NT * ITERS;
    printf("Part 1 (%d threads x %d):\n", NT, ITERS);
    printf("  spinlock  plain_a = %ld  %s\n", g_plain_a, g_plain_a == want ? "OK" : "WRONG");
    printf("  cas       g_cas   = %d  %s\n", g_cas, g_cas == want ? "OK" : "WRONG");
    printf("  ticket    plain_b = %ld  %s (serve=%d next=%d)\n", g_plain_b, g_plain_b == want ? "OK" : "WRONG",
           g_serve, g_next);
    printf("Part 2 (x=5, A: lr.w; B stores; A: sc.w 99). Spec: any store by another hart -> SC must fail\n");
    aba_case("B stores 5 (same value)", 5, 5);
    aba_case("B stores 7 then 5 (A-B-A)", 7, 5);
    aba_case("B stores 7 (value changed)", 7, 7);
    return 0;
}
