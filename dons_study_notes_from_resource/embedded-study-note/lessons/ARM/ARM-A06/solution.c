/* ARM-A06 ARMv8 CRC32 명령으로 CRC 가속 — intrinsic(arm_acle.h) + 손 asm + 3-way 인터리브 + PMULL combine
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c solution.S shared_ref.c shared_bugs.c -lz -o sol && ./sol
 *       (starter: starter.c starter.S 로 바꿔서. 체커는 zlib.h 가 보이면 -lz 를 자동으로 붙인다)
 * Part A: CRC-32(IEEE, zlib) / CRC-32C(Castagnoli) 를 __crc32b/h/w/d · __crc32cb/ch/cw/cd 로. 정렬 머리(1·2·4 B) →
 *         8 B 본문 → 꼬리(4·2·1 B). 명령은 조건화(~)를 안 하므로 ~ 는 래퍼가 한다. asm 판은 solution.S.
 * Part B: crc32cx 는 지연 3 / 처리량 1 (이 맥 실측) → 독립 스트림 3개로 지연을 숨기고, 결과를 GF(2) 시프트로 합친다.
 *         시프트 = PMULL(vmull_p64)로 c·x^(8n−33) 을 곱하고 crc32cx(0, 곱) 으로 mod P (x^33 은 명령이 붙인다).
 * Part C: scatter-gather 세그먼트별 CRC → combine (NVMe/iSCSI 데이터 다이제스트), STM32 CRC 유닛을 호스트에서 재현.
 * 테스트: 카탈로그 check 값, zlib crc32()/crc32_combine(), 비트 단위/테이블/slicing-8 기준(ALGO-M01/M02 축약판,
 *         shared_ref.c), 길이 0..64 × 정렬 0..15 전수, 길이 0..4096 전수(3-way), 페이지 끝 경계, 고정 시드 랜덤,
 *         shared_bugs.c 의 버그 6종이 잡히는지.
 */
#include <arm_acle.h>
#include <arm_neon.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <zlib.h>

#define POLY_IEEE_R 0xEDB88320u   /* CRC-32  0x04C11DB7 반사 */
#define POLY_C_R    0x82F63B78u   /* CRC-32C 0x1EDC6F41 반사 */
/* FEAT_CRC32 + FEAT_PMULL. Apple 기본 타깃엔 이미 켜져 있지만 리눅스 generic armv8-a 대비로 함수 단위로 켠다 */
#define HW __attribute__((target("+crc,+aes")))

/* ---- shared_ref.c: 소프트웨어 기준 (ALGO-M01/M02 축약판) ---- */
void     ref_init(void);
uint32_t ref_bitwise(uint32_t crc, const void *p, size_t n, uint32_t poly_r);
uint32_t ref_table(uint32_t crc, const void *p, size_t n, uint32_t poly_r);
uint32_t ref_slice8(uint32_t crc, const void *p, size_t n, uint32_t poly_r);
uint32_t gf2_mulmod(uint32_t a, uint32_t b, uint32_t poly_r);
uint32_t xpow_mod(uint64_t e, uint32_t poly_r);
uint32_t ref_mpeg2_bitwise(uint32_t state, const void *p, size_t n);
/* ---- starter.S / solution.S ---- */
uint32_t crc32c_asm(uint32_t crc, const void *p, size_t n);
/* ---- shared_bugs.c: 일부러 틀린 버전 (테스트가 잡아야 한다) ---- */
uint32_t bug_no_invert(uint32_t crc, const void *p, size_t n);
uint32_t bug_ieee_body(uint32_t crc, const void *p, size_t n);
uint32_t bug_bswap_body(uint32_t crc, const void *p, size_t n);
uint32_t bug_tail_no1(uint32_t crc, const void *p, size_t n);
uint32_t bug_3way_no33(uint32_t crc, const void *p, size_t n);
uint32_t bug_3way_swapk(uint32_t crc, const void *p, size_t n);

__attribute__((unused)) static inline uint16_t ld16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
__attribute__((unused)) static inline uint32_t ld32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
__attribute__((unused)) static inline uint64_t ld64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

/* ================= Part A — 1-way: 정렬 머리 → 8 B 본문 → 꼬리 ================= */

/* 조건화 없는 레지스터 갱신. 데이터 바이트를 메모리 순서대로 정확히 n 개 넣는다 (p+n 밖은 읽지 않는다). */
HW uint32_t crc32_raw_hw(uint32_t c, const uint8_t *p, size_t n) {
    if (n >= 1 && ((uintptr_t)p & 1)) { c = __crc32b(c, *p);       p += 1; n -= 1; }
    if (n >= 2 && ((uintptr_t)p & 2)) { c = __crc32h(c, ld16(p));  p += 2; n -= 2; }
    if (n >= 4 && ((uintptr_t)p & 4)) { c = __crc32w(c, ld32(p));  p += 4; n -= 4; }
    for (; n >= 8; p += 8, n -= 8)      c = __crc32d(c, ld64(p));  /* 이제 p 는 8 B 정렬 */
    if (n & 4) { c = __crc32w(c, ld32(p)); p += 4; }
    if (n & 2) { c = __crc32h(c, ld16(p)); p += 2; }
    if (n & 1)   c = __crc32b(c, *p);
    return c;
}

HW uint32_t crc32c_raw_hw(uint32_t c, const uint8_t *p, size_t n) {
    if (n >= 1 && ((uintptr_t)p & 1)) { c = __crc32cb(c, *p);      p += 1; n -= 1; }
    if (n >= 2 && ((uintptr_t)p & 2)) { c = __crc32ch(c, ld16(p)); p += 2; n -= 2; }
    if (n >= 4 && ((uintptr_t)p & 4)) { c = __crc32cw(c, ld32(p)); p += 4; n -= 4; }
    for (; n >= 8; p += 8, n -= 8)      c = __crc32cd(c, ld64(p));
    if (n & 4) { c = __crc32cw(c, ld32(p)); p += 4; }
    if (n & 2) { c = __crc32ch(c, ld16(p)); p += 2; }
    if (n & 1)   c = __crc32cb(c, *p);
    return c;
}

/* zlib 모양 API: 처음 crc = 0, 이어 부르면 스트리밍. 조건화(init ~0, xorout ~0)는 여기서. */
uint32_t crc32_arm(uint32_t crc, const void *p, size_t n)  { return ~crc32_raw_hw(~crc, p, n); }
uint32_t crc32c_arm(uint32_t crc, const void *p, size_t n) { return ~crc32c_raw_hw(~crc, p, n); }

/* ================= Part B — GF(2) 시프트 (PMULL) · combine · 3-way ================= */

/* c·k·x^33 mod P: PMULL 로 32×32 → 63비트 무캐리 곱, crc32(c)x(0, 곱) 이 x^32·(곱·x) mod P 를 돌려준다.
 * 그래서 k = x^(8n−33) mod P 를 넣으면 결과 = c·x^(8n) mod P = "c 뒤에 0 바이트 n 개" (조건화 없는 상태 기준). */
HW uint32_t crc_mulk(uint32_t c, uint32_t k, int castagnoli) {
    uint64_t prod = vgetq_lane_u64(vreinterpretq_u64_p128(vmull_p64((poly64_t)c, (poly64_t)k)), 0);
    return castagnoli ? __crc32cd(0, prod) : __crc32d(0, prod);
}

/* 상태 c 를 0 바이트 n 개만큼 민다: c·x^(8n) mod P. 8n−33 < 0 (n < 5) 이면 명령으로 0 바이트를 직접 넣는다. */
HW uint32_t crc_shift(uint32_t c, uint64_t n, int castagnoli) {
    if (n < 5) {
        while (n--) c = castagnoli ? __crc32cb(c, 0) : __crc32b(c, 0);
        return c;
    }
    return crc_mulk(c, xpow_mod(8 * n - 33, castagnoli ? POLY_C_R : POLY_IEEE_R), castagnoli);
}

/* crc(A‖B) = shift(crc(A), |B|) ⊕ crc(B)  — 조건화된 값끼리 성립 (init = xorout = ~0 이 상쇄, ALGO-M02 참고) */
uint32_t crc32c_combine_hw(uint32_t crc1, uint32_t crc2, uint64_t len2) { return crc_shift(crc1, len2, 1) ^ crc2; }
uint32_t crc32_combine_hw(uint32_t crc1, uint32_t crc2, uint64_t len2)  { return crc_shift(crc1, len2, 0) ^ crc2; }

/* 스트림 길이 B 와 시프트 상수: k1 = x^(8B−33), k2 = x^(16B−33) mod P_c  (값은 테스트가 xpow_mod 로 재확인) */
static const struct { size_t blk; uint32_t k1, k2; } K3C[2] = {
    { 1024, 0x170076FAu, 0xA51B6135u },     /* 3 KiB 덩어리 */
    {  128, 0x0D3B6092u, 0xB9E02B86u },     /* 384 B 덩어리 */
};

/* 3·blk 바이트씩: [A | B | C] 를 독립 체인 c0(이어받은 상태), c1(0), c2(0) 로 동시에 → c0·x^(16blk) ⊕ c1·x^(8blk) ⊕ c2 */
HW static uint32_t crc32c_3x(uint32_t c, const uint8_t **pp, size_t *pn, size_t blk, uint32_t k1, uint32_t k2) {
    const uint8_t *p = *pp;
    size_t n = *pn;
    while (n >= 3 * blk) {
        uint32_t c0 = c, c1 = 0, c2 = 0;
        for (size_t i = 0; i < blk; i += 8) {          /* 세 체인이 서로 기다리지 않는다 → 지연 3 을 숨김 */
            c0 = __crc32cd(c0, ld64(p + i));
            c1 = __crc32cd(c1, ld64(p + blk + i));
            c2 = __crc32cd(c2, ld64(p + 2 * blk + i));
        }
        c = crc_mulk(c0, k2, 1) ^ crc_mulk(c1, k1, 1) ^ c2;
        p += 3 * blk;
        n -= 3 * blk;
    }
    *pp = p;
    *pn = n;
    return c;
}

uint32_t crc32c_3way(uint32_t crc, const void *buf, size_t n) {
    const uint8_t *p = buf;
    uint32_t c = ~crc;
    size_t head = (size_t)(-(uintptr_t)p & 7);         /* 8 B 정렬까지 */
    if (head > n) head = n;
    c = crc32c_raw_hw(c, p, head);
    p += head;
    n -= head;
    for (int t = 0; t < 2; t++) c = crc32c_3x(c, &p, &n, K3C[t].blk, K3C[t].k1, K3C[t].k2);
    return ~crc32c_raw_hw(c, p, n);                    /* 남은 < 384 B 는 1-way */
}

/* ================= Part C — 펌웨어 ================= */

/* NVMe/iSCSI: 4 KiB 블록이 여러 DMA 세그먼트로 흩어져 있다. 세그먼트마다 독립 CRC(엔진/코어별) → combine. */
typedef struct { const uint8_t *p; size_t len; } sg_ent;

uint32_t crc32c_sgl(const sg_ent *sg, size_t cnt) {
    uint32_t acc = 0;                                   /* 빈 메시지의 CRC = 0 */
    for (size_t i = 0; i < cnt; i++) {
        uint32_t ci = crc32c_3way(0, sg[i].p, sg[i].len);   /* 이 줄은 병렬로 돌 수 있다 */
        acc = crc32c_combine_hw(acc, ci, sg[i].len);
    }
    return acc;
}

/* STM32 CRC 유닛 기본 설정 (poly 0x04C11DB7, init 0xFFFFFFFF, 32비트 워드 MSB 먼저, 반사·xorout 없음) 재현.
 * 반사형 명령으로 비반사 CRC: 상태 s = rbit(r), 입력 워드도 rbit → 워드의 비트 31 이 먼저 들어간다. */
HW uint32_t stm32_crc_hw(const uint32_t *w, size_t nw) {
    uint32_t s = 0xFFFFFFFFu;                           /* rbit(0xFFFFFFFF) */
    for (size_t i = 0; i < nw; i++) s = __crc32w(s, __rbit(w[i]));
    return __rbit(s);
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x2545F4914F6CDD1Dull;          /* 고정 시드 xorshift64 */
static uint64_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }

typedef uint32_t (*crc_fn)(uint32_t, const void *, size_t);
static uint32_t z_crc32(uint32_t c, const void *p, size_t n) { return (uint32_t)crc32(c, p, (uInt)n); }

static uint8_t g_buf[256 * 1024 + 64];

/* 테스트 묶음: 길이 0..small × 정렬 0..15 전수 + 길이 0..maxlen 전수(오프셋 랜덤) + 랜덤 시작 crc. 불일치 수를 돌려준다. */
static long suite_c(crc_fn f, size_t small, size_t maxlen) {
    long bad = 0;
    for (size_t n = 0; n <= small; n++)
        for (size_t a = 0; a < 16; a++)
            bad += f(0, g_buf + a, n) != ref_bitwise(0, g_buf + a, n, POLY_C_R);
    for (size_t n = 0; n <= maxlen; n++) {
        size_t a = rnd() & 15;
        uint32_t c0 = (uint32_t)rnd();
        bad += f(c0, g_buf + a, n) != ref_slice8(c0, g_buf + a, n, POLY_C_R);
    }
    return bad;
}

/* clmul 기준 (소프트웨어): PMULL 사용법 검증용 */
static void clmul_ref(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo) {
    uint64_t h = 0, l = 0;
    for (int i = 0; i < 64; i++)
        if ((b >> i) & 1) { l ^= a << i; if (i) h ^= a >> (64 - i); }
    *hi = h; *lo = l;
}

/* 테스트에서 직접 쓰는 intrinsic 도 HW 함수 안에 둔다 (gcc 는 main 에서 쓰면 "target specific option mismatch") */
HW static uint64x2_t pmull64(uint64_t a, uint64_t b) { return vreinterpretq_u64_p128(vmull_p64((poly64_t)a, (poly64_t)b)); }
HW static uint32_t crc32c_zero4(uint32_t c) { return __crc32cw(c, 0); }
HW static uint32_t crc32c_zero8(uint32_t c) { return __crc32cd(c, 0); }

static int test_page_end(crc_fn f, crc_fn ref) {
    long pg = sysconf(_SC_PAGESIZE);
    uint8_t *m = mmap(NULL, (size_t)pg * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (m == MAP_FAILED) return -1;
    for (long i = 0; i < pg; i++) m[i] = (uint8_t)(i * 7 + 3);
    mprotect(m + pg, (size_t)pg, PROT_NONE);            /* 둘째 페이지에 닿으면 SIGBUS/SIGSEGV */
    int bad = 0;
    for (size_t n = 0; n <= 64; n++) {
        const uint8_t *p = m + pg - n;                  /* 마지막 바이트가 페이지 끝 */
        bad += f(0, p, n) != ref(0, p, n);
    }
    munmap(m, (size_t)pg * 2);
    return bad;
}
static uint32_t ref_c_table(uint32_t c, const void *p, size_t n) { return ref_table(c, p, n, POLY_C_R); }

int main(void) {
    ref_init();
    for (size_t i = 0; i < sizeof g_buf; i++) g_buf[i] = (uint8_t)rnd();
    const uint8_t *chk = (const uint8_t *)"123456789";

    /* ---- Part A ---- */
    printf("check(\"123456789\"): bitwise IEEE=0x%08X  C=0x%08X  | crc32_arm=0x%08X crc32c_arm=0x%08X crc32c_asm=0x%08X\n",
           ref_bitwise(0, chk, 9, POLY_IEEE_R), ref_bitwise(0, chk, 9, POLY_C_R),
           crc32_arm(0, chk, 9), crc32c_arm(0, chk, 9), crc32c_asm(0, chk, 9));
    CHECK(ref_bitwise(0, chk, 9, POLY_IEEE_R) == 0xCBF43926u);     /* RevEng CRC-32/ISO-HDLC */
    CHECK(ref_bitwise(0, chk, 9, POLY_C_R) == 0xE3069283u);        /* RevEng CRC-32/ISCSI */
    CHECK(crc32_arm(0, chk, 9) == 0xCBF43926u);
    CHECK(crc32c_arm(0, chk, 9) == 0xE3069283u);
    CHECK(crc32c_asm(0, chk, 9) == 0xE3069283u);
    CHECK(crc32c_arm(0x12345678u, chk, 0) == 0x12345678u);         /* n = 0 → 그대로 */
    CHECK(crc32c_asm(0x12345678u, chk, 0) == 0x12345678u);

    long bz = 0, bc = 0, ba = 0;
    for (size_t n = 0; n <= 64; n++)
        for (size_t a = 0; a < 16; a++) {
            bz += crc32_arm(0, g_buf + a, n) != z_crc32(0, g_buf + a, n);
            bc += crc32c_arm(0, g_buf + a, n) != ref_bitwise(0, g_buf + a, n, POLY_C_R);
            ba += crc32c_asm(0, g_buf + a, n) != ref_bitwise(0, g_buf + a, n, POLY_C_R);
        }
    printf("len 0..64 x align 0..15: crc32_arm vs zlib bad=%ld, crc32c_arm vs bitwise bad=%ld, crc32c_asm bad=%ld\n", bz, bc, ba);
    CHECK(bz == 0); CHECK(bc == 0); CHECK(ba == 0);

    bz = bc = ba = 0;
    for (int t = 0; t < 2000; t++) {
        size_t n = rnd() % 4097, a = rnd() & 15;
        uint32_t c0 = (uint32_t)rnd();
        bz += crc32_arm(c0, g_buf + a, n) != z_crc32(c0, g_buf + a, n);
        bc += crc32c_arm(c0, g_buf + a, n) != ref_table(c0, g_buf + a, n, POLY_C_R);
        ba += crc32c_asm(c0, g_buf + a, n) != ref_slice8(c0, g_buf + a, n, POLY_C_R);
    }
    CHECK(bz == 0); CHECK(bc == 0); CHECK(ba == 0);

    long bs = 0;                                        /* 스트리밍: 임의 조각으로 나눠 이어 부르기 */
    for (int t = 0; t < 300; t++) {
        size_t n = rnd() % 3000, off = 0;
        uint32_t c1 = 0, c2 = 0;
        while (off < n) {
            size_t k = rnd() % 40; if (k > n - off) k = n - off;
            c1 = crc32c_arm(c1, g_buf + off, k);
            c2 = crc32c_asm(c2, g_buf + off, k);
            off += k;
        }
        uint32_t whole = ref_table(0, g_buf, n, POLY_C_R);
        bs += (c1 != whole) + (c2 != whole);
    }
    CHECK(bs == 0);
    int pe1 = test_page_end(crc32c_arm, ref_c_table), pe2 = test_page_end(crc32c_asm, ref_c_table);
    printf("page-end (len 0..64 ending at a PROT_NONE page): crc32c_arm bad=%d, crc32c_asm bad=%d\n", pe1, pe2);
    CHECK(pe1 == 0); CHECK(pe2 == 0);

    /* ---- Part B ---- */
    long bp = 0;
    for (int t = 0; t < 2000; t++) {
        uint64_t a = rnd(), b = rnd(), h, l;
        clmul_ref(a, b, &h, &l);
        uint64x2_t v = pmull64(a, b);
        bp += vgetq_lane_u64(v, 0) != l || vgetq_lane_u64(v, 1) != h;
    }
    CHECK(bp == 0);                                      /* vmull_p64 = 64×64 → 128 무캐리 곱 */
    for (int t = 0; t < 2; t++) {
        CHECK(K3C[t].k1 == xpow_mod(8 * K3C[t].blk - 33, POLY_C_R));
        CHECK(K3C[t].k2 == xpow_mod(16 * K3C[t].blk - 33, POLY_C_R));
    }
    long bsh = 0;
    for (uint64_t n = 0; n <= 300; n++)
        for (int cas = 0; cas < 2; cas++) {
            uint32_t pr = cas ? POLY_C_R : POLY_IEEE_R, c = (uint32_t)rnd();
            bsh += crc_shift(c, n, cas) != gf2_mulmod(xpow_mod(8 * n, pr), c, pr);
        }
    for (int t = 0; t < 300; t++) {                     /* 큰 n (최대 2^40 바이트) */
        uint64_t n = rnd() >> 24;
        int cas = t & 1;
        uint32_t pr = cas ? POLY_C_R : POLY_IEEE_R, c = (uint32_t)rnd();
        bsh += crc_shift(c, n, cas) != gf2_mulmod(xpow_mod(8 * n, pr), c, pr);
    }
    CHECK(bsh == 0);
    CHECK(crc_shift(0x12345678u, 4, 1) == crc32c_zero4(0x12345678u));    /* 4 바이트 = crc32cw(c, 0) */
    CHECK(crc_shift(0x12345678u, 8, 1) == crc32c_zero8(0x12345678u));    /* 8 바이트 = crc32cx(c, 0) */

    long bcb = 0, bzc = 0;
    for (int t = 0; t < 2000; t++) {
        size_t n = rnd() % 5000, cut = n ? rnd() % (n + 1) : 0;
        if (t < 20) { size_t l2 = (size_t)(t % 10); cut = l2 > n ? 0 : n - l2; }   /* len2 = 0..9 (n < 5 경로) */
        uint32_t ca = crc32c_arm(0, g_buf, cut), cb = crc32c_arm(0, g_buf + cut, n - cut);
        bcb += crc32c_combine_hw(ca, cb, n - cut) != crc32c_arm(0, g_buf, n);
        uint32_t za = z_crc32(0, g_buf, cut), zb = z_crc32(0, g_buf + cut, n - cut);
        bzc += crc32_combine_hw(za, zb, n - cut) != (uint32_t)crc32_combine(za, zb, (z_off_t)(n - cut));
    }
    printf("combine: crc32c vs one-shot bad=%ld, crc32 vs zlib crc32_combine bad=%ld\n", bcb, bzc);
    CHECK(bcb == 0); CHECK(bzc == 0);

    long b3 = 0;
    for (size_t n = 0; n <= 4096; n++) {                /* 길이 전수, 오프셋·시작값 랜덤 */
        size_t a = rnd() & 15;
        uint32_t c0 = (uint32_t)rnd();
        b3 += crc32c_3way(c0, g_buf + a, n) != crc32c_arm(c0, g_buf + a, n);
    }
    for (int t = 0; t < 200; t++) {
        size_t n = rnd() % (256 * 1024), a = rnd() & 63;
        uint32_t c0 = (uint32_t)rnd();
        b3 += crc32c_3way(c0, g_buf + a, n) != ref_slice8(c0, g_buf + a, n, POLY_C_R);
    }
    printf("3-way: len 0..4096 (all) + 200 random up to 256 KiB: bad=%ld\n", b3);
    CHECK(b3 == 0);
    CHECK(crc32c_3way(0, chk, 9) == 0xE3069283u);
    CHECK(test_page_end(crc32c_3way, ref_c_table) == 0);

    /* 버그 6종: 강한 묶음(길이 ≤ 64 전수 + ≤ 1200 전수)은 모두 잡고, 약한 묶음(≤ 300)은 3-way 상수 버그를 놓친다 */
    struct { const char *name; crc_fn f; } bugs[] = {
        { "no_invert", bug_no_invert }, { "ieee_body", bug_ieee_body }, { "bswap_body", bug_bswap_body },
        { "tail_no1", bug_tail_no1 }, { "3way_no33", bug_3way_no33 }, { "3way_swapk", bug_3way_swapk },
    };
    printf("bugs (mismatches, strong / weak suite):");
    for (int i = 0; i < 6; i++) {
        long s = suite_c(bugs[i].f, 64, 1200), w = suite_c(bugs[i].f, 16, 300);
        printf(" %s=%ld/%ld", bugs[i].name, s, w);
        CHECK(s > 0);
        if (i >= 4) CHECK(w == 0);                      /* 384 B 미만에서는 3-way 경로가 안 돈다 */
    }
    printf("\n");
    CHECK(suite_c(crc32c_3way, 64, 1200) == 0);

    /* ---- Part C ---- */
    long bsg = 0;
    for (int t = 0; t < 500; t++) {
        sg_ent sg[16];
        size_t cnt = rnd() % 17, total = 0;
        uint8_t *dst = g_buf + 128 * 1024;              /* 이어 붙인 사본 (비교용) */
        for (size_t i = 0; i < cnt; i++) {
            size_t len = (rnd() & 3) == 0 ? 0 : rnd() % 1500;   /* 0 길이 세그먼트도 */
            sg[i].p = g_buf + (rnd() % 60000);
            sg[i].len = len;
            memcpy(dst + total, sg[i].p, len);
            total += len;
        }
        bsg += crc32c_sgl(sg, cnt) != crc32c_arm(0, dst, total);
    }
    CHECK(bsg == 0);
    CHECK(crc32c_sgl(NULL, 0) == 0);

    printf("mpeg2 check(\"123456789\") = 0x%08X\n", ref_mpeg2_bitwise(0xFFFFFFFFu, chk, 9));
    CHECK(ref_mpeg2_bitwise(0xFFFFFFFFu, chk, 9) == 0x0376E6E7u);  /* RevEng CRC-32/MPEG-2 */
    long bst = 0;
    for (int t = 0; t < 1000; t++) {
        uint32_t w[64];
        uint8_t be[256];
        size_t nw = rnd() % 65;
        for (size_t i = 0; i < nw; i++) {
            w[i] = (uint32_t)rnd();
            be[4 * i] = (uint8_t)(w[i] >> 24); be[4 * i + 1] = (uint8_t)(w[i] >> 16);
            be[4 * i + 2] = (uint8_t)(w[i] >> 8); be[4 * i + 3] = (uint8_t)w[i];
        }
        bst += stm32_crc_hw(w, nw) != ref_mpeg2_bitwise(0xFFFFFFFFu, be, 4 * nw);
    }
    CHECK(bst == 0);
    const uint32_t w1[1] = { 0x12345678u };
    printf("stm32_crc_hw({0x12345678}) = 0x%08X\n", stm32_crc_hw(w1, 1));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
