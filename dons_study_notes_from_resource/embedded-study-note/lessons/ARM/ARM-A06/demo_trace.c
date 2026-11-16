/* ARM-A06 demo: crc32c 의 머리/본문/꼬리 단계를 한 줄씩 — 레지스터(조건화 없는 상태) 값 추적
 * 빌드: cc -std=c11 -O2 demo_trace.c -o /tmp/ARM-A06_trace && /tmp/ARM-A06_trace
 * 1) "123456789" 를 8 B 정렬 주소에 두고  2) 같은 29 바이트 메시지를 주소 % 8 = 5 에 두고 — 단계와 상태를 출력.
 */
#include <arm_acle.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HW __attribute__((target("+crc")))

HW static uint32_t trace(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;                                    /* ~crc, crc = 0 */
    printf("  start  p%%8=%u n=%zu  state=0x%08X\n", (unsigned)((uintptr_t)p & 7), n, c);
    if (n >= 1 && ((uintptr_t)p & 1)) { c = __crc32cb(c, *p); printf("  head crc32cb  1 B -> 0x%08X\n", c); p += 1; n -= 1; }
    if (n >= 2 && ((uintptr_t)p & 2)) { uint16_t v; memcpy(&v, p, 2); c = __crc32ch(c, v); printf("  head crc32ch  2 B -> 0x%08X\n", c); p += 2; n -= 2; }
    if (n >= 4 && ((uintptr_t)p & 4)) { uint32_t v; memcpy(&v, p, 4); c = __crc32cw(c, v); printf("  head crc32cw  4 B -> 0x%08X\n", c); p += 4; n -= 4; }
    for (; n >= 8; p += 8, n -= 8) { uint64_t v; memcpy(&v, p, 8); c = __crc32cd(c, v); printf("  body crc32cx  8 B (x=0x%016llX) -> 0x%08X\n", (unsigned long long)v, c); }
    if (n & 4) { uint32_t v; memcpy(&v, p, 4); c = __crc32cw(c, v); printf("  tail crc32cw  4 B -> 0x%08X\n", c); p += 4; }
    if (n & 2) { uint16_t v; memcpy(&v, p, 2); c = __crc32ch(c, v); printf("  tail crc32ch  2 B -> 0x%08X\n", c); p += 2; }
    if (n & 1) { c = __crc32cb(c, *p); printf("  tail crc32cb  1 B -> 0x%08X\n", c); }
    printf("  final ~state = 0x%08X\n", ~c);
    return ~c;
}

int main(void) {
    static _Alignas(16) uint8_t buf[64];
    memcpy(buf, "123456789", 9);
    printf("1) \"123456789\" at an 8-aligned address\n");
    trace(buf, 9);
    const char *msg = "The quick brown fox jumps ove";                /* 29 B */
    memcpy(buf + 5, msg, 29);
    printf("2) 29-byte message at address %% 8 = 5\n");
    uint32_t a = trace(buf + 5, 29);
    memcpy(buf + 16, msg, 29);
    printf("   same bytes at an 8-aligned address (no head):\n");
    uint32_t b = trace(buf + 16, 29);
    printf("   equal: %s\n", a == b ? "yes" : "NO");
    return 0;
}
