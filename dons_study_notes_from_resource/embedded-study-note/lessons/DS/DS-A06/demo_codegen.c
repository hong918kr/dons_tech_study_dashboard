/* DS-A06 demo_codegen.c — 매핑과 비트맵 탐색이 실제로 몇 개의 명령이 되는가 (링크 없이 -S 로만 본다)
 *
 *   cc -std=c11 -O2 -S demo_codegen.c -o -                                                 # 이 맥 (arm64)
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S -o - demo_codegen.c  # CLZ/RBIT 있음
 *   clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S -o - demo_codegen.c  # CLZ 없음
 */
typedef unsigned int u32;

#define ALIGN_LOG2  3u
#define SLI         4u
#define FL_SHIFT    (SLI + ALIGN_LOG2)
#define SMALL_LIMIT (1u << FL_SHIFT)

static inline u32 fls32(u32 x) { return 31u - (u32)__builtin_clz(x); }

u32 map_key(u32 size) {                                   /* (fl << 8) | sl */
    if (size < SMALL_LIMIT) return size >> ALIGN_LOG2;
    u32 f = fls32(size);
    return ((f - (FL_SHIFT - 1u)) << 8) | ((size >> (f - SLI)) ^ (1u << SLI));
}

u32 map_key_up(u32 size) {
    if (size >= SMALL_LIMIT) size += (1u << (fls32(size) - SLI)) - 1u;
    return map_key(size);
}

u32 find_bit(u32 fl_map, const u32 *sl_map, u32 fl, u32 sl) {
    u32 m = sl_map[fl] & (~0u << sl);
    if (m == 0u) {
        u32 f = fl_map & (~0u << (fl + 1u));
        if (f == 0u) return 0xFFFFFFFFu;
        fl = (u32)__builtin_ctz(f);
        m = sl_map[fl];
    }
    return (fl << 8) | (u32)__builtin_ctz(m);
}
