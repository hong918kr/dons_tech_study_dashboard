/* demo_realthreads.c — 같은 check-then-commit 로직을 진짜 스레드 2개로 경쟁시킨다.
 * 시뮬과 비교용: 실제 스케줄러가 어떤 순서를 내는지, TSan이 무엇을 보는지.
 *
 * 기본 빌드(순서 측정): active 비트와 티켓을 한 워드에 넣어, 각 단계가 원자적 RMW 하나로
 *   "상태 관찰 + 순서 기록"을 동시에 한다. (티켓을 따로 뽑으면 그 사이 틈 때문에 순서가 거짓이 된다)
 *   cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_realthreads.c -o demo_rt
 *
 * TSan 빌드(일부러 plain bool, 진짜 data race):
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread -DPLAIN_RACE -DROUNDS=2000 demo_realthreads.c -o demo_rt_tsan
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef ROUNDS
#define ROUNDS 300000u
#endif
#define SPIN_LIMIT 200000000ull         /* 멈추지 않게 대기는 항상 상한이 있다 */

static _Atomic uint32_t g_round, g_done;
static _Atomic bool g_abort;

#ifdef PLAIN_RACE
static bool g_active;                   /* 일부러 원자적이지 않게 */
#else
/* bit0 = active, bit1.. = 티켓 카운터 */
static _Atomic uint32_t g_word;

/* check: 티켓을 뽑으면서 그 순간의 active를 함께 읽는다 */
static uint32_t step_check(bool *ok)
{
    uint32_t old = atomic_fetch_add(&g_word, 2u);
    *ok = (old & 1u) != 0;
    return (old >> 1) + 1;
}

/* commit: active를 내리면서 티켓을 뽑는다 */
static uint32_t step_commit(void)
{
    uint32_t old = atomic_load(&g_word);
    while (!atomic_compare_exchange_weak(&g_word, &old, (old & ~1u) + 2u)) { }
    return (old >> 1) + 1;
}
#endif

/* who: 0 = RX, 1 = TMR. tick[who][0] = check, tick[who][1] = commit (0 = 커밋 안 함) */
static uint32_t tick[2][2];

static bool wait_for(_Atomic uint32_t *p, uint32_t want)
{
    for (uint64_t spins = 0; spins < SPIN_LIMIT; spins++) {
        if (atomic_load_explicit(p, memory_order_acquire) >= want) return true;
        if (atomic_load_explicit(&g_abort, memory_order_relaxed)) return false;
        if ((spins & 0xFFFFu) == 0xFFFFu) sched_yield();
    }
    atomic_store_explicit(&g_abort, true, memory_order_relaxed);
    return false;
}

static void *racer(void *arg)
{
    int who = (int)(intptr_t)arg;
    uint32_t x = 0x9E3779B9u + (uint32_t)who;
    for (uint32_t r = 1; r <= ROUNDS; r++) {
        if (!wait_for(&g_round, r)) return NULL;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        for (volatile uint32_t j = x & 15u; j > 0; j--) { }     /* 시작 시점 흔들기 */
#ifdef PLAIN_RACE
        bool ok = g_active;                                     /* check */
        if (ok) g_active = false;                               /* commit */
        tick[who][0] = 1;
        tick[who][1] = ok ? 2u : 0u;
#else
        bool ok;
        tick[who][0] = step_check(&ok);
        tick[who][1] = ok ? step_commit() : 0u;
#endif
        atomic_fetch_add_explicit(&g_done, 1, memory_order_release);
    }
    return NULL;
}

typedef struct { char pat[6]; uint32_t n; } pat_t;
static pat_t pats[8];
static int npat;

static void tally(const char *s)
{
    for (int i = 0; i < npat; i++)
        if (strcmp(pats[i].pat, s) == 0) { pats[i].n++; return; }
    if (npat < 8) { strcpy(pats[npat].pat, s); pats[npat].n = 1; npat++; }
}

int main(void)
{
    pthread_t th[2];
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    pthread_create(&th[0], NULL, racer, (void *)(intptr_t)0);
    pthread_create(&th[1], NULL, racer, (void *)(intptr_t)1);
    uint32_t doubles = 0, completed = 0;
    for (uint32_t r = 1; r <= ROUNDS; r++) {
#ifdef PLAIN_RACE
        g_active = true;
#else
        atomic_store(&g_word, 1u);      /* active=1, 티켓 0 */
#endif
        atomic_store_explicit(&g_done, 0, memory_order_relaxed);
        atomic_store_explicit(&g_round, r, memory_order_release);
        if (!wait_for(&g_done, 2)) { puts("wait timed out (loaded machine?)"); break; }

        /* 티켓 순으로 RrTt 문자열을 만든다 */
        struct { uint32_t t; char c; } ev[4] = {
            { tick[0][0], 'R' }, { tick[0][1], 'r' }, { tick[1][0], 'T' }, { tick[1][1], 't' } };
        char s[6] = { 0 };
        int len = 0;
        for (int pass = 0; pass < 4; pass++) {
            int best = -1;
            for (int i = 0; i < 4; i++)
                if (ev[i].t && (best < 0 || ev[i].t < ev[best].t)) best = i;
            if (best < 0) break;
            s[len++] = ev[best].c;
            ev[best].t = 0;
        }
        tally(s);
        if (strchr(s, 'r') && strchr(s, 't')) doubles++;
        completed++;
    }
    pthread_join(th[0], NULL);
    pthread_join(th[1], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double sec = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;
    printf("real threads: rounds=%u wall=%.3f s, double-commit=%u (%.2f%%), distinct orderings=%d\n",
           completed, sec, doubles, 100.0 * doubles / (completed ? completed : 1), npat);
    for (int i = 0; i < npat; i++) printf("   %-6s %u\n", pats[i].pat, pats[i].n);
    return 0;
}
