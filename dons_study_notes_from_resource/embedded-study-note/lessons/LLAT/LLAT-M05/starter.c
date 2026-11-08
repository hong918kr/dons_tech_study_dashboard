/* LLAT-M05 스레드 간 핸드오프 지연 (ping-pong)
 *
 *   Part A  측정 수학 rt_stats (배치 표본 → 왕복 1번의 min/p50/p99, 피코초) · QoS 적용 qos_apply ·
 *           캐시 라인 하나(64비트 seq)로 핑퐁: spin_until_eq + pp_run (핑=홀수, 퐁=짝수)
 *   Part B  배치(레이아웃) same_line · 깃발 두 개 핑퐁 · 잠드는 핸드오프 blk_wait_eq / blk_post
 *           (os_sync_wait_on_address — 값이 이미 바뀌었으면 잠들지 않는다 = lost wakeup 없음)
 *   Part C  듀얼 코어 메일박스 RPC: 요청 라인 / 응답 라인 (128 B 패딩), 평범한 payload + release/acquire seq
 *
 * 체크 테스트는 "프로토콜 정확성" 만 본다: 양쪽이 모든 순번을 정확히 한 번씩 봤나, 최종 값, 응답 내용.
 * 시간은 절대 CHECK 하지 않는다 → demo_pingpong.c (맥: 레이아웃·QoS·대기 방식별 왕복 지연),
 * demo_linux_affinity.c (리눅스 VM: pthread_setaffinity_np 로 vCPU 쌍 고정).
 * 기다리는 함수는 모두 마감 시각(deadline)이 있어서, 상대가 틀려도 멈추지 않고 false 를 돌려준다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
 */
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <os/os_sync_wait_on_address.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LINE 128   /* sysctl hw.cachelinesize = 128 (이 맥). Cortex-A 는 보통 64 */

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }   /* 41.67 ns 눈금 */

/* ======================= Part A — 측정 수학 ======================= */
/* 표본 i = batch 번 왕복에 걸린 mach tick 수. 앞 warm 개는 버리고, 왕복 1번당 피코초로 바꿔
 * min / p50 (낮은 쪽 중앙값, 인덱스 (m-1)/2) / p99 (nearest-rank, 인덱스 ceil(0.99 m)-1) / max.
 * ps = ticks * numer * 1000 / (denom * batch)  (버림). 잘못된 입력(n <= warm, batch 0, denom 0, n > 4096)은 전부 0. */
typedef struct { uint64_t min_ps, p50_ps, p99_ps, max_ps; } rt_stats_t;

static int cmp_u64(const void *x, const void *y) {
    uint64_t a = *(const uint64_t *)x, b = *(const uint64_t *)y;
    return (a > b) - (a < b);
}

static rt_stats_t rt_stats(const uint64_t *ticks, int n, int warm, uint32_t batch, uint32_t numer, uint32_t denom) {
    rt_stats_t r = { 0, 0, 0, 0 };
    (void)ticks; (void)n; (void)warm; (void)batch; (void)numer; (void)denom; (void)cmp_u64;
    return r; /* TODO: 검사 → 앞 warm 개 버린 복사본 정렬 → 인덱스로 뽑고 tick → ps (128비트 곱) */
}

/* 지금 스레드의 QoS 를 q 로 요청하고, qos_class_self() 가 q 를 돌려주면 true.
 * (QoS 는 "추천" 이다 — 어느 코어에서 도는지는 보장하지 않는다. 확인할 수 있는 건 요청이 받아들여졌다는 것뿐) */
static bool qos_apply(qos_class_t q) {
    (void)q;
    return false; /* TODO: pthread_set_qos_class_self_np + qos_class_self 로 확인 */
}

/* ======================= Part A — 캐시 라인 하나로 핑퐁 ======================= */
/* *p 가 want 가 될 때까지 acquire load 로 돈다. 4096번마다 시계를 보고, 그 뒤로는 sched_yield 를 섞는다
 * (스레드가 코어보다 많을 때 상대에게 코어를 넘기기 위해). deadline_ns 를 넘기면 false. */
static bool spin_until_eq(_Atomic uint64_t *p, uint64_t want, uint64_t deadline_ns) {
    (void)p; (void)want; (void)deadline_ns;
    return false; /* TODO: acquire load 로 돌기, 4096번마다 마감 확인 + sched_yield */
}

enum { WAIT_SPIN, WAIT_BLOCK };

/* 핑퐁의 한쪽. 라운드 k(1..rounds)에서
 *   initiator: post_to ← pmul*k + poff 를 쓰고, wait_on == wmul*k + woff 가 될 때까지 기다린다.
 *   responder: wait_on == wmul*k + woff 를 기다린 뒤, post_to ← pmul*k + poff 를 쓴다.
 * 기다림이 성공할 때마다 seen[k]++ (seen 은 rounds+1 칸). 끝낸 라운드 수를 돌려준다 (시간 초과면 거기서 멈춤).
 *   한 라인: 핑 {post=seq 2k-1, wait=seq 2k},  퐁 {wait=seq 2k-1, post=seq 2k}
 *   깃발 둘: 핑 {post=a k, wait=b k},           퐁 {wait=a k, post=b k} */
typedef struct {
    _Atomic uint64_t *post_to, *wait_on;
    uint64_t pmul, poff, wmul, woff;
    bool initiator;
    int mode;              /* WAIT_SPIN | WAIT_BLOCK */
    uint64_t rounds;
    uint32_t *seen;
    uint64_t deadline_ns;
} pp_side_t;

static bool blk_wait_eq(_Atomic uint64_t *p, uint64_t want, uint64_t deadline_ns);
static void blk_post(_Atomic uint64_t *p, uint64_t v);

static bool side_wait(const pp_side_t *s, uint64_t want) {
    return s->mode == WAIT_BLOCK ? blk_wait_eq(s->wait_on, want, s->deadline_ns)
                                 : spin_until_eq(s->wait_on, want, s->deadline_ns);
}
static void side_post(const pp_side_t *s, uint64_t v) {
    if (s->mode == WAIT_BLOCK) blk_post(s->post_to, v);
    else atomic_store_explicit(s->post_to, v, memory_order_release);
}

static uint64_t pp_run(const pp_side_t *s) {
    (void)s; (void)side_wait; (void)side_post;   /* side_wait / side_post 를 써서 구현 */
    return 0; /* TODO: k = 1..rounds — initiator 는 post → wait, responder 는 wait → post, 성공하면 seen[k]++ */
}

/* ======================= Part B — 배치(레이아웃) ======================= */
/* 두 주소가 크기 line 짜리 정렬 블록 하나에 들어 있으면 true (line 은 2의 거듭제곱이 아니어도 됨) */
static bool same_line(const void *a, const void *b, size_t line) {
    (void)a; (void)b; (void)line;
    return false; /* TODO */
}

typedef struct { _Atomic uint64_t a, b; } two_packed_t;                         /* 8 B 거리 */
typedef struct { _Alignas(LINE) _Atomic uint64_t a; _Alignas(LINE) _Atomic uint64_t b; } two_padded_t;   /* 128 B */
typedef struct { _Alignas(LINE) _Atomic uint64_t a; char pad[56]; _Atomic uint64_t b; } two_half_t;     /* 64 B */

/* ======================= Part B — 잠드는 핸드오프 ======================= */
/* *p == want 가 될 때까지 기다리되, 바쁘게 돌지 않고 커널에서 잔다.
 * os_sync_wait_on_address 는 "*p 가 아직 v 이면 잔다" 를 커널 안에서 원자적으로 확인한다
 * → post 가 load 와 wait 사이에 끼어도 v 와 달라져 있어서 바로 돌아온다 (lost wakeup 없음).
 * EINTR / EFAULT(일시적) / ETIMEDOUT 은 다시 읽고 반복. deadline_ns 를 넘기면 false. */
static bool blk_wait_eq(_Atomic uint64_t *p, uint64_t want, uint64_t deadline_ns) {
    (void)p; (void)want; (void)deadline_ns;
    return false; /* TODO: load → 같으면 true → 아니면 그 값 v 로 os_sync_wait_on_address_with_timeout → 반복 */
}

/* 값을 release 로 쓰고 기다리는 스레드 하나를 깨운다 (기다리는 이가 없으면 ENOENT — 정상) */
static void blk_post(_Atomic uint64_t *p, uint64_t v) {
    (void)p; (void)v; /* TODO: release store + os_sync_wake_by_address_any */
}

/* ======================= Part C — 듀얼 코어 메일박스 RPC ======================= */
/* 코어 0(클라이언트)이 명령을 보내고 코어 1(서버)이 계산해 답한다. 요청과 응답은 서로 다른 라인.
 * payload 는 평범한 변수 — seq 의 release store / acquire load 가 순서를 보장한다 (C-A03 publish). */
typedef struct {
    _Alignas(LINE) struct { _Atomic uint64_t seq; uint32_t cmd; uint32_t arg[4]; } req;
    _Alignas(LINE) struct { _Atomic uint64_t seq; uint32_t status; uint32_t result; } rsp;
} mbx_t;

/* 서버가 하는 계산 (주어진 코드). cmd 0 = 합, 1 = 가중합, 그 외 = 상태 1(알 수 없는 명령) */
static uint32_t mbx_compute(uint32_t cmd, const uint32_t a[4], uint32_t *status) {
    *status = 0;
    if (cmd == 0) return a[0] + a[1] + a[2] + a[3];
    if (cmd == 1) return a[0] + 3u * a[1] + 5u * a[2] + 7u * a[3];
    *status = 1;
    return 0;
}

/* 클라이언트: 직전 호출의 답을 이미 받았다고 가정. payload 를 쓰고 req.seq 를 +1 (release),
 * rsp.seq 가 같은 번호가 될 때까지 spin_until_eq. 답(result)과 status 를 돌려준다.
 * 시간 초과면 false. */
static bool mbx_call(mbx_t *m, uint32_t cmd, const uint32_t arg[4], uint32_t *result, uint32_t *status,
                     uint64_t deadline_ns) {
    (void)m; (void)cmd; (void)arg; (void)result; (void)status; (void)deadline_ns;
    return false; /* TODO: payload → req.seq+1 (release) → rsp.seq 기다림 → 결과 읽기 */
}

/* 서버: req.seq 가 *last + 1 이 될 때까지 기다린 뒤 (acquire), payload 를 읽어 계산하고
 * 응답을 쓰고 rsp.seq 를 그 번호로 (release). *last 를 갱신하고 true. 시간 초과면 false. */
static bool mbx_serve_one(mbx_t *m, uint64_t *last, uint64_t deadline_ns) {
    (void)m; (void)last; (void)deadline_ns;
    return false; /* TODO: req.seq == *last+1 기다림 (acquire) → 계산 → rsp 쓰기 → rsp.seq (release) */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static void *side_thread(void *arg) { pp_side_t *s = arg; uint64_t *r = malloc(sizeof *r); *r = pp_run(s); return r; }

/* 두 쪽을 각각 스레드로 돌리고 (끝낸 라운드 수, seen 이 모두 정확히 1인가) 를 돌려준다 */
static void run_pair(pp_side_t *ping, pp_side_t *pong, uint64_t *done_ping, uint64_t *done_pong,
                     bool *once_ping, bool *once_pong) {
    pthread_t t1, t2;
    pthread_create(&t2, NULL, side_thread, pong);
    pthread_create(&t1, NULL, side_thread, ping);
    void *r1, *r2;
    pthread_join(t1, &r1); pthread_join(t2, &r2);
    *done_ping = *(uint64_t *)r1; *done_pong = *(uint64_t *)r2;
    free(r1); free(r2);
    *once_ping = *once_pong = true;
    for (uint64_t k = 1; k <= ping->rounds; k++) {
        if (ping->seen[k] != 1) *once_ping = false;
        if (pong->seen[k] != 1) *once_pong = false;
    }
}

typedef struct { qos_class_t q; bool ok; qos_class_t got; } qarg_t;
static void *qos_thread(void *arg) { qarg_t *a = arg; a->ok = qos_apply(a->q); a->got = qos_class_self(); return NULL; }

static void test_part_a(void) {
    printf("-- Part A: rt_stats / qos_apply / one-line ping-pong\n");
    /* 1 tick = 125/3 ns. 표본 하나 = 64 왕복. 3 tick = 125 ns → 왕복 1번 1953.125 ps → 버림 1953 */
    uint64_t t1[] = { 999, 3, 6, 9, 12 };                      /* 첫 표본은 워밍업으로 버린다 */
    rt_stats_t r = rt_stats(t1, 5, 1, 64, 125, 3);
    CHECK(r.min_ps == 1953 && r.p50_ps == 3906 && r.p99_ps == 7812 && r.max_ps == 7812);
    CHECK(t1[0] == 999 && t1[1] == 3);                          /* 입력은 그대로 */
    uint64_t t2[100];
    for (int i = 0; i < 100; i++) t2[i] = (uint64_t)(100 - i);  /* 100..1 */
    r = rt_stats(t2, 100, 0, 1, 1, 1);                          /* 1 tick = 1 ns, batch 1 */
    CHECK(r.min_ps == 1000 && r.p50_ps == 50000 && r.p99_ps == 99000 && r.max_ps == 100000);
    uint64_t t3[] = { 2, 2 };
    r = rt_stats(t3, 2, 2, 64, 125, 3);                         /* 남는 표본 없음 */
    CHECK(r.min_ps == 0 && r.p50_ps == 0 && r.max_ps == 0);
    r = rt_stats(t3, 2, 0, 0, 125, 3);                          /* batch 0 */
    CHECK(r.p50_ps == 0);
    uint64_t t4[] = { 1ull << 40 };                             /* tick*numer*1000 이 64비트를 넘는 크기 */
    r = rt_stats(t4, 1, 0, 1, 125, 3);
    CHECK(r.p50_ps == 45812984490666666ull);

    qos_class_t qs[3] = { QOS_CLASS_USER_INTERACTIVE, QOS_CLASS_UTILITY, QOS_CLASS_BACKGROUND };
    for (int i = 0; i < 3; i++) {
        qarg_t a = { qs[i], false, QOS_CLASS_UNSPECIFIED };
        pthread_t t; pthread_create(&t, NULL, qos_thread, &a); pthread_join(t, NULL);
        CHECK(a.ok && a.got == qs[i]);
    }

    enum { R = 20000 };
    static _Alignas(LINE) _Atomic uint64_t seq;
    static uint32_t seen_p[R + 1], seen_q[R + 1];
    atomic_store(&seq, 0);
    uint64_t dl = now_ns() + 10000000000ull;   /* 10 s */
    /* poff = -1 (부호 없는 정수 wrap) → 2k + (2^64 - 1) = 2k - 1 */
    pp_side_t ping = { &seq, &seq, 2, (uint64_t)-1, 2, 0, true, WAIT_SPIN, R, seen_p, dl };
    pp_side_t pong = { &seq, &seq, 2, 0, 2, (uint64_t)-1, false, WAIT_SPIN, R, seen_q, dl };
    uint64_t dp, dq; bool op, oq;
    run_pair(&ping, &pong, &dp, &dq, &op, &oq);
    CHECK(dp == R && dq == R);
    CHECK(op && oq);                                            /* 양쪽 모두 모든 순번을 정확히 한 번 */
    CHECK(atomic_load(&seq) == 2ull * R);

    static _Atomic uint64_t never;
    atomic_store(&never, 7);
    uint64_t t0 = now_ns();
    CHECK(!spin_until_eq(&never, 8, t0 + 20000000));            /* 안 오는 값: 20 ms 뒤 false (멈추지 않음) */
    CHECK(spin_until_eq(&never, 7, t0));                        /* 이미 같으면 마감이 지났어도 true */
}

static void test_part_b(void) {
    printf("-- Part B: layout / two flags / blocking handoff\n");
    static two_packed_t pk; static two_padded_t pd; static two_half_t hf;
    CHECK(same_line(&pk.a, &pk.b, LINE));
    CHECK(!same_line(&pd.a, &pd.b, LINE));                                             /* 128 떨어짐 */
    CHECK(same_line(&hf.a, &hf.b, 128) && !same_line(&hf.a, &hf.b, 64));             /* 64 떨어짐 */
    CHECK(!same_line(&pk.a, &pk.b, 0));
    CHECK(sizeof(two_padded_t) == 2 * LINE && _Alignof(two_padded_t) == LINE);

    enum { R = 20000 };
    static uint32_t sp[R + 1], sq[R + 1];
    atomic_store(&pd.a, 0); atomic_store(&pd.b, 0);
    uint64_t dl = now_ns() + 10000000000ull;
    pp_side_t ping = { &pd.a, &pd.b, 1, 0, 1, 0, true, WAIT_SPIN, R, sp, dl };
    pp_side_t pong = { &pd.b, &pd.a, 1, 0, 1, 0, false, WAIT_SPIN, R, sq, dl };
    uint64_t dp, dq; bool op, oq;
    run_pair(&ping, &pong, &dp, &dq, &op, &oq);
    CHECK(dp == R && dq == R && op && oq);
    CHECK(atomic_load(&pd.a) == R && atomic_load(&pd.b) == R);

    static _Alignas(LINE) _Atomic uint64_t w;
    atomic_store(&w, 5);
    CHECK(blk_wait_eq(&w, 5, now_ns()));                        /* 이미 같음 → 바로 true */
    blk_post(&w, 6);                                            /* 기다리는 이 없이 먼저 post (ENOENT, 무해) */
    CHECK(blk_wait_eq(&w, 6, now_ns() + 1000000));              /* 늦게 온 waiter 도 놓치지 않는다 */
    uint64_t t0 = now_ns();
    CHECK(!blk_wait_eq(&w, 99, t0 + 30000000));                 /* 30 ms 시간 초과 → false */
    CHECK(now_ns() - t0 >= 25000000);                           /* 실제로 잤다 (마감 전에 포기하지 않음) */

    enum { RB = 3000 };
    static _Alignas(LINE) _Atomic uint64_t bs;
    static uint32_t bp[RB + 1], bq[RB + 1];
    atomic_store(&bs, 0);
    dl = now_ns() + 10000000000ull;
    pp_side_t bping = { &bs, &bs, 2, (uint64_t)-1, 2, 0, true, WAIT_BLOCK, RB, bp, dl };
    pp_side_t bpong = { &bs, &bs, 2, 0, 2, (uint64_t)-1, false, WAIT_BLOCK, RB, bq, dl };
    run_pair(&bping, &bpong, &dp, &dq, &op, &oq);
    CHECK(dp == RB && dq == RB && op && oq);
    CHECK(atomic_load(&bs) == 2ull * RB);
}

enum { NCALL = 20000 };
typedef struct { mbx_t *m; uint64_t served, bad_seq; } srv_t;
static void *server(void *arg) {
    srv_t *s = arg;
    uint64_t last = 0, dl = now_ns() + 10000000000ull;
    for (int i = 0; i < NCALL; i++) {
        uint64_t before = last;
        if (!mbx_serve_one(s->m, &last, dl)) break;
        if (last != before + 1) s->bad_seq++;
        s->served++;
    }
    return NULL;
}

static void test_part_c(void) {
    printf("-- Part C: dual-core mailbox RPC\n");
    static mbx_t m;
    CHECK(!same_line(&m.req.seq, &m.rsp.seq, LINE));
    CHECK(_Alignof(mbx_t) == LINE && sizeof m.req <= LINE);
    srv_t s = { &m, 0, 0 };
    pthread_t t; pthread_create(&t, NULL, server, &s);
    uint32_t seed = 0xC0FFEEu, ok = 0, st_ok = 0, calls = 0;
    uint64_t dl = now_ns() + 10000000000ull;
    for (int i = 0; i < NCALL; i++) {
        uint32_t a[4] = { xs32(&seed) & 0xFFFF, xs32(&seed) & 0xFFFF, xs32(&seed) & 0xFFFF, xs32(&seed) & 0xFFFF };
        uint32_t cmd = (uint32_t)i % 3u, res = 0xDEAD, st = 9, est, exp = mbx_compute(cmd, a, &est);
        if (!mbx_call(&m, cmd, a, &res, &st, dl)) break;
        calls++;
        if (res == exp) ok++;
        if (st == est) st_ok++;
    }
    pthread_join(t, NULL);
    CHECK(calls == NCALL && s.served == NCALL);
    CHECK(ok == NCALL && st_ok == NCALL);                       /* 답이 모두 그 요청의 답 (이전 요청 것 아님) */
    CHECK(s.bad_seq == 0);
    CHECK(atomic_load(&m.req.seq) == NCALL && atomic_load(&m.rsp.seq) == NCALL);
}

int main(void) {
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
