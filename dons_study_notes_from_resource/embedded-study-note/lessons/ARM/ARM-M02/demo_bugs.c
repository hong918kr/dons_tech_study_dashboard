/* ARM-M02 demo_bugs.c — 제약/clobber 를 하나씩 빼먹은 inline asm 이 -O2 에서 실제로 틀리는지 본다.
 *   cc  -std=c11 -O2 demo_bugs.c -o /tmp/ARM-M02_bugs && /tmp/ARM-M02_bugs          (Apple clang 21, -Wasm-operand-widths 경고 1개는 의도)
 *   cc  -std=c11 -O2 -S -fno-asynchronous-unwind-tables demo_bugs.c -o -            (asm 확인)
 *   VM: gcc -std=gnu11 -O2 demo_bugs.c -o /tmp/ARM-M02_bugs && /tmp/ARM-M02_bugs     (gcc 15)
 * 각 줄: bad_ 함수의 결과 vs 기대값 → BUG 재현 / ok(이 컴파일러에선 우연히 맞음).
 * 기대값은 손으로 쓴 상수다. 버그 판에서 무엇이 나오는지는 컴파일러·옵션에 따라 다르다 (그게 요점).
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NOI __attribute__((noinline))
typedef struct { uint64_t lo, hi; } u128;
static int g_rep, g_tot;
static void report(const char *name, uint64_t got, uint64_t want) {
    g_tot++;
    if (got != want) g_rep++;
    printf("%-26s got=0x%016" PRIx64 " want=0x%016" PRIx64 "  %s\n", name, got, want,
           got != want ? "BUG reproduced" : "ok (not reproduced here)");
}

/* 1) early-clobber 누락: lo 를 쓴 뒤 ah/bh 를 읽는데 "=r" → lo 가 ah 의 레지스터를 받을 수 있다 */
NOI u128 bad_add128(uint64_t ah, uint64_t al, uint64_t bh, uint64_t bl) {
    uint64_t h, l;
    __asm__("adds %[l], %[al], %[bl]\n\tadc %[h], %[ah], %[bh]"
            : [l] "=r"(l), [h] "=r"(h) : [al] "r"(al), [bl] "r"(bl), [ah] "r"(ah), [bh] "r"(bh) : "cc");
    return (u128){l, h};
}
/* 2) 캐리 체인을 asm 문장 여러 개로 쪼갬: 문장 사이 컴파일러 코드(cmp/subs)가 C 플래그를 덮는다 */
NOI void bad_bn_add(uint64_t *r, const uint64_t *a, const uint64_t *b, long n) {
    __asm__("adds %0, %1, %2" : "=r"(r[0]) : "r"(a[0]), "r"(b[0]) : "cc");
    for (long i = 1; i < n; i++)
        __asm__("adcs %0, %1, %2" : "=r"(r[i]) : "r"(a[i]), "r"(b[i]) : "cc");
}
/* 3) "cc" 누락: asm 이 플래그를 바꾸는데 컴파일러는 asm 앞에서 만든 플래그를 asm 뒤에서 쓴다 */
NOI int64_t bad_cc_clang(int64_t x, int64_t b, int64_t y) {     /* clang 이 걸리는 모양 */
    int64_t d = x - b;
    __asm__("adds %0, %0, #1" : "+r"(y));
    return d == 0 ? y : d;
}
NOI int64_t bad_cc_gcc(int64_t a, int64_t b, int64_t x) {       /* gcc 가 걸리는 모양 */
    int c = a < b;
    __asm__("adds %0, %0, #1" : "+r"(x));
    return c ? x : 0;
}
/* 4) "memory" 누락 (asm 이 포인터로 쓴다): 컴파일러는 *p 가 그대로라고 보고 옛 값을 돌려준다 */
NOI uint64_t bad_zero16(uint64_t *p) {
    p[0] = 5;
    __asm__ volatile("stp xzr, xzr, [%0]" : : "r"(p));
    return p[0];
}
/* 5) "memory" 누락 (asm 이 포인터로 읽는다 = DMA 엔진에 디스크립터 주소만 넘김): 저장 자체가 사라질 수 있다 */
NOI void scribble(void) { volatile uint32_t junk[64]; for (int i = 0; i < 64; i++) junk[i] = 0x5A5A5A5Au; }
NOI uint32_t bad_dev_read_volatile(uint32_t x) {
    uint32_t d[2]; d[0] = x; d[1] = x + 1;
    uint32_t a, b;
    __asm__ volatile("ldp %w0, %w1, [%2]" : "=r"(a), "=r"(b) : "r"(d));
    return a + b;
}
NOI uint32_t bad_dev_read_plain(uint32_t x) {
    uint32_t d[2]; d[0] = x; d[1] = x + 1;
    uint32_t a, b;
    __asm__("ldp %w0, %w1, [%2]" : "=r"(a), "=r"(b) : "r"(d));
    return a + b;
}
/* 6) 폭(width) 불일치: uint32_t 피연산자를 %x 로 → 위 32비트 쓰레기 / 64비트 결과를 %w 로 → 위 32비트 0 */
NOI uint64_t bad_add_off(uint64_t base, uint64_t big) {
    uint32_t off = (uint32_t)big;            /* 잘라도 명령은 안 생긴다: x1 의 위 32비트는 big 그대로 */
    uint64_t r;
    __asm__("add %0, %1, %2" : "=r"(r) : "r"(base), "r"(off));   /* clang: -Wasm-operand-widths 경고 */
    return r;
}
NOI uint64_t bad_rbit64(uint64_t x) { uint64_t r; __asm__("rbit %w0, %w1" : "=r"(r) : "r"(x)); return r; }
/* 7) volatile 누락: 출력만 보고 "같은 입력 → 같은 값" 으로 간주 → 두 번 읽기가 하나로 합쳐지거나 루프 밖으로 */
static inline uint64_t cnt_nv(void) { uint64_t v; __asm__("mrs %0, cntvct_el0" : "=r"(v)); return v; }
NOI uint64_t bad_elapsed(volatile uint32_t *spin, int n) {
    uint64_t t0 = cnt_nv();
    for (int i = 0; i < n; i++) (void)*spin;
    uint64_t t1 = cnt_nv();
    return t1 - t0;
}
NOI uint64_t bad_poll(uint64_t timeout, uint64_t cap) {   /* 데드라인까지 돌기 — cap 은 안전장치 */
    uint64_t deadline = cnt_nv() + timeout, spins = 0;
    while (cnt_nv() < deadline && spins < cap) spins++;
    return spins;
}

int main(void) {
    volatile uint64_t vin = 1;               /* 상수 전파 막기 */
    uint64_t one = vin;
#if defined(__clang__)
    printf("compiler: clang %s\n", __clang_version__);
#else
    printf("compiler: gcc %s\n", __VERSION__);
#endif
    u128 s = bad_add128(one, ~0ull, 2 * one, one);          /* (1:~0) + (2:1) = (4:0) */
    report("1 early-clobber hi", s.hi, 4);
    uint64_t a[3] = {one, 0, 0}, b[3] = {one, 0, 0}, r[3] = {0};
    bad_bn_add(r, a, b, 3);                                 /* {1,0,0}+{1,0,0} = {2,0,0} */
    report("2 split carry r[1]", r[1], 0);
    report("2 split carry r[2]", r[2], 0);
    report("3 no \"cc\" (clang shape)", (uint64_t)bad_cc_clang(5, 3, -1), 2);   /* d = 2 */
    report("3 no \"cc\" (gcc shape)", (uint64_t)bad_cc_gcc(1, 2, 5), 6);        /* 1<2 → x+1 */
    uint64_t buf[2] = {7, 7};
    report("4 no \"memory\" stale", bad_zero16(buf), 0);
    scribble();
    report("5 no \"memory\" dead st (v)", bad_dev_read_volatile(10), 21);
    scribble();
    report("5 no \"memory\" dead st", bad_dev_read_plain(10), 21);
    report("6 %x on uint32_t", bad_add_off(0x1000, 0xFFFFFFFF00000010ull), 0x1010);
    report("6 %w on uint64_t", bad_rbit64(one), 0x8000000000000000ull);
    volatile uint32_t sp = 0;
    report("7 no volatile elapsed>0", bad_elapsed(&sp, 1000000) > 0, 1);
    uint64_t spins = bad_poll(1000000 /* 1 ms @1GHz, 42 ms @24MHz */, 200000000ull);
    report("7 no volatile poll<cap", spins < 200000000ull, 1);
    printf("%d/%d bugs reproduced with this compiler\n", g_rep, g_tot);
    return 0;
}
