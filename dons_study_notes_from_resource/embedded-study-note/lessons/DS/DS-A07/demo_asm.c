/* DS-A07 demo_asm.c — 조회가 정말로 "반복 없는" 코드로 컴파일되는지 본다 (freestanding, main 없음)
 * cc -O2 -S -o - demo_asm.c                                                       # 이 맥 arm64
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S -o - demo_asm.c   # Cortex-M4
 */
typedef unsigned int u32;
typedef struct { u32 key, val; } slot_t;

#define CK_EMPTY 0xFFFFFFFFu

static inline u32 mix(u32 seed, u32 key) {
    u32 h = key ^ seed;
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
    return h;
}

/* 표 크기 고정: T0, T1 각각 1024칸. 찾으면 값의 주소, 없으면 0. */
#define CAP 1024u
slot_t g_t0[CAP], g_t1[CAP];
u32 g_s0, g_s1;

u32 *ck_lookup(u32 key) {
    if (key == CK_EMPTY) return 0;
    slot_t *a = &g_t0[mix(g_s0, key) & (CAP - 1u)];
    if (a->key == key) return &a->val;
    slot_t *b = &g_t1[mix(g_s1, key) & (CAP - 1u)];
    if (b->key == key) return &b->val;
    return 0;
}
