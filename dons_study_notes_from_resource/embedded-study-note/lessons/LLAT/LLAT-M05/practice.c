/* LLAT-M05 추가 연습 — 한 번에 검증
 *
 *   연습 1  세 스레드 토큰 링 (캐시 라인 하나를 3 코어가 차례로)
 *   연습 2  조건 변수 핸드오프 (mutex + cond, 시간 제한)
 *   연습 3  WFE 로 기다리기 (sevl / wfe / ldaxr — 라인이 바뀌면 이벤트로 깨어남)
 *   연습 4  타임스탬프를 싣는 한 방향 지연 기록 (공용 카운터 mach_absolute_time)
 *
 * 테스트는 프로토콜 정확성만 본다. 연습 4 의 지연 중앙값은 출력만 한다 (CHECK 아님).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
 */
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(__aarch64__)
#error "arm64 only: 연습 3 uses wfe / ldaxr"
#endif

#define LINE 128
static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static bool spin_until_eq(_Atomic uint64_t *p, uint64_t want, uint64_t deadline_ns) {
    for (uint32_t i = 1;; i++) {
        if (atomic_load_explicit(p, memory_order_acquire) == want) return true;
        if ((i & 4095) == 0) {
            if (now_ns() > deadline_ns) return false;
            sched_yield();
        }
    }
}

/* ===== 연습 1 — 토큰 링: 스레드 i 는 토큰이 r*n + i 일 때 받아서 +1 해 넘긴다 ===== */
typedef struct { _Atomic uint64_t *tok; int id, n; uint64_t rounds, got, deadline; } ring_arg_t;
static void *ring_body(void *arg) {
    ring_arg_t *a = arg;
    for (uint64_t r = 0; r < a->rounds; r++) {
        uint64_t want = r * (uint64_t)a->n + (uint64_t)a->id;
        if (!spin_until_eq(a->tok, want, a->deadline)) return NULL;
        a->got++;
        atomic_store_explicit(a->tok, want + 1, memory_order_release);
    }
    return NULL;
}

/* ===== 연습 2 — 조건 변수 핸드오프 ===== */
typedef struct { pthread_mutex_t mu; pthread_cond_t cv; uint64_t val; } cv_box_t;

static bool cv_wait_eq(cv_box_t *b, uint64_t want, uint64_t deadline_ns) {
    bool ok = true;
    pthread_mutex_lock(&b->mu);
    while (b->val != want) {                                  /* while: 가짜 깨움·다른 값 */
        uint64_t now = now_ns();
        if (now >= deadline_ns) { ok = false; break; }
        uint64_t left = deadline_ns - now;
        struct timespec rel = { (time_t)(left / 1000000000u), (long)(left % 1000000000u) };
        int rc = pthread_cond_timedwait_relative_np(&b->cv, &b->mu, &rel);
        if (rc != 0 && rc != ETIMEDOUT) { ok = false; break; }
    }
    pthread_mutex_unlock(&b->mu);
    return ok;
}
static void cv_post(cv_box_t *b, uint64_t v) {
    pthread_mutex_lock(&b->mu);
    b->val = v;                                               /* 락 안에서 바꾼다 → lost wakeup 없음 */
    pthread_cond_signal(&b->cv);
    pthread_mutex_unlock(&b->mu);
}
typedef struct { cv_box_t *b; uint64_t rounds; bool ping; uint64_t done; } cv_arg_t;
static void *cv_body(void *arg) {
    cv_arg_t *a = arg;
    uint64_t dl = now_ns() + 10000000000ull;
    for (uint64_t k = 1; k <= a->rounds; k++) {
        if (a->ping) { cv_post(a->b, 2 * k - 1); if (!cv_wait_eq(a->b, 2 * k, dl)) return NULL; }
        else         { if (!cv_wait_eq(a->b, 2 * k - 1, dl)) return NULL; cv_post(a->b, 2 * k); }
        a->done++;
    }
    return NULL;
}

/* ===== 연습 3 — WFE 대기 ===== */
/* sevl 로 이벤트 레지스터를 켜 두면 첫 wfe 는 바로 통과. ldaxr 가 라인을 exclusive 모니터에 걸고,
 * 다른 코어가 그 라인에 쓰면 모니터가 풀리며 이벤트 → wfe 가 깨어난다. 마감 확인은 1024번마다. */
static bool wfe_wait_eq(_Atomic uint64_t *p, uint64_t want, uint64_t deadline_ns) {
    __asm__ volatile("sevl" ::: "memory");
    for (uint32_t i = 1;; i++) {
        uint64_t v;
        __asm__ volatile("wfe" ::: "memory");
        __asm__ volatile("ldaxr %0, [%1]" : "=r"(v) : "r"(p) : "memory");
        if (v == want) { __asm__ volatile("clrex" ::: "memory"); return true; }
        if ((i & 1023) == 0 && now_ns() > deadline_ns) { __asm__ volatile("clrex" ::: "memory"); return false; }
    }
}
typedef struct { _Atomic uint64_t *seq; uint64_t rounds; bool ping; uint64_t done; } wfe_arg_t;
static void *wfe_body(void *arg) {
    wfe_arg_t *a = arg;
    uint64_t dl = now_ns() + 10000000000ull;
    for (uint64_t k = 1; k <= a->rounds; k++) {
        if (a->ping) {
            atomic_store_explicit(a->seq, 2 * k - 1, memory_order_release);
            if (!wfe_wait_eq(a->seq, 2 * k, dl)) return NULL;
        } else {
            if (!wfe_wait_eq(a->seq, 2 * k - 1, dl)) return NULL;
            atomic_store_explicit(a->seq, 2 * k, memory_order_release);
        }
        a->done++;
    }
    return NULL;
}

/* ===== 연습 4 — 타임스탬프 한 방향 기록 ===== */
/* 핑: ts(평범한 변수) 에 mach_absolute_time 을 쓰고 seq = k (release). 퐁: seq == k (acquire) 를 보면
 * 지금 시각 - ts 를 기록하고 ack = k (release). 핑은 ack 를 보고 나서야 다음 ts 를 쓴다 (덮어쓰기 금지). */
typedef struct {
    _Alignas(LINE) _Atomic uint64_t seq; uint64_t ts, tag;   /* tag = 이 메시지의 번호 (낡은 payload 검출) */
    _Alignas(LINE) _Atomic uint64_t ack;
} ow_t;
static bool ow_send(ow_t *o, uint64_t k, uint64_t dl) {
    o->ts = mach_absolute_time();
    o->tag = k;
    atomic_store_explicit(&o->seq, k, memory_order_release);
    return spin_until_eq(&o->ack, k, dl);
}
static bool ow_recv(ow_t *o, uint64_t k, uint64_t *ts, uint64_t *tag, uint64_t *delta, uint64_t dl) {
    if (!spin_until_eq(&o->seq, k, dl)) return false;
    uint64_t now = mach_absolute_time();
    *ts = o->ts;
    *tag = o->tag;
    *delta = now - *ts;                       /* 같은 시스템 카운터라 코어가 달라도 뺄 수 있다 */
    atomic_store_explicit(&o->ack, k, memory_order_release);
    return true;
}
enum { OWN = 20000 };
static uint64_t g_ow_ts[OWN + 1], g_ow_tag[OWN + 1], g_ow_d[OWN + 1];
typedef struct { ow_t *o; uint64_t got; } owr_arg_t;
static void *ow_pong(void *arg) {
    owr_arg_t *a = arg;
    uint64_t dl = now_ns() + 10000000000ull;
    for (uint64_t k = 1; k <= OWN; k++) {
        if (!ow_recv(a->o, k, &g_ow_ts[k], &g_ow_tag[k], &g_ow_d[k], dl)) return NULL;
        a->got++;
    }
    return NULL;
}
static int cmp_u64(const void *x, const void *y) {
    uint64_t a = *(const uint64_t *)x, b = *(const uint64_t *)y; return (a > b) - (a < b);
}

int main(void) {
    printf("-- 연습 1: 3-thread token ring\n");
    {
        enum { NT = 3, RR = 10000 };
        static _Alignas(LINE) _Atomic uint64_t tok;
        atomic_store(&tok, 0);
        ring_arg_t a[NT]; pthread_t t[NT];
        uint64_t dl = now_ns() + 10000000000ull;
        for (int i = 0; i < NT; i++) { a[i] = (ring_arg_t){ &tok, i, NT, RR, 0, dl }; pthread_create(&t[i], NULL, ring_body, &a[i]); }
        for (int i = 0; i < NT; i++) pthread_join(t[i], NULL);
        CHECK(a[0].got == RR && a[1].got == RR && a[2].got == RR);
        CHECK(atomic_load(&tok) == (uint64_t)NT * RR);
    }
    printf("-- 연습 2: condvar handoff\n");
    {
        static cv_box_t b = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0 };
        cv_arg_t p = { &b, 3000, true, 0 }, q = { &b, 3000, false, 0 };
        pthread_t t1, t2;
        pthread_create(&t2, NULL, cv_body, &q); pthread_create(&t1, NULL, cv_body, &p);
        pthread_join(t1, NULL); pthread_join(t2, NULL);
        CHECK(p.done == 3000 && q.done == 3000 && b.val == 6000);
        uint64_t t0 = now_ns();
        CHECK(!cv_wait_eq(&b, 12345, t0 + 20000000));           /* 안 오는 값 → 20 ms 뒤 false */
        CHECK(cv_wait_eq(&b, 6000, t0));                        /* 이미 같음 → true */
    }
    printf("-- 연습 3: WFE wait\n");
    {
        static _Alignas(LINE) _Atomic uint64_t seq;
        atomic_store(&seq, 0);
        wfe_arg_t p = { &seq, 20000, true, 0 }, q = { &seq, 20000, false, 0 };
        pthread_t t1, t2;
        pthread_create(&t2, NULL, wfe_body, &q); pthread_create(&t1, NULL, wfe_body, &p);
        pthread_join(t1, NULL); pthread_join(t2, NULL);
        CHECK(p.done == 20000 && q.done == 20000 && atomic_load(&seq) == 40000);
        uint64_t t0 = now_ns();
        CHECK(!wfe_wait_eq(&seq, 1, t0 + 20000000));            /* 아무도 안 써도 마감에 돌아온다 */
    }
    printf("-- 연습 4: one-way timestamps\n");
    {
        static ow_t o;
        owr_arg_t r = { &o, 0 };
        pthread_t t; pthread_create(&t, NULL, ow_pong, &r);
        uint64_t dl = now_ns() + 10000000000ull, sent = 0;
        for (uint64_t k = 1; k <= OWN; k++) { if (!ow_send(&o, k, dl)) break; sent++; }
        pthread_join(t, NULL);
        CHECK(sent == OWN && r.got == OWN);
        bool mono = true, sane = true, tags = true;
        for (uint64_t k = 2; k <= OWN; k++) if (g_ow_ts[k] < g_ow_ts[k - 1]) mono = false;   /* 같은 tick 은 가능 (왕복 ≈ 2 tick) */
        for (uint64_t k = 1; k <= OWN; k++) if (g_ow_tag[k] != k) tags = false;
        for (uint64_t k = 1; k <= OWN; k++) if (g_ow_d[k] > (1ull << 62)) sane = false;   /* 음수(wrap) 없음 */
        CHECK(tags);                                            /* 퐁이 본 payload 는 매번 그 번호의 것 (낡은 것 없음) */
        CHECK(mono);                                            /* ts 는 줄어들지 않는다 */
        CHECK(sane);
        mach_timebase_info_data_t tb; mach_timebase_info(&tb);
        qsort(g_ow_d + 1, OWN, sizeof g_ow_d[0], cmp_u64);
        printf("   (info) one-way median %llu ticks = %.0f ns, p99 %.0f ns — not checked\n",
               (unsigned long long)g_ow_d[OWN / 2], (double)g_ow_d[OWN / 2] * tb.numer / tb.denom,
               (double)g_ow_d[OWN - OWN / 100] * tb.numer / tb.denom);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
