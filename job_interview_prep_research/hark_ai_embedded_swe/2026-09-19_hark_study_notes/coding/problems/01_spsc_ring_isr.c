/* 01_spsc_ring_isr.c — ISR-safe SPSC ring buffer
 *
 * 빌드: cc -std=c11 -Wall -Wextra -O2 01_spsc_ring_isr.c -o run_01
 * 또는: make prob N=01 && make run N=01
 *
 * 요구사항: 문제 파일(01_spsc_ring_isr.md)을 읽고, 아래 // TODO 섹션들을 채워라.
 * 모든 테스트가 PASS 되면 완성이다.
 *
 * 힌트:
 * - head/tail은 producer/consumer만 store 한다
 * - 데이터 쓰기 후 index 갱신, index 읽기 후 데이터 읽기 순서를 지켜야 한다
 * - memory_order_acquire/release를 써서 메모리 순서를 보장하자
 * - index는 2^32에서 wrap 해도 정확해야 한다 (부호 없는 뺄셈)
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
    // TODO: head, tail, dropped 필드를 추가하세요.
    // - head: producer(ISR)만 store. _Atomic uint32_t 타입.
    // - tail: consumer(task)만 store. _Atomic uint32_t 타입.
    // - dropped: full 때문에 버린 바이트 수. uint32_t 타입 (producer만 건드림).
} ringbuf_t;

/* ------------------------------------------------------------------ */
/* 2. 초기화                                                           */
/* ------------------------------------------------------------------ */

static inline void rb_init(ringbuf_t *rb)
{
    // TODO: head, tail을 0으로 초기화하세요. atomic_init 또는 atomic_store 사용.
    // dropped도 0으로 초기화하세요.
}

/* ------------------------------------------------------------------ */
/* 3. 관찰 함수 (양쪽에서 호출 가능)                                    */
/* ------------------------------------------------------------------ */

/* 들어 있는 바이트 수. h - t는 unsigned 산술이라 index가 2^32에서
 * 0으로 넘어가도 정확하다. */
static inline uint32_t rb_count(const ringbuf_t *rb)
{
    // TODO: head를 memory_order_acquire로, tail을 memory_order_acquire로 읽고,
    //       h - t를 돌려주세요.
    // 예: uint32_t h = atomic_load_explicit(&rb->head, memory_order_acquire);
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
    // TODO: 구현하세요.
    // 1. head를 memory_order_relaxed로 읽는다 (producer는 자신의 head만 믿는다).
    // 2. tail을 memory_order_acquire로 읽는다 (consumer가 읽었다는 신호를 받는다).
    // 3. (h - t) == RB_SIZE 이면 full: dropped++, false 반환.
    // 4. 아니면 buf[h & (RB_SIZE - 1)] = b로 데이터를 쓴다.
    // 5. head를 memory_order_release로 h + 1로 갱신한다 (데이터 쓰기가 먼저 보이도록).
    // 6. true 반환.
}

/* 여러 바이트를 한 번에 넣는다. head는 마지막에 딱 한 번만 올린다.
 * 실제로 넣은 바이트 수를 돌려준다(부분 성공 허용). */
static inline uint32_t rb_write(ringbuf_t *rb, const uint8_t *src, uint32_t n)
{
    // TODO: 구현하세요.
    // 1. head, tail을 읽고 free 공간을 계산한다.
    // 2. n > free이면 dropped를 조정하고 n을 free로 제한한다.
    // 3. n == 0이면 0 반환.
    // 4. off = h & (RB_SIZE - 1)로 시작 위치를 계산.
    // 5. first = min(RB_SIZE - off, n)으로 끝까지 연속 쓰기 길이를 계산.
    // 6. memcpy로 first개를 쓴다.
    // 7. n > first이면 처음부터 나머지를 쓴다 (wrap).
    // 8. head를 memory_order_release로 h + n으로 갱신.
    // 9. 실제로 쓴 n을 반환.
}

/* ------------------------------------------------------------------ */
/* 5. Consumer 쪽 (task / main loop 문맥)                              */
/* ------------------------------------------------------------------ */

/* 한 바이트 꺼낸다. empty면 false. */
static inline bool rb_get(ringbuf_t *rb, uint8_t *out)
{
    // TODO: 구현하세요.
    // 1. tail을 memory_order_relaxed로 읽는다 (consumer는 자신의 tail만 믿는다).
    // 2. head를 memory_order_acquire로 읽는다 (producer가 썼다는 신호를 받는다).
    // 3. h == t이면 empty: false 반환 (out 미변경).
    // 4. 아니면 *out = buf[t & (RB_SIZE - 1)]로 데이터를 읽는다.
    // 5. tail을 memory_order_release로 t + 1로 갱신한다 (데이터 읽기가 먼저 보이도록).
    // 6. true 반환.
}

/* 최대 n 바이트를 꺼낸다. 실제로 꺼낸 수를 돌려준다. */
static inline uint32_t rb_read(ringbuf_t *rb, uint8_t *dst, uint32_t n)
{
    // TODO: 구현하세요.
    // rb_write의 반대 버전. rb_get처럼 메모리 순서를 지킨다.
    // 1. tail, head를 읽고 avail = h - t 계산.
    // 2. n > avail이면 n을 avail로 제한.
    // 3. n == 0이면 0 반환.
    // 4. off = t & (RB_SIZE - 1), first = min(RB_SIZE - off, n).
    // 5. memcpy로 first개를 읽는다.
    // 6. n > first이면 처음부터 나머지를 읽는다 (wrap).
    // 7. tail을 memory_order_release로 t + n으로 갱신.
    // 8. 실제로 읽은 n을 반환.
}

/* ------------------------------------------------------------------ */
/* 6. 별칭 — 문제 지문에서 쓰는 push/pop 이름                           */
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
