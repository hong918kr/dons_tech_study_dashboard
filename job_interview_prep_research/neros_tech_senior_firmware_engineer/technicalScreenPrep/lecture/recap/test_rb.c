/* Task 1 테스트 — 면접에서 "how would you test it?" 에 그대로 쓸 수 있는 형태 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rb.h"

// --- tests ---
static void test_init_empty(void)
{
    uint8_t buf[4];
    rb_t rb;
    rb_init(&rb, buf, sizeof buf);
    uint8_t b;
    assert(rb_is_empty(&rb) && !rb_is_full(&rb));
    assert(!rb_pop_byte(&rb, &b) && !rb_peek(&rb, 0, &b));
}

static void test_fifo_and_full(void)
{
    uint8_t buf[3], b;
    rb_t rb;
    rb_init(&rb, buf, sizeof buf);
    assert(rb_push_byte(&rb, 1) && rb_push_byte(&rb, 2) && rb_push_byte(&rb, 3));
    assert(rb_is_full(&rb) && !rb_push_byte(&rb, 4));          /* 용량 전부 사용, 넘치면 거절 */
    assert(rb_pop_byte(&rb, &b) && b == 1);
}

static void test_wraparound(void)
{
    uint8_t buf[3], b;
    rb_t rb;
    rb_init(&rb, buf, sizeof buf);
    for (uint8_t i = 0; i < 20; i++) {                          /* 여러 바퀴 */
        assert(rb_push_byte(&rb, i));
        assert(rb_pop_byte(&rb, &b) && b == i);
    }
    assert(rb_is_empty(&rb));
}

static void test_bulk_all_or_nothing(void)
{
    uint8_t buf[5], out[5];
    rb_t rb;
    rb_init(&rb, buf, sizeof buf);
    const uint8_t a[] = {1, 2, 3}, big[] = {9, 9, 9};
    assert(rb_push(&rb, a, 3));
    assert(!rb_push(&rb, big, 3) && rb.count == 3);             /* 자리 2개 < 3 → 아무것도 안 넣음 */
    assert(!rb_pop(&rb, out, 4) && rb.count == 3);              /* 3개 < 4 → 아무것도 안 뺌 */
    assert(rb_pop(&rb, out, 3) && memcmp(out, a, 3) == 0);
    assert(rb_push(&rb, a, 0) && rb_pop(&rb, out, 0));          /* len 0 은 성공, 변화 없음 */
}

static void test_peek_offset(void)
{
    uint8_t buf[4], b;
    rb_t rb;
    rb_init(&rb, buf, sizeof buf);
    const uint8_t a[] = {10, 20, 30};
    rb_push(&rb, a, 3);
    rb_pop_byte(&rb, &b);
    rb_push_byte(&rb, 40);
    rb_push_byte(&rb, 50);                                      /* wrap 된 상태: 20 30 40 50 */
    assert(rb_peek(&rb, 0, &b) && b == 20);
    assert(rb_peek(&rb, 3, &b) && b == 50);
    assert(!rb_peek(&rb, 4, &b) && rb.count == 4);              /* 범위 밖, 소비 안 함 */
}

int main(void)
{
    test_init_empty();
    test_fifo_and_full();
    test_wraparound();
    test_bulk_all_or_nothing();
    test_peek_offset();
    puts("test_rb: PASS");
    return 0;
}
// --- end ---
