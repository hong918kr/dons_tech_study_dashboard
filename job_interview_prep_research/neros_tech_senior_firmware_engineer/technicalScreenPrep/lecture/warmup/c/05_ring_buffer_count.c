/* 05 · Ring buffer — head · tail · count (아무 크기나 가능)
 * prompt: rb_t {buf[RB_CAP], head, tail, count}. rb_push(rb, v) → bool (가득 차면 false), rb_pop(rb, &v) → bool (비었으면 false).
 * note: count 가 full / empty 를 구분하므로 용량을 전부 쓴다. 인덱스는 `(i + 1) % RB_CAP` 로 감는다 → 크기가 2의 거듭제곱일 필요 없음
 * note: 단점: push 와 pop 이 둘 다 count 를 고친다 → ISR 생산자 + main 소비자면 count 가 공유 변수가 되어 보호가 필요 (06 은 이 문제가 없다)
 * note: 테스트 포인트 — 정확히 가득, 한 칸 넘침, 한 바퀴 돈 뒤의 순서
 */
// --- solution ---
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define RB_CAP 5u                                /* 아무 크기 */

typedef struct {
    uint8_t buf[RB_CAP];
    uint8_t head;                                /* 다음에 쓸 칸 */
    uint8_t tail;                                /* 다음에 읽을 칸 */
    uint8_t count;
} rb_t;

bool rb_push(rb_t *rb, uint8_t v)
{
    if (rb->count == RB_CAP)
        return false;
    rb->buf[rb->head] = v;
    rb->head = (uint8_t)((rb->head + 1) % RB_CAP);
    rb->count++;
    return true;
}

bool rb_pop(rb_t *rb, uint8_t *v)
{
    if (rb->count == 0)
        return false;
    *v = rb->buf[rb->tail];
    rb->tail = (uint8_t)((rb->tail + 1) % RB_CAP);
    rb->count--;
    return true;
}

static void test_rb(void)
{
    rb_t rb = {0};
    uint8_t v;
    for (uint8_t i = 0; i < RB_CAP; i++)
        assert(rb_push(&rb, i));
    assert(!rb_push(&rb, 99));                   /* 가득 */
    assert(rb_pop(&rb, &v) && v == 0);
    assert(rb_push(&rb, 5));                     /* wrap: index 0 에 들어감 */
    for (uint8_t i = 1; i <= 5; i++)
        assert(rb_pop(&rb, &v) && v == i);
    assert(!rb_pop(&rb, &v));                    /* 빔 */
}

int main(void)
{
    test_rb();
    puts("PASS");
    return 0;
}
// --- end ---
