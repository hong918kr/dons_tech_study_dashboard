/* ARM-A06 일부러 틀린 CRC-32C 6종 (TODO 아님) — 하네스의 테스트 묶음이 이것들을 잡는지 확인한다.
 * 모두 메모리 안전하다 (p+n 밖을 읽지 않는다): 값만 틀린다.
 *  bug_no_invert  : 조건화(~)를 빼먹음 — 명령이 ~ 를 해줄 거라고 착각
 *  bug_ieee_body  : 본문만 __crc32d (CRC-32 IEEE) — crc32cx 와 crc32x 혼동 (머리·꼬리는 맞음)
 *  bug_bswap_body : 8 B 를 빅 엔디언으로 읽음 — "네트워크 순서" 착각
 *  bug_tail_no1   : 꼬리에서 1 바이트 단계 누락
 *  bug_3way_no33  : 3-way 시프트 상수를 x^(8B) 로 (x^33 보정 누락)
 *  bug_3way_swapk : 3-way 에서 c0 와 c1 의 상수를 바꿔 씀
 */
#include <arm_acle.h>
#include <arm_neon.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define HW __attribute__((target("+crc,+aes")))

static inline uint16_t ld16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static inline uint32_t ld32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint64_t ld64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

/* 맞는 1-way (variant 로 한 군데씩 망가뜨린다) */
enum { OK = 0, IEEE_BODY = 1, BSWAP_BODY = 2, TAIL_NO1 = 3 };
HW static uint32_t raw_c(uint32_t c, const uint8_t *p, size_t n, int bug) {
    if (n >= 1 && ((uintptr_t)p & 1)) { c = __crc32cb(c, *p);      p += 1; n -= 1; }
    if (n >= 2 && ((uintptr_t)p & 2)) { c = __crc32ch(c, ld16(p)); p += 2; n -= 2; }
    if (n >= 4 && ((uintptr_t)p & 4)) { c = __crc32cw(c, ld32(p)); p += 4; n -= 4; }
    for (; n >= 8; p += 8, n -= 8) {
        uint64_t v = ld64(p);
        if (bug == IEEE_BODY)       c = __crc32d(c, v);                 /* 틀림: crc32x */
        else if (bug == BSWAP_BODY) c = __crc32cd(c, __builtin_bswap64(v));   /* 틀림: BE */
        else                        c = __crc32cd(c, v);
    }
    if (n & 4) { c = __crc32cw(c, ld32(p)); p += 4; }
    if (n & 2) { c = __crc32ch(c, ld16(p)); p += 2; }
    if ((n & 1) && bug != TAIL_NO1) c = __crc32cb(c, *p);              /* 틀림: 1 B 누락 */
    return c;
}

uint32_t bug_no_invert(uint32_t crc, const void *p, size_t n)  { return raw_c(crc, p, n, OK); }
uint32_t bug_ieee_body(uint32_t crc, const void *p, size_t n)  { return ~raw_c(~crc, p, n, IEEE_BODY); }
uint32_t bug_bswap_body(uint32_t crc, const void *p, size_t n) { return ~raw_c(~crc, p, n, BSWAP_BODY); }
uint32_t bug_tail_no1(uint32_t crc, const void *p, size_t n)   { return ~raw_c(~crc, p, n, TAIL_NO1); }

HW static uint32_t mulk(uint32_t c, uint32_t k) {
    return __crc32cd(0, vgetq_lane_u64(vreinterpretq_u64_p128(vmull_p64((poly64_t)c, (poly64_t)k)), 0));
}

/* 3-way (한 단계, B = 128 → 384 B 덩어리). k1/k2 를 인자로 받아 버그 상수를 넣는다. */
HW static uint32_t three_way(uint32_t crc, const uint8_t *p, size_t n, uint32_t k1, uint32_t k2) {
    const size_t B = 128;
    uint32_t c = ~crc;
    size_t head = (size_t)(-(uintptr_t)p & 7);
    if (head > n) head = n;
    c = raw_c(c, p, head, OK); p += head; n -= head;
    while (n >= 3 * B) {
        uint32_t c0 = c, c1 = 0, c2 = 0;
        for (size_t i = 0; i < B; i += 8) {
            c0 = __crc32cd(c0, ld64(p + i));
            c1 = __crc32cd(c1, ld64(p + B + i));
            c2 = __crc32cd(c2, ld64(p + 2 * B + i));
        }
        c = mulk(c0, k2) ^ mulk(c1, k1) ^ c2;
        p += 3 * B; n -= 3 * B;
    }
    return ~raw_c(c, p, n, OK);
}
/* 맞는 상수: k1 = x^(8·128−33) = 0x0D3B6092, k2 = x^(16·128−33) = 0xB9E02B86 (mod P_c) */
uint32_t bug_3way_no33(uint32_t crc, const void *p, size_t n) {
    return three_way(crc, p, n, 0xB8FDB1E7u, 0x88E56F72u);   /* x^(8·128), x^(16·128) — x^33 보정 없음 */
}
uint32_t bug_3way_swapk(uint32_t crc, const void *p, size_t n) {
    return three_way(crc, p, n, 0xB9E02B86u, 0x0D3B6092u);   /* k1 ↔ k2 */
}
