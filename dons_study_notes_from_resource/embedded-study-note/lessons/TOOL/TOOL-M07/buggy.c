/* TOOL-M07 buggy.c — 디버깅 대상 프로그램 (고치지 말고 디버거로 범인 줄을 찾는다)
 *
 * UART 송신 경로 흉내: link_task() 가 프레임을 SLIP 으로 인코딩해 링 버퍼 dev.tx 에 넣고,
 * uart_tx_isr() 가 매 tick 1~2 바이트씩 빼 간다. 결정적(LCG 시드 고정) — 매번 같은 tick 에서 깨진다.
 * 증상: 7000 tick 넘어서 "bad link_state 0xdd" 가 찍히고 exit 1.
 *   cc -std=c11 -g -O0 buggy.c -o buggy && ./buggy
 */
#include <stdint.h>
#include <stdio.h>

#define RB_CAP       16u
#define SLIP_END     0xC0u   /* 프레임 끝 */
#define SLIP_ESC     0xDBu   /* 이스케이프: 데이터 안의 END/ESC 는 2바이트로 바꿔 보낸다 */
#define SLIP_ESC_END 0xDCu
#define SLIP_ESC_ESC 0xDDu

enum { LINK_IDLE = 0, LINK_SENDING = 1, LINK_WAIT_ACK = 2, LINK_ERROR = 3 };

struct ringbuf {
    uint32_t head;           /* 다음에 쓸 칸 */
    uint32_t tail;           /* 다음에 읽을 칸 */
    uint32_t count;          /* 들어 있는 바이트 수 (head == tail 이면 empty 인지 full 인지 count 로 구분) */
    uint8_t  buf[RB_CAP];
};

struct uart_dev {
    struct ringbuf tx;       /* SLIP 인코딩된 바이트가 UART 로 나가길 기다리는 큐 */
    uint32_t link_state;     /* LINK_* (0..3) — tx.buf 바로 뒤에 놓인 이웃 필드 */
    uint32_t frames_sent;
    uint32_t bad_state;
};

struct uart_dev dev;         /* 전역: 주소가 고정이라 워치포인트 걸기 좋다 */
uint32_t g_tick;
static uint32_t g_seed = 1234u;
static uint32_t frame_left;  /* 이번 프레임에 남은 payload 바이트 */
static uint8_t  pending;     /* 큐가 차서 못 넣은 바이트 (다음 tick 에 재시도) */
static int      has_pending;

static uint32_t rnd(void)
{
    g_seed = g_seed * 1103515245u + 12345u;
    return (g_seed >> 16) & 0x7fffu;
}

static int rb_put(struct ringbuf *rb, uint8_t b)
{
    if (rb->count == RB_CAP)
        return 0;
    rb->buf[rb->head] = b;
    rb->head = (rb->head + 1) % RB_CAP;
    rb->count++;
    return 1;
}

static int rb_put_escaped(struct ringbuf *rb, uint8_t b)
{
    if (RB_CAP - rb->count < 2)
        return 0;
    rb->buf[rb->head] = SLIP_ESC;
    rb->buf[rb->head + 1] = (b == SLIP_END) ? SLIP_ESC_END : SLIP_ESC_ESC;
    rb->head = (rb->head + 2) % RB_CAP;
    rb->count += 2;
    return 1;
}

static int rb_get(struct ringbuf *rb, uint8_t *out)
{
    if (rb->count == 0)
        return 0;
    *out = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % RB_CAP;
    rb->count--;
    return 1;
}

static int slip_put(struct ringbuf *rb, uint8_t b)
{
    if (b == SLIP_END || b == SLIP_ESC)
        return rb_put_escaped(rb, b);
    return rb_put(rb, b);
}

static void link_task(void)
{
    switch (dev.link_state) {
    case LINK_IDLE:
        if (rnd() % 4 == 0) {
            frame_left = 4 + rnd() % 8;
            dev.link_state = LINK_SENDING;
        }
        break;
    case LINK_SENDING:
        for (int i = 0; i < 2 && frame_left > 0; i++) {
            if (!has_pending) {
                pending = (uint8_t)rnd();
                has_pending = 1;
            }
            if (!slip_put(&dev.tx, pending))
                break;
            has_pending = 0;
            frame_left--;
        }
        if (frame_left == 0 && rb_put(&dev.tx, SLIP_END)) {
            dev.frames_sent++;
            dev.link_state = LINK_WAIT_ACK;
        }
        break;
    case LINK_WAIT_ACK:
        if (rnd() % 3 == 0)
            dev.link_state = LINK_IDLE;
        break;
    default:
        dev.bad_state++;
        printf("tick %u: bad link_state 0x%x\n", g_tick, dev.link_state);
        dev.link_state = LINK_ERROR;
        break;
    }
}

static void uart_tx_isr(void)
{
    uint8_t b;
    int n = 1 + (int)(rnd() % 2);
    while (n-- > 0 && rb_get(&dev.tx, &b)) {
    }
}

int main(void)
{
    dev.link_state = LINK_IDLE;
    for (g_tick = 1; g_tick <= 20000; g_tick++) {
        link_task();
        uart_tx_isr();
        if (dev.link_state == LINK_ERROR)
            break;
    }
    printf("ticks=%u frames=%u bad_state=%u\n", g_tick, dev.frames_sent, dev.bad_state);
    return dev.bad_state ? 1 : 0;
}
