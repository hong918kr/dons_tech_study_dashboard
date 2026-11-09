/* CAM-M11 추가 연습 — 한 파일로 모두 검증한다.
 *   P1 triple buffer: 드라이버-앱 사이 "최신 프레임" 교환 (lock-free, 생산자는 절대 안 기다림)
 *   P2 stale IRQ: streamoff 뒤에 늦게 도착한 완료 IRQ 를 epoch 로 버린다
 *   P3 refcount fan-out: 소비자 두 명이 같은 버퍼를 읽고, 마지막 사람이 반납한다
 *   P4 NEXT 슬롯 latch: HW 가 다음 주소를 미리 들고 있으면 qbuf 는 한 프레임 일찍 와야 한다
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

/* ---- P1: triple buffer ---- */
#define TB_WORDS 32u
#define TB_NEW   4u      /* slot 값: 인덱스(0..2) | NEW 플래그 */
typedef struct {
    _Atomic uint32_t slot;
    uint32_t w;          /* 생산자 전용 인덱스 */
    uint32_t r;          /* 소비자 전용 인덱스 */
    uint32_t data[3][TB_WORDS];
} tb_t;

static void tb_init(tb_t *t) {
    memset(t, 0, sizeof *t);
    t->w = 0; t->r = 1;
    atomic_init(&t->slot, 2u);   /* 2 번 버퍼가 교환 슬롯에, 아직 새 프레임 아님 */
}
static uint32_t *tb_write_ptr(tb_t *t) { return t->data[t->w]; }

/* 다 쓴 프레임을 슬롯과 맞바꾼다. acq_rel: 내가 쓴 내용을 내보내고(release), 받아 오는 버퍼를
 * 소비자가 다 읽은 뒤의 것으로 본다(acquire). */
static void tb_publish(tb_t *t) {
    uint32_t old = atomic_exchange_explicit(&t->slot, t->w | TB_NEW, memory_order_acq_rel);
    t->w = old & 3u;
}
/* 새 프레임이 있으면 맞바꿔서 true. 없으면 지금 들고 있는 버퍼를 계속 쓴다. */
static bool tb_acquire(tb_t *t, const uint32_t **out) {
    if (atomic_load_explicit(&t->slot, memory_order_relaxed) & TB_NEW) {
        uint32_t old = atomic_exchange_explicit(&t->slot, t->r, memory_order_acq_rel);
        t->r = old & 3u;
        *out = t->data[t->r];
        return true;
    }
    return false;
}

static void tb_fill(uint32_t *p, uint32_t seq) { for (unsigned i = 0; i < TB_WORDS; i++) p[i] = seq * 7u + i; }
static bool tb_ok(const uint32_t *p, uint32_t seq) {
    for (unsigned i = 0; i < TB_WORDS; i++) if (p[i] != seq * 7u + i) return false;
    return true;
}

typedef struct { tb_t *t; uint32_t frames; } tb_arg_t;

static void *tb_writer(void *a) {
    tb_arg_t *x = a;
    for (uint32_t s = 1; s <= x->frames; s++) {
        uint32_t *p = tb_write_ptr(x->t);
        p[0] = s;                                     /* 프레임 번호는 첫 워드 */
        for (unsigned i = 1; i < TB_WORDS; i++) p[i] = s * 7u + i;
        tb_publish(x->t);
    }
    return NULL;
}

static void test_triple_buffer(void) {
    printf("--- P1 triple buffer\n");
    static tb_t t;
    const uint32_t *p = NULL;
    tb_init(&t);
    CHECK(!tb_acquire(&t, &p));
    tb_fill(tb_write_ptr(&t), 10); tb_publish(&t);
    tb_fill(tb_write_ptr(&t), 11); tb_publish(&t);
    tb_fill(tb_write_ptr(&t), 12); tb_publish(&t);    /* 소비자 없이 3번 게시 → 최신만 남는다 */
    CHECK(tb_acquire(&t, &p) && tb_ok(p, 12));
    CHECK(!tb_acquire(&t, &p));                       /* 새 프레임 없음 */
    tb_fill(tb_write_ptr(&t), 13); tb_publish(&t);
    CHECK(tb_acquire(&t, &p) && tb_ok(p, 13));

    /* 자유 실행: 찢어진 프레임이 없고 번호는 단조 증가 (TSan 대상) */
    static tb_t s;
    tb_init(&s);
    tb_arg_t a = {&s, 200000};
    pthread_t th;
    int rc = pthread_create(&th, NULL, tb_writer, &a);
    CHECK(rc == 0);
    uint32_t last = 0, got = 0, torn = 0;
    bool mono = true;
    for (unsigned long spin = 0; spin < 200000000ul && last < a.frames; spin++) {
        if (tb_acquire(&s, &p)) {
            uint32_t seq = p[0];
            for (unsigned i = 1; i < TB_WORDS; i++) if (p[i] != seq * 7u + i) { torn++; break; }
            if (seq <= last) mono = false;
            last = seq;
            got++;
        }
    }
    pthread_join(th, NULL);
    CHECK(torn == 0);
    CHECK(mono);
    CHECK(last == a.frames);   /* 마지막으로 받은 것은 마지막 프레임 */
    printf("    자유 실행에서 소비자가 받은 프레임 수 = %u / %u (매번 다름)\n", got, a.frames);
}

/* ---- P2: stale IRQ / epoch ---- */
typedef struct {
    uint32_t epoch;
    bool streaming;
    int active;
    int completed[8]; unsigned ncomp;
    unsigned stale;
    bool use_epoch;
} q2_t;

static void q2_streamon(q2_t *q, int buf) { q->epoch++; q->streaming = true; q->active = buf; }
static void q2_streamoff(q2_t *q) { q->streaming = false; q->active = -1; }

/* IRQ 는 그 프레임을 시작할 때의 epoch 를 함께 가지고 온다(예: 레지스터 태그, 또는 ISR 이 읽어 둔 값) */
static void q2_irq_done(q2_t *q, uint32_t irq_epoch) {
    if (q->use_epoch && irq_epoch != q->epoch) { q->stale++; return; }
    if (!q->streaming || q->active < 0) { q->stale++; return; }
    q->completed[q->ncomp++] = q->active;
    q->active = -1;
}

static void test_stale_irq(void) {
    printf("--- P2 stale IRQ\n");
    /* streamoff → streamon 사이에 지난 프레임의 IRQ 가 늦게 도착한다 */
    q2_t q;
    memset(&q, 0, sizeof q);
    q.use_epoch = true;
    q2_streamon(&q, 5);                  /* epoch 1, buf5 */
    uint32_t pending = q.epoch;          /* 이 시점에 시작한 프레임의 IRQ 가 날아오는 중 */
    q2_streamoff(&q);
    q2_streamon(&q, 6);                  /* epoch 2, buf6 는 방금 건 새 버퍼 */
    q2_irq_done(&q, pending);            /* 지난 프레임의 늦은 IRQ */
    CHECK(q.ncomp == 0 && q.stale == 1 && q.active == 6);
    q2_irq_done(&q, q.epoch);            /* 진짜 완료 */
    CHECK(q.ncomp == 1 && q.completed[0] == 6);

    /* epoch 없이 같은 순서: 시작도 안 한 buf6 가 완료 처리된다 (= 쓰는 도중 앱에게 넘어감) */
    memset(&q, 0, sizeof q);
    q.use_epoch = false;
    q2_streamon(&q, 5);
    pending = q.epoch;
    q2_streamoff(&q);
    q2_streamon(&q, 6);
    q2_irq_done(&q, pending);
    CHECK(q.ncomp == 1 && q.completed[0] == 6 && q.stale == 0);
}

/* ---- P3: refcount fan-out ---- */
#define P3_BUF 4
typedef struct {
    int refs[P3_BUF];
    bool queued[P3_BUF];
    unsigned requeued, err;
} q3_t;

static void q3_deliver(q3_t *q, int idx, int readers) { q->queued[idx] = false; q->refs[idx] = readers; }
/* 소비자 하나가 다 읽었다. 마지막 소비자가 반납한다. */
static int q3_release(q3_t *q, int idx) {
    if (q->refs[idx] <= 0) { q->err++; return -EINVAL; }
    if (--q->refs[idx] == 0) { q->queued[idx] = true; q->requeued++; }
    return 0;
}

static void test_refcount(void) {
    printf("--- P3 refcount fan-out\n");
    q3_t q;
    memset(&q, 0, sizeof q);
    q3_deliver(&q, 2, 2);                 /* ISP 통계 + H.264 인코더가 같은 프레임 */
    CHECK(q3_release(&q, 2) == 0 && !q.queued[2]);
    CHECK(q3_release(&q, 2) == 0 && q.queued[2]);
    CHECK(q.requeued == 1);
    CHECK(q3_release(&q, 2) == -EINVAL);  /* 한 번 더 반납 = 버그 */
    CHECK(q.err == 1);
}

/* ---- P4: 1 슬롯 vs NEXT 슬롯 latch ---- */
typedef struct {
    bool has_next_reg;   /* true 면 HW 에 NEXT 주소 레지스터가 있다 (다음 프레임 주소를 미리 써 둠) */
    int cur, next;
    int inq[8]; unsigned qn;
    unsigned done, drops;
} q4_t;

static int q4_pop(q4_t *q) {
    if (q->qn == 0) return -1;
    int v = q->inq[0];
    for (unsigned i = 1; i < q->qn; i++) q->inq[i - 1] = q->inq[i];
    q->qn--;
    return v;
}
static void q4_qbuf(q4_t *q, int idx) { q->inq[q->qn++] = idx; }

/* 프레임 경계(EOF)마다 불린다. */
static void q4_frame_boundary(q4_t *q) {
    if (!q->has_next_reg) {
        int n = q4_pop(q);             /* 이 순간 inq 에 있으면 바로 건다 */
        if (n < 0) { q->drops++; return; }
        q->done++;
        q->cur = n;
        return;
    }
    if (q->next < 0) { q->drops++; }   /* HW 는 경계에서 NEXT 로만 갈아탄다 */
    else { q->done++; q->cur = q->next; q->next = -1; }
    if (q->next < 0) q->next = q4_pop(q);   /* 드라이버는 경계 뒤 ISR 에서 NEXT 를 다시 채운다 */
}

static void test_next_slot(void) {
    printf("--- P4 NEXT 슬롯 latch\n");
    /* 프레임 동안 buf3 가 들어오고 그 프레임 끝에서 갈아탈 수 있는가 */
    q4_t a;
    memset(&a, 0, sizeof a);
    a.has_next_reg = false; a.cur = 0; a.next = -1;
    q4_qbuf(&a, 3);                    /* 경계 1 직전에 qbuf */
    q4_frame_boundary(&a);
    CHECK(a.done == 1 && a.drops == 0 && a.cur == 3);

    q4_t b;
    memset(&b, 0, sizeof b);
    b.has_next_reg = true; b.cur = 0; b.next = -1;
    q4_qbuf(&b, 3);                    /* 같은 타이밍인데 NEXT 레지스터가 비어 있다 */
    q4_frame_boundary(&b);
    CHECK(b.done == 0 && b.drops == 1 && b.cur == 0 && b.next == 3);   /* 경계 1 은 놓침, NEXT 는 이제 채워짐 */
    q4_frame_boundary(&b);
    CHECK(b.done == 1 && b.cur == 3);  /* 한 프레임 늦게 갈아탄다 */
}

int main(void) {
    test_triple_buffer();
    test_stale_irq();
    test_refcount();
    test_next_slot();
    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
