/* 06 · Ring buffer — free-running head/tail (2의 거듭제곱, count 없음)
 * prompt: rb_t {buf[8], head, tail}. head/tail 은 마스크 없이 계속 증가, 접근할 때만 & (SIZE - 1). 개수 = head - tail.
 * note: 05 와 비교: count 가 없어서 push 는 head 만, pop 은 tail 만 쓴다 → ISR 생산자 / main 소비자(SPSC)에서 lock 없이 쓸 수 있는 구조
 * note: unsigned 뺄셈이라 head 가 2^32 를 넘어 0 으로 돌아도 head - tail 이 맞다 → 테스트는 UINT32_MAX 근처에서 시작
 * note: % 대신 & 마스크 → 나눗셈 없음. 대신 크기가 반드시 2의 거듭제곱
 */
// --- solution ---
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define RB_SIZE 8u                               /* 2 의 거듭제곱 */
#define RB_MASK (RB_SIZE - 1u)

typedef struct {
    uint8_t  buf[RB_SIZE];
    uint32_t head, tail;
} rb_t;

bool rb_push(rb_t *rb, uint8_t v)
{
    if (rb->head - rb->tail == RB_SIZE)
        return false;
    rb->buf[rb->head & RB_MASK] = v;
    rb->head++;
    return true;
}

bool rb_pop(rb_t *rb, uint8_t *v)
{
    if (rb->head == rb->tail)
        return false;
    *v = rb->buf[rb->tail & RB_MASK];
    rb->tail++;
    return true;
}

static void test_rb_index_wrap(void)
{
    rb_t rb = {{0}, UINT32_MAX - 3, UINT32_MAX - 3};      /* 32비트 wrap 을 지나가게 */
    uint8_t v;
    for (uint8_t i = 0; i < RB_SIZE; i++)
        assert(rb_push(&rb, i));
    assert(!rb_push(&rb, 99) && rb.head - rb.tail == RB_SIZE);
    for (uint8_t i = 0; i < RB_SIZE; i++)
        assert(rb_pop(&rb, &v) && v == i);
    assert(!rb_pop(&rb, &v));
}

int main(void)
{
    test_rb_index_wrap();
    puts("PASS");
    return 0;
}
// --- end ---
