/* 01_spsc_ring_isr.c — ISR-safe SPSC ring buffer (모범답안)
 *
 * 빌드: cc -std=c11 -Wall -Wextra -O2 01_spsc_ring_isr.c -o sol_01
 * 또는: make sol N=01
 *
 * 구조: producer 한 명(UART RX ISR), consumer 한 명(main loop / task).
 *       head는 producer만 쓰고 tail은 consumer만 쓴다. 각 index의 writer가
 *       한 명뿐이라 read-modify-write 경쟁이 없고, 따라서 lock이 필요 없다.
 *
 * 주의: 여기서 말하는 ISR/task는 설명용이고, 이 파일은 호스트에서 단일 스레드로
 *       도는 자기 검증 테스트만 포함한다. 하드웨어 헤더는 쓰지 않는다.
 */

#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. 자료구조                                                         */
/* ------------------------------------------------------------------ */

#define RB_SIZE 256u /* 반드시 2의 거듭제곱 */

_Static_assert((RB_SIZE & (RB_SIZE - 1u)) == 0u, "RB_SIZE must be power of 2");

typedef struct {
    uint8_t buf[RB_SIZE];
    /* free-running index. RB_SIZE로 자르지 않고 계속 증가시킨다.
     * 접근할 때만 & (RB_SIZE-1)로 자른다. */
    _Atomic uint32_t head;    /* producer(ISR)만 store 한다 */
    _Atomic uint32_t tail;    /* consumer(task)만 store 한다 */
    uint32_t         dropped; /* full 때문에 버린 바이트. producer만 만진다 */
} ringbuf_t;

/* ------------------------------------------------------------------ */
/* 2. 초기화                                                           */
/* ------------------------------------------------------------------ */

static inline void rb_init(ringbuf_t *rb)
{
    atomic_init(&rb->head, 0u);
    atomic_init(&rb->tail, 0u);
    rb->dropped = 0u;
    /* buf[]는 일부러 지우지 않는다. head==tail이면 내용은 의미가 없다. */
}

/* ------------------------------------------------------------------ */
/* 3. 관찰 함수 (양쪽에서 호출 가능)                                    */
/* ------------------------------------------------------------------ */

/* 들어 있는 바이트 수. h - t는 unsigned 산술이라 index가 2^32에서
 * 0으로 넘어가도 정확하다. */
static inline uint32_t rb_count(const ringbuf_t *rb)
{
    uint32_t h = atomic_load_explicit(&rb->head, memory_order_acquire);
    uint32_t t = atomic_load_explicit(&rb->tail, memory_order_acquire);
    return h - t;
}

static inline uint32_t rb_space(const ringbuf_t *rb)
{
    return RB_SIZE - rb_count(rb);
}

static inline bool rb_is_empty(const ringbuf_t *rb)
{
    return rb_count(rb) == 0u;
}

static inline bool rb_is_full(const ringbuf_t *rb)
{
    return rb_count(rb) == RB_SIZE;
}

/* ------------------------------------------------------------------ */
/* 4. Producer 쪽 (ISR 문맥)                                           */
/* ------------------------------------------------------------------ */

/* 한 바이트 넣는다. full이면 false를 돌려주고 dropped를 올린다.
 * ISR은 기다릴 수 없으므로 블로킹하지 않는다. */
static inline bool rb_put(ringbuf_t *rb, uint8_t b)
{
    /* head는 나만 쓰므로 relaxed로 읽어도 된다. */
    uint32_t h = atomic_load_explicit(&rb->head, memory_order_relaxed);
    /* tail은 상대가 쓴다. acquire로 읽어야 상대의 "다 읽었다"가 보인다. */
    uint32_t t = atomic_load_explicit(&rb->tail, memory_order_acquire);

    if ((uint32_t)(h - t) == RB_SIZE) {
        rb->dropped++;
        return false;
    }

    rb->buf[h & (RB_SIZE - 1u)] = b;
    /* release store: 위의 데이터 쓰기가 head 갱신보다 먼저 보이도록 보장한다.
     * 순서가 뒤집히면 consumer가 쓰레기 값을 유효 데이터로 읽는다. */
    atomic_store_explicit(&rb->head, h + 1u, memory_order_release);
    return true;
}

/* 여러 바이트를 한 번에 넣는다. head는 마지막에 딱 한 번만 올린다.
 * 실제로 넣은 바이트 수를 돌려준다(부분 성공 허용). */
static inline uint32_t rb_write(ringbuf_t *rb, const uint8_t *src, uint32_t n)
{
    uint32_t h    = atomic_load_explicit(&rb->head, memory_order_relaxed);
    uint32_t t    = atomic_load_explicit(&rb->tail, memory_order_acquire);
    uint32_t free = RB_SIZE - (uint32_t)(h - t);

    if (n > free) {
        rb->dropped += (n - free);
        n = free;
    }
    if (n == 0u) {
        return 0u;
    }

    uint32_t off   = h & (RB_SIZE - 1u);  /* 실제 배열 위치 */
    uint32_t first = RB_SIZE - off;       /* 끝까지 연속으로 쓸 수 있는 길이 */
    if (first > n) {
        first = n;
    }
    memcpy(&rb->buf[off], src, first);
    if (n > first) {
        memcpy(&rb->buf[0], src + first, n - first); /* wrap 된 나머지 */
    }

    atomic_store_explicit(&rb->head, h + n, memory_order_release);
    return n;
}

/* ------------------------------------------------------------------ */
/* 5. Consumer 쪽 (task / main loop 문맥)                              */
/* ------------------------------------------------------------------ */

/* 한 바이트 꺼낸다. empty면 false. */
static inline bool rb_get(ringbuf_t *rb, uint8_t *out)
{
    /* tail은 나만 쓰므로 relaxed. */
    uint32_t t = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    /* head는 상대가 쓴다. acquire로 읽어야 상대가 쓴 buf[] 내용도 같이 보인다. */
    uint32_t h = atomic_load_explicit(&rb->head, memory_order_acquire);

    if (h == t) {
        return false; /* empty */
    }

    *out = rb->buf[t & (RB_SIZE - 1u)];
    /* release store: 데이터를 다 읽은 뒤에야 slot을 producer에게 돌려준다. */
    atomic_store_explicit(&rb->tail, t + 1u, memory_order_release);
    return true;
}

/* 최대 n 바이트를 꺼낸다. 실제로 꺼낸 수를 돌려준다. */
static inline uint32_t rb_read(ringbuf_t *rb, uint8_t *dst, uint32_t n)
{
    uint32_t t   = atomic_load_explicit(&rb->tail, memory_order_relaxed);
    uint32_t h   = atomic_load_explicit(&rb->head, memory_order_acquire);
    uint32_t avail = h - t;

    if (n > avail) {
        n = avail;
    }
    if (n == 0u) {
        return 0u;
    }

    uint32_t off   = t & (RB_SIZE - 1u);
    uint32_t first = RB_SIZE - off;
    if (first > n) {
        first = n;
    }
    memcpy(dst, &rb->buf[off], first);
    if (n > first) {
        memcpy(dst + first, &rb->buf[0], n - first);
    }

    atomic_store_explicit(&rb->tail, t + n, memory_order_release);
    return n;
}

/* ------------------------------------------------------------------ */
/* 6. 별칭 — 문제 지문에서 쓰는 push/pop 이름                           */
/*    S01 드릴의 이름은 rb_put/rb_get이라 그쪽을 정식으로 두고,        */
/*    같은 함수를 push/pop 이름으로도 부를 수 있게 한다.               */
/* ------------------------------------------------------------------ */

static inline bool rb_push(ringbuf_t *rb, uint8_t b) { return rb_put(rb, b); }
static inline bool rb_pop(ringbuf_t *rb, uint8_t *out) { return rb_get(rb, out); }

/* ------------------------------------------------------------------ */
/* 7. 테스트                                                           */
/* ------------------------------------------------------------------ */

#define OK(msg) printf("  ok  %s\n", (msg))

static void test_init_empty(void)
{
    ringbuf_t rb;
    rb_init(&rb);
    assert(rb_is_empty(&rb));
    assert(!rb_is_full(&rb));
    assert(rb_count(&rb) == 0u);
    assert(rb_space(&rb) == RB_SIZE);

    uint8_t b = 0xAA;
    assert(rb_get(&rb, &b) == false); /* empty에서 get은 실패 */
    assert(b == 0xAA);                /* 실패 시 out을 건드리지 않는다 */
    OK("init 직후 empty, get 실패, out 미변경");
}

static void test_single_roundtrip(void)
{
    ringbuf_t rb;
    rb_init(&rb);

    assert(rb_put(&rb, 0x5A));
    assert(rb_count(&rb) == 1u);
    assert(!rb_is_empty(&rb));

    uint8_t b = 0;
    assert(rb_get(&rb, &b));
    assert(b == 0x5A);
    assert(rb_is_empty(&rb));
    OK("put 1개 -> count 1 -> get 하면 같은 값, 다시 empty");
}

static void test_fifo_order(void)
{
    ringbuf_t rb;
    rb_init(&rb);

    for (uint32_t i = 0; i < 16u; i++) {
        assert(rb_push(&rb, (uint8_t)(i * 3u + 1u)));
    }
    assert(rb_count(&rb) == 16u);

    for (uint32_t i = 0; i < 16u; i++) {
        uint8_t b = 0;
        assert(rb_pop(&rb, &b));
        assert(b == (uint8_t)(i * 3u + 1u)); /* FIFO 순서 */
    }
    assert(rb_is_empty(&rb));
    OK("FIFO 순서 유지 (push/pop 별칭 포함)");
}

static void test_full_behaviour(void)
{
    ringbuf_t rb;
    rb_init(&rb);

    /* 한 칸 비워 두지 않으므로 RB_SIZE개가 전부 들어가야 한다. */
    for (uint32_t i = 0; i < RB_SIZE; i++) {
        assert(rb_put(&rb, (uint8_t)i));
    }
    assert(rb_is_full(&rb));
    assert(rb_count(&rb) == RB_SIZE);
    assert(rb_space(&rb) == 0u);

    assert(rb_put(&rb, 0xFF) == false); /* full이면 드롭 */
    assert(rb.dropped == 1u);
    assert(rb_count(&rb) == RB_SIZE);   /* 드롭이 내용을 망치지 않는다 */

    uint8_t b = 0;
    assert(rb_get(&rb, &b));
    assert(b == 0u);                    /* 가장 오래된 값이 나온다 */
    assert(rb_put(&rb, 0x99));          /* 한 칸 비었으니 다시 성공 */
    assert(rb_is_full(&rb));
    OK("RB_SIZE개 전부 사용, full이면 드롭 + dropped 증가");
}

static void test_wrap_around_buffer(void)
{
    ringbuf_t rb;
    rb_init(&rb);

    /* 버퍼 끝 근처로 밀어 놓는다: 250개 넣고 250개 뺀다. */
    for (uint32_t i = 0; i < 250u; i++) {
        assert(rb_put(&rb, (uint8_t)i));
    }
    for (uint32_t i = 0; i < 250u; i++) {
        uint8_t b = 0;
        assert(rb_get(&rb, &b));
        assert(b == (uint8_t)i);
    }
    assert(rb_is_empty(&rb));
    assert(rb_count(&rb) == 0u);

    /* 이제 head=tail=250. 20개를 넣으면 물리적으로 wrap 한다. */
    for (uint32_t i = 0; i < 20u; i++) {
        assert(rb_put(&rb, (uint8_t)(0xB0u + i)));
    }
    for (uint32_t i = 0; i < 20u; i++) {
        uint8_t b = 0;
        assert(rb_get(&rb, &b));
        assert(b == (uint8_t)(0xB0u + i));
    }
    assert(rb_is_empty(&rb));
    OK("버퍼 끝을 넘어가는 wrap에서도 순서·내용 유지");
}

static void test_index_overflow(void)
{
    ringbuf_t rb;
    rb_init(&rb);

    /* index를 2^32 경계 직전으로 옮긴다. 테스트 전용 조작이다. */
    atomic_store_explicit(&rb.head, 0xFFFFFFF8u, memory_order_relaxed);
    atomic_store_explicit(&rb.tail, 0xFFFFFFF8u, memory_order_relaxed);
    assert(rb_is_empty(&rb));

    for (uint32_t i = 0; i < 16u; i++) { /* 경계를 8개 넘어간다 */
        assert(rb_put(&rb, (uint8_t)(0x40u + i)));
    }
    assert(rb_count(&rb) == 16u); /* h - t가 unsigned wrap을 견딘다 */
    assert(atomic_load_explicit(&rb.head, memory_order_relaxed) == 8u);

    for (uint32_t i = 0; i < 16u; i++) {
        uint8_t b = 0;
        assert(rb_get(&rb, &b));
        assert(b == (uint8_t)(0x40u + i));
    }
    assert(rb_is_empty(&rb));
    OK("index가 0xFFFFFFFF를 넘어가도 count와 순서가 정확");
}

static void test_bulk_write_read(void)
{
    ringbuf_t rb;
    rb_init(&rb);

    uint8_t src[300];
    for (uint32_t i = 0; i < 300u; i++) {
        src[i] = (uint8_t)(i ^ 0x5Au);
    }

    /* 300 > RB_SIZE 이므로 256개만 들어가고 44개는 드롭된다. */
    uint32_t w = rb_write(&rb, src, 300u);
    assert(w == RB_SIZE);
    assert(rb.dropped == 300u - RB_SIZE);
    assert(rb_is_full(&rb));

    uint8_t dst[300];
    memset(dst, 0, sizeof dst);
    uint32_t r = rb_read(&rb, dst, 300u);
    assert(r == RB_SIZE);
    assert(memcmp(dst, src, RB_SIZE) == 0);
    assert(rb_is_empty(&rb));
    OK("bulk write/read: 공간만큼만 쓰고 내용이 그대로 나온다");
}

static void test_bulk_split_at_wrap(void)
{
    ringbuf_t rb;
    rb_init(&rb);

    /* head를 200으로 밀어 두면 bulk write가 memcpy 두 번으로 쪼개진다. */
    for (uint32_t i = 0; i < 200u; i++) {
        assert(rb_put(&rb, (uint8_t)i));
    }
    uint8_t sink[200];
    assert(rb_read(&rb, sink, 200u) == 200u);
    assert(rb_is_empty(&rb));

    uint8_t src[100];
    for (uint32_t i = 0; i < 100u; i++) {
        src[i] = (uint8_t)(0xC0u + i);
    }
    assert(rb_write(&rb, src, 100u) == 100u); /* 56 + 44 로 쪼개진다 */
    assert(rb_count(&rb) == 100u);

    uint8_t dst[100];
    memset(dst, 0, sizeof dst);
    assert(rb_read(&rb, dst, 100u) == 100u);  /* 읽기도 쪼개진다 */
    assert(memcmp(dst, src, 100u) == 0);
    assert(rb_is_empty(&rb));
    OK("wrap 지점을 가로지르는 bulk write/read가 memcpy 2번으로 정확");
}

/* producer와 consumer가 번갈아 도는 상황을 결정적으로 흉내 낸다.
 * ISR이 3바이트씩 넣고 main loop가 2바이트씩 빼는 식으로 속도가 다르다. */
static void test_interleaved_producer_consumer(void)
{
    ringbuf_t rb;
    rb_init(&rb);

    uint32_t produced = 0, consumed = 0;
    uint8_t  expect   = 0;

    for (uint32_t round = 0; round < 1000u; round++) {
        for (uint32_t k = 0; k < 3u; k++) { /* "ISR" */
            if (rb_put(&rb, (uint8_t)(produced & 0xFFu))) {
                produced++;
            }
        }
        for (uint32_t k = 0; k < 2u; k++) { /* "main loop" */
            uint8_t b = 0;
            if (rb_get(&rb, &b)) {
                assert(b == expect); /* 한 바이트도 건너뛰거나 겹치지 않는다 */
                expect++;
                consumed++;
            }
        }
        assert(rb_count(&rb) == produced - consumed);
        assert(rb_count(&rb) <= RB_SIZE);
    }

    uint8_t b = 0;
    while (rb_get(&rb, &b)) { /* 남은 것 비우기 */
        assert(b == expect);
        expect++;
        consumed++;
    }
    assert(consumed == produced);
    assert(rb.dropped > 0u); /* 3:2 비율이라 언젠가 반드시 넘친다 */
    OK("producer/consumer 속도 차이 1000라운드: 누락·중복 없음");
}

int main(void)
{
    printf("=== 01 SPSC ring buffer (ISR-safe) ===\n");
    test_init_empty();
    test_single_roundtrip();
    test_fifo_order();
    test_full_behaviour();
    test_wrap_around_buffer();
    test_index_overflow();
    test_bulk_write_read();
    test_bulk_split_at_wrap();
    test_interleaved_producer_consumer();
    printf("ALL TESTS PASSED\n");
    return 0;
}
