/* CAM-M11 캡처 버퍼 큐와 DMA: qbuf/dqbuf 소유권 모델
 *
 * V4L2 와 비슷한 캡처 큐를 host 에서 흉내 낸다.
 *   상태: DEQUEUED(앱) → QUEUED(드라이버) → ACTIVE(HW/DMA) → DONE(드라이버) → DEQUEUED(앱)
 *   HW 는 cap_hw_frame() 이다: SOF(대상 버퍼 결정) → 락 없이 메모리에 쓰기 → EOF(완료 IRQ).
 *   버퍼 부족 정책 3종: DROP_NEW / REUSE_LAST / OVERWRITE_OLDEST.
 *   mutex 는 실제 드라이버의 spin_lock_irqsave 자리다 (ELX-M04 참고). 버퍼 내용(mem[])은 락이 아니라
 *   "지금 주인이 누구인가" 로 보호한다.
 * 채점 테스트는 전부 결정적이다: 단일 스레드 스크립트, 또는 rendezvous 로 순서를 강제한 실제 스레드,
 * 또는 어떤 스케줄에서도 성립하는 보존 법칙(conservation)만 CHECK 한다.
 */
#define _POSIX_C_SOURCE 200809L   /* -std=c11 에서 nanosleep / clock_gettime / cond_timedwait */
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_BUF      8
#define FRAME_WORDS  64u
#define POISON_WORD  0xA5A5A5A5u
#define FRAME_TAG    0x5A000000u

/* 프레임 내용: 워드마다 seq 와 위치가 들어가므로 찢어진(torn) 프레임을 잡을 수 있다.
 * 최상위 바이트가 0x5A 라서 POISON_WORD(0xA5...) 와 절대 같지 않다. */
static inline uint32_t frame_word(uint32_t seq, uint32_t i) {
    return FRAME_TAG | ((seq & 0xFFFFu) << 8) | (i & 0xFFu);
}

typedef enum { B_DEQUEUED, B_QUEUED, B_ACTIVE, B_DONE } buf_state_t;
typedef enum { POLICY_DROP_NEW, POLICY_REUSE_LAST, POLICY_OVERWRITE_OLDEST, POLICY_COUNT } policy_t;
static const char *const POLICY_NAME[POLICY_COUNT] = {"DROP_NEW", "REUSE_LAST", "OVERWRITE_OLDEST"};

typedef struct {
    buf_state_t state;
    uint32_t seq;             /* 이 버퍼에 마지막으로 쓴 프레임 번호 */
    bool cpu_view;            /* 캐시 모델: true = CPU 가 봐도 되는 상태, false = 장치 쪽 (ARCH-M12) */
    uint32_t mem[FRAME_WORDS];
} buf_t;

typedef struct { int a[MAX_BUF + 1]; unsigned head, tail; } fifo_t;

static bool fifo_empty(const fifo_t *f) { return f->head == f->tail; }
static unsigned fifo_len(const fifo_t *f) { return (f->tail + MAX_BUF + 1 - f->head) % (MAX_BUF + 1); }
static void fifo_push(fifo_t *f, int v) { f->a[f->tail] = v; f->tail = (f->tail + 1) % (MAX_BUF + 1); }
static int fifo_pop(fifo_t *f) { int v = f->a[f->head]; f->head = (f->head + 1) % (MAX_BUF + 1); return v; }

typedef struct {
    pthread_mutex_t lock;     /* 상태·리스트·카운터 보호. 드라이버의 spin_lock_irqsave 자리 */
    pthread_cond_t  done_cv;  /* DONE 생김 / DMA 끝남 / streamoff */
    policy_t policy;
    unsigned nbuf;
    buf_t buf[MAX_BUF];
    fifo_t inq, doneq;        /* QUEUED FIFO, DONE FIFO */
    int active;               /* HW 가 쓰는 버퍼. 없으면 -1 */
    bool parked;              /* DROP_NEW: active 안에 끝난 프레임을 보관 중 */
    bool streaming, dma_busy;
    uint32_t hw_seq;          /* 다음 프레임 번호. 버린 프레임도 센다 (V4L2 sequence 와 같음) */
    uint64_t frames, dequeued, dropped, flushed;
    uint64_t poison_hits, inv_fail, sync_err, qbuf_einval;
    int last_poison_idx;
    void (*mid_dma_hook)(void *);  /* 시험용: DMA 절반 뒤에 불림 (강제 스케줄) */
    void *hook_arg;
} cap_t;

typedef struct { int index; uint32_t seq; } cap_info_t;

/* ---- 캐시 유지보수 모델: 호출 순서만 본다 (실제 clean/invalidate 는 ARCH-M12) ---- */
void cache_for_device(cap_t *c, int i) {
    if (!c->buf[i].cpu_view) c->sync_err++;
    c->buf[i].cpu_view = false;
}
static void cache_for_cpu(cap_t *c, int i) {
    if (c->buf[i].cpu_view) c->sync_err++;
    c->buf[i].cpu_view = true;
}

/* TODO(A3): 소유권 불변식 검사. 위반 개수를 돌려준다 (0 = 정상). 락을 잡은 채 불린다.
 *   - 각 버퍼는 inq / doneq / active 중 "정확히 한 곳" 에만 있고, 그 자리와 state 가 맞아야 한다:
 *       inq→B_QUEUED, doneq→B_DONE, active→B_ACTIVE. 앱이 가진 버퍼(B_DEQUEUED)는 어디에도 없어야 한다.
 *   - cpu_view 는 state 가 B_DEQUEUED/B_DONE 일 때만 true.
 *   - streaming 중에는 active >= 0 (HW 는 언제나 쓸 곳이 있어야 한다). */
unsigned check_ownership(const cap_t *c) {
    (void)c;
    return 0;
}

static void audit(cap_t *c) { c->inv_fail += check_ownership(c); }

void cap_init(cap_t *c, policy_t policy, unsigned nbuf) {
    memset(c, 0, sizeof *c);
    pthread_mutex_init(&c->lock, NULL);
    pthread_cond_init(&c->done_cv, NULL);
    c->policy = policy;
    c->nbuf = nbuf;
    c->active = -1;
    c->last_poison_idx = -1;
    for (unsigned i = 0; i < nbuf; i++) c->buf[i].cpu_view = true;
}

void cap_destroy(cap_t *c) {
    pthread_mutex_destroy(&c->lock);
    pthread_cond_destroy(&c->done_cv);
}

/* TODO(A1): 앱 → 드라이버. idx 범위와 state==B_DEQUEUED 를 먼저 확인한다(아니면 qbuf_einval++, -EINVAL).
 * 통과하면 mem 전체를 POISON_WORD 로 채우고, cache_for_device, B_QUEUED, inq 에 push, audit, 0 을 돌려준다.
 * 검증 전에 poison 을 칠하면 안 된다 — 왜? (double qbuf 가 DMA 중인 버퍼를 덮는다) */
int cap_qbuf(cap_t *c, int idx) {
    (void)c; (void)idx;
    return -EINVAL;
}

static struct timespec deadline_after_ms(int ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    return ts;
}

/* TODO(A2): 드라이버 → 앱. doneq 에서 가장 오래된 버퍼를 꺼내 B_DEQUEUED 로 바꾸고 info 를 채운 뒤 index 를 돌려준다.
 * doneq 가 비었을 때: streaming 이 아니면 -EINVAL, timeout_ms==0 이면 -EAGAIN,
 * 그 외에는 done_cv 를 timeout_ms 만큼 기다린다(시간 초과면 -EAGAIN). 대기는 while 로. dequeued++, audit 잊지 말 것. */
int cap_dqbuf(cap_t *c, cap_info_t *info, int timeout_ms) {
    (void)c; (void)info; (void)timeout_ms;
    return -EAGAIN;
}

/* 앱이 버퍼 내용을 볼 때 쓰는 포인터. 앱 소유가 아니면 NULL (포인터를 오래 들고 있으면 위험한 이유) */
uint32_t *cap_ptr(cap_t *c, int idx) {
    uint32_t *p = NULL;
    pthread_mutex_lock(&c->lock);
    if (idx >= 0 && (unsigned)idx < c->nbuf && c->buf[idx].state == B_DEQUEUED) p = c->buf[idx].mem;
    pthread_mutex_unlock(&c->lock);
    return p;
}

/* 완료: ACTIVE → DONE. 캐시는 여기서 CPU 쪽으로 (vb2_buffer_done → __vb2_buf_mem_finish 와 같은 자리) */
void complete_active(cap_t *c) {
    int x = c->active;
    cache_for_cpu(c, x);
    c->buf[x].state = B_DONE;
    fifo_push(&c->doneq, x);
    c->active = -1;
    pthread_cond_broadcast(&c->done_cv);
}

/* 큐의 다음 버퍼를 HW 에 건다: QUEUED → ACTIVE. 여기서 poison 이 그대로인지 확인한다 */
static void arm_next(cap_t *c) {
    int n = fifo_pop(&c->inq);
    for (unsigned i = 0; i < FRAME_WORDS; i++) {
        if (c->buf[n].mem[i] != POISON_WORD) {
            c->poison_hits++;
            c->last_poison_idx = n;
            break;
        }
    }
    c->buf[n].state = B_ACTIVE;
    c->active = n;
}

/* TODO(B1): SOF. 프레임 번호를 소비하고(frames++, seq = hw_seq++) 이번 프레임을 쓸 버퍼 번호를 돌려준다.
 * parked 면(DROP_NEW 가 끝난 프레임을 active 에 보관 중): inq 가 비었으면 dropped++ 후 -1 (이 프레임은 안 쓴다).
 * inq 에 버퍼가 있으면 complete_active + arm_next 로 보관 프레임을 내보내고 parked 를 풀고 계속한다.
 * 쓸 버퍼가 정해지면 buf[active].seq = seq 를 기록하고 active 를 돌려준다. */
static int hw_start_of_frame(cap_t *c) {
    (void)c;
    return -1;
}

/* TODO(B2): EOF (완료 IRQ). inq 에 다음 버퍼가 있으면 complete_active + arm_next 로 끝.
 * 없으면 정책대로:
 *   DROP_NEW         — parked = true. active 는 그대로, 끝난 프레임을 그 안에 보관한다.
 *   REUSE_LAST       — active 는 그대로, 방금 프레임은 다음 프레임에 덮이니 dropped++.
 *   OVERWRITE_OLDEST — complete_active 후 doneq 맨 앞(가장 오래된 프레임)을 빼앗아 ACTIVE 로. dropped++.
 *                      빼앗은 버퍼는 다시 장치 쪽이므로 cache_for_device 필요. */
static void hw_end_of_frame(cap_t *c) {
    (void)c;
}

/* DMA 엔진이 프레임 하나를 처리한다. mem[] 은 락 없이 쓴다 — ACTIVE 버퍼의 주인은 HW 뿐이므로. */
void cap_hw_frame(cap_t *c) {
    pthread_mutex_lock(&c->lock);
    if (!c->streaming) { pthread_mutex_unlock(&c->lock); return; }
    int idx = hw_start_of_frame(c);
    uint32_t seq = idx >= 0 ? c->buf[idx].seq : 0;
    c->dma_busy = idx >= 0;
    audit(c);
    pthread_mutex_unlock(&c->lock);

    if (idx >= 0) {
        uint32_t *mem = c->buf[idx].mem;
        for (unsigned i = 0; i < FRAME_WORDS / 2; i++) mem[i] = frame_word(seq, i);
        if (c->mid_dma_hook) c->mid_dma_hook(c->hook_arg);
        for (unsigned i = FRAME_WORDS / 2; i < FRAME_WORDS; i++) mem[i] = frame_word(seq, i);
    }

    pthread_mutex_lock(&c->lock);
    if (idx >= 0) {
        c->dma_busy = false;
        hw_end_of_frame(c);
    }
    audit(c);
    pthread_cond_broadcast(&c->done_cv);
    pthread_mutex_unlock(&c->lock);
}

/* STREAMON: 큐에 버퍼가 하나도 없으면 HW 에 걸 것이 없으므로 -EINVAL (vb2 의 min_queued_buffers 비슷한 조건) */
int cap_streamon(cap_t *c) {
    pthread_mutex_lock(&c->lock);
    if (c->streaming || fifo_empty(&c->inq)) { pthread_mutex_unlock(&c->lock); return -EINVAL; }
    arm_next(c);
    c->streaming = true;
    audit(c);
    pthread_mutex_unlock(&c->lock);
    return 0;
}

/* TODO(C2): STREAMOFF. dma_busy 인 동안에는 buffer 를 회수하면 안 된다: done_cv 를 timeout_ms 까지 기다리고
 * (0 이면 안 기다림) 아직 busy 면 -EBUSY. 그다음 streaming=false, 모든 버퍼를 B_DEQUEUED 로 돌리고
 * (QUEUED/ACTIVE 는 cache_for_cpu), 앱이 못 받은 프레임을 flushed 에 더한다: doneq 길이 + parked 면 1.
 * inq/doneq 비우기, active=-1, parked=false, done_cv broadcast(dqbuf 대기자 깨움), audit, 0 반환. */
int cap_streamoff(cap_t *c, int timeout_ms) {
    (void)c; (void)timeout_ms;
    return -1;
}

/* ------- 소비자 쪽 ------- */
typedef struct { uint32_t expected; uint64_t missed, regress; } gap_t;

/* TODO(B3): 도착한 seq 로 공백을 갱신하고, 이번에 새로 빠진 프레임 수를 돌려준다.
 * expected 와의 차이를 부호 있는 32비트로 보아 음수면 역행(regress++, 0 반환, expected 는 그대로).
 * 그 외에는 missed 에 (seq-expected) 를 더하고 expected = seq+1. uint32_t wrap 을 올바르게 처리할 것. */
uint32_t gap_update(gap_t *g, uint32_t seq) {
    (void)g; (void)seq;
    return 0;
}

#define MAX_LOG 64
typedef struct {
    gap_t gap;
    uint64_t delivered, bad_payload, qbuf_err;
    uint32_t last_seq;
    bool have_last, non_monotonic;
    uint32_t log[MAX_LOG];
    unsigned nlog;
    long slow_ns;             /* 소비 한 건에 걸리는 가짜 처리 시간 (0 = 없음) */
} consumer_t;

static void sleep_ns(long ns) {
    struct timespec ts = {ns / 1000000000L, ns % 1000000000L};
    nanosleep(&ts, NULL);
}

/* TODO(C1): 프레임 하나 소비. cap_dqbuf(timeout_ms) → 음수면 0 반환.
 * cap_ptr 로 포인터를 받아 모든 워드가 frame_word(seq,i) 인지 확인(틀리면 bad_payload++), gap_update,
 * delivered++, log 기록(nlog<MAX_LOG), seq 단조 증가 확인(아니면 non_monotonic=true), slow_ns 만큼 쉬고,
 * cap_qbuf 로 돌려준다(실패 시 qbuf_err++). 소비했으면 1 반환. */
int app_consume_one(cap_t *c, consumer_t *u, int timeout_ms) {
    (void)c; (void)u; (void)timeout_ms;
    return 0;
}

#ifndef CAM_NO_MAIN
/* ================================ 시험 하네스 ================================ */
static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static void queue_all(cap_t *c) {
    for (unsigned i = 0; i < c->nbuf; i++) (void)cap_qbuf(c, (int)i);
}

/* 스크립트 DSL: F=HW 프레임, D=dqbuf+검증+qbuf, K=dqbuf 후 앱이 보유, Q=가장 먼저 보유한 버퍼 qbuf */
static void run_script(cap_t *c, consumer_t *u, const char *s) {
    int held[MAX_BUF]; unsigned h_head = 0, h_n = 0;
    for (; *s; s++) {
        if (*s == 'F') cap_hw_frame(c);
        else if (*s == 'D') (void)app_consume_one(c, u, 0);
        else if (*s == 'K') {
            cap_info_t i;
            int idx = cap_dqbuf(c, &i, 0);
            if (idx >= 0 && h_n < MAX_BUF) {
                (void)gap_update(&u->gap, i.seq);
                u->delivered++;
                if (u->nlog < MAX_LOG) u->log[u->nlog++] = i.seq;
                held[(h_head + h_n++) % MAX_BUF] = idx;
            }
        } else if (*s == 'Q' && h_n > 0) {
            (void)cap_qbuf(c, held[h_head]);
            h_head = (h_head + 1) % MAX_BUF; h_n--;
        }
    }
}

static bool log_is(const consumer_t *u, const uint32_t *exp, unsigned n) {
    if (u->nlog != n) return false;
    for (unsigned i = 0; i < n; i++) if (u->log[i] != exp[i]) return false;
    return true;
}

static void test_part_a(void) {
    printf("--- Part A: 상태 전이와 API\n");
    cap_t c; consumer_t u; cap_info_t info;
    memset(&u, 0, sizeof u); memset(&info, 0, sizeof info);
    cap_init(&c, POLICY_DROP_NEW, 4);
    CHECK(cap_qbuf(&c, 0) == 0);
    CHECK(c.buf[0].state == B_QUEUED);
    CHECK(c.buf[0].mem[0] == POISON_WORD && c.buf[0].mem[FRAME_WORDS - 1] == POISON_WORD);
    CHECK(cap_qbuf(&c, 0) == -EINVAL);          /* double qbuf */
    CHECK(fifo_len(&c.inq) == 1);
    CHECK(cap_qbuf(&c, -1) == -EINVAL);
    CHECK(cap_qbuf(&c, 4) == -EINVAL);
    CHECK(c.qbuf_einval == 3);
    CHECK(cap_dqbuf(&c, &info, 0) == -EINVAL);  /* streaming 전 + doneq 비었음 */
    CHECK(cap_qbuf(&c, 1) == 0);
    CHECK(cap_streamon(&c) == 0);
    CHECK(c.active == 0 && c.buf[0].state == B_ACTIVE);
    CHECK(cap_dqbuf(&c, &info, 0) == -EAGAIN);  /* 프레임 없음 */
    CHECK(cap_dqbuf(&c, &info, 1) == -EAGAIN);  /* 기다려도 없음 (시간 초과) */

    cap_hw_frame(&c);
    CHECK(c.buf[0].state == B_DONE && c.active == 1);
    CHECK(cap_qbuf(&c, 0) == -EINVAL);          /* DONE 은 아직 드라이버 것 */
    CHECK(cap_dqbuf(&c, &info, 0) == 0 && info.index == 0 && info.seq == 0);
    CHECK(c.buf[0].state == B_DEQUEUED);
    CHECK(cap_ptr(&c, 0) != NULL && cap_ptr(&c, 0)[3] == frame_word(0, 3));
    CHECK(cap_ptr(&c, 1) == NULL);              /* ACTIVE 는 앱이 못 본다 */
    CHECK(cap_ptr(&c, 99) == NULL);
    CHECK(cap_qbuf(&c, 0) == 0);
    CHECK(c.inv_fail == 0);
    CHECK(c.sync_err == 0);
    CHECK(cap_streamoff(&c, 100) == 0);
    CHECK(c.active == -1 && c.buf[0].state == B_DEQUEUED && c.buf[1].state == B_DEQUEUED);
    cap_destroy(&c);

    /* streamon 은 큐가 비면 거절 */
    cap_t e;
    cap_init(&e, POLICY_DROP_NEW, 2);
    CHECK(cap_streamon(&e) == -EINVAL);
    cap_destroy(&e);

    /* 소유권 검사기 자체를 시험한다: 일부러 망가뜨린 상태를 잡아야 한다 */
    cap_t b;
    cap_init(&b, POLICY_DROP_NEW, 3);
    CHECK(check_ownership(&b) == 0);
    fifo_push(&b.inq, 0);                       /* state 는 DEQUEUED 인데 inq 에 있음 */
    CHECK(check_ownership(&b) >= 1);
    cap_destroy(&b);

    cap_init(&b, POLICY_DROP_NEW, 3);
    (void)cap_qbuf(&b, 0);
    (void)cap_streamon(&b);                     /* 0 = ACTIVE */
    fifo_push(&b.doneq, 0);                     /* ACTIVE 인 0 이 doneq 에도 → 주인 둘 */
    CHECK(check_ownership(&b) >= 1);
    cap_destroy(&b);

    cap_init(&b, POLICY_DROP_NEW, 3);
    b.buf[2].state = B_QUEUED;                  /* 어느 리스트에도 없는 분실 버퍼 */
    b.buf[2].cpu_view = false;
    CHECK(check_ownership(&b) >= 1);
    cap_destroy(&b);

    cap_init(&b, POLICY_DROP_NEW, 3);
    (void)cap_qbuf(&b, 1);
    (void)cap_streamon(&b);
    b.buf[1].cpu_view = true;                   /* ACTIVE 인데 CPU 쪽 시점 */
    CHECK(check_ownership(&b) >= 1);
    b.buf[1].cpu_view = false;
    b.active = -1;                              /* streaming 인데 HW 에 버퍼 없음 (이 버퍼는 분실도 됨) */
    CHECK(check_ownership(&b) >= 1);
    cap_destroy(&b);
}

typedef struct { const char *name; policy_t p; uint32_t exp[10]; unsigned n; uint64_t dropped, gaps; } stall_case_t;

static void test_part_b_stall(void) {
    printf("--- Part B: 스톨 시나리오 (F x8, D x3, F x2, D x3)\n");
    static const stall_case_t T[POLICY_COUNT] = {
        {"DROP_NEW",         POLICY_DROP_NEW,         {0, 1, 2, 3, 8, 9}, 6, 4, 4},
        {"REUSE_LAST",       POLICY_REUSE_LAST,       {0, 1, 2, 8, 9},    5, 5, 5},
        {"OVERWRITE_OLDEST", POLICY_OVERWRITE_OLDEST, {5, 6, 7, 8, 9},    5, 5, 5},
    };
    for (int t = 0; t < POLICY_COUNT; t++) {
        const stall_case_t *x = &T[t];
        cap_t c; consumer_t u;
        memset(&u, 0, sizeof u);
        cap_init(&c, x->p, 4);
        queue_all(&c);
        (void)cap_streamon(&c);
        run_script(&c, &u, "FFFFFFFFDDDFFDDD");
        printf("[%s] delivered:", x->name);
        for (unsigned i = 0; i < u.nlog; i++) printf(" %u", u.log[i]);
        printf("  dropped=%llu gaps=%llu\n", (unsigned long long)c.dropped, (unsigned long long)u.gap.missed);
        CHECK(log_is(&u, x->exp, x->n));
        CHECK(c.dropped == x->dropped);
        CHECK(u.gap.missed == x->gaps);
        CHECK(c.frames == 10 && c.frames == c.dequeued + c.dropped + 0);
        CHECK(u.bad_payload == 0 && u.qbuf_err == 0 && !u.non_monotonic);
        CHECK(c.inv_fail == 0 && c.sync_err == 0 && c.poison_hits == 0);
        (void)cap_streamoff(&c, 100);
        CHECK(c.flushed == 0);
        cap_destroy(&c);
    }
}

static void test_part_b_held(void) {
    printf("--- Part B: 앱이 n-1 개를 잡고 있을 때 (빼앗을 DONE 이 자기 자신뿐)\n");
    /* 3 버퍼. F F 로 0,1 완료, K K 로 앱이 보유, F F 는 ACTIVE 하나로 버틴다, Q Q 로 반납, F, D D */
    static const uint32_t e_drop[] = {0, 1, 2, 4}, e_other[] = {0, 1, 4};
    for (int p = 0; p < POLICY_COUNT; p++) {
        cap_t c; consumer_t u;
        memset(&u, 0, sizeof u);
        cap_init(&c, (policy_t)p, 3);
        queue_all(&c);
        (void)cap_streamon(&c);
        run_script(&c, &u, "FFKKFFQQFDD");
        if (p == POLICY_DROP_NEW) {
            CHECK(log_is(&u, e_drop, 4));
            CHECK(c.dropped == 1);
        } else {
            CHECK(log_is(&u, e_other, 3));
            CHECK(c.dropped == 2);
        }
        CHECK(c.inv_fail == 0 && c.sync_err == 0);
        cap_destroy(&c);
    }
}

/* 시험용 훅: DMA 절반 뒤에 불린다 (강제 스케줄) */
typedef struct { cap_t *c; int r_qbuf, r_dq, r_off; cap_info_t info; int idx; bool ptr_null; } hook_t;
static void hook_qbuf(void *a) { hook_t *h = a; h->r_qbuf = cap_qbuf(h->c, h->idx); }
static void hook_dq(void *a) {
    hook_t *h = a;
    h->r_dq = cap_dqbuf(h->c, &h->info, 0);
    h->ptr_null = cap_ptr(h->c, h->c->active) == NULL;
}
static void hook_off(void *a) { hook_t *h = a; h->r_off = cap_streamoff(h->c, 0); }

static void test_part_b_forced(void) {
    printf("--- Part B: DMA 중간에 끼어드는 강제 스케줄\n");
    /* 2 버퍼. 프레임 0 뒤 앱이 버퍼 0 을 보유 → 다음 프레임은 buf1 하나로 받는다 */
    cap_t c; hook_t h; cap_info_t i;
    memset(&h, 0, sizeof h);
    cap_init(&c, POLICY_REUSE_LAST, 2);
    queue_all(&c); (void)cap_streamon(&c);
    cap_hw_frame(&c);
    CHECK(cap_dqbuf(&c, &i, 0) == 0);
    h.c = &c; h.idx = 0;
    c.mid_dma_hook = hook_qbuf; c.hook_arg = &h;
    cap_hw_frame(&c);                            /* DMA 도중 qbuf → EOF 때 다음 버퍼가 있다 */
    c.mid_dma_hook = NULL;
    CHECK(h.r_qbuf == 0);
    CHECK(c.dropped == 0 && c.buf[1].state == B_DONE && c.active == 0);
    CHECK(c.poison_hits == 0 && c.inv_fail == 0);
    cap_destroy(&c);

    /* 같은 시나리오인데 qbuf 가 EOF 직후에 온다 → 한 프레임을 잃는다 */
    cap_init(&c, POLICY_REUSE_LAST, 2);
    queue_all(&c); (void)cap_streamon(&c);
    cap_hw_frame(&c);
    CHECK(cap_dqbuf(&c, &i, 0) == 0);
    cap_hw_frame(&c);                            /* EOF 에 inq 비었음 */
    CHECK(c.dropped == 1 && c.buf[1].state == B_ACTIVE);
    CHECK(cap_qbuf(&c, 0) == 0);
    cap_hw_frame(&c);
    CHECK(cap_dqbuf(&c, &i, 0) == 1 && i.seq == 2);  /* seq 1 은 사라졌다 */
    cap_destroy(&c);

    /* DMA 도중 앱은 ACTIVE 버퍼에 손댈 수 없다 */
    memset(&h, 0, sizeof h);
    cap_init(&c, POLICY_DROP_NEW, 2);
    queue_all(&c); (void)cap_streamon(&c);
    h.c = &c;
    c.mid_dma_hook = hook_dq; c.hook_arg = &h;
    cap_hw_frame(&c);
    c.mid_dma_hook = NULL;
    CHECK(h.r_dq == -EAGAIN && h.ptr_null);
    cap_destroy(&c);

    /* DMA 도중 streamoff 는 거절된다 */
    memset(&h, 0, sizeof h);
    cap_init(&c, POLICY_DROP_NEW, 2);
    queue_all(&c); (void)cap_streamon(&c);
    h.c = &c;
    c.mid_dma_hook = hook_off; c.hook_arg = &h;
    cap_hw_frame(&c);
    c.mid_dma_hook = NULL;
    CHECK(h.r_off == -EBUSY);
    CHECK(c.buf[0].state == B_DONE && c.buf[0].mem[63] == frame_word(0, 63));
    CHECK(cap_streamoff(&c, 100) == 0);
    CHECK(c.flushed == 1);                       /* 끝났지만 못 받아 간 프레임 0 */
    CHECK(c.frames == c.dequeued + c.dropped + c.flushed);
    CHECK(cap_dqbuf(&c, &i, 50) == -EINVAL);     /* 멈춘 큐에서는 기다리지 않는다 */
    cap_destroy(&c);
}

static void test_part_b_poison(void) {
    printf("--- Part B: poison — qbuf 뒤에 버퍼를 건드리는 앱\n");
    cap_t c; cap_info_t i;
    cap_init(&c, POLICY_DROP_NEW, 2);
    queue_all(&c); (void)cap_streamon(&c);
    cap_hw_frame(&c);                            /* frame 0: buf0 DONE, buf1 ACTIVE */
    CHECK(cap_dqbuf(&c, &i, 0) == 0);
    uint32_t *stale = cap_ptr(&c, 0);
    CHECK(stale != NULL);
    CHECK(cap_qbuf(&c, 0) == 0);
    CHECK(c.buf[0].mem[0] == POISON_WORD);       /* qbuf 후에 읽으면 poison 만 보인다 */
    CHECK(stale[0] == POISON_WORD);
    stale[7] = 0x12345678u;                      /* 버그: 넘겨준 버퍼에 쓰기 */
    cap_hw_frame(&c);                            /* EOF 에서 buf0 이 ACTIVE 로 → poison 검사 */
    CHECK(c.poison_hits == 1 && c.last_poison_idx == 0);
    cap_destroy(&c);

    /* 올바른 앱은 0 */
    cap_init(&c, POLICY_DROP_NEW, 2);
    queue_all(&c); (void)cap_streamon(&c);
    cap_hw_frame(&c);
    CHECK(cap_dqbuf(&c, &i, 0) == 0);
    CHECK(cap_qbuf(&c, 0) == 0);
    cap_hw_frame(&c);
    CHECK(c.poison_hits == 0 && c.last_poison_idx == -1);
    cap_destroy(&c);
}

static void test_part_b_gap(void) {
    printf("--- Part B: sequence 공백 검출 (wrap 포함)\n");
    gap_t g; memset(&g, 0, sizeof g);
    CHECK(gap_update(&g, 0) == 0 && g.expected == 1);
    CHECK(gap_update(&g, 1) == 0 && g.missed == 0);
    CHECK(gap_update(&g, 5) == 3 && g.missed == 3 && g.expected == 6);
    CHECK(gap_update(&g, 5) == 0 && g.regress == 1 && g.expected == 6);  /* 중복/역행은 gap 이 아니다 */
    memset(&g, 0, sizeof g);
    g.expected = 0xFFFFFFFEu;
    CHECK(gap_update(&g, 0xFFFFFFFEu) == 0 && g.expected == 0xFFFFFFFFu);
    CHECK(gap_update(&g, 1) == 2 && g.missed == 2 && g.expected == 2);   /* FFFFFFFF, 0 이 빠짐 */
    memset(&g, 0, sizeof g);
    g.expected = 10;
    CHECK(gap_update(&g, 3) == 0 && g.regress == 1 && g.missed == 0);
}

/* ---- 실제 스레드 + rendezvous: 순서를 강제한 IRQ 스레드 ---- */
typedef struct {
    cap_t *c;
    pthread_mutex_t m; pthread_cond_t cv;
    int req, ack; bool stop;
} gate_t;

static void *irq_thread(void *arg) {
    gate_t *g = arg;
    pthread_mutex_lock(&g->m);
    for (;;) {
        while (g->req == g->ack && !g->stop) {
            struct timespec dl = deadline_after_ms(1000);
            (void)pthread_cond_timedwait(&g->cv, &g->m, &dl);   /* 시험은 언제나 끝에 stop 을 세운다 */
        }
        if (g->stop) break;
        pthread_mutex_unlock(&g->m);
        cap_hw_frame(g->c);
        pthread_mutex_lock(&g->m);
        g->ack++;
        pthread_cond_broadcast(&g->cv);
    }
    pthread_mutex_unlock(&g->m);
    return NULL;
}

/* 프레임 하나를 IRQ 스레드에 시키고 끝날 때까지 기다린다 (제한 시간 있음) */
static bool gate_fire(gate_t *g) {
    bool ok = true;
    pthread_mutex_lock(&g->m);
    int want = ++g->req;
    pthread_cond_broadcast(&g->cv);
    struct timespec dl = deadline_after_ms(2000);
    while (g->ack < want) {
        if (pthread_cond_timedwait(&g->cv, &g->m, &dl) == ETIMEDOUT) { ok = false; break; }
    }
    pthread_mutex_unlock(&g->m);
    return ok;
}

static void test_part_c_forced(void) {
    printf("--- Part C: 진짜 IRQ 스레드 + 강제 순서 (결과가 Part B 와 같아야 한다)\n");
    static const uint32_t e[POLICY_COUNT][10] = {{0,1,2,3,8,9},{0,1,2,8,9},{5,6,7,8,9}};
    static const unsigned en[POLICY_COUNT] = {6,5,5};
    for (int p = 0; p < POLICY_COUNT; p++) {
        cap_t c; consumer_t u; gate_t g; pthread_t th;
        memset(&u, 0, sizeof u); memset(&g, 0, sizeof g);
        cap_init(&c, (policy_t)p, 4);
        g.c = &c;
        pthread_mutex_init(&g.m, NULL); pthread_cond_init(&g.cv, NULL);
        queue_all(&c);
        (void)cap_streamon(&c);
        int rc = pthread_create(&th, NULL, irq_thread, &g);
        CHECK(rc == 0);
        bool ok = true;
        for (int f = 0; f < 8; f++) ok = gate_fire(&g) && ok;
        run_script(&c, &u, "DDD");
        for (int f = 0; f < 2; f++) ok = gate_fire(&g) && ok;
        run_script(&c, &u, "DDD");
        pthread_mutex_lock(&g.m); g.stop = true; pthread_cond_broadcast(&g.cv); pthread_mutex_unlock(&g.m);
        pthread_join(th, NULL);
        CHECK(ok);
        CHECK(log_is(&u, e[p], en[p]));
        CHECK(c.inv_fail == 0 && c.poison_hits == 0 && u.bad_payload == 0);
        pthread_mutex_destroy(&g.m); pthread_cond_destroy(&g.cv);
        cap_destroy(&c);
    }
}

/* ---- 자유 실행: 스케줄은 매번 다르다. 어떤 스케줄에서도 성립하는 것만 CHECK ---- */
typedef struct {
    cap_t *c; unsigned frames; long period_ns; int done;
    pthread_mutex_t m;
} prod_t;

static void *producer(void *arg) {
    prod_t *p = arg;
    for (unsigned i = 0; i < p->frames; i++) {
        cap_hw_frame(p->c);
        if (p->period_ns) sleep_ns(p->period_ns);
    }
    pthread_mutex_lock(&p->m); p->done = 1; pthread_mutex_unlock(&p->m);
    return NULL;
}

static void test_part_c_free(void) {
    printf("--- Part C: 자유 실행 스트레스 (보존 법칙 + 불변식)\n");
    for (int p = 0; p < POLICY_COUNT; p++) {
        cap_t c; consumer_t u; prod_t pr; pthread_t th;
        memset(&u, 0, sizeof u); memset(&pr, 0, sizeof pr);
        cap_init(&c, (policy_t)p, 4);
        u.slow_ns = 80000;                 /* 소비자가 느리다 */
        pr.c = &c; pr.frames = 1500; pr.period_ns = 20000;
        pthread_mutex_init(&pr.m, NULL);
        queue_all(&c);
        (void)cap_streamon(&c);
        int rc = pthread_create(&th, NULL, producer, &pr);
        CHECK(rc == 0);
        int idle = 0;
        for (long budget = 0; budget < 200000 && idle < 3; budget++) {
            int got = app_consume_one(&c, &u, 2);
            int done;
            pthread_mutex_lock(&pr.m); done = pr.done; pthread_mutex_unlock(&pr.m);
            if (got == 0 && done) idle++; else if (got) idle = 0;
        }
        pthread_join(th, NULL);
        int off = cap_streamoff(&c, 200);
        printf("[%s] frames=%llu delivered=%llu dropped=%llu flushed=%llu gaps=%llu (자유 실행: 수치는 매번 다름)\n",
               POLICY_NAME[p], (unsigned long long)c.frames, (unsigned long long)u.delivered,
               (unsigned long long)c.dropped, (unsigned long long)c.flushed, (unsigned long long)u.gap.missed);
        CHECK(off == 0);
        CHECK(c.frames == 1500);
        CHECK(c.frames == c.dequeued + c.dropped + c.flushed);
        CHECK(u.delivered == c.dequeued);
        CHECK(u.gap.missed <= c.dropped + c.flushed);
        CHECK(u.bad_payload == 0 && u.qbuf_err == 0);
        CHECK(!u.non_monotonic && u.gap.regress == 0);
        CHECK(c.inv_fail == 0 && c.poison_hits == 0 && c.sync_err == 0);
        pthread_mutex_destroy(&pr.m);
        cap_destroy(&c);
    }
}

/* ---- 가상 시간 비교: 소비자는 period 프레임마다 1 개 소비. 벽시계 없음 → 숫자가 항상 같다 ---- */
typedef struct { uint64_t frames, delivered, dropped, age_sum; uint32_t age_max; } vstat_t;

static void run_virtual(policy_t p, unsigned nbuf, unsigned period, unsigned frames, vstat_t *v) {
    cap_t c; consumer_t u; cap_info_t info;
    memset(&u, 0, sizeof u); memset(v, 0, sizeof *v);
    cap_init(&c, p, nbuf);
    queue_all(&c);
    (void)cap_streamon(&c);
    for (unsigned f = 0; f < frames; f++) {
        cap_hw_frame(&c);
        if ((f + 1) % period == 0) {
            int idx = cap_dqbuf(&c, &info, 0);
            if (idx >= 0) {
                uint32_t age = c.hw_seq - 1 - info.seq;   /* 0 = 방금 끝난 프레임 */
                v->delivered++; v->age_sum += age;
                if (age > v->age_max) v->age_max = age;
                (void)gap_update(&u.gap, info.seq);
                (void)cap_qbuf(&c, idx);
            }
        }
    }
    (void)cap_streamoff(&c, 100);
    v->frames = c.frames; v->dropped = c.dropped;
    cap_destroy(&c);
}

static void test_part_c_compare(void) {
    printf("--- Part C: 정책 비교 (nbuf=4, 소비자 3 프레임에 1개, 60 프레임, 가상 시간)\n");
    vstat_t r[POLICY_COUNT];
    printf("%-18s %7s %7s %8s %7s\n", "policy", "deliv", "drop", "age_avg", "age_max");
    for (int p = 0; p < POLICY_COUNT; p++) {
        run_virtual((policy_t)p, 4, 3, 60, &r[p]);
        printf("%-18s %7llu %7llu %8.2f %7u\n", POLICY_NAME[p], (unsigned long long)r[p].delivered,
               (unsigned long long)r[p].dropped,
               r[p].delivered ? (double)r[p].age_sum / (double)r[p].delivered : 0.0, r[p].age_max);
    }
    /* 정상 상태의 최대 age (n=버퍼 수, P=소비 주기): DROP_NEW = n*P-1, REUSE_LAST = (n-1)*P-1, OVERWRITE = n-2 */
    CHECK(r[POLICY_DROP_NEW].age_max == 4 * 3 - 1);
    CHECK(r[POLICY_REUSE_LAST].age_max == 3 * 3 - 1);
    CHECK(r[POLICY_OVERWRITE_OLDEST].age_max == 4 - 2);
    CHECK(r[POLICY_DROP_NEW].delivered == r[POLICY_REUSE_LAST].delivered);
    for (int p = 0; p < POLICY_COUNT; p++) CHECK(r[p].delivered + r[p].dropped <= r[p].frames);

    vstat_t s[POLICY_COUNT];
    printf("%-18s %7s %7s %8s %7s  (nbuf=3, 소비자 4 프레임에 1개, 80 프레임)\n", "policy", "deliv", "drop", "age_avg", "age_max");
    for (int p = 0; p < POLICY_COUNT; p++) {
        run_virtual((policy_t)p, 3, 4, 80, &s[p]);
        printf("%-18s %7llu %7llu %8.2f %7u\n", POLICY_NAME[p], (unsigned long long)s[p].delivered,
               (unsigned long long)s[p].dropped,
               s[p].delivered ? (double)s[p].age_sum / (double)s[p].delivered : 0.0, s[p].age_max);
    }
    CHECK(s[POLICY_DROP_NEW].age_max == 3 * 4 - 1);
    CHECK(s[POLICY_REUSE_LAST].age_max == 2 * 4 - 1);
    CHECK(s[POLICY_OVERWRITE_OLDEST].age_max == 3 - 2);
}

int main(void) {
    test_part_a();
    test_part_b_stall();
    test_part_b_held();
    test_part_b_forced();
    test_part_b_poison();
    test_part_b_gap();
    test_part_c_forced();
    test_part_c_free();
    test_part_c_compare();
    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
#endif /* CAM_NO_MAIN */
