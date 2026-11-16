/* BM-A08 추가 연습 — 호스트 C (Mac: cc, VM: run_qemu.sh practice).
 * 연습 1 mpu_decode + 인코더 전수 검사     연습 2 mpu_check: 접근 판정 모델 (우선순위·SRD·PRIVDEFENA·HFNMIENA)
 * 연습 3 mpu_cover: 영역 하나 + SRD 로 범위 덮기   연습 4 guard_sim/guard_min: 가드 크기 규칙
 * 연습 5 QEMU 실측 표 대조 (solution / demo_probe 의 실제 출력 → 모델 예측과 비교)
 * 인코더는 solution.c 의 mpu_encode 와 같은 코드 (BM-A08 solution 축약판).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------------- 인코더 (BM-A08 solution 축약판) ---------------- */
typedef struct { uint32_t rbar, rasr; } mpu_region_t;
typedef enum { MPU_OK = 0, MPU_ERR_REGION, MPU_ERR_SIZE, MPU_ERR_ALIGN, MPU_ERR_SRD, MPU_ERR_AP,
               MPU_ERR_ATTR } mpu_rc_t;
#define MPU_NREGIONS 8u
#define MPU_RBAR_VALID (1u << 4)
#define AP_NONE 0u
#define AP_FULL 3u
#define AP_RO   6u
#define ATTR_FLASH 0x02u
#define ATTR_SRAM  0x03u
#define CTRL_ENABLE 1u
#define CTRL_HFNMIENA 2u
#define CTRL_PRIVDEFENA 4u

static int attr_valid(uint32_t attr) {
    uint32_t tex = attr >> 3, cb = attr & 3u;
    if (attr > 0x3Fu) return 0;
    if (tex == 1u && cb == 1u) return 0;
    if (tex == 2u && cb != 0u) return 0;
    if (tex == 3u) return 0;
    return 1;
}
static mpu_rc_t mpu_encode(uint32_t region, uint32_t base, uint64_t size, uint32_t ap, uint32_t xn,
                           uint32_t attr, uint32_t srd, mpu_region_t *out) {
    if (region >= MPU_NREGIONS) return MPU_ERR_REGION;
    if (size < 32u || size > (1ull << 32) || (size & (size - 1u)) != 0u) return MPU_ERR_SIZE;
    if (((uint64_t)base & (size - 1u)) != 0u) return MPU_ERR_ALIGN;
    if (srd > 0xFFu || (size < 256u && srd != 0u)) return MPU_ERR_SRD;
    if (ap > 7u || ap == 4u) return MPU_ERR_AP;
    if (xn > 1u || !attr_valid(attr)) return MPU_ERR_ATTR;
    uint32_t log2 = 0;
    while ((1ull << log2) < size) log2++;
    out->rbar = base | MPU_RBAR_VALID | region;
    out->rasr = (xn << 28) | (ap << 24) | (attr << 16) | (srd << 8) | ((log2 - 1u) << 1) | 1u;
    return MPU_OK;
}

/* ================= 연습 1: mpu_decode ================= */
typedef struct { uint32_t region, base, ap, xn, attr, srd, enable; uint64_t size; } mpu_info_t;
static void mpu_decode(mpu_region_t r, mpu_info_t *o) {
    o->region = r.rbar & 0xFu;
    o->base = r.rbar & ~0x1Fu;
    o->enable = r.rasr & 1u;
    o->size = 1ull << (((r.rasr >> 1) & 0x1Fu) + 1u);
    o->srd = (r.rasr >> 8) & 0xFFu;
    o->attr = (r.rasr >> 16) & 0x3Fu;
    o->ap = (r.rasr >> 24) & 7u;
    o->xn = (r.rasr >> 28) & 1u;
}
/* 독립 참조: 규칙을 "정의 그대로" 다시 쓴 판정 (반복문 대신 집합 표현) */
static mpu_rc_t ref_rule(uint32_t region, uint32_t base, uint64_t size, uint32_t ap, uint32_t xn, uint32_t attr,
                         uint32_t srd) {
    int pow2 = 0;
    for (int k = 5; k <= 32; ++k) if (size == (1ull << k)) pow2 = 1;
    if (region > 7u) return MPU_ERR_REGION;
    if (!pow2) return MPU_ERR_SIZE;
    if ((uint64_t)base % size != 0u) return MPU_ERR_ALIGN;
    if (srd >= 256u || (size <= 128u && srd != 0u)) return MPU_ERR_SRD;
    static const int ap_ok[8] = { 1, 1, 1, 1, 0, 1, 1, 1 };
    if (ap >= 8u || !ap_ok[ap]) return MPU_ERR_AP;
    /* Table B3-13 예약: 001 01, 010 01/10/11, 011 xx (S 비트 무시) */
    uint32_t tcb = ((attr >> 3) << 2) | (attr & 3u);   /* TEX:C:B 5 비트 */
    static const uint32_t reserved[] = { 0x05, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F };
    int res = 0;
    for (size_t i = 0; i < sizeof reserved / sizeof reserved[0]; ++i) res |= (tcb == reserved[i]);
    if (xn >= 2u || attr >= 64u || res) return MPU_ERR_ATTR;
    return MPU_OK;
}
static void ex1(void) {
    uint64_t ok = 0, rej = 0, mism = 0, rt_bad = 0;
    uint32_t attr_ok = 0;
    for (uint32_t a = 0; a < 64u; ++a) attr_ok += (uint32_t)attr_valid(a);
    /* (a) 크기 x base: 2^0..2^33, 거듭제곱 아닌 값, 정렬/비정렬 base */
    for (int k = 0; k <= 33; ++k) {
        uint64_t sizes[3] = { 1ull << k, (1ull << k) + 1u, 3ull << k };
        for (int si = 0; si < 3; ++si) {
            uint64_t size = sizes[si];
            uint32_t bases[40];
            int nb = 0;
            bases[nb++] = 0u; bases[nb++] = 0xFFFFFFE0u;
            if (k < 32) bases[nb++] = (uint32_t)(1ull << k);
            for (int j = 0; j < 32 && j < k; ++j) bases[nb++] = (uint32_t)(1ull << j);
            for (int b = 0; b < nb; ++b) {
                mpu_region_t r = { 0xAAAAAAAAu, 0x55555555u };
                mpu_rc_t got = mpu_encode(3u, bases[b], size, AP_FULL, 1u, ATTR_SRAM, 0u, &r);
                mpu_rc_t want = ref_rule(3u, bases[b], size, AP_FULL, 1u, ATTR_SRAM, 0u);
                mism += (got != want);
                if (got == MPU_OK) {
                    ok++;
                    mpu_info_t d;
                    mpu_decode(r, &d);
                    rt_bad += !(d.region == 3u && d.base == bases[b] && d.size == size && d.ap == AP_FULL &&
                                d.xn == 1u && d.attr == ATTR_SRAM && d.srd == 0u && d.enable == 1u);
                } else {
                    rej++;
                    rt_bad += !(r.rbar == 0xAAAAAAAAu && r.rasr == 0x55555555u);   /* 실패 시 *out 불변 */
                }
            }
        }
    }
    /* (b) 영역 0..9 x ap 0..15 x attr 0..65 x xn 0..2 x srd 4 가지 x 크기 4 가지 */
    static const uint64_t sz[4] = { 32u, 128u, 256u, 4096u };
    static const uint32_t srds[4] = { 0u, 1u, 0xFFu, 0x100u };
    for (uint32_t reg = 0; reg < 10u; ++reg)
        for (uint32_t ap = 0; ap < 16u; ++ap)
            for (uint32_t at = 0; at < 66u; ++at)
                for (uint32_t xn = 0; xn < 3u; ++xn)
                    for (int s = 0; s < 4; ++s)
                        for (int q = 0; q < 4; ++q) {
                            mpu_region_t r = { 0u, 0u };
                            mpu_rc_t got = mpu_encode(reg, 0x20000000u, sz[s], ap, xn, at, srds[q], &r);
                            mpu_rc_t want = ref_rule(reg, 0x20000000u, sz[s], ap, xn, at, srds[q]);
                            mism += (got != want);
                            if (got == MPU_OK) {
                                ok++;
                                mpu_info_t d;
                                mpu_decode(r, &d);
                                rt_bad += !(d.region == reg && d.size == sz[s] && d.ap == ap && d.xn == xn &&
                                            d.attr == at && d.srd == srds[q]);
                            } else rej++;
                        }
    printf("  ex1: accepted %llu, rejected %llu, mismatches vs reference %llu, round-trip errors %llu, "
           "legal attr codes %u/64\n", (unsigned long long)ok, (unsigned long long)rej,
           (unsigned long long)mism, (unsigned long long)rt_bad, attr_ok);
    CHECK(mism == 0u);
    CHECK(rt_bad == 0u);
    CHECK(attr_ok == 48u);
    CHECK(ok > 0u && rej > 0u);
}

/* ================= 연습 2: mpu_check — 접근 하나의 판정 ================= */
typedef enum { ACC_R, ACC_W, ACC_X, ACC_VEC } acc_t;
typedef struct { uint32_t fault; int region; } chk_t;   /* fault: 0 / 0x01 IACCVIOL / 0x82 DACCVIOL|MMARVALID;
                                                           region: 0..7 히트, -1 배경 맵, -2 MPU 우회 */
static int default_xn(uint32_t a) { return (a >= 0x40000000u && a < 0x60000000u) || a >= 0xA0000000u; }
static chk_t fault_of(acc_t acc) { chk_t c = { acc == ACC_X ? 0x01u : 0x82u, 0 }; return c; }
static chk_t mpu_check(const mpu_region_t r[8], uint32_t ctrl, uint32_t addr, acc_t acc, int priv, int negpri) {
    chk_t c = { 0u, -2 };
    int bypass = !(ctrl & CTRL_ENABLE) || (negpri && !(ctrl & CTRL_HFNMIENA)) || acc == ACC_VEC ||
                 (addr >> 20) == 0xE00u;                  /* PPB */
    if (bypass) {
        if (acc == ACC_X && default_xn(addr)) c.fault = 0x01u;
        return c;
    }
    for (int n = 7; n >= 0; --n) {                        /* 번호가 큰 영역부터 → 먼저 맞는 것이 이긴다 */
        mpu_info_t d;
        mpu_decode(r[n], &d);
        if (!d.enable || d.size < 4u) continue;
        uint64_t mask = d.size - 1u;
        if (d.base & mask) continue;                      /* QEMU: 오정렬 영역은 무시 (실기: UNPREDICTABLE) */
        if (addr < d.base || (uint64_t)addr > d.base + mask) continue;
        if (d.size >= 256u && ((d.srd >> ((addr - d.base) / (d.size / 8u))) & 1u)) continue;  /* 꺼진 조각: 통과 */
        static const uint8_t pr[8] = { 0, 1, 1, 1, 0, 1, 1, 1 }, pw[8] = { 0, 1, 1, 1, 0, 0, 0, 0 };
        static const uint8_t ur[8] = { 0, 0, 1, 1, 0, 0, 1, 1 }, uw[8] = { 0, 0, 0, 1, 0, 0, 0, 0 };
        int rd = priv ? pr[d.ap] : ur[d.ap], wr = priv ? pw[d.ap] : uw[d.ap];
        int ok = acc == ACC_R ? rd : acc == ACC_W ? wr : (rd && !d.xn && addr < 0xE0000000u);
        c.region = n;
        if (!ok) c.fault = fault_of(acc).fault;
        return c;
    }
    if (priv && (ctrl & CTRL_PRIVDEFENA)) {               /* 배경 영역 = 기본 메모리 맵 */
        c.region = -1;
        if (acc == ACC_X && default_xn(addr)) c.fault = 0x01u;
        return c;
    }
    c.region = -1;
    c.fault = fault_of(acc).fault;
    return c;
}

/* solution 의 부팅 배치를 실측 주소로 재현 (g_ta=0x20000400, guard=+224) */
static void boot_tab(mpu_region_t t[8]) {
    memset(t, 0, 8 * sizeof t[0]);
    mpu_encode(0, 0x00000000u, 256u * 1024u, AP_RO, 0u, ATTR_FLASH, 0u, &t[0]);
    mpu_encode(1, 0x20000000u, 65536u, AP_FULL, 1u, ATTR_SRAM, 0u, &t[1]);
    mpu_encode(2, 0x00000000u, 256u, AP_NONE, 1u, ATTR_FLASH, 0u, &t[2]);
    mpu_encode(3, 0x200004E0u, 32u, AP_NONE, 1u, ATTR_SRAM, 0u, &t[3]);
}
static void ex2(void) {
    mpu_region_t t[8];
    boot_tab(t);
    const uint32_t C = CTRL_ENABLE | CTRL_PRIVDEFENA;
    chk_t c;
    c = mpu_check(t, C, 0x0u, ACC_R, 1, 0);        CHECK(c.fault == 0x82u && c.region == 2);
    c = mpu_check(t, C, 0x100u, ACC_X, 1, 0);      CHECK(c.fault == 0u && c.region == 0);      /* .text */
    c = mpu_check(t, C, 0x100u, ACC_W, 1, 0);      CHECK(c.fault == 0x82u && c.region == 0);   /* 플래시 RO */
    c = mpu_check(t, C, 0x5Cu, ACC_VEC, 1, 0);     CHECK(c.fault == 0u && c.region == -2);     /* 벡터 읽기 */
    c = mpu_check(t, C, 0x20000A00u, ACC_X, 1, 0); CHECK(c.fault == 0x01u && c.region == 1);   /* XN RAM */
    c = mpu_check(t, C, 0x200004F8u, ACC_W, 1, 0); CHECK(c.fault == 0x82u && c.region == 3);   /* 가드 */
    c = mpu_check(t, C, 0x200004DCu, ACC_W, 1, 0); CHECK(c.fault == 0u && c.region == 1);      /* 가드 바로 아래 */
    c = mpu_check(t, C, 0x40028000u, ACC_R, 1, 0); CHECK(c.fault == 0u && c.region == -1);     /* 배경 */
    c = mpu_check(t, C, 0x40028000u, ACC_X, 1, 0); CHECK(c.fault == 0x01u);                   /* 기본 맵 XN */
    c = mpu_check(t, CTRL_ENABLE, 0x40028000u, ACC_R, 1, 0); CHECK(c.fault == 0x82u);          /* PRIVDEFENA=0 */
    c = mpu_check(t, CTRL_ENABLE, 0xE000ED00u, ACC_R, 1, 0); CHECK(c.fault == 0u && c.region == -2);  /* PPB */
    c = mpu_check(t, C, 0x40028000u, ACC_R, 0, 0); CHECK(c.fault == 0x82u);                    /* 비특권: 배경 없음 */
    c = mpu_check(t, C, 0x20000000u, ACC_W, 0, 0); CHECK(c.fault == 0u);                       /* AP=011 비특권 RW */
    c = mpu_check(t, C, 0x0u, ACC_R, 1, 1);        CHECK(c.fault == 0u && c.region == -2);     /* HardFault, HFNMIENA=0 */
    c = mpu_check(t, C | CTRL_HFNMIENA, 0x0u, ACC_R, 1, 1); CHECK(c.fault == 0x82u);           /* → lockup 경로 */
    /* 서브리전: 256 B AP_NONE, SRD=0x0F (앞 4 조각 끔 → 아래 SRAM 영역이 받는다) */
    mpu_encode(5, 0x20000900u, 256u, AP_NONE, 1u, ATTR_SRAM, 0x0Fu, &t[5]);
    int sub_fault[8];
    for (int i = 0; i < 8; ++i) sub_fault[i] = mpu_check(t, C, 0x20000900u + 32u * (uint32_t)i, ACC_W, 1, 0).fault != 0u;
    CHECK(!sub_fault[0] && !sub_fault[3] && sub_fault[4] && sub_fault[7]);
    /* 성질: 모든 주소·접근에서 HFNMIENA=0 + negpri 는 기본 맵과 같다 / 영역 번호를 뒤집으면 겹친 곳의 결과가 바뀐다 */
    uint32_t viol = 0, x = 0x12345678u;
    for (int i = 0; i < 20000; ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        uint32_t a = (i & 1) ? (x & 0x3FFFFu) : (0x20000000u | (x & 0xFFFFu));
        acc_t acc = (acc_t)(x >> 30 & 3u);
        chk_t h = mpu_check(t, C, a, acc, 1, 1), off = mpu_check(t, 0u, a, acc, 1, 0);
        viol += (h.fault != off.fault);
    }
    CHECK(viol == 0u);
    printf("  ex2: 20000 random accesses, HardFault(HFNMIENA=0) == MPU-off: violations %u\n", viol);
}

/* ================= 연습 3: mpu_cover — [base, base+len) 를 영역 하나(+SRD)로 정확히 ================= */
typedef struct { uint32_t base; uint64_t size; uint32_t srd; } cover_t;
static int mpu_cover(uint32_t base, uint32_t len, cover_t *o) {
    if (len == 0u) return 0;
    for (int k = 5; k <= 32; ++k) {
        uint64_t S = 1ull << k;
        uint64_t B = (uint64_t)base & ~(S - 1u);
        if ((uint64_t)base + len > B + S) continue;
        if (base == B && len == S) { o->base = (uint32_t)B; o->size = S; o->srd = 0u; return 1; }
        if (S < 256u) continue;
        uint64_t sub = S / 8u;
        if ((base - B) % sub || len % sub) continue;
        uint32_t first = (uint32_t)((base - B) / sub), cnt = (uint32_t)(len / sub);
        uint32_t en = ((1u << cnt) - 1u) << first;
        o->base = (uint32_t)B; o->size = S; o->srd = (~en) & 0xFFu;
        return 1;
    }
    return 0;
}
static int covers_exactly(cover_t c, uint32_t base, uint32_t len) {   /* 32 B 단위로 집합 비교 */
    if (base < c.base || (uint64_t)base + len > c.base + c.size) return 0;   /* 범위가 영역 밖으로 */
    for (uint64_t a = c.base; a < c.base + c.size; a += 32u) {
        int in_region = c.size < 256u ? 1 : !((c.srd >> ((a - c.base) / (c.size / 8u))) & 1u);
        int want = a >= base && a < (uint64_t)base + len;
        if (in_region != want) return 0;
    }
    return 1;
}
static void ex3(void) {
    uint32_t found = 0, fail = 0, minbad = 0;
    for (uint32_t b = 0; b < 4096u; b += 32u)
        for (uint32_t l = 32; b + l <= 8192u; l += 32u) {
            cover_t c;
            int got = mpu_cover(b, l, &c);
            int brute_k = -1;                             /* 참조: 모든 크기 x 모든 SRD 256 가지 */
            for (int k = 5; k <= 14 && brute_k < 0; ++k) {
                uint64_t S = 1ull << k;
                cover_t t = { (uint32_t)(b & ~(uint32_t)(S - 1u)), S, 0u };
                for (uint32_t srd = 0; srd < (S >= 256u ? 256u : 1u); ++srd) {
                    t.srd = srd;
                    if (covers_exactly(t, b, l)) { brute_k = k; break; }
                }
            }
            if (got) { found++; fail += !covers_exactly(c, b, l); minbad += (brute_k < 0 || c.size != (1ull << brute_k)); }
            else fail += (brute_k >= 0);
        }
    cover_t c;
    CHECK(mpu_cover(0x20000000u, 40u * 1024u, &c) && c.size == 65536u && c.srd == 0xE0u);   /* 40 KB = 64 KB 의 5/8 */
    CHECK(mpu_cover(0x20000020u, 0x60u, &c) && c.size == 256u && c.srd == 0xF1u);         /* 96 B @+32: 256 B 의 조각 1..3 */
    CHECK(!mpu_cover(0x20000F00u, 0x200u, &c));                                          /* 4 KB 경계를 넘는 512 B: 불가 */
    CHECK(fail == 0u && minbad == 0u);
    printf("  ex3: %u coverable (base,len) pairs, wrong %u, not-minimal %u\n", found, fail, minbad);
}

/* ================= 연습 4: 가드 크기 규칙 ================= */
/* 스택 꼭대기 top 에서 함수들이 drops[i] 씩 SP 를 내리고 그 SP 에 쓴다. 가드 안 첫 접근 → fault →
 * 예외 프레임 exc_frame B 를 SP-exc_frame 부터 위로 쓰다가 가드에서 멈춘다 (QEMU 순서).
 * 반환: 1 = 잡힘, 0 = 못 잡음. *below = 가드 아래에 써진 워드 수 (조용한 손상). */
static int guard_sim(uint32_t top, uint32_t glo, uint32_t gsize, const uint32_t *drops, int n, uint32_t exc_frame,
                     uint32_t *below) {
    uint32_t sp = top, ghi = glo + gsize;
    *below = 0;
    for (int i = 0; i < n; ++i) {
        sp -= drops[i];
        if (sp >= glo && sp < ghi) {                      /* 가드 접근 → fault, 이어서 예외 프레임 쌓기 */
            for (uint32_t a = sp - exc_frame; a < sp; a += 4u) {
                if (a >= glo && a < ghi) break;
                if (a < glo) (*below)++;
            }
            return 1;
        }
        if (sp < glo) { (*below)++; return 0; }           /* 가드를 건너뜀: 첫 쓰기부터 손상 */
    }
    return 0;
}
static uint32_t guard_min(uint32_t max_drop, uint32_t exc_frame) {   /* MPU 영역이라 2 의 거듭제곱, ≥ 32 */
    uint32_t need = max_drop + exc_frame, g = 32u;
    while (g < need) g <<= 1;
    return g;
}
static void ex4(void) {
    uint32_t below;
    /* 실측 재현: solution T7 — 꼭대기 0x20000900, task_recurse 는 꼬리 호출(0 B), recurse 프레임 24 B, 가드 0x200004E0 32 B */
    uint32_t drops[60];
    drops[0] = 0u;
    for (int i = 1; i < 60; ++i) drops[i] = 24u;
    int det = guard_sim(0x20000900u, 0x200004E0u, 32u, drops, 60, 32u, &below);
    CHECK(det == 1 && below == 2u);                       /* 실측: victim 2 워드 손상 */
    det = guard_sim(0x20000900u, 0x200004C0u, 64u, drops, 60, 32u, &below);
    CHECK(det == 1 && below == 0u);                       /* 실측: 64 B 가드면 0 */
    uint32_t big[1] = { 160u };                           /* T9: 가드 위 32 B (0x20000520) 에서 160 B 내림 */
    det = guard_sim(0x20000520u, 0x200004E0u, 32u, big, 1, 32u, &below);
    CHECK(det == 0 && below == 1u);
    CHECK(guard_min(24u, 32u) == 64u && guard_min(160u, 32u) == 256u && guard_min(24u, 104u) == 128u);
    /* 성질: guard_min 이상이면 어떤 프레임 열에서도 잡히고 손상 0 (fixed-seed) */
    uint32_t x = 0xC0FFEEu, bad = 0, runs = 0;
    for (int t = 0; t < 5000; ++t) {
        uint32_t d[64], maxd = 0;
        for (int i = 0; i < 64; ++i) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            d[i] = 8u * (1u + (x % 32u));                 /* 8..256 B, 8 B 정렬 */
            if (d[i] > maxd) maxd = d[i];
        }
        uint32_t ef = (x & 1u) ? 32u : 104u;              /* 기본 / FP 확장 프레임 */
        uint32_t g = guard_min(maxd, ef);
        uint32_t glo = 0x20010000u - 0x8000u;             /* 정렬된 가드 (크기 ≤ 512) */
        int ok = guard_sim(glo + g + 256u + (x & 0xF8u), glo, g, d, 64, ef, &below);   /* 합 ≥ 512 → 반드시 도달 */
        runs++;
        bad += !(ok == 1 && below == 0u);
    }
    CHECK(bad == 0u);
    printf("  ex4: T7 model below-guard words 32B=2, 64B=0; %u random frame sequences with guard_min: bad %u\n",
           runs, bad);
}

/* ================= 연습 5: QEMU 실측 표 대조 ================= */
typedef struct { const char *name; uint32_t ctrl; uint32_t addr; acc_t acc; int negpri; int variant;
                 uint32_t observed; } obs_t;   /* observed = 실측 CFSR 의 MMFSR 부분 (MSTKERR 제외), 0 = fault 없음 */
static void ex5(void) {
    const uint32_t C = CTRL_ENABLE | CTRL_PRIVDEFENA;
    static const obs_t k[] = {   /* run_qemu.sh solution / probe 의 실제 출력에서 옮김 */
        { "T2 NULL read",      0, 0x00000000u, ACC_R, 0, 0, 0x82u },
        { "T3 NULL->0x10",     0, 0x00000010u, ACC_R, 0, 0, 0x82u },
        { "T4 NULL write",     0, 0x00000000u, ACC_W, 0, 0, 0x82u },
        { "T5 read vec[1]",    0, 0x00000004u, ACC_R, 0, 0, 0x82u },
        { "T5 IRQ7 vector",    0, 0x0000005Cu, ACC_VEC, 0, 0, 0u },
        { "T6 NULL is R0",     0, 0x00000000u, ACC_R, 0, 1, 0u },
        { "T7 recursion32",    0, 0x200004F8u, ACC_W, 0, 0, 0x82u },
        { "T9 big frame32",    0, 0x20000480u, ACC_W, 0, 0, 0u },
        { "T9 guard 256 B",    0, 0x20000480u, ACC_W, 0, 2, 0x82u },
        { "T10 XN RAM",        0, 0x20000A00u, ACC_X, 0, 0, 0x01u },
        { "T10 RAMX region",   0, 0x20000A00u, ACC_X, 0, 3, 0u },
        { "T10 W^X write",     0, 0x20000A00u, ACC_W, 0, 3, 0x82u },
        { "T11 SRD sub0",      0, 0x20000900u, ACC_W, 0, 4, 0u },
        { "T11 SRD sub3",      0, 0x2000097Cu, ACC_W, 0, 4, 0u },
        { "T11 SRD sub4",      0, 0x20000980u, ACC_W, 0, 4, 0x82u },
        { "T11 SRD sub7",      0, 0x200009FCu, ACC_W, 0, 4, 0x82u },
        { "T12 noPRIVDEF",     CTRL_ENABLE, 0x40028000u, ACC_R, 0, 0, 0x82u },
        { "T12 PPB CPUID",     CTRL_ENABLE, 0xE000ED00u, ACC_R, 0, 0, 0u },
        { "T12 PRIVDEF=1",     0, 0x40028000u, ACC_R, 0, 0, 0u },
        { "T13 HF probe",      0, 0x00000000u, ACC_R, 1, 0, 0u },
        { "lockup HFNMIENA=1", CTRL_ENABLE | CTRL_PRIVDEFENA | CTRL_HFNMIENA, 0x0u, ACC_R, 1, 0, 0x82u },
    };
    uint32_t agree = 0, n = (uint32_t)(sizeof k / sizeof k[0]);
    for (uint32_t i = 0; i < n; ++i) {
        mpu_region_t t[8];
        boot_tab(t);
        if (k[i].variant == 1) {                          /* 번호 맞바꿈 */
            mpu_region_t tmp = t[0]; t[0] = t[2]; t[2] = tmp;
            t[0].rbar = (t[0].rbar & ~0xFu) | 0u; t[2].rbar = (t[2].rbar & ~0xFu) | 2u;
        }
        if (k[i].variant == 2) mpu_encode(3, 0x20000400u, 256u, AP_NONE, 1u, ATTR_SRAM, 0u, &t[3]);
        if (k[i].variant == 3) mpu_encode(4, 0x20000A00u, 32u, AP_RO, 0u, ATTR_SRAM, 0u, &t[4]);
        if (k[i].variant == 4) mpu_encode(5, 0x20000900u, 256u, AP_NONE, 1u, ATTR_SRAM, 0x0Fu, &t[5]);
        chk_t c = mpu_check(t, k[i].ctrl ? k[i].ctrl : C, k[i].addr, k[i].acc, 1, k[i].negpri);
        int same = c.fault == k[i].observed;
        agree += (uint32_t)same;
        if (!same) printf("  ex5 mismatch: %s model=0x%02x observed=0x%02x\n", k[i].name, c.fault, k[i].observed);
    }
    /* demo_probe 의 "규칙 위반" 실측: 모델은 QEMU 동작을 따르도록 정의 (오정렬 무시, 128 B SRD 무시, AP=100 무권한) */
    mpu_region_t p[8];
    memset(p, 0, sizeof p);
    p[0].rbar = 0x20000500u | 0x10u; p[0].rasr = (9u << 1) | 1u;          /* P5 1 KB @ 256 B 정렬 */
    CHECK(mpu_check(p, C, 0x20000500u, ACC_R, 1, 0).fault == 0u);
    p[0].rbar = 0x20000400u | 0x10u; p[0].rasr = (0xFFu << 8) | (6u << 1) | 1u;   /* P6 128 B, SRD=0xFF */
    CHECK(mpu_check(p, C, 0x20000400u, ACC_R, 1, 0).fault == 0x82u);
    p[0].rasr = (4u << 24) | (9u << 1) | 1u;                                 /* P7 AP=100 */
    CHECK(mpu_check(p, C, 0x20000400u, ACC_R, 1, 0).fault == 0x82u);
    p[0].rasr = (3u << 1) | 1u;                                              /* P8 SIZE=3 → 16 B */
    CHECK(mpu_check(p, C, 0x20000400u, ACC_R, 1, 0).fault == 0x82u && mpu_check(p, C, 0x20000410u, ACC_R, 1, 0).fault == 0u);
    CHECK(agree == n);
    printf("  ex5: QEMU observations %u, model agrees %u\n", n, agree);
}

int main(void) {
    ex1();
    ex2();
    ex3();
    ex4();
    ex5();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
