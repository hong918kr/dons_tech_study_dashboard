/* ENV: cross (object only) — ARM-B12 코드 밀도 비교용 "작은 펌웨어" 함수 9개.
 * 실행하지 않는다. 4개 타깃으로 -Os 컴파일해서 objdump 로 크기만 본다 (make_dumps.sh).
 * freestanding: <stdint.h>/<stddef.h>/<stdbool.h> 만 사용.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 1) 레지스터 비트 set/clear (RMW on a volatile MMIO register) */
void reg_set_bits(volatile uint32_t *reg, uint32_t mask) { *reg |= mask; }
void reg_clear_bits(volatile uint32_t *reg, uint32_t mask) { *reg &= ~mask; }

/* 2) 레지스터 필드 갱신: [shift+width-1:shift] = val (C-B04 의 SET_FIELD) */
void reg_write_field(volatile uint32_t *reg, uint32_t mask, unsigned shift, uint32_t val) {
    uint32_t v = *reg;
    v &= ~(mask << shift);
    v |= (val & mask) << shift;
    *reg = v;
}

/* 3) 링 버퍼 push (크기 2의 거듭제곱, UART RX ISR 에서 호출하는 모양) */
#define RB_SIZE 64u
typedef struct {
    volatile uint16_t head;
    volatile uint16_t tail;
    uint8_t buf[RB_SIZE];
} ring_t;

bool rb_push(ring_t *r, uint8_t b) {
    uint16_t h = r->head;
    uint16_t next = (uint16_t)((h + 1u) & (RB_SIZE - 1u));
    if (next == r->tail) return false; /* full */
    r->buf[h] = b;
    r->head = next;
    return true;
}

/* 4) CRC-8 (poly 0x07, init 0x00), 비트 단위 루프 — 테이블 없음 */
uint8_t crc8(const uint8_t *p, size_t n) {
    uint8_t c = 0;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++)
            c = (uint8_t)((c & 0x80u) ? (c << 1) ^ 0x07u : (c << 1));
    }
    return c;
}

/* 5) switch 상태 기계: 프레임 파서 SOF(0x7E) LEN PAYLOAD... CHK */
enum { ST_IDLE, ST_LEN, ST_DATA, ST_CHK, ST_DONE, ST_ERR };
typedef struct {
    uint8_t state, len, idx, sum;
    uint8_t data[32];
} fsm_t;

int fsm_feed(fsm_t *s, uint8_t b) {
    switch (s->state) {
    case ST_IDLE:
        if (b == 0x7E) { s->state = ST_LEN; s->sum = 0; }
        break;
    case ST_LEN:
        if (b == 0 || b > sizeof s->data) { s->state = ST_ERR; break; }
        s->len = b; s->idx = 0; s->state = ST_DATA;
        break;
    case ST_DATA:
        s->data[s->idx++] = b; s->sum = (uint8_t)(s->sum + b);
        if (s->idx == s->len) s->state = ST_CHK;
        break;
    case ST_CHK:
        s->state = (b == s->sum) ? ST_DONE : ST_ERR;
        break;
    default:
        break;
    }
    return s->state;
}

/* 6) 구조체 복사 (설정 블록 24 B) */
typedef struct {
    uint32_t baud;
    uint16_t flags;
    uint8_t  parity, stop;
    uint32_t timeout_ms;
    uint32_t fifo_lvl;
    uint32_t crc;
    uint32_t magic;
} cfg_t;

void cfg_copy(cfg_t *dst, const cfg_t *src) { *dst = *src; }

/* 7) 포화 덧셈 (오디오/ADC 누적) */
int16_t sat_add_s16(int16_t a, int16_t b) {
    int32_t s = (int32_t)a + b;
    if (s > INT16_MAX) return INT16_MAX;
    if (s < INT16_MIN) return INT16_MIN;
    return (int16_t)s;
}

/* 8) 작은 memcpy (바이트 루프; -ffreestanding 이라 memcpy 호출로 안 바뀐다) */
void small_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
}

/* 9) ADC 평균: 진짜 나눗셈 (n 은 런타임 값) — 나눗셈 명령 유무가 드러난다 */
uint32_t adc_average(const uint16_t *samples, uint32_t n) {
    uint32_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc += samples[i];
    return n ? acc / n : 0;
}
