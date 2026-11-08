/* SIV-M09 동시성 스트레스 테스트 설계
 *
 * 가상의 SoC "KES1" 을 host 에서 흉내 낸다. 공유 자원 = DDR(g_mem), DMA 2채널이 함께 쓰는 완료 큐(CQ),
 * INT_STS(W1C) 상태 워드. 트래픽 agent 는 pthread 하나씩이다:
 *   CPU0/CPU1 = 자기 영역 memtest (소유자 시그니처 checker)
 *   DMA0/DMA1 = 블록 복사 + 엔진 CRC32 → CQ 에 완료 엔트리 → INT_STS.DONEx (ISR 이 태그 순서·CRC checker)
 *   TMR       = 주기 이벤트, TMR_SEQ 갱신 → INT_STS.TMR, ISR 의 ack 를 기다림 (lost/spurious checker)
 *   ISR       = INT_STS 를 W1C 로 받아 처리
 * 결함 3종(run_cfg_t.bug): cq_claim (CQ 티켓을 load/store 두 번으로 claim), irq_rmw (INT_STS 를
 * 읽고-OR-쓰기로 set), dma_overrun (DMA1 쓰기 꼬리 beat 반복). 모두 단독 실행에서는 안 보인다.
 * 실행 모드: det  = 시드 고정 토큰 스케줄러 — 진짜 스레드지만 한 번에 하나만 달리고, 시드가 같으면
 *                   인터리빙도 같다 (재현·리플레이).
 *            free = 진짜 동시 실행 (stress).
 * 채점 테스트(인자 없이 실행)는 전부 결정적이다.
 *   ./solution --stress <none|cq_claim|irq_rmw|dma_overrun> [--seconds S] [--period-us P] [--window W] [--seed N]
 */
#define _POSIX_C_SOURCE 200809L   /* -std=c11 에서 clock_gettime/nanosleep/sched_yield 선언 */
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ================= SoC 메모리 맵 (워드 인덱스; 바이트 주소 = DDR_BASE + 4*idx) ================= */
#define DDR_BASE  0x80000000u
#define BLK       8u        /* 디스크립터 1개 = 8 워드 */
#define NBLK      8u        /* 채널마다 블록 8개, seq % NBLK 로 재사용 */
#define SRC0      0x000u
#define SRC1      0x040u
#define DST0      0x080u
#define DST1      0x0C0u    /* DST1 끝(0x0FF) 바로 뒤가 CPU0 영역 */
#define CPU0_BASE 0x100u
#define CPU1_BASE 0x120u
#define CPU_W     32u
#define MEM_WORDS 0x140u
#define CQ_SIZE   4u        /* 2의 거듭제곱, NBLK-1 이하여야 블록 재사용 전에 ISR 검사가 끝난다 */

#define IRQ_DONE0 (1u << 0)
#define IRQ_DONE1 (1u << 1)
#define IRQ_TMR   (1u << 2)

enum { A_CPU0, A_CPU1, A_DMA0, A_DMA1, A_TMR, A_ISR, NAG };
static const char *const AG_NAME[NAG] = {"CPU0", "CPU1", "DMA0", "DMA1", "TMR", "ISR"};
static const uint32_t AG_OWNER[NAG] = {0xC0, 0xC1, 0xD0, 0xD1, 0, 0};   /* 시그니처 최상위 바이트 */
#define AG(a)   (1u << (a))
#define CFG_ALL (AG(A_CPU0) | AG(A_CPU1) | AG(A_DMA0) | AG(A_DMA1) | AG(A_TMR))
#define TAG(ch, seq) ((AG_OWNER[A_DMA0 + (ch)] << 24) | ((uint32_t)(seq) & 0xFFFFFFu))

enum { BUG_NONE, BUG_CQ_CLAIM, BUG_IRQ_RMW, BUG_DMA_OVERRUN, NBUG };
static const char *const BUG_NAME[NBUG] = {"none", "cq_claim", "irq_rmw", "dma_overrun"};

enum { F_NONE, F_FOREIGN, F_CORRUPT, F_MISSING, F_DUP, F_CRC_ENGINE, F_CRC_DST, F_LOST_IRQ, F_SPURIOUS, F_HANG, NKIND };
static const char *const KIND_NAME[NKIND] = {"NONE", "FOREIGN_WRITE", "CORRUPT", "MISSING", "DUP",
                                             "CRC_ENGINE", "CRC_DST", "LOST_IRQ", "SPURIOUS_IRQ", "HANG"};

enum { Y_CPU_WR, Y_CPU_RD, Y_ITER, Y_COPY, Y_CQ, Y_RACE, Y_POLL, Y_TICK };   /* yield 지점 종류 */

typedef struct {
    int bug;
    unsigned agents;       /* AG() 마스크. DMA/TMR 이 있으면 ISR 은 자동 */
    bool det;              /* true = 시드 고정 토큰 스케줄러 */
    uint32_t seed;
    unsigned cpu_iters, descs, ticks;   /* 0 = 시간이 끝날 때까지 (free) */
    double seconds;        /* free 에서 작업 수가 0 일 때 실행 시간 */
    unsigned window;       /* free: race window 안 spin 횟수 (경쟁 창 넓히기) */
    unsigned period_us;    /* free: DMA 디스크립터 / TMR tick 사이 간격 (0 = 쉬지 않고) */
    uint64_t max_polls;    /* 대기 루프 예산 (det / hook 테스트), 0 = 무제한 */
    unsigned timeout_ms;   /* free 대기 루프 벽시계 예산 */
    uint64_t max_steps;    /* det 스케줄러 전체 예산 (백스톱) */
} run_cfg_t;

typedef struct {
    int kind, agent;       /* agent = 검출한 checker (HANG 이면 멈춘 agent, -1 = 스케줄러) */
    bool det;
    uint64_t when;         /* det: 스케줄러 step, free: 시작 후 µs */
    uint32_t addr, exp, act;
    char detail[112];
} fail_t;

typedef struct {
    unsigned cpu_iters[2], dma_done[2], dma_checked[2];
    unsigned tmr_ticks, tmr_acked, irq_taken, empty_irq;
    uint64_t steps;
    double secs;
    unsigned nfail;
    fail_t first;
} run_result_t;

typedef struct { uint32_t tag, len, crc; } cq_entry_t;
typedef struct { _Atomic uint32_t seq; cq_entry_t e; } cq_slot_t;

/* ================= 공유 상태 (SoC + 하네스) ================= */
static _Atomic uint32_t g_mem[MEM_WORDS];             /* 버스 = 워드 단위 원자적 접근 (relaxed) */
static _Atomic uint32_t g_sts, g_tmr_seq, g_tmr_acked;
static struct { cq_slot_t s[CQ_SIZE]; _Atomic uint32_t wr; uint32_t rd; } g_cq;
static _Atomic int g_done[NAG];
static atomic_bool g_abort, g_stop;
static run_cfg_t g_cfg;
static run_result_t g_res;
static uint32_t g_next[2];                            /* ISR 전용: 채널별 다음 기대 seq */
static pthread_mutex_t g_fail_mu = PTHREAD_MUTEX_INITIALIZER;
static fail_t g_fail;
static atomic_uint g_nfail;
static uint64_t g_t0;
static void (*g_hook)(int me, int site);              /* 단일 스레드 테스트용 인터리빙 주입 */

static struct {                                       /* det 토큰 스케줄러 */
    pthread_mutex_t mu;
    pthread_cond_t cv[NAG];
    int turn;
    bool live[NAG];
    uint32_t rng;
    uint64_t steps;
} S;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
static uint32_t xs32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

__attribute__((format(printf, 6, 7)))
static void fail(int kind, int agent, uint32_t addr, uint32_t exp, uint32_t act, const char *fmt, ...) {
    pthread_mutex_lock(&g_fail_mu);
    if (atomic_fetch_add(&g_nfail, 1) == 0) {         /* 첫 실패만 자세히 (뒤는 개수만) */
        g_fail.kind = kind; g_fail.agent = agent; g_fail.det = g_cfg.det;
        g_fail.when = g_cfg.det ? S.steps : (now_ns() - g_t0) / 1000u;
        g_fail.addr = addr; g_fail.exp = exp; g_fail.act = act;
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(g_fail.detail, sizeof g_fail.detail, fmt, ap);
        va_end(ap);
    }
    pthread_mutex_unlock(&g_fail_mu);
    atomic_store(&g_abort, true);                     /* stop-on-first-fail: 상태를 보존한다 */
}

/* ---- det 스케줄러: 토큰을 가진 스레드만 달린다. 다음 차례는 시드 고정 xorshift 로 고른다 ---- */
static void sched_pick_locked(void) {
    int cand[NAG], n = 0;
    for (int a = 0; a < NAG; a++)
        if (S.live[a]) cand[n++] = a;
    if (n == 0) { S.turn = -1; return; }
    S.turn = cand[xs32(&S.rng) % (uint32_t)n];
    if (++S.steps == g_cfg.max_steps)
        fail(F_HANG, -1, 0, 0, 0, "det scheduler step budget %llu exhausted", (unsigned long long)g_cfg.max_steps);
    pthread_cond_signal(&S.cv[S.turn]);
}
static void sched_enter(int me) {
    if (!g_cfg.det) return;
    pthread_mutex_lock(&S.mu);
    while (S.turn != me) pthread_cond_wait(&S.cv[me], &S.mu);
    pthread_mutex_unlock(&S.mu);
}
static void sched_exit(int me) {
    if (!g_cfg.det) return;
    pthread_mutex_lock(&S.mu);
    S.live[me] = false;
    sched_pick_locked();
    pthread_mutex_unlock(&S.mu);
}
static void yield_point(int me, int site) {
    if (!g_cfg.det) { if (g_hook) g_hook(me, site); return; }
    pthread_mutex_lock(&S.mu);
    sched_pick_locked();
    while (S.turn != me) pthread_cond_wait(&S.cv[me], &S.mu);
    pthread_mutex_unlock(&S.mu);
}
/* 결함 코드의 "읽기와 쓰기 사이". det/hook: 다른 agent 가 끼어들 수 있는 지점, free: 창 넓히기 spin */
static void race_window(int me) {
    if (g_cfg.det || g_hook) { yield_point(me, Y_RACE); return; }
    for (volatile unsigned i = 0; i < g_cfg.window; i++) { }
}

/* free 모드 생산자 속도 조절: 다음 이벤트 시각까지 spin (det 에서는 아무것도 안 함) */
static void pace(uint64_t *next) {
    if (g_cfg.det || !g_cfg.period_us) return;
    *next += (uint64_t)g_cfg.period_us * 1000u;
    while (now_ns() < *next && !atomic_load(&g_abort) && !atomic_load(&g_stop)) { }
}

typedef struct { uint64_t polls, t0; } waiter_t;
static waiter_t waiter_start(void) { waiter_t w = {0, g_cfg.det ? 0 : now_ns()}; return w; }
/* 기다릴 수 있으면 true. 예산(det: poll 횟수, free: 벽시계) 초과나 abort 면 false */
static bool wait_more(int me, waiter_t *w) {
    if (atomic_load(&g_abort)) return false;
    w->polls++;
    if (g_cfg.max_polls && w->polls > g_cfg.max_polls) return false;
    if (g_cfg.det || g_hook) { yield_point(me, Y_POLL); return true; }
    if (g_cfg.timeout_ms && now_ns() - w->t0 > (uint64_t)g_cfg.timeout_ms * 1000000u) return false;
    sched_yield();
    return true;
}

/* ================= 버스 · 시그니처 · CRC (주어짐) ================= */
static inline uint32_t mem_rd(uint32_t w) { return atomic_load_explicit(&g_mem[w], memory_order_relaxed); }
static inline void mem_wr(uint32_t w, uint32_t v) { atomic_store_explicit(&g_mem[w], v, memory_order_relaxed); }
static uint32_t baddr(uint32_t w) { return DDR_BASE + 4u * w; }

/* [31:24] = 쓴 agent, [23:0] = (seq, idx, seed) 해시 → 깨진 워드를 보면 "누가 썼나" 를 안다 */
static uint32_t sig_word(int agent, uint32_t seq, uint32_t idx) {
    uint32_t x = (seq + g_cfg.seed) * 0x9E3779B1u ^ (idx + 1u) * 0x85EBCA77u;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12;
    return AG_OWNER[agent] << 24 | (x & 0x00FFFFFFu);
}
static int sig_owner(uint32_t v) {
    for (int a = A_CPU0; a <= A_DMA1; a++)
        if (v >> 24 == AG_OWNER[a]) return a;
    return -1;
}
/* CRC-32 (IEEE 802.3, reflected 0xEDB88320, init/xorout 0xFFFFFFFF) — SIV-M02 와 같은 정의 */
static uint32_t crc32_byte(uint32_t c, uint8_t b) {
    c ^= b;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    return c;
}
static uint32_t crc32_word(uint32_t c, uint32_t w) {   /* 워드를 little-endian 바이트 순서로 */
    for (int k = 0; k < 4; k++) c = crc32_byte(c, (uint8_t)(w >> (8 * k)));
    return c;
}
static uint32_t crc_expected(int ch, uint32_t seq) {   /* 생성기 정의에서 계산 — 메모리를 안 본다 */
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < BLK; i++) c = crc32_word(c, sig_word(A_DMA0 + ch, seq, i));
    return ~c;
}

/* ================= 공유 자원 모델 (주어짐, 결함 스위치 포함) ================= */
static void sts_set(int me, uint32_t bits) {           /* 이벤트 → INT_STS 비트 세우기 */
    if (g_cfg.bug == BUG_IRQ_RMW) {                    /* 결함: 공유 워드를 읽고-OR-쓰기 (두 번의 접근) */
        uint32_t v = atomic_load(&g_sts);
        race_window(me);
        atomic_store(&g_sts, v | bits);
    } else {
        atomic_fetch_or(&g_sts, bits);                 /* 정상: 한 번의 원자적 set (HW: 비트별 set 로직) */
    }
}
static uint32_t cq_claim(int me) {                     /* CQ 티켓(쓰기 위치) 받기 */
    if (g_cfg.bug == BUG_CQ_CLAIM) {                   /* 결함: wr 을 load → store 로 증가 */
        uint32_t w = atomic_load(&g_cq.wr);
        race_window(me);
        atomic_store(&g_cq.wr, w + 1);
        return w;
    }
    return atomic_fetch_add(&g_cq.wr, 1);
}
static uint32_t dma_copy(int me, uint32_t src, uint32_t dst, uint32_t n) {   /* DMA 엔진: 복사 + 읽기 쪽 CRC */
    uint32_t crc = 0xFFFFFFFFu, v = 0;
    for (uint32_t i = 0; i < n; i++) {
        v = mem_rd(src + i);
        crc = crc32_word(crc, v);
        mem_wr(dst + i, v);
        yield_point(me, Y_COPY);
    }
    if (g_cfg.bug == BUG_DMA_OVERRUN && me == A_DMA1)  /* 결함: 쓰기 strobe 가 꼬리 1 beat 더 열림 */
        mem_wr(dst + n, v);
    return ~crc;
}

/* ================= Part A: 트래픽 checker 와 CQ / W1C ================= */

/* TODO(A1): CPU memtest 한 바퀴. 영역의 모든 워드에 sig_word(me, iter, i) 를 쓰고(쓸 때마다
 * yield_point(me, Y_CPU_WR)), 다시 전부 읽어 비교한다(읽을 때마다 yield_point(me, Y_CPU_RD)).
 * 틀린 워드마다 fail(): 실제값의 소유자가 다른 agent 면 F_FOREIGN ("written by <agent>" 포함),
 * 아니면 F_CORRUPT. addr 는 바이트 주소. 틀린 워드 수를 돌려준다. */
static unsigned cpu_agent_pass(int me, uint32_t iter) {
    (void)me; (void)iter;
    return 0;
}

/* CQ: 슬롯마다 seq. 빈 슬롯 = 티켓 w 를 기다리는 seq==w, 찬 슬롯 = seq==w+1. */
static bool cq_push(int me, uint32_t tag, uint32_t len, uint32_t crc) {
    uint32_t w = cq_claim(me);
    cq_slot_t *sl = &g_cq.s[w & (CQ_SIZE - 1)];
    /* TODO(A2): sl->seq 가 w 가 될 때까지 wait_more() 로 기다린다(acquire). 예산 초과면 abort 가 아닐 때
     * F_HANG(agent=me, exp=w, act=현재 seq) 을 기록하고 false. 슬롯을 얻으면 yield_point(me, Y_CQ) 뒤
     * 엔트리를 쓰고 seq = w+1 을 release 로 publish 한다. */
    (void)sl; (void)tag; (void)len; (void)crc;
    return false;
}
/* TODO(A3): 소비자(ISR) 쪽. cq_peek = rd 위치 슬롯이 찼으면(seq==rd+1, acquire) 복사해 true.
 * cq_release = 검사가 끝난 뒤 그 슬롯을 다음 바퀴 티켓(rd+CQ_SIZE)에 release 로 돌려주고 rd++. */
static bool cq_peek(cq_entry_t *out) {
    (void)out;
    return false;
}
static void cq_release(void) {
}

/* TODO(A4): W1C 로 인터럽트 받기. INT_STS 를 읽고, 읽은 비트만 1 을 써서 지운다
 * (그 사이 새로 선 비트는 남아야 한다). 읽은 비트를 돌려준다. */
static uint32_t irq_take(void) {
    return 0;
}

/* TODO(A5): DMA 완료 엔트리 checker. 태그 → (ch, seq). 채널 순서: seq > 기대 → F_MISSING,
 * seq < 기대 → F_DUP (잘못된 태그도 F_DUP). 엔진 CRC/len 이 생성기 기대와 다르면 F_CRC_ENGINE.
 * dst 블록(DSTx + (seq%NBLK)*BLK)의 CRC 가 다르면 첫 번째 틀린 워드 주소·값으로 F_CRC_DST.
 * 통과하면 g_next[ch] = seq+1, g_res.dma_checked[ch]++. 판정(F_*)을 돌려준다. */
static int dma_check_entry(const cq_entry_t *e) {
    (void)e;
    return F_NONE;
}

/* ================= ISR 쪽 (주어짐) ================= */
static bool producers_running(void) {
    return !atomic_load(&g_done[A_DMA0]) || !atomic_load(&g_done[A_DMA1]) || !atomic_load(&g_done[A_TMR]);
}
static void isr_tmr(void) {
    uint32_t seq = atomic_load(&g_tmr_seq), acked = atomic_load(&g_tmr_acked);
    if (seq == acked) {
        fail(F_SPURIOUS, A_ISR, 0, acked + 1, seq, "INT_STS.TMR set but TMR_SEQ still %u (already acked)", seq);
        return;
    }
    atomic_store(&g_tmr_acked, seq);
    g_res.tmr_acked++;
}
static unsigned isr_drain(void) {
    cq_entry_t e;
    unsigned n = 0;
    while (!atomic_load(&g_abort) && cq_peek(&e)) {
        dma_check_entry(&e);
        cq_release();          /* 검사가 끝난 뒤에 슬롯 반환 → 그 전에는 블록이 재사용되지 않는다 */
        n++;
    }
    return n;
}

/* ================= Part B: 인터리빙 전수 탐색 · 검출 통계 ================= */
typedef struct { unsigned schedules, lost; } explore_res_t;

/* TODO(B1): k 개 agent 가 공유 워드 x 에 자기 비트(1<<a)를 세운다. atomic 이면 프로그램은 1단계
 * (x |= bit), 아니면 2단계 (reg[a] = x; 그다음 x = reg[a] | bit). 모든 인터리빙을 재귀로 펼쳐
 * 끝난 스케줄 수와, 끝났을 때 x 가 모든 비트를 갖지 못한(갱신 유실) 스케줄 수를 센다. */
static void explore_rec(int k, bool atomic, uint32_t x, int *pc, uint32_t *reg, explore_res_t *r) {
    (void)k; (void)atomic; (void)x; (void)pc; (void)reg; (void)r;
}
static explore_res_t explore_rmw(int k, bool atomic) {
    explore_res_t r = {0, 0};
    int pc[8] = {0};
    uint32_t reg[8] = {0};
    explore_rec(k, atomic, 0, pc, reg, &r);
    return r;
}

/* TODO(B2): 한 번 실행에서 버그가 검출될 확률이 p 일 때 n 번 안에 한 번이라도 검출될 확률,
 * 그리고 신뢰도 cl 로 검출하려면 필요한 최소 실행 수. log1p 로 작은 p 에서도 정확하게. */
static double p_detect(double p, double n) { (void)p; (void)n; return 0.0; }
static uint64_t runs_needed(double p, double cl) {
    (void)p; (void)cl;
    return 0;
}

/* ================= Part C: 종료 판정 · 리포트 ================= */

/* TODO(C1): 생산자가 모두 끝난 뒤 ISR 의 마지막 판정. 마지막 W1C·drain 이후에도 CQ 에 엔트리가 남아
 * 있으면 = DONE 비트가 사라진 것 → F_LOST_IRQ (act = 남은 엔트리 태그). 아니면 채널별로
 * g_next[ch] != g_res.dma_done[ch] 이면 F_MISSING (exp = 생산 수, act = 검사 수). */
static void isr_final(void) {
}

/* TODO(C2): 디버그 엔지니어가 바로 움직일 수 있는 한 줄.
 *   FAIL @step=812 checker=CPU0 kind=FOREIGN_WRITE addr=0x80000400 exp=... act=... diff=... | <detail> | suspect: <힌트>
 * (free 모드면 @us=..., addr 가 0 이면 addr 생략, agent<0 이면 SCHED). 쓴 길이를 돌려준다. */
static int fail_format(char *buf, size_t n, const fail_t *f) {
    (void)f;
    if (n) buf[0] = '\0';
    return 0;
}

/* ================= agent 스레드 (주어짐) ================= */
static void *cpu_agent(void *arg) {
    int me = (int)(intptr_t)arg;
    sched_enter(me);
    for (uint32_t it = 0; !atomic_load(&g_abort) && !atomic_load(&g_stop); it++) {
        if (g_cfg.cpu_iters && it >= g_cfg.cpu_iters && !producers_running()) break;
        cpu_agent_pass(me, it);
        g_res.cpu_iters[me - A_CPU0] = it + 1;
        yield_point(me, Y_ITER);
    }
    sched_exit(me);
    return NULL;
}
static void *dma_agent(void *arg) {
    int me = (int)(intptr_t)arg, ch = me - A_DMA0;
    uint32_t src = ch ? SRC1 : SRC0, dst = ch ? DST1 : DST0;
    uint64_t next = now_ns();
    sched_enter(me);
    for (uint32_t seq = 0; !atomic_load(&g_abort) && !atomic_load(&g_stop) && (!g_cfg.descs || seq < g_cfg.descs); seq++) {
        uint32_t b = (seq % NBLK) * BLK;
        for (uint32_t i = 0; i < BLK; i++) mem_wr(src + b + i, sig_word(me, seq, i));   /* 드라이버: 소스 준비 */
        uint32_t crc = dma_copy(me, src + b, dst + b, BLK);
        if (!cq_push(me, TAG(ch, seq), BLK, crc)) break;
        sts_set(me, ch ? IRQ_DONE1 : IRQ_DONE0);
        g_res.dma_done[ch] = seq + 1;
        pace(&next);
    }
    atomic_store(&g_done[me], 1);
    sched_exit(me);
    return NULL;
}
static void *tmr_agent(void *arg) {
    uint64_t next = now_ns();
    (void)arg;
    sched_enter(A_TMR);
    for (uint32_t n = 1; !atomic_load(&g_abort) && !atomic_load(&g_stop) && (!g_cfg.ticks || n <= g_cfg.ticks); n++) {
        yield_point(A_TMR, Y_TICK);
        atomic_store(&g_tmr_seq, n);                   /* TMR_SEQ 먼저, 그다음 상태 비트 */
        sts_set(A_TMR, IRQ_TMR);
        g_res.tmr_ticks = n;
        waiter_t w = waiter_start();
        while (atomic_load(&g_tmr_acked) != n) {
            if (!wait_more(A_TMR, &w)) {
                if (!atomic_load(&g_abort))
                    fail(F_LOST_IRQ, A_TMR, 0, n, atomic_load(&g_tmr_acked), "TMR seq %u raised, never acked (INT_STS=0x%X)",
                         n, atomic_load(&g_sts));
                goto out;
            }
        }
        pace(&next);
    }
out:
    atomic_store(&g_done[A_TMR], 1);
    sched_exit(A_TMR);
    return NULL;
}
static void *isr_agent(void *arg) {
    (void)arg;
    sched_enter(A_ISR);
    while (!atomic_load(&g_abort)) {
        bool fin = !producers_running();               /* W1C 전에 읽는다: 그 뒤 raise 는 이미 보인다 */
        uint32_t s = irq_take();
        if (s) g_res.irq_taken++;
        if (s & IRQ_TMR) isr_tmr();
        if ((s & (IRQ_DONE0 | IRQ_DONE1)) && isr_drain() == 0) g_res.empty_irq++;
        if (fin) {
            if (!atomic_load(&g_abort)) isr_final();
            break;
        }
        if (g_cfg.det) yield_point(A_ISR, Y_POLL);
        else if (!s) sched_yield();
    }
    sched_exit(A_ISR);
    return NULL;
}

static void sys_reset(const run_cfg_t *cfg) {
    g_cfg = *cfg;
    if (!g_cfg.seed) g_cfg.seed = 1;
    for (uint32_t w = 0; w < MEM_WORDS; w++) mem_wr(w, 0);
    atomic_store(&g_sts, 0); atomic_store(&g_tmr_seq, 0); atomic_store(&g_tmr_acked, 0);
    for (uint32_t i = 0; i < CQ_SIZE; i++) { atomic_store(&g_cq.s[i].seq, i); g_cq.s[i].e = (cq_entry_t){0, 0, 0}; }
    atomic_store(&g_cq.wr, 0); g_cq.rd = 0;
    for (int a = 0; a < NAG; a++) atomic_store(&g_done[a], !(cfg->agents & AG(a)));
    atomic_store(&g_abort, false); atomic_store(&g_stop, false);
    memset(&g_res, 0, sizeof g_res);
    g_next[0] = g_next[1] = 0;
    memset(&g_fail, 0, sizeof g_fail);
    atomic_store(&g_nfail, 0);
    g_hook = NULL;
    S.turn = -1; S.rng = g_cfg.seed; S.steps = 0;
    for (int a = 0; a < NAG; a++) S.live[a] = false;
    g_t0 = now_ns();
}

static int run_system(const run_cfg_t *cfg, run_result_t *res) {
    static void *(*const FN[NAG])(void *) = {cpu_agent, cpu_agent, dma_agent, dma_agent, tmr_agent, isr_agent};
    pthread_t th[NAG];
    sys_reset(cfg);
    unsigned ag = cfg->agents;
    if (ag & (AG(A_DMA0) | AG(A_DMA1) | AG(A_TMR))) ag |= AG(A_ISR);
    pthread_mutex_init(&S.mu, NULL);
    for (int a = 0; a < NAG; a++) pthread_cond_init(&S.cv[a], NULL);
    for (int a = 0; a < NAG; a++)
        if ((ag & AG(a)) && pthread_create(&th[a], NULL, FN[a], (void *)(intptr_t)a) != 0) {
            fprintf(stderr, "pthread_create failed\n");
            exit(2);
        }
    if (cfg->det) {
        pthread_mutex_lock(&S.mu);
        for (int a = 0; a < NAG; a++) S.live[a] = (ag & AG(a)) != 0;
        sched_pick_locked();
        pthread_mutex_unlock(&S.mu);
    } else if (cfg->seconds > 0) {
        uint64_t end = g_t0 + (uint64_t)(cfg->seconds * 1e9);
        struct timespec ms = {0, 1000000};
        while (!atomic_load(&g_abort) && now_ns() < end) nanosleep(&ms, NULL);
        atomic_store(&g_stop, true);
    }
    for (int a = 0; a < NAG; a++)
        if (ag & AG(a)) pthread_join(th[a], NULL);
    g_res.steps = S.steps;
    g_res.secs = (double)(now_ns() - g_t0) / 1e9;
    g_res.nfail = atomic_load(&g_nfail);
    g_res.first = g_fail;
    *res = g_res;
    for (int a = 0; a < NAG; a++) pthread_cond_destroy(&S.cv[a]);
    pthread_mutex_destroy(&S.mu);
    return g_res.nfail ? 1 : 0;
}

/* ================= 장시간 stress 모드 (채점 밖) ================= */
static int stress_main(int argc, char **argv) {
    run_cfg_t c = {.agents = CFG_ALL, .seed = 1, .seconds = 10, .timeout_ms = 200};
    int bug = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--stress") && i + 1 < argc) {
            i++;
            for (int b = 0; b < NBUG; b++)
                if (!strcmp(argv[i], BUG_NAME[b])) bug = b;
        } else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) c.seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--window") && i + 1 < argc) c.window = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--period-us") && i + 1 < argc) c.period_us = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) c.seed = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--timeout-ms") && i + 1 < argc) c.timeout_ms = (unsigned)strtoul(argv[++i], NULL, 0);
        else bug = -1, i = argc;
    }
    if (bug < 0) {
        fprintf(stderr, "usage: %s --stress none|cq_claim|irq_rmw|dma_overrun [--seconds S] [--period-us P] [--window W] [--seed N] [--timeout-ms T]\n", argv[0]);
        return 2;
    }
    c.bug = bug;
    run_result_t r;
    char line[512];
    int rc = run_system(&c, &r);
    printf("stress bug=%s period=%uus window=%u seed=%u limit=%.1fs\n", BUG_NAME[bug], c.period_us, c.window, c.seed, c.seconds);
    if (r.nfail) {
        fail_format(line, sizeof line, &r.first);
        printf("DETECTED at %.6f s (run ended %.3f s, %u failure records)\n%s\n", r.first.when / 1e6, r.secs, r.nfail, line);
    } else {
        printf("no failure in %.3f s\n", r.secs);
    }
    printf("traffic: cpu iters %u+%u, dma descs %u+%u (checked %u+%u), tmr ticks %u (acked %u), irqs %u (empty %u)\n",
           r.cpu_iters[0], r.cpu_iters[1], r.dma_done[0], r.dma_done[1], r.dma_checked[0], r.dma_checked[1],
           r.tmr_ticks, r.tmr_acked, r.irq_taken, r.empty_irq);
    return rc;
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static run_cfg_t det_cfg(int bug, unsigned agents, uint32_t seed) {
    return (run_cfg_t){.bug = bug, .agents = agents, .det = true, .seed = seed, .cpu_iters = 2,
                       .descs = 16, .ticks = 12, .max_polls = 400, .max_steps = 200000};
}
static void clear_fail(void) {
    memset(&g_fail, 0, sizeof g_fail);
    atomic_store(&g_nfail, 0);
    atomic_store(&g_abort, false);
}
static uint32_t crc32_str(const char *s) {
    uint32_t c = 0xFFFFFFFFu;
    while (*s) c = crc32_byte(c, (uint8_t)*s++);
    return ~c;
}
static void fill_dst(int ch, uint32_t seq) {
    uint32_t dst = (ch ? DST1 : DST0) + (seq % NBLK) * BLK;
    for (uint32_t i = 0; i < BLK; i++) mem_wr(dst + i, sig_word(A_DMA0 + ch, seq, i));
}
static bool same_fail(const fail_t *a, const fail_t *b) {
    return a->kind == b->kind && a->agent == b->agent && a->when == b->when && a->addr == b->addr &&
           a->exp == b->exp && a->act == b->act && strcmp(a->detail, b->detail) == 0;
}

static int hk_mode, hk_fired;
static unsigned hk_count;
static void test_hook(int me, int site) {
    if (hk_mode == 10 && me == A_CPU0 && site == Y_CPU_WR && ++hk_count == 4)
        mem_wr(CPU0_BASE + 2, sig_word(A_DMA1, 9, 7));                 /* 다른 마스터가 덮어씀 */
    if (hk_mode == 11 && me == A_CPU0 && site == Y_CPU_WR && ++hk_count == 4)
        mem_wr(CPU0_BASE + 1, mem_rd(CPU0_BASE + 1) ^ 0x00000100u);   /* 비트 하나 뒤집힘 */
    if (hk_fired || me != A_DMA0) return;
    if (hk_mode == 1 && site == Y_RACE) { hk_fired = 1; cq_push(A_DMA1, TAG(1, 0), BLK, 0x1111u); }
    if (hk_mode == 2 && site == Y_RACE) { hk_fired = 1; sts_set(A_TMR, IRQ_TMR); }
    if (hk_mode == 3 && site == Y_RACE) {
        hk_fired = 1;
        if (irq_take() & IRQ_TMR) isr_tmr();
    }
    if (hk_mode == 4 && site == Y_CQ) { hk_fired = 1; cq_push(A_DMA1, TAG(1, 0), BLK, 0x1111u); }
}
static void hook_setup(int bug, int mode) {
    sys_reset(&(run_cfg_t){.bug = bug, .max_polls = 20});
    g_hook = test_hook;
    hk_mode = mode; hk_fired = 0; hk_count = 0;
}

static bool kind_ok(int bug, const fail_t *f) {
    switch (bug) {
    case BUG_CQ_CLAIM:
        return f->kind == F_MISSING || f->kind == F_DUP || f->kind == F_CRC_ENGINE || f->kind == F_CRC_DST ||
               (f->kind == F_HANG && (f->agent == A_DMA0 || f->agent == A_DMA1));
    case BUG_IRQ_RMW: return f->kind == F_LOST_IRQ || f->kind == F_SPURIOUS;
    case BUG_DMA_OVERRUN:
        return f->kind == F_FOREIGN && f->agent == A_CPU0 && f->addr == baddr(CPU0_BASE) && sig_owner(f->act) == A_DMA1;
    default: return false;
    }
}

int main(int argc, char **argv) {
    if (argc > 1) return stress_main(argc, argv);
    run_cfg_t c;
    run_result_t r, r2;
    cq_entry_t e;
    char line[512];

    /* ---- Part A: 부품 (단일 스레드) ---- */
    sys_reset(&(run_cfg_t){.max_polls = 20});
    CHECK(crc32_str("123456789") == 0xCBF43926u);
    CHECK(sig_owner(sig_word(A_DMA1, 5, 3)) == A_DMA1 && sig_owner(sig_word(A_CPU0, 0, 0)) == A_CPU0 && sig_owner(0x12345678u) == -1);
    CHECK(cpu_agent_pass(A_CPU0, 0) == 0 && mem_rd(CPU0_BASE + 5) == sig_word(A_CPU0, 0, 5) && atomic_load(&g_nfail) == 0);
    CHECK(cpu_agent_pass(A_CPU1, 1) == 0 && mem_rd(CPU1_BASE + CPU_W - 1) == sig_word(A_CPU1, 1, CPU_W - 1));
    hook_setup(BUG_NONE, 10);                          /* 쓰기 도중 DMA1 데이터가 CPU0 영역에 들어옴 */
    CHECK(cpu_agent_pass(A_CPU0, 3) == 1);
    CHECK(g_fail.kind == F_FOREIGN && g_fail.agent == A_CPU0 && g_fail.addr == 0x80000408u &&
          g_fail.act == sig_word(A_DMA1, 9, 7) && strstr(g_fail.detail, "written by DMA1") != NULL);
    hook_setup(BUG_NONE, 11);                          /* 자기 데이터의 비트 하나가 뒤집힘 */
    CHECK(cpu_agent_pass(A_CPU0, 3) == 1 && g_fail.kind == F_CORRUPT && g_fail.addr == 0x80000404u &&
          (g_fail.exp ^ g_fail.act) == 0x00000100u);

    sys_reset(&(run_cfg_t){.max_polls = 20});
    CHECK(cq_push(A_DMA0, TAG(0, 0), BLK, 0x11u) && cq_push(A_DMA1, TAG(1, 0), BLK, 0x22u));
    CHECK(cq_peek(&e) && e.tag == TAG(0, 0) && e.crc == 0x11u);
    cq_release();
    CHECK(cq_peek(&e) && e.tag == TAG(1, 0) && e.crc == 0x22u);
    cq_release();
    CHECK(!cq_peek(&e));
    bool ok = true;
    for (uint32_t i = 0; i < 10; i++) {                /* 크기 4 링을 두 바퀴 넘게 */
        ok = ok && cq_push(A_DMA0, TAG(0, i), BLK, i) && cq_peek(&e) && e.tag == TAG(0, i);
        cq_release();
    }
    CHECK(ok && g_cq.rd == 12 && atomic_load(&g_cq.wr) == 12);
    ok = true;
    for (uint32_t i = 0; i < CQ_SIZE; i++) ok = ok && cq_push(A_DMA1, TAG(1, i), BLK, 0);
    CHECK(ok && atomic_load(&g_nfail) == 0);
    CHECK(!cq_push(A_DMA1, TAG(1, 4), BLK, 0) && g_fail.kind == F_HANG && g_fail.agent == A_DMA1 && g_fail.exp == 16);

    sys_reset(&(run_cfg_t){.max_polls = 20});
    sts_set(A_TMR, IRQ_TMR);
    sts_set(A_DMA1, IRQ_DONE1);
    CHECK(irq_take() == (IRQ_TMR | IRQ_DONE1) && atomic_load(&g_sts) == 0 && irq_take() == 0);

    {   /* DMA 완료 checker 판정표 */
        static const struct { int ch; uint32_t seq; bool crc_ok; int bad_word; int kind; } T[] = {
            {0, 0, true, -1, F_NONE},  {0, 1, true, -1, F_NONE},      {0, 3, true, -1, F_MISSING},
            {0, 1, true, -1, F_DUP},   {0, 2, false, -1, F_CRC_ENGINE}, {0, 2, true, 5, F_CRC_DST},
            {0, 2, true, -1, F_NONE},  {1, 0, true, -1, F_NONE},      {1, 1, true, 3, F_CRC_DST},
        };
        sys_reset(&(run_cfg_t){.max_polls = 20});
        int good = 0;
        for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
            clear_fail();
            fill_dst(T[i].ch, T[i].seq);
            uint32_t dst = (T[i].ch ? DST1 : DST0) + (T[i].seq % NBLK) * BLK;
            if (T[i].bad_word >= 0) mem_wr(dst + (uint32_t)T[i].bad_word, mem_rd(dst + (uint32_t)T[i].bad_word) ^ 0x40u);
            uint32_t crc = crc_expected(T[i].ch, T[i].seq) ^ (T[i].crc_ok ? 0u : 1u);
            cq_entry_t x = {TAG(T[i].ch, T[i].seq), BLK, crc};
            int k = dma_check_entry(&x);
            good += k == T[i].kind && (k == F_NONE ? atomic_load(&g_nfail) == 0 : g_fail.kind == k);
            if (i == 5) CHECK(g_fail.addr == 0x80000254u && (g_fail.exp ^ g_fail.act) == 0x40u);
        }
        CHECK(good == (int)(sizeof T / sizeof T[0]));
        CHECK(g_next[0] == 3 && g_next[1] == 1 && g_res.dma_checked[0] == 3 && g_res.dma_checked[1] == 1);
    }

    c = det_cfg(BUG_NONE, CFG_ALL, 1);                 /* 진짜 스레드 6개, det 스케줄러 */
    CHECK(run_system(&c, &r) == 0 && r.nfail == 0);
    CHECK(r.dma_checked[0] == c.descs && r.dma_checked[1] == c.descs && r.tmr_acked == c.ticks &&
          r.cpu_iters[0] >= c.cpu_iters && r.cpu_iters[1] >= c.cpu_iters);
    printf("[A] det good run: steps=%llu cpu iters %u/%u, dma checked %u/%u, tmr acked %u, irqs %u (empty %u)\n",
           (unsigned long long)r.steps, r.cpu_iters[0], r.cpu_iters[1], r.dma_checked[0], r.dma_checked[1],
           r.tmr_acked, r.irq_taken, r.empty_irq);

    /* ---- Part B: 경쟁을 결정적으로 ---- */
    explore_res_t x2 = explore_rmw(2, false), x3 = explore_rmw(3, false), a2 = explore_rmw(2, true), a3 = explore_rmw(3, true);
    CHECK(x2.schedules == 6 && x2.lost == 4);
    CHECK(x3.schedules == 90 && x3.lost == 84);
    CHECK(a2.schedules == 2 && a2.lost == 0 && a3.schedules == 6 && a3.lost == 0);
    printf("[B] interleavings: rmw k=2 %u/%u lost, k=3 %u/%u lost; atomic k=3 %u/%u lost\n",
           x2.lost, x2.schedules, x3.lost, x3.schedules, a3.lost, a3.schedules);

    hook_setup(BUG_CQ_CLAIM, 1);                       /* DMA0 의 claim 창에 DMA1 이 끼어든다 */
    CHECK(!cq_push(A_DMA0, TAG(0, 0), BLK, 0x0000u) && hk_fired);
    CHECK(g_fail.kind == F_HANG && g_fail.agent == A_DMA0 && atomic_load(&g_cq.wr) == 1);
    CHECK(cq_peek(&e) && e.tag == TAG(1, 0));          /* 남은 건 DMA1 것뿐: DMA0 완료는 사라졌다 */
    hook_setup(BUG_NONE, 4);                           /* 정상 claim: 같은 끼어들기에도 둘 다 들어간다 */
    CHECK(cq_push(A_DMA0, TAG(0, 0), BLK, 0x0000u) && hk_fired && cq_peek(&e) && e.tag == TAG(0, 0));
    cq_release();
    CHECK(cq_peek(&e) && e.tag == TAG(1, 0) && atomic_load(&g_nfail) == 0);

    hook_setup(BUG_IRQ_RMW, 2);                        /* DMA0 RMW 창에 TMR 이 비트를 세움 → 덮어써짐 */
    sts_set(A_DMA0, IRQ_DONE0);
    CHECK(hk_fired && irq_take() == IRQ_DONE0);        /* TMR 비트 유실 */
    hook_setup(BUG_IRQ_RMW, 3);                        /* DMA0 RMW 창에 ISR 이 TMR 을 W1C 로 처리 */
    atomic_store(&g_tmr_seq, 1);
    sts_set(A_TMR, IRQ_TMR);
    sts_set(A_DMA0, IRQ_DONE0);
    uint32_t s3 = irq_take();
    CHECK(hk_fired && s3 == (IRQ_TMR | IRQ_DONE0));    /* 지운 TMR 비트가 되살아남 */
    isr_tmr();
    CHECK(g_fail.kind == F_SPURIOUS && g_fail.agent == A_ISR && g_fail.act == 1);
    hook_setup(BUG_NONE, 2);
    sts_set(A_DMA0, IRQ_DONE0);
    CHECK(!hk_fired && irq_take() == IRQ_DONE0);       /* 원자적 set 에는 끼어들 창 자체가 없다 */

    CHECK(runs_needed(0.5, 0.95) == 5 && runs_needed(0.01, 0.95) == 299 && runs_needed(0.001, 0.95) == 2995);
    CHECK(p_detect(0.01, 299) >= 0.95 && p_detect(0.01, 298) < 0.95 && fabs(p_detect(0.5, 3) - 0.875) < 1e-12);
    CHECK(runs_needed(1e-6, 0.95) == 2995731u && runs_needed(0.0, 0.95) == UINT64_MAX);

    uint32_t hit = 0;                                  /* 리플레이: 같은 시드 = 같은 실패, 같은 step */
    for (uint32_t s = 1; s <= 32 && !hit; s++) {
        c = det_cfg(BUG_IRQ_RMW, CFG_ALL, s);
        if (run_system(&c, &r)) hit = s;
    }
    CHECK(hit != 0);
    c = det_cfg(BUG_IRQ_RMW, CFG_ALL, hit);
    run_system(&c, &r2);
    CHECK(r.nfail && r2.nfail && same_fail(&r.first, &r2.first) && r.steps == r2.steps);
    fail_format(line, sizeof line, &r.first);
    printf("[B] irq_rmw first detected at seed %u (%s by %s), replay identical (steps=%llu)\n    %s\n", hit,
           KIND_NAME[r.first.kind], r.first.agent >= 0 ? AG_NAME[r.first.agent] : "SCHED", (unsigned long long)r.steps, line);
    int good_seeds = 0;
    for (uint32_t s = 1; s <= 8; s++) {
        c = det_cfg(BUG_NONE, CFG_ALL, s);
        good_seeds += run_system(&c, &r) == 0;
    }
    CHECK(good_seeds == 8);

    /* ---- Part C: 야간 회귀 — 결함 × 트래픽 조합 캠페인 (det, 시드 1..8) ---- */
    static const struct { const char *name; unsigned ag; } CF[] = {
        {"CPU", AG(A_CPU0) | AG(A_CPU1)}, {"DMA1", AG(A_DMA1)}, {"TMR", AG(A_TMR)},
        {"DMA0+1", AG(A_DMA0) | AG(A_DMA1)}, {"DMA+TMR", AG(A_DMA0) | AG(A_DMA1) | AG(A_TMR)}, {"ALL", CFG_ALL}};
    enum { NCF = 6, NSEED = 8 };
    int det[NBUG][NCF] = {{0}}, kinds_ok = 1;
    fail_t ex[NBUG];
    uint32_t ex_seed[NBUG] = {0};
    uint64_t t_c = now_ns();
    for (int b = 0; b < NBUG; b++)
        for (int k = 0; k < NCF; k++)
            for (uint32_t s = 1; s <= NSEED; s++) {
                c = det_cfg(b, CF[k].ag, s);
                if (!run_system(&c, &r)) continue;
                det[b][k]++;
                kinds_ok &= kind_ok(b, &r.first);
                if (k == NCF - 1 && !ex_seed[b]) { ex[b] = r.first; ex_seed[b] = s; }
            }
    printf("[C] detection matrix (det mode, seeds 1..%d, %.2f s)\n    %-12s", NSEED, (double)(now_ns() - t_c) / 1e9, "bug \\ cfg");
    for (int k = 0; k < NCF; k++) printf("%8s", CF[k].name);
    printf("\n");
    for (int b = 0; b < NBUG; b++) {
        printf("    %-12s", BUG_NAME[b]);
        for (int k = 0; k < NCF; k++) printf("%6d/%d", det[b][k], NSEED);
        printf("\n");
    }
    int zero_alone = 1, all_found = 1, good_clean = 1;
    for (int k = 0; k < NCF; k++) good_clean &= det[BUG_NONE][k] == 0;
    for (int b = 1; b < NBUG; b++) {
        zero_alone &= det[b][0] == 0 && det[b][1] == 0 && det[b][2] == 0;
        all_found &= det[b][NCF - 1] > 0;
    }
    CHECK(good_clean);
    CHECK(zero_alone);                                 /* 단독 트래픽으로는 셋 다 안 보인다 */
    CHECK(all_found);                                  /* 전부 같이 돌리면 셋 다 잡힌다 */
    CHECK(kinds_ok);                                   /* 잡혔을 때 맞는 checker 가 맞는 이름으로 */
    CHECK(det[BUG_DMA_OVERRUN][3] == 0 && det[BUG_DMA_OVERRUN][4] == 0);   /* CPU checker 없이는 못 봄 */
    for (int b = 1; b < NBUG; b++)
        if (ex_seed[b]) {
            fail_format(line, sizeof line, &ex[b]);
            printf("    %s seed %u: %s\n", BUG_NAME[b], ex_seed[b], line);
            printf("      p_hat=%.3f/run -> %llu runs for 95%%\n", det[b][NCF - 1] / (double)NSEED,
                   (unsigned long long)runs_needed(det[b][NCF - 1] / (double)NSEED, 0.95));
        }
    fail_format(line, sizeof line, &ex[BUG_DMA_OVERRUN]);
    CHECK(strstr(line, "FAIL @step=") == line && strstr(line, "checker=CPU0 kind=FOREIGN_WRITE addr=0x80000400") &&
          strstr(line, "written by DMA1") && strstr(line, "suspect: another master"));
    fail_t fz = {.kind = F_LOST_IRQ, .agent = A_TMR, .det = false, .when = 1234, .exp = 7, .act = 6};
    snprintf(fz.detail, sizeof fz.detail, "x");
    fail_format(line, sizeof line, &fz);
    CHECK(strcmp(line, "FAIL @us=1234 checker=TMR kind=LOST_IRQ exp=0x00000007 act=0x00000006 diff=0x00000001 | x | "
                       "suspect: status bit lost: non-atomic RMW on shared status/doorbell") == 0);

    sys_reset(&(run_cfg_t){.agents = AG(A_DMA0) | AG(A_DMA1)});   /* isr_final 단독 */
    g_res.dma_done[0] = 2; g_next[0] = 2; g_res.dma_done[1] = 3; g_next[1] = 2;
    isr_final();
    CHECK(g_fail.kind == F_MISSING && g_fail.exp == 3 && g_fail.act == 2 && strstr(g_fail.detail, "DMA1"));
    sys_reset(&(run_cfg_t){.max_polls = 20});
    CHECK(cq_push(A_DMA0, TAG(0, 0), BLK, 0));
    isr_final();
    CHECK(g_fail.kind == F_LOST_IRQ && g_fail.act == TAG(0, 0));

    /* free 모드: 진짜 동시 실행, 정상 모델 — 작업 수가 고정이라 결과는 결정적 */
    c = (run_cfg_t){.agents = CFG_ALL, .seed = 7, .cpu_iters = 20, .descs = 3000, .ticks = 300, .timeout_ms = 2000};
    CHECK(run_system(&c, &r) == 0 && r.nfail == 0);
    CHECK(r.dma_checked[0] == 3000 && r.dma_checked[1] == 3000 && r.tmr_acked == 300 && r.cpu_iters[0] >= 20);
    printf("[C] free good run: dma %u+%u checked, tmr %u acked, cpu iters %u+%u, irqs %u (empty %u)\n",
           r.dma_checked[0], r.dma_checked[1], r.tmr_acked, r.cpu_iters[0], r.cpu_iters[1], r.irq_taken, r.empty_irq);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
