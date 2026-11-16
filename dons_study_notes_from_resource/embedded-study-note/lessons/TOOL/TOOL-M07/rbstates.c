/* TOOL-M07 rbstates.c — 포매터(rbdump / 요약) 채점용: 상태를 알고 있는 링 버퍼 몇 개를 만들고 checkpoint() 에서 멈춘다.
 *   cc -std=c11 -g -O0 rbstates.c -o rbstates
 *   lldb -b -o 'b checkpoint' -o run -o 'command script import ringbuf_cmd.py' -o 'rbdump rb_wrapped' ./rbstates
 * 비어 있는 칸은 0xA0+i 로 채워 둔다 → 포매터가 안 쓰는 칸을 읽으면 눈에 띈다.
 */
#include <stdint.h>
#include <stdio.h>

#define RB_CAP 16u

struct ringbuf {             /* buggy.c 와 같은 모양 */
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint8_t  buf[RB_CAP];
};

static struct ringbuf mk(uint32_t tail, uint32_t count, const char *s)
{
    struct ringbuf r = { (tail + count) % RB_CAP, tail, count, { 0 } };
    for (uint32_t i = 0; i < RB_CAP; i++)
        r.buf[i] = (uint8_t)(0xA0 + i);
    for (uint32_t i = 0; i < count; i++)
        r.buf[(tail + i) % RB_CAP] = (uint8_t)s[i];
    return r;
}

struct ringbuf rb_linear, rb_wrapped, rb_full, rb_empty, rb_slip, rb_bad;
struct ringbuf *prb = &rb_wrapped;

void checkpoint(void)
{
}

int main(void)
{
    rb_linear  = mk(2, 5, "hello");               /* tail 2 → head 7, 안 감김 */
    rb_wrapped = mk(12, 7, "ABCDEFG");            /* tail 12 → 15 → 0 → head 3, 감김 */
    rb_full    = mk(4, 16, "0123456789abcdef");   /* head == tail == 4, count 16 */
    rb_empty   = mk(9, 0, "");                    /* head == tail == 9, count 0 */
    rb_slip    = mk(14, 4, "\xC0\xDB\x00\x7F");   /* 0x80 이상 바이트와 0x00 */
    rb_bad     = mk(13, 5, "vwxyz");              /* 그다음 일부러 망가뜨린다 (추가 연습 rbcheck) */
    rb_bad.count = 20;
    rb_bad.head = 15;
    checkpoint();
    printf("%u\n", prb->count);
    return 0;
}
