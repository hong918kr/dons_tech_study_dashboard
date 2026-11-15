/* ALGO-A09 demo_target — 겹침 판정 한 줄이 Cortex-M4 에서 무엇이 되나 (freestanding: stdint/stdbool 만).
 * build: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -
 * 세 가지 판정 (영역 a = [ab, ab+as), b = [bb, bb+bs)):
 *   ov_u64   끝을 uint64_t 로 — 언제나 맞다 (base+size 가 2^32 이상이어도)
 *   ov_last  끝을 "마지막 바이트" (base+size-1) 로 — size != 0 이고 base+size <= 2^32 일 때 맞다 (32비트만)
 *   ov_wrap  끝을 uint32_t base+size 로 — 0xFFFFF000+0x1000 이 0 이 되어 틀린다 (7. 실수)
 */
#include <stdbool.h>
#include <stdint.h>

bool ov_u64(uint32_t ab, uint32_t as, uint32_t bb, uint32_t bs) {
    uint64_t ae = (uint64_t)ab + as, be = (uint64_t)bb + bs;
    return as != 0 && bs != 0 && ab < be && bb < ae;
}

bool ov_last(uint32_t ab, uint32_t as, uint32_t bb, uint32_t bs) {
    if (as == 0 || bs == 0) return false;
    uint32_t al = ab + (as - 1u), bl = bb + (bs - 1u);
    return ab <= bl && bb <= al;
}

bool ov_wrap(uint32_t ab, uint32_t as, uint32_t bb, uint32_t bs) {
    uint32_t ae = ab + as, be = bb + bs;
    return as != 0 && bs != 0 && ab < be && bb < ae;
}
