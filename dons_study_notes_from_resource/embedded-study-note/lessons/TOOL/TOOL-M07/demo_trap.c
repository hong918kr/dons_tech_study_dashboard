/* TOOL-M07 demo_trap.c — 펌웨어식 ASSERT: 불변식이 깨지면 바로 trap (lldb -k 데모용) */
#include <stdint.h>
#include <stdio.h>

struct ringbuf { uint32_t head, tail, count; uint8_t buf[16]; };
struct ringbuf q;

#define ASSERT(c) do { if (!(c)) __builtin_trap(); } while (0)

static void q_check(const struct ringbuf *r)
{
    ASSERT(r->count <= 16 && r->head == (r->tail + r->count) % 16);
}

int main(void)
{
    for (uint32_t i = 0; i < 40; i++) {
        q.buf[q.head] = (uint8_t)i;
        q.head = (q.head + 1) % 16;
        q.count++;                       /* 꽉 찼는지 안 본다 → 17번째에서 count 17 */
        q_check(&q);
    }
    printf("never\n");
    return 0;
}
