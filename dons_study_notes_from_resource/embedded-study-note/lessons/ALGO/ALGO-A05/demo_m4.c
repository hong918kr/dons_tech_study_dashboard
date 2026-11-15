/* ALGO-A05 demo — 스트리밍 디코더를 Cortex-M4 로 컴파일해 본다 (freestanding: stdint/stddef 만)
 *   (1) sizeof(lzs_t) 가 32비트 포인터에서 몇 바이트인지 (상수 lzs_sizeof 의 asm 값)
 *   (2) match 복사 루프 (링에서 바이트 단위)의 실제 Thumb-2 코드
 *   (3) 디코더 전체의 .text 크기 (플래시 비용)
 * solution.c 의 lzs_* 와 같은 논리 (헤더 검사 포함). 링·싱크는 호출자가 준다.
 * build: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4.c -o -
 *        clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os -c demo_m4.c -o demo_m4.o && llvm-size demo_m4.o
 */
#include <stddef.h>
#include <stdint.h>

enum { LZ_OK = 0, LZ_E_CAP = -1, LZ_E_HDR = -2, LZ_E_TRUNC = -3, LZ_E_DIST = -4, LZ_E_OVERRUN = -5, LZ_E_TRAIL = -6 };
enum { S_HDR, S_FLAG, S_TOK, S_M2, S_DONE, S_ERR };
typedef struct {
    uint8_t  *ring;
    uint32_t  ring_size, limit;
    void    (*put)(void *ctx, uint8_t b);
    void     *ctx;
    uint32_t  total, produced, mask;
    uint8_t   hdr[6];
    uint8_t   hdr_got, state, flags, nbit, b0, lbits, minm;
    int       err;
} lzs_t;

const unsigned lzs_sizeof = sizeof(lzs_t);

static int lzs_fail(lzs_t *s, int e) { s->state = S_ERR; s->err = e; return e; }
static void lzs_emit(lzs_t *s, uint8_t c) {
    s->ring[s->produced & s->mask] = c;
    s->put(s->ctx, c);
    s->produced++;
}
static int lzs_header(lzs_t *s, uint8_t b) {
    s->hdr[s->hdr_got++] = b;
    if (s->hdr_got < 6) return LZ_OK;
    unsigned ob = s->hdr[0], mm = s->hdr[1];
    uint32_t total = (uint32_t)s->hdr[2] | (uint32_t)s->hdr[3] << 8 | (uint32_t)s->hdr[4] << 16 | (uint32_t)s->hdr[5] << 24;
    if (ob < 8 || ob > 12 || mm < 2 || mm > 4) return lzs_fail(s, LZ_E_HDR);
    if ((1u << ob) > s->ring_size) return lzs_fail(s, LZ_E_HDR);
    if (total > s->limit) return lzs_fail(s, LZ_E_CAP);
    s->total = total; s->mask = (1u << ob) - 1u; s->lbits = (uint8_t)(16 - ob); s->minm = (uint8_t)mm;
    s->state = total == 0 ? S_DONE : S_FLAG;
    return LZ_OK;
}

/* match 복사만 따로 (asm 을 읽기 쉽게) */
void lzs_copy(lzs_t *s, uint32_t dist, uint32_t len) {
    while (len--) lzs_emit(s, s->ring[(s->produced - dist) & s->mask]);
}

int lzs_push(lzs_t *s, uint8_t b) {
    if (s->state == S_ERR) return s->err;
    if (s->state == S_HDR) return lzs_header(s, b);
    switch (s->state) {
    case S_FLAG:
        s->flags = b; s->nbit = 0; s->state = S_TOK;
        return LZ_OK;
    case S_TOK:
        if (s->flags & 1u) { s->b0 = b; s->state = S_M2; return LZ_OK; }
        lzs_emit(s, b);
        break;
    case S_M2: {
        unsigned w = (unsigned)s->b0 | (unsigned)b << 8;
        uint32_t dist = (w >> s->lbits) + 1u, len = (w & ((1u << s->lbits) - 1u)) + s->minm;
        if (dist > s->produced) return lzs_fail(s, LZ_E_DIST);
        if (len > s->total - s->produced) return lzs_fail(s, LZ_E_OVERRUN);
        lzs_copy(s, dist, len);
        break;
    }
    default:
        return lzs_fail(s, LZ_E_TRAIL);
    }
    s->flags >>= 1; s->nbit++;
    s->state = s->produced == s->total ? S_DONE : s->nbit == 8 ? S_FLAG : S_TOK;
    return LZ_OK;
}
