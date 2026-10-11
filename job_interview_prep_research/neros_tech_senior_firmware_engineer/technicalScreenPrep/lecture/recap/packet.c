#include "packet.h"
#include "crc.h"

// --- impl ---
int extract_packet(rb_t *rb, uint8_t *out, size_t out_cap)
{
    uint8_t b, len, crc_rx;
    uint8_t body[1 + 255];                         /* LEN + PAYLOAD (CRC 계산용) */

    for (;;) {
        /* 1) sync 찾기: 앞쪽 쓰레기 버리기 */
        while (rb_peek(rb, 0, &b) && b != PKT_SYNC)
            rb_pop_byte(rb, &b);
        /* 2) 헤더(SYNC + LEN)가 다 왔나 */
        if (!rb_peek(rb, 1, &len))
            return PKT_NEED_MORE;
        /* 3) 프레임 전체(SYNC + LEN + PAYLOAD + CRC)가 다 왔나 — peek 만, 아직 안 뺌 */
        size_t frame = (size_t)len + 3;
        if (frame > rb->capacity) {                /* 버퍼에 절대 다 못 들어오는 길이 = 가짜 sync */
            rb_pop_byte(rb, &b);                   /*  → 안 버리면 영원히 NEED_MORE (데드락) */
            continue;
        }
        if (rb->count < frame)
            return PKT_NEED_MORE;
        /* 4) CRC 확인: LEN 과 PAYLOAD 를 peek 으로 복사 */
        for (size_t i = 0; i <= len; i++)
            rb_peek(rb, (uint8_t)(1 + i), &body[i]);
        rb_peek(rb, (uint8_t)(len + 2), &crc_rx);
        if (crc8(body, (size_t)len + 1) != crc_rx) {
            rb_pop_byte(rb, &b);                   /* 가짜 sync 일 수 있다 → sync 한 바이트만 버리고 다시 찾기 */
            continue;
        }
        /* 5) 정상 프레임: rb 에서 제거하고 payload 복사 */
        if (len > out_cap) {
            for (size_t i = 0; i < frame; i++)
                rb_pop_byte(rb, &b);
            return PKT_TOO_BIG;
        }
        rb_pop_byte(rb, &b);                       /* SYNC */
        rb_pop_byte(rb, &b);                       /* LEN */
        rb_pop(rb, out, len);                      /* PAYLOAD */
        rb_pop_byte(rb, &b);                       /* CRC */
        return len;
    }
}
// --- end ---
