/* 01_spsc_ring_isr.c — ISR-safe SPSC ring buffer (starter)
 *
 * 빌드/실행: make run N=01
 *
 * 규칙:
 *   - 아래 TODO 부분만 채운다. 테스트(main과 test_* 함수)는 건드리지 않는다.
 *   - 인터럽트를 끄지 말 것. mutex/spinlock 금지. 동적 할당 금지.
 *   - head는 producer만, tail은 consumer만 store 한다.
 *   - 채우기 전에도 컴파일은 된다(테스트는 assert에서 멈춘다).
 *
 * 힌트가 필요하면 problems/01_spsc_ring_isr.md의 "요구사항"만 다시 읽는다.
 * notes/01_spsc_ring_isr.md는 다 풀고 나서 연다.
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
    _Atomic uint32_t head;    /* producer(ISR)만 store 한다 */
    _Atomic uint32_t tail;    /* consumer(task)만 store 한다 */
    uint32_t         dropped; /* full 때문에 버린 바이트 수 */
} ringbuf_t;

/* ------------------------------------------------------------------ */
/* 2. 여기부터 구현                                                    */
/* ------------------------------------------------------------------ */

static inline void rb_init(ringbuf_t *rb)
{
    /* TODO: head, tail, dropped를 0으로. buf[]는 지울 필요 없다. */
    (void)rb;
}

/* 들어 있는 바이트 수. index가 2^32를 넘어가도 정확해야 한다. */
static inline uint32_t rb_count(const ringbuf_t *rb)
{
    /* TODO */
    (void)rb;
    return 0u;
}

static inline uint32_t rb_space(const ringbuf_t *rb)
{
    /* TODO */
    (void)rb;
    return 0u;
}

static inline bool rb_is_empty(const ringbuf_t *rb)
{
    /* TODO */
    (void)rb;
    return true;
}

static inline bool rb_is_full(const ringbuf_t *rb)
{
    /* TODO */
    (void)rb;
    return false;
}

/* Producer(ISR): 한 바이트 넣는다. full이면 false + dropped 증가. */
static inline bool rb_put(ringbuf_t *rb, uint8_t b)
{
    /* TODO: head를 읽고, tail을 읽고, full 검사, 데이터 먼저 쓰고,
     *       마지막에 head를 올린다. 이 순서가 뒤집히면 안 된다. */
    (void)rb;
    (void)b;
    return false;
}

/* Producer(ISR): 여러 바이트. 실제로 넣은 수를 돌려준다.
 * head는 마지막에 한 번만 올린다. wrap 지점에서는 memcpy 두 번. */
static inline uint32_t rb_write(ringbuf_t *rb, const uint8_t *src, uint32_t n)
{
    /* TODO */
    (void)rb;
    (void)src;
    (void)n;
    return 0u;
}

/* Consumer(task): 한 바이트 꺼낸다. empty면 false이고 out은 건드리지 않는다. */
static inline bool rb_get(ringbuf_t *rb, uint8_t *out)
{
    /* TODO: tail을 읽고, head를 읽고, empty 검사, 데이터 먼저 읽고,
     *       마지막에 tail을 올린다. */
    (void)rb;
    (void)out;
    return false;
}

/* Consumer(task): 최대 n 바이트. 실제로 꺼낸 수를 돌려준다. */
static inline uint32_t rb_read(ringbuf_t *rb, uint8_t *dst, uint32_t n)
{
    /* TODO */
    (void)rb;
    (void)dst;
    (void)n;
    return 0u;
}

/* 문제 지문에서 쓰는 push/pop 이름. 그대로 두면 된다. */
static inline bool rb_push(ringbuf_t *rb, uint8_t b) { return rb_put(rb, b); }
static inline bool rb_pop(ringbuf_t *rb, uint8_t *out) { return rb_get(rb, out); }

/* ------------------------------------------------------------------ */
/* 3. 테스트 — 여기부터는 수정하지 않는다                              */
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
    assert(rb_get(&rb, &b) == false);
    assert(b == 0xAA);
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
        assert(b == (uint8_t)(i * 3u + 1u));
    }
    assert(rb_is_empty(&rb));
    OK("FIFO 순서 유지 (push/pop 별칭 포함)");
}

static void test_full_behaviour(void)
{
    ringbuf_t rb;
    rb_init(&rb);

    for (uint32_t i = 0; i < RB_SIZE; i++) {
        assert(rb_put(&rb, (uint8_t)i));
    }
    assert(rb_is_full(&rb));
    assert(rb_count(&rb) == RB_SIZE);
    assert(rb_space(&rb) == 0u);

    assert(rb_put(&rb, 0xFF) == false);
    assert(rb.dropped == 1u);
    assert(rb_count(&rb) == RB_SIZE);

    uint8_t b = 0;
    assert(rb_get(&rb, &b));
    assert(b == 0u);
    assert(rb_put(&rb, 0x99));
    assert(rb_is_full(&rb));
    OK("RB_SIZE개 전부 사용, full이면 드롭 + dropped 증가");
}

static void test_wrap_around_buffer(void)
{
    ringbuf_t rb;
    rb_init(&rb);

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

    atomic_store_explicit(&rb.head, 0xFFFFFFF8u, memory_order_relaxed);
    atomic_store_explicit(&rb.tail, 0xFFFFFFF8u, memory_order_relaxed);
    assert(rb_is_empty(&rb));

    for (uint32_t i = 0; i < 16u; i++) {
        assert(rb_put(&rb, (uint8_t)(0x40u + i)));
    }
    assert(rb_count(&rb) == 16u);
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
    assert(rb_write(&rb, src, 100u) == 100u);
    assert(rb_count(&rb) == 100u);

    uint8_t dst[100];
    memset(dst, 0, sizeof dst);
    assert(rb_read(&rb, dst, 100u) == 100u);
    assert(memcmp(dst, src, 100u) == 0);
    assert(rb_is_empty(&rb));
    OK("wrap 지점을 가로지르는 bulk write/read가 memcpy 2번으로 정확");
}

static void test_interleaved_producer_consumer(void)
{
    ringbuf_t rb;
    rb_init(&rb);

    uint32_t produced = 0, consumed = 0;
    uint8_t  expect   = 0;

    for (uint32_t round = 0; round < 1000u; round++) {
        for (uint32_t k = 0; k < 3u; k++) {
            if (rb_put(&rb, (uint8_t)(produced & 0xFFu))) {
                produced++;
            }
        }
        for (uint32_t k = 0; k < 2u; k++) {
            uint8_t b = 0;
            if (rb_get(&rb, &b)) {
                assert(b == expect);
                expect++;
                consumed++;
            }
        }
        assert(rb_count(&rb) == produced - consumed);
        assert(rb_count(&rb) <= RB_SIZE);
    }

    uint8_t b = 0;
    while (rb_get(&rb, &b)) {
        assert(b == expect);
        expect++;
        consumed++;
    }
    assert(consumed == produced);
    assert(rb.dropped > 0u);
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
