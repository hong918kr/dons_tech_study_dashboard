/* DS-B03 링 버퍼 기초: Full/Empty 판별 3가지 방식
 * UART 수신(RX) 바이트 링 버퍼 — producer = UART RX ISR, consumer = main 루프.
 * 세 방식이 같은 rb_t 를 공유하고, 각 방식은 필요한 필드만 쓴다 (테스트 하네스를 하나로 쓰려고).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RB_N 5u   /* 저장 칸 수. 일부러 2의 거듭제곱이 아닌 수 (DS-B04 와 대비) */

typedef struct {
    uint8_t  buf[RB_N];
    uint32_t head;       /* 다음에 쓸 칸 0..N-1  (ISR 만 쓴다)   */
    uint32_t tail;       /* 다음에 읽을 칸 0..N-1 (main 만 쓴다) */
    uint32_t count;      /* 방식 A 전용: 들어 있는 바이트 수      */
    uint8_t  head_wrap;  /* 방식 C 전용: head 가 감은 바퀴 수의 홀짝 (미러 비트) */
    uint8_t  tail_wrap;  /* 방식 C 전용: tail 의 미러 비트 */
} rb_t;

static void rb_init(rb_t *r) { memset(r, 0, sizeof *r); }

/* 한 칸 전진: N-1 다음은 0. % 대신 비교 (Cortex-M0 에는 나눗셈 명령이 없다) */
static inline uint32_t next_idx(uint32_t i) { return (i + 1u == RB_N) ? 0u : i + 1u; }

/* ---------------- Part A: count 변수 방식 ---------------- */
static bool cnt_push(rb_t *r, uint8_t b) {
    (void)r; (void)b;
    return false; /* TODO: 가득 찼으면 false (overrun). 아니면 buf[head] 에 쓰고 head 전진, count++ */
}
static bool cnt_pop(rb_t *r, uint8_t *out) {
    (void)r; (void)out;
    return false; /* TODO: 비었으면 false. 아니면 buf[tail] 을 꺼내고 tail 전진, count-- */
}
static uint32_t cnt_len(const rb_t *r)   { (void)r; return 0u;    /* TODO */ }
static bool     cnt_empty(const rb_t *r) { (void)r; return false; /* TODO */ }
static bool     cnt_full(const rb_t *r)  { (void)r; return false; /* TODO */ }

/* ---------------- Part B: 한 칸 비우기 (one-slot-empty) 방식 ---------------- */
static bool slot_push(rb_t *r, uint8_t b) {
    (void)r; (void)b;
    return false; /* TODO: head 를 한 칸 옮겼을 때 tail 과 만나면 '가득 참' (count 필드는 쓰지 않는다) */
}
static bool slot_pop(rb_t *r, uint8_t *out) {
    (void)r; (void)out;
    return false; /* TODO: head == tail 이면 비었다 */
}
static uint32_t slot_len(const rb_t *r) {
    (void)r;
    return 0u; /* TODO: head 가 tail 뒤로 감겼는지(head < tail)에 따라 두 경우 */
}
static bool slot_empty(const rb_t *r) { (void)r; return false; /* TODO */ }
static bool slot_full(const rb_t *r)  { (void)r; return false; /* TODO */ }

/* ---------------- Part C: 미러 비트 (mirror bit) 방식 ---------------- */
/* 한 칸 전진하다 배열 끝을 넘으면 0 으로 돌아가며 미러 비트를 뒤집는다 */
static inline void adv_mirror(uint32_t *i, uint8_t *wrap) {
    (void)i; (void)wrap; /* TODO */
}
static bool mir_empty(const rb_t *r) { (void)r; return false; /* TODO: 인덱스도 같고 미러 비트도 같다 */ }
static bool mir_full(const rb_t *r)  { (void)r; return false; /* TODO: 인덱스는 같고 미러 비트가 다르다 */ }
static uint32_t mir_len(const rb_t *r) {
    (void)r;
    return 0u; /* TODO: 미러 비트가 같으면 같은 바퀴, 다르면 head 가 한 바퀴 앞 */
}
static bool mir_push(rb_t *r, uint8_t b) {
    (void)r; (void)b; (void)adv_mirror;
    return false; /* TODO */
}
static bool mir_pop(rb_t *r, uint8_t *out) {
    (void)r; (void)out;
    return false; /* TODO */
}

/* ---------------- 공통 인터페이스: 세 방식을 같은 테스트로 돌린다 ---------------- */
typedef struct {
    const char *name;
    uint32_t    cap;                          /* 실제로 담을 수 있는 바이트 수 */
    bool     (*push)(rb_t *, uint8_t);
    bool     (*pop)(rb_t *, uint8_t *);
    uint32_t (*len)(const rb_t *);
    bool     (*empty)(const rb_t *);
    bool     (*full)(const rb_t *);
} rb_ops_t;

static const rb_ops_t OPS_COUNT  = { "count",          RB_N,      cnt_push,  cnt_pop,  cnt_len,  cnt_empty,  cnt_full  };
static const rb_ops_t OPS_SLOT   = { "one-slot-empty", RB_N - 1u, slot_push, slot_pop, slot_len, slot_empty, slot_full };
static const rb_ops_t OPS_MIRROR = { "mirror-bit",     RB_N,      mir_push,  mir_pop,  mir_len,  mir_empty,  mir_full  };

/* 일부러 틀린 링: head/tail 만으로 N 칸을 다 쓰려 한다 → N 개를 넣으면 head==tail, '비었다' 고 착각 */
static bool naive_push(rb_t *r, uint8_t b) { r->buf[r->head] = b; r->head = next_idx(r->head); return true; }
static bool naive_full(const rb_t *r) { (void)r; return false; }   /* full 을 구별할 정보가 없다 */
static const rb_ops_t OPS_NAIVE  = { "naive(bug)",     RB_N,      naive_push, slot_pop, slot_len, slot_empty, naive_full };

/* ---------------- 참조 모델: 감기지 않는 1차원 배열 큐 ("당연히 맞는" 구현) ---------------- */
#define FUZZ_OPS 100000u
typedef struct { uint8_t q[FUZZ_OPS]; uint32_t front, back, cap; } model_t;
static model_t g_model;                       /* push 는 최대 FUZZ_OPS 번이므로 넘치지 않는다 */
static uint32_t model_len(const model_t *m) { return m->back - m->front; }

typedef struct { uint32_t push_ok, push_rej, pop_ok, pop_rej, at_full, at_empty; } fuzz_stats_t;

static uint32_t xorshift32(uint32_t *s) {     /* 고정 시드 → 매번 같은 연산 열 (재현 가능) */
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

/* 같은 무작위 push/pop 열을 링과 모델에 똑같이 적용하고 매 단계 결과·상태를 비교한다.
 * 처음 어긋난 지점을 한 줄 출력하고 false, 끝까지 같으면 true. */
static bool fuzz_vs_model(const rb_ops_t *o, uint32_t seed, fuzz_stats_t *st) {
    (void)o; (void)seed; (void)st; (void)xorshift32; (void)model_len; (void)g_model;
    return false; /* TODO: 아래 순서로 구현
     * 1) rb_t 와 g_model 을 초기화 (m->cap = o->cap), *st 를 0 으로
     * 2) FUZZ_OPS 번: xorshift32 로 x 를 뽑고, 64 연산마다 push 확률 70% / 30% 교대
     * 3) push: 링 결과 ok 와 모델 기대값 want(= len < cap) 비교, want 면 모델에도 넣는다
     *    pop : ok/want(= len > 0) 와 꺼낸 바이트 비교
     * 4) 매 연산 뒤 len / empty / full 을 모델과 비교, 처음 어긋나면 한 줄 출력 후 false
     * 5) 통계(st) 갱신, 끝까지 같으면 true */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* "HELLO" 를 UART 로 받은 상황: 가득 참 → overrun → 감기(wrap) → 순서대로 꺼내기 */
static void check_hello(const rb_ops_t *o) {
    rb_t r;
    rb_init(&r);
    uint8_t c = 0;
    const char *msg = "HELLO";
    CHECK(o->empty(&r) && !o->full(&r) && o->len(&r) == 0u);
    CHECK(!o->pop(&r, &c));                          /* 빈 버퍼 pop 은 실패 */
    uint32_t accepted = 0;
    for (int i = 0; i < 5; i++) accepted += o->push(&r, (uint8_t)msg[i]);
    CHECK(accepted == o->cap);                       /* count/mirror: 5, one-slot-empty: 4 */
    CHECK(o->full(&r) && !o->empty(&r) && o->len(&r) == o->cap);
    CHECK(!o->push(&r, 'X'));                        /* 가득 참 → 새 바이트는 버린다 (overrun) */
    CHECK(o->pop(&r, &c) && c == 'H');
    CHECK(o->push(&r, '!'));                         /* head 가 배열 끝을 넘어 앞쪽 칸으로 감긴다 */
    char out[8] = {0};
    uint32_t n = 0;
    while (n < 7u && o->pop(&r, &c)) out[n++] = (char)c;
    CHECK(strcmp(out, o->cap == RB_N ? "ELLO!" : "ELL!") == 0);
    CHECK(o->empty(&r) && o->len(&r) == 0u);
}

int main(void) {
    /* Part A — count */
    printf("-- Part A: count\n");
    check_hello(&OPS_COUNT);

    /* Part B — one-slot-empty: 가득 찬 상태에서도 head != tail (빈 칸 하나) */
    printf("-- Part B: one-slot-empty\n");
    check_hello(&OPS_SLOT);
    rb_t r;
    rb_init(&r);
    for (int i = 0; i < 4; i++) slot_push(&r, (uint8_t)i);
    CHECK(r.head != r.tail && next_idx(r.head) == r.tail);

    /* Part C — mirror bit: 가득 차면 인덱스는 같고 미러 비트만 다르다 */
    printf("-- Part C: mirror-bit\n");
    check_hello(&OPS_MIRROR);
    rb_init(&r);
    for (int i = 0; i < 5; i++) mir_push(&r, (uint8_t)i);
    CHECK(r.head == r.tail && r.head_wrap != r.tail_wrap);

    /* Part C — 참조 모델과 랜덤 대조 (시드 고정) */
    printf("-- Part C: random test vs reference model (%u ops, seed 0xC0FFEE)\n", FUZZ_OPS);
    const rb_ops_t *all[3] = { &OPS_COUNT, &OPS_SLOT, &OPS_MIRROR };
    printf("%-15s %8s %8s %8s %9s %8s %8s\n", "scheme", "push_ok", "overrun", "pop_ok", "pop_empty", "at_full", "at_empty");
    for (int k = 0; k < 3; k++) {
        fuzz_stats_t st = {0};
        CHECK(fuzz_vs_model(all[k], 0xC0FFEEu, &st));
        printf("%-15s %8u %8u %8u %9u %8u %8u\n", all[k]->name,
               st.push_ok, st.push_rej, st.pop_ok, st.pop_rej, st.at_full, st.at_empty);
    }
    printf("-- 테스트가 버그를 잡는가: head/tail 만 쓰는 순진한 링 (아래 불일치 한 줄은 예상된 것)\n");
    fuzz_stats_t st = {0};
    CHECK(!fuzz_vs_model(&OPS_NAIVE, 0xC0FFEEu, &st));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
