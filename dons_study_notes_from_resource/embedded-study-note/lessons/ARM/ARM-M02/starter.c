/* ARM-M02 C 인라인 어셈블리: 제약조건과 Clobber (AArch64, GNU extended asm)
 *
 *   Part A  래퍼 6종: rbit32 / rbit64 / clz64 (clz64(0)=64) / ctz64 / add128 (ADDS+ADC 를 asm 하나에) /
 *           read_cntvct + read_cntfrq (mrs, volatile)
 *   Part B  제약·수정자 경계: %w + uxtw (위 32비트 쓰레기), "cc" (asm 이 NZCV 를 바꿈), "memory" (포인터로 쓰기),
 *           "w" + %d/%s (FP 레지스터), "I"/"K"/"L" 즉시값
 *   Part C  드라이버 헬퍼: mmio_write32 ("=Q" + "rZ") / mmio_read32 ("Q") / dma_wmb·dma_rmb (dmb + "memory"),
 *           가짜 NIC TX 링에 디스크립터 게시 → 도어벨, CNTVCT 데드라인 폴링
 *
 * 틀린 제약이 -O2 에서 실제로 무엇을 망가뜨리는지는 demo_bugs.c (clang / gcc 실측 출력은 md).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/ARM-M02_solution && /tmp/ARM-M02_solution
 */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(__aarch64__)
#error "ARM-M02 needs an AArch64 host (Apple Silicon / arm64 Linux)"
#endif

/* ======================= Part A — 기본 래퍼 ======================= */

/* RBIT: 비트 순서 뒤집기. 32비트 값은 %w (w 레지스터) 로 쓴다. */
static inline uint32_t rbit32(uint32_t x) {
    uint32_t r = 0;
    (void)x; /* TODO: RBIT — 32비트 값이니 %w 수정자 */
    return r;
}

static inline uint64_t rbit64(uint64_t x) {
    uint64_t r = 0;
    (void)x; /* TODO: RBIT — 64비트 (%x) */
    return r;
}

/* CLZ: 0 이면 64 (ISA 정의). __builtin_clzll(0) 은 UB 라서 여기선 asm 이 더 "정의된" 쪽이다. */
static inline unsigned clz64(uint64_t x) {
    uint64_t r = 0;
    (void)x; /* TODO: CLZ */
    return (unsigned)r;
}

/* CTZ = CLZ(RBIT(x)). r 을 쓴 뒤에는 x 를 다시 읽지 않으므로 early-clobber 가 필요 없다. */
static inline unsigned ctz64(uint64_t x) {
    uint64_t r = 0;
    (void)x; /* TODO: RBIT 다음 CLZ — asm 문장 하나에 두 명령 */
    return (unsigned)r;
}

/* 128비트 덧셈: 캐리 체인(ADDS → ADC)은 반드시 asm 문장 하나 안에.
 * lo 는 ah/bh 를 읽기 전에 써지므로 "=&r" (early-clobber). ADDS 가 NZCV 를 바꾸므로 "cc". */
static inline void add128(uint64_t a_hi, uint64_t a_lo, uint64_t b_hi, uint64_t b_lo, uint64_t *hi, uint64_t *lo) {
    uint64_t h = 0, l = 0;
    /* TODO: ADDS (하위) → ADC (상위) 를 asm 문장 "하나" 에.
     *       어느 출력이 early-clobber("=&r") 여야 하나? clobber 목록은?
     */
    (void)a_hi; (void)a_lo; (void)b_hi; (void)b_lo;

    *hi = h;
    *lo = l;
}

/* 가상 카운터. 입력이 없으므로 volatile 이 없으면 컴파일러가 "같은 값" 으로 보고 합치거나 루프 밖으로 뺀다. */
static inline uint64_t read_cntvct(void) {
    uint64_t v = 0;
    /* TODO: MRS CNTVCT_EL0 — volatile 이 왜 필요한가 */
    return v;
}

static inline uint64_t read_cntfrq(void) {
    uint64_t v = 0;
    /* TODO: MRS CNTFRQ_EL0 */
    return v;
}

/* ======================= Part B — 제약·수정자 경계 ======================= */

/* base + (32비트 오프셋, 0 확장). uint32_t 인자를 담은 x 레지스터의 위 32비트는 쓰레기일 수 있다 →
 * %w 로 w 레지스터를 쓰고 uxtw 로 0 확장. */
static inline uint64_t add_u64_u32(uint64_t base, uint32_t off) {
    uint64_t r = 0;
    (void)base; (void)off; /* TODO: ADD (extended register) — off 는 %w + uxtw */
    return r;
}

/* ADDS 로 +1: 결과는 레지스터지만 NZCV 를 덮는다 → "cc" 를 꼭 적는다. */
static inline uint64_t inc_flags(uint64_t y) {
    /* TODO: ADDS y, y, #1 — 읽고 쓰는 오퍼랜드("+r") + 플래그 clobber */
    return y;
}

/* 포인터가 가리키는 16바이트를 0 으로. 주소만 "r" 로 넘기므로 "memory" 가 없으면 컴파일러는 *p 를 모른다. */
static inline void zero16(void *p) {
    (void)p; /* TODO: STP XZR, XZR, [p] — 주소만 넘길 때 필요한 clobber */
}

/* FP/SIMD 레지스터: 제약 "w", 출력할 때 %d (double) / %s (float) 수정자. 수정자 없으면 "v0" 이 찍힌다. */
static inline double fmadd_d(double a, double b, double c) {
    double r = 0.0; (void)a; (void)b; (void)c;
    /* TODO: FMADD (d 레지스터) — 제약 "w", 수정자 %d */
    return r;
}

static inline float fmadd_s(float a, float b, float c) {
    float r = 0.0f; (void)a; (void)b; (void)c;
    /* TODO: FMADD (s 레지스터) — 수정자 %s */
    return r;
}

/* 즉시값 제약: I = ADD 즉시값(0..4095, 또는 <<12), K = 32비트 논리 즉시값, L = 64비트 논리 즉시값. */
static inline uint64_t add_4095(uint64_t x) {
    uint64_t r = 0;
    (void)x; /* TODO: ADD 즉시값 4095 — 제약 "I" */
    return r;
}

static inline uint32_t keep_high_nibbles(uint32_t x) {
    uint32_t r = 0;
    (void)x; /* TODO: AND #0xF0F0F0F0 (32비트 논리 즉시값 "K") */
    return r;
}

static inline uint64_t keep_even_bytes(uint64_t x) {
    uint64_t r = 0;
    (void)x; /* TODO: AND #0x00FF00FF00FF00FF (64비트 논리 즉시값 "L") */
    return r;
}

/* ======================= Part C — 드라이버 헬퍼 + 가짜 NIC ======================= */

/* MMIO 쓰기/읽기: 메모리 오퍼랜드 "Q" (= [Xn], 오프셋 없음) + volatile. "rZ": 0 이면 wzr 을 써도 된다 (gcc 가 씀). */
static inline void mmio_write32(volatile uint32_t *addr, uint32_t v) {
    (void)addr; (void)v; /* TODO: STR — 메모리 오퍼랜드 "=Q", 값은 "rZ" */
}

static inline uint32_t mmio_read32(const volatile uint32_t *addr) {
    uint32_t v = 0;
    (void)addr; /* TODO: LDR — 메모리 오퍼랜드 "Q" */
    return v;
}

/* DMA 용 배리어: 하드웨어 순서(dmb) + 컴파일러 순서("memory") 둘 다. */
static inline void dma_wmb(void) {
    /* TODO: DMB OSHST + 컴파일러 배리어 */
}

static inline void dma_rmb(void) {
    /* TODO: DMB OSHLD + 컴파일러 배리어 */
}

enum { RING = 8, DESC_OWN = 1u, DESC_DONE = 2u };
typedef struct {
    uint64_t addr;
    uint32_t len;
    uint32_t flags;               /* OWN: 장치 소유, DONE: 장치가 처리 완료 */
} tx_desc_t;
typedef struct {
    tx_desc_t ring[RING];
    uint32_t tail_db;             /* MMIO 도어벨: 드라이버가 쓴 tail(누적 개수) */
    uint32_t head;                /* 장치가 처리한 개수 */
} nic_t;

/* 디스크립터 채우기 → dma_wmb → 도어벨. 반환: 사용한 링 인덱스. */
__attribute__((noinline)) uint32_t nic_tx_post(nic_t *n, uint32_t *sw_tail, uint64_t buf, uint32_t len) {
    uint32_t idx = *sw_tail % RING;
    /* TODO: ring[idx] 에 addr / len / flags = DESC_OWN 채우기 */
    (void)n; (void)buf; (void)len; (void)dma_wmb;

    /* TODO: 배리어 → *sw_tail 증가 → 도어벨(tail_db)에 MMIO 쓰기 */


    return idx;
}

/* DONE 이 될 때까지 CNTVCT 데드라인으로 폴링. 0 = 완료, -1 = 타임아웃. */
static int nic_wait_done(nic_t *n, uint32_t idx, uint64_t timeout_ticks) {
    /* TODO: deadline = 지금 + timeout_ticks; DONE 이면 (읽기 배리어 후) 0, 데드라인 지나면 -1 */
    (void)n; (void)idx; (void)timeout_ticks;
    dma_rmb();
    return 1;

}

/* ======================= 테스트 하네스 (starter 와 동일) ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t xorshift64(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }

/* C 기준 구현 */
static uint64_t ref_rbit(uint64_t x, int bits) { uint64_t r = 0; for (int i = 0; i < bits; i++) r |= ((x >> i) & 1u) << (bits - 1 - i); return r; }
static unsigned ref_clz64(uint64_t x) { return x ? (unsigned)__builtin_clzll(x) : 64u; }
static unsigned ref_ctz64(uint64_t x) { return x ? (unsigned)__builtin_ctzll(x) : 64u; }

/* asm 이 인라인될 자리를 일부러 만든다 (버그가 드러나는 모양 그대로) */
__attribute__((noinline)) static uint64_t add_off_trunc(uint64_t base, uint64_t big) { return add_u64_u32(base, (uint32_t)big); }
__attribute__((noinline)) static int64_t pick(int64_t x, int64_t b, int64_t y) {
    int64_t d = x - b;                         /* subs 의 플래그를 asm 뒤 csel 이 재사용하려 한다 */
    y = (int64_t)inc_flags((uint64_t)y);
    return d == 0 ? y : d;
}
__attribute__((noinline)) static uint64_t zero_then_read(uint64_t *p) { p[0] = 5; zero16(p); return p[0]; }
__attribute__((noinline)) static uint64_t spin_elapsed(volatile uint32_t *spin, int n) {
    uint64_t t0 = read_cntvct();
    for (int i = 0; i < n; i++) (void)*spin;
    return read_cntvct() - t0;
}

/* 장치 모델: 도어벨까지 OWN 인 디스크립터를 처리하고 DONE 표시 */
static uint64_t dev_process(nic_t *n) {
    uint64_t bytes = 0;
    while (n->head != n->tail_db) {
        tx_desc_t *d = &n->ring[n->head % RING];
        if (!(d->flags & DESC_OWN)) break;
        bytes += d->len;
        d->flags = DESC_DONE;
        n->head++;
    }
    return bytes;
}
/* 함수 기계어에서 DMB OSHST (0xD50332BF) 를 찾는다 — ret(0xD65F03C0) 까지 */
static bool code_has_dmb_oshst(uint32_t (*fn)(nic_t *, uint32_t *, uint64_t, uint32_t)) {
    const uint32_t *w = (const uint32_t *)(uintptr_t)fn;
    for (int i = 0; i < 512; i++) {
        if (w[i] == 0xD50332BFu) return true;
        if (w[i] == 0xD65F03C0u) return false;
    }
    return false;
}

int main(void) {
    /* ---- Part A ---- */
    CHECK(rbit32(1u) == 0x80000000u);
    CHECK(rbit32(0x12345678u) == 0x1E6A2C48u);
    CHECK(rbit64(1u) == 0x8000000000000000ull);
    CHECK(clz64(0) == 64u && clz64(1) == 63u && clz64(~0ull) == 0u);
    CHECK(ctz64(0) == 64u && ctz64(0x8000000000000000ull) == 63u && ctz64(0x50) == 4u);
    int bad_bits = 0;
    for (int i = 0; i < 100000; i++) {
        uint64_t x = xorshift64() >> (xorshift64() & 63);          /* 위쪽 0 이 다양한 값 */
        if (rbit32((uint32_t)x) != (uint32_t)ref_rbit(x & 0xFFFFFFFFu, 32)) bad_bits++;
        if (rbit64(x) != ref_rbit(x, 64)) bad_bits++;
        if (clz64(x) != ref_clz64(x) || ctz64(x) != ref_ctz64(x)) bad_bits++;
    }
    CHECK(bad_bits == 0);
    uint64_t hi = 1, lo = 1;
    add128(0, ~0ull, 0, 1, &hi, &lo);                               /* 캐리가 위 64비트로 */
    CHECK(hi == 1 && lo == 0);
    add128(~0ull, ~0ull, 0, 1, &hi, &lo);                           /* 2^128-1 + 1 = 0 (wrap) */
    CHECK(hi == 0 && lo == 0);
    add128(5, 7, 5, 7, &hi, &lo);                                   /* 같은 값 입력 (같은 레지스터 공유 가능) */
    CHECK(hi == 10 && lo == 14);
    int bad_add = 0;
    for (int i = 0; i < 100000; i++) {
        uint64_t ah = xorshift64(), al = xorshift64(), bh = xorshift64(), bl = (i & 1) ? xorshift64() : ~al;
        unsigned __int128 s = (((unsigned __int128)ah << 64) | al) + (((unsigned __int128)bh << 64) | bl);
        add128(ah, al, bh, bl, &hi, &lo);
        if (hi != (uint64_t)(s >> 64) || lo != (uint64_t)s) bad_add++;
    }
    CHECK(bad_add == 0);
    uint64_t freq = read_cntfrq();
    printf("info: CNTFRQ_EL0 = %llu Hz\n", (unsigned long long)freq);
    CHECK(freq >= 1000000u && freq <= 2000000000u);                /* 1 MHz..2 GHz: 읽혔다는 것만 */
    int back = 0;
    uint64_t prev = read_cntvct();
    for (int i = 0; i < 1000; i++) { uint64_t c = read_cntvct(); if (c < prev) back++; prev = c; }
    CHECK(back == 0);                                               /* 단조 증가 */
    volatile uint32_t spin = 0;
    uint64_t el = spin_elapsed(&spin, 2000000);
    printf("info: 2M volatile loads took %llu ticks\n", (unsigned long long)el);
    CHECK(el > 0);                                                  /* 두 번 읽기가 합쳐지지 않았다 */

    /* ---- Part B ---- */
    CHECK(add_off_trunc(0x1000, 0xFFFFFFFF00000010ull) == 0x1010u);
    CHECK(add_off_trunc(~0ull, 0xABCD000000000001ull) == 0u);      /* 64비트 wrap 은 정상 */
    CHECK(add_u64_u32(0, 0xFFFFFFFFu) == 0xFFFFFFFFull);           /* 부호 확장 아님 */
    CHECK(pick(5, 3, -1) == 2);                                     /* d != 0 → d */
    CHECK(pick(3, 3, -1) == 0 && pick(7, 7, 41) == 42);            /* d == 0 → y+1 */
    CHECK(inc_flags(~0ull) == 0u);
    uint64_t z[3] = {7, 7, 0xC0FFEEull};
    CHECK(zero_then_read(z) == 0u && z[1] == 0u && z[2] == 0xC0FFEEull);
    double ad = 1.0 + 0x1p-30, bd = 1.0 - 0x1p-30;                  /* a*b = 1 - 2^-60 (정확) */
    CHECK(fmadd_d(ad, bd, -1.0) == -0x1p-60);                      /* 융합: 중간 반올림 없음 */
    float af = 1.0f + 0x1p-12f, bf = 1.0f - 0x1p-12f;
    CHECK(fmadd_s(af, bf, -1.0f) == -0x1p-24f);
    int bad_fma = 0;
    for (int i = 0; i < 20000; i++) {
        double a = (double)(int64_t)xorshift64() * 0x1p-60, b = (double)(int64_t)xorshift64() * 0x1p-61,
               c = (double)(int64_t)xorshift64() * 0x1p-62;
        if (memcmp(&(double){fmadd_d(a, b, c)}, &(double){fma(a, b, c)}, sizeof(double)) != 0) bad_fma++;
        float fa = (float)a, fb = (float)b, fc = (float)c;
        if (memcmp(&(float){fmadd_s(fa, fb, fc)}, &(float){fmaf(fa, fb, fc)}, sizeof(float)) != 0) bad_fma++;
    }
    CHECK(bad_fma == 0);
    CHECK(add_4095(1) == 4096u);
    CHECK(keep_high_nibbles(0x12345678u) == 0x10305070u);
    CHECK(keep_even_bytes(0x1122334455667788ull) == 0x0022004400660088ull);

    /* ---- Part C ---- */
    uint32_t reg = 0xFFFFFFFFu;
    mmio_write32(&reg, 0);                                          /* "rZ": gcc 는 str wzr */
    CHECK(reg == 0u);
    mmio_write32(&reg, 0xA5A5F00Du);
    CHECK(mmio_read32(&reg) == 0xA5A5F00Du);
    static nic_t nic;
    uint32_t tail = 0;
    uint32_t i0 = nic_tx_post(&nic, &tail, 0x80001000u, 64);
    uint32_t i1 = nic_tx_post(&nic, &tail, 0x80002000u, 1500);
    uint32_t i2 = nic_tx_post(&nic, &tail, 0x80003000u, 60);
    CHECK(i0 == 0 && i1 == 1 && i2 == 2 && nic.tail_db == 3u && tail == 3u);
    CHECK(nic.ring[1].addr == 0x80002000u && nic.ring[1].len == 1500u && nic.ring[1].flags == DESC_OWN);
    CHECK(dev_process(&nic) == 64u + 1500u + 60u && nic.head == 3u);
    CHECK(code_has_dmb_oshst(nic_tx_post));                         /* 바이너리에 배리어가 실제로 있다 */
    for (uint32_t k = 0; k < 7; k++) nic_tx_post(&nic, &tail, 0x90000000u + k, 100 + k);
    CHECK(tail == 10u && nic.tail_db == 10u && nic.ring[0].addr == 0x90000005u && nic.ring[1].len == 106u);
    CHECK(dev_process(&nic) == 7u * 100u + 21u && nic.head == 10u);
    if (freq != 0 && el > 0) {
        uint64_t t0 = read_cntvct();
        CHECK(nic_wait_done(&nic, 1, freq / 1000) == 0);            /* 이미 DONE → 바로 0 */
        nic.ring[2].flags = DESC_OWN;                               /* 장치가 안 끝낸 디스크립터 */
        t0 = read_cntvct();
        int rc = nic_wait_done(&nic, 2, freq / 1000);               /* 1 ms 타임아웃 */
        uint64_t waited = read_cntvct() - t0;
        printf("info: timeout path waited %llu ticks (>= %llu)\n", (unsigned long long)waited, (unsigned long long)(freq / 1000));
        CHECK(rc == -1 && waited >= freq / 1000);
    } else {
        printf("SKIP 2 polling checks: counter not working yet (Part A)\n");
        g_total += 2;
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
