/* ARCH-M15 MPU 영역 계산기와 접근 검사 — 체크 테스트
 *
 *   Part A  ARMv7-M PMSAv7 영역 1개: SIZE 필드(2^(SIZE+1) 바이트, 최소 32 B), 크기 정렬, 설정 검증,
 *           AP 3비트 특권/비특권 R/W 표, 영역 히트(서브리전 SRD 포함), 바이트 1개 판정 mpu_check
 *   Part B  어려운 부분: 겹침 → 번호 큰 영역이 이김, 서브리전 구멍 → 아래 영역/기본 맵으로 떨어짐,
 *           PRIVDEFENA / HFNMIENA / PPB, 여러 바이트 접근(영역 경계 걸침) = 32 B 그래뉼 단위 검사,
 *           독립 참조 모델과 무작위 20만 접근 비교
 *   Part C  펌웨어 현실: 버퍼 → 영역 계산기(mpu_calc, 서브리전으로 넘침 줄이기), RBAR/RASR 인코드·디코드,
 *           RTOS 태스크 구성(Flash RX-only, SRAM XN, 주변장치 창, 32 B 스택 가드)과 MemManage 보고
 *
 * 규칙은 ARMv7-M ARM(DDI 0403) PMSAv7 과 Cortex-M4 Generic User Guide 의 MPU 절을 따랐다.
 * 스펙이 UNPREDICTABLE 로 두는 설정(SIZE<4, 128 B 이하 영역의 SRD≠0, AP=100, 크기에 안 맞는 base)은
 * region_validate 가 에러로 거르고, 검사기는 그런 설정에서 실제 코어 동작을 흉내 내지 않는다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs_state = 0x1F123BB5u;
static uint32_t xorshift32(void) {
    uint32_t x = xs_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return xs_state = x;
}

/* ======================= 공통 타입 (주어짐) ======================= */
/* RBAR / RASR 필드 위치 — CMSIS core_cm4.h 의 MPU_RBAR_*_Pos / MPU_RASR_*_Pos 와 같다 */
#define RBAR_ADDR_MASK  0xFFFFFFE0u      /* ADDR: 의미 있는 비트는 [31:N], N = log2(영역 크기) */
#define RBAR_VALID      (1u << 4)        /* 1 이면 REGION[3:0] 이 RNR 대신 영역 번호를 고른다 */
#define RBAR_REGION_MSK 0xFu
#define RASR_ENABLE     (1u << 0)
#define RASR_SIZE_POS   1                /* [5:1]   SIZE  */
#define RASR_SRD_POS    8                /* [15:8]  SRD   */
#define RASR_SCB_POS    16               /* [21:16] TEX[2:0] S C B */
#define RASR_AP_POS     24               /* [26:24] AP    */
#define RASR_XN         (1u << 28)

#define MPU_NREG 8

typedef struct {
    bool     en;
    uint32_t base;
    uint8_t  size_f;    /* SIZE 필드: 영역 크기 = 2^(size_f+1) 바이트 (4 → 32 B … 31 → 4 GB) */
    uint8_t  srd;       /* 서브리전 disable: bit k = 1 → k 번째 1/8 조각은 이 영역에 속하지 않는다 */
    uint8_t  ap;        /* AP[2:0] */
    bool     xn;
    uint8_t  texscb;    /* TEX[2:0] S C B — 메모리 타입. 이 모델은 보관만 한다 (의미는 BM-A08) */
} region_t;

typedef struct {
    bool enable;        /* MPU_CTRL.ENABLE */
    bool hfnmiena;      /* MPU_CTRL.HFNMIENA: 0 이면 HardFault/NMI(우선순위 < 0) 동안 MPU 를 쓰지 않는다 */
    bool privdefena;    /* MPU_CTRL.PRIVDEFENA: 1 이면 특권 접근이 어느 영역에도 안 걸릴 때 기본 맵 사용 */
    region_t r[MPU_NREG];
} mpu_t;

typedef enum { ACC_R, ACC_W, ACC_X } acc_t;          /* 데이터 읽기 / 데이터 쓰기 / 명령 fetch */

typedef enum { CFG_OK = 0, CFG_SIZE_BAD, CFG_BASE_ALIGN, CFG_SRD_SMALL, CFG_AP_RESERVED } cfg_err_t;

typedef enum {
    OK_REGION,       /* 걸린 영역이 허용 */
    OK_DEFAULT,      /* 어느 영역에도 안 걸림 + 특권 + PRIVDEFENA → 기본 맵 */
    OK_MPU_OFF,      /* MPU 꺼짐, 또는 HardFault/NMI 에서 HFNMIENA=0 → 기본 맵 */
    OK_PPB,          /* PPB(0xE000_0000–0xE00F_FFFF)는 MPU 와 무관하게 기본 맵 */
    F_NOMATCH,       /* 어느 영역에도 안 걸림 (비특권, 또는 PRIVDEFENA=0) */
    F_AP,            /* 걸린 영역의 AP 가 거부 */
    F_XN,            /* 걸린 영역이 XN 인데 명령 fetch */
    F_DEFAULT_XN,    /* 기본 맵의 XN 구역(Peripheral·External device·System)에서 명령 fetch */
    F_PPB_UNPRIV     /* 비특권 PPB 접근 → MemManage 가 아니라 BusFault */
} why_t;

typedef struct {
    bool     allowed;
    why_t    why;
    int      region;      /* 판정한 영역 번호, 기본 맵·PPB 면 -1 */
    bool     memmanage;   /* 폴트 종류: true = MemManage, false = BusFault (allowed 면 의미 없음) */
    bool     iaccviol, daccviol, mmarvalid;  /* MMFSR 흉내 */
    uint32_t mmfar;       /* mmarvalid 일 때만 의미: 폴트 난 (첫) 바이트 주소 */
} mm_report_t;

static uint64_t region_bytes(uint8_t size_f) { return (uint64_t)1 << (size_f + 1); }

/* ARMv7-M 기본 메모리 맵의 XN 구역: Peripheral 0x4000_0000–0x5FFF_FFFF, External device 0xA000_0000–,
 * System 0xE000_0000– (Code / SRAM / External RAM 은 실행 가능) */
static bool default_xn(uint32_t a) { return (a >= 0x40000000u && a <= 0x5FFFFFFFu) || a >= 0xA0000000u; }
static bool in_ppb(uint32_t a) { return a >= 0xE0000000u && a <= 0xE00FFFFFu; }

static mm_report_t rep_ok(why_t why, int region) {
    mm_report_t r; memset(&r, 0, sizeof r);
    r.allowed = true; r.why = why; r.region = region;
    return r;
}
static mm_report_t rep_fault(why_t why, int region, acc_t acc, uint32_t addr) {
    mm_report_t r; memset(&r, 0, sizeof r);
    r.allowed = false; r.why = why; r.region = region;
    r.memmanage = (why != F_PPB_UNPRIV);
    if (r.memmanage) {
        r.iaccviol  = (acc == ACC_X);
        r.daccviol  = (acc != ACC_X);
        r.mmarvalid = r.daccviol;            /* 명령 fetch 위반은 MMFAR 를 채우지 않는다 */
        if (r.mmarvalid) r.mmfar = addr;
    }
    return r;
}

/* 켜진 서브리전 바이트 수 (128 B 이하 영역은 서브리전이 없으니 전체) */
static uint64_t enabled_bytes(const region_t *r) {
    uint64_t sz = region_bytes(r->size_f);
    if (sz < 256) return sz;
    unsigned on = 0;
    for (unsigned k = 0; k < 8; k++) if (!(r->srd & (1u << k))) on++;
    return sz / 8 * on;
}

/* ======================= Part A — 영역 1개와 바이트 1개 판정 ======================= */

/* 설정 검증. 꺼진 영역(en=0)은 검사하지 않는다(CFG_OK). 첫 번째로 걸린 이유 하나만, 이 순서로:
 * SIZE<4 또는 >31 → CFG_SIZE_BAD, base 가 크기의 배수가 아님 → CFG_BASE_ALIGN,
 * 128 B 이하 영역에 SRD≠0 → CFG_SRD_SMALL, AP=100(또는 >7) → CFG_AP_RESERVED */
static cfg_err_t region_validate(const region_t *r) {
    if (!r->en) return CFG_OK;
    if (r->size_f < 4 || r->size_f > 31) return CFG_SIZE_BAD;
    uint64_t sz = region_bytes(r->size_f);
    if ((r->base & (sz - 1)) != 0) return CFG_BASE_ALIGN;
    if (sz < 256 && r->srd != 0) return CFG_SRD_SMALL;
    if (r->ap == 4 || r->ap > 7) return CFG_AP_RESERVED;
    return CFG_OK;
}

/* AP[2:0] → 허용 여부.  000 없음/없음 · 001 RW/없음 · 010 RW/RO · 011 RW/RW · 100 예약(→ 금지)
 *                       101 RO/없음 · 110 RO/RO · 111 RO/RO           (특권/비특권) */
static bool ap_allows(uint8_t ap, bool priv, bool write) {
    switch (ap) {
    case 0:  return false;
    case 1:  return priv;
    case 2:  return priv || !write;
    case 3:  return true;
    case 5:  return priv && !write;
    case 6:
    case 7:  return !write;
    default: return false;
    }
}

/* 영역 r 이 addr 를 포함하는가: 켜져 있고, [base, base+size) 안이고, 그 서브리전이 꺼져 있지 않다.
 * 4 GB 영역(base+size = 2^32)도 맞아야 한다. 서브리전은 256 B 이상 영역에만 있다. */
static bool region_hit(const region_t *r, uint32_t addr) {
    if (!r->en) return false;
    uint64_t sz = region_bytes(r->size_f);
    uint32_t off = addr - r->base;
    if ((uint64_t)off >= sz) return false;
    if (sz >= 256) {
        unsigned sub = (unsigned)(off >> (r->size_f + 1 - 3));     /* off / (sz/8) */
        if (r->srd & (1u << sub)) return false;
    }
    return true;
}

/* 바이트 1개 접근 판정.  순서:
 *  1) PPB → MPU 무시: 실행은 F_DEFAULT_XN, 비특권은 F_PPB_UNPRIV, 아니면 OK_PPB
 *  2) MPU 꺼짐 또는 (hf_nmi && !HFNMIENA) → 기본 맵 (실행 + default_xn → F_DEFAULT_XN, 아니면 OK_MPU_OFF)
 *  3) 영역 7 → 0 순서로 첫 히트가 판정: 실행 && XN → F_XN, AP 거부 → F_AP (실행은 읽기 권한 필요), 아니면 OK_REGION
 *  4) 히트 없음: 특권 && PRIVDEFENA → 기본 맵 (OK_DEFAULT / F_DEFAULT_XN), 아니면 F_NOMATCH */
static mm_report_t mpu_check(const mpu_t *m, uint32_t addr, acc_t acc, bool priv, bool hf_nmi) {
    if (in_ppb(addr)) {
        if (acc == ACC_X) return rep_fault(F_DEFAULT_XN, -1, acc, addr);
        if (!priv) return rep_fault(F_PPB_UNPRIV, -1, acc, addr);
        return rep_ok(OK_PPB, -1);
    }
    if (!m->enable || (hf_nmi && !m->hfnmiena)) {
        if (acc == ACC_X && default_xn(addr)) return rep_fault(F_DEFAULT_XN, -1, acc, addr);
        return rep_ok(OK_MPU_OFF, -1);
    }
    for (int i = MPU_NREG - 1; i >= 0; i--) {
        const region_t *r = &m->r[i];
        if (!region_hit(r, addr)) continue;
        if (acc == ACC_X && r->xn) return rep_fault(F_XN, i, acc, addr);
        if (!ap_allows(r->ap, priv, acc == ACC_W)) return rep_fault(F_AP, i, acc, addr);
        return rep_ok(OK_REGION, i);
    }
    if (priv && m->privdefena) {
        if (acc == ACC_X && default_xn(addr)) return rep_fault(F_DEFAULT_XN, -1, acc, addr);
        return rep_ok(OK_DEFAULT, -1);
    }
    return rep_fault(F_NOMATCH, -1, acc, addr);
}

/* ======================= Part B — 여러 바이트 접근 ======================= */

/* [addr, addr+size) 접근. 검증된 설정에서는 판정이 32 B 정렬 그래뉼 안에서 일정하므로
 * 첫 바이트 + 그 뒤 각 그래뉼의 첫 바이트만 본다. 첫 폴트 바이트의 보고를 돌려주고,
 * 모두 허용이면 첫 바이트의 보고를 돌려준다. (addr+size 가 2^32 를 넘는 접근은 다루지 않는다) */
static mm_report_t mpu_access(const mpu_t *m, uint32_t addr, unsigned size, acc_t acc, bool priv, bool hf_nmi) {
    mm_report_t first = mpu_check(m, addr, acc, priv, hf_nmi);
    if (!first.allowed) return first;
    uint64_t end = (uint64_t)addr + size;
    for (uint64_t g = ((uint64_t)addr | 31u) + 1; g < end; g += 32) {
        mm_report_t r = mpu_check(m, (uint32_t)g, acc, priv, hf_nmi);
        if (!r.allowed) return r;
    }
    return first;
}

/* ======================= Part C — 계산기와 레지스터 인코딩 ======================= */

/* [start, end) 를 덮는, 크기에 정렬된 2^(f+1) 블록 중 f ≥ min_f 인 가장 작은 것.
 * 256 B 이상이면 범위와 겹치지 않는 서브리전을 끈다(SRD). out: en=1, ap/xn/texscb = 0. 0 = OK, -1 = 없음 */
static int fit_region(uint32_t start, uint64_t end, uint8_t min_f, region_t *out) {
    for (unsigned f = min_f; f <= 31; f++) {
        uint64_t sz = region_bytes((uint8_t)f);
        uint64_t base = (uint64_t)start & ~(sz - 1);
        if (base + sz < end) continue;
        memset(out, 0, sizeof *out);
        out->en = true; out->base = (uint32_t)base; out->size_f = (uint8_t)f;
        if (sz >= 256) {
            uint64_t sub = sz / 8;
            for (unsigned k = 0; k < 8; k++) {
                uint64_t lo = base + k * sub, hi = lo + sub;
                if (hi <= start || lo >= end) out->srd |= (uint8_t)(1u << k);
            }
        }
        return 0;
    }
    return -1;
}

/* 버퍼 [start, start+len) 를 영역 1개로 덮는다. 넘침 over = 영역이 실제로 덮는 바이트 − len 을 최소로.
 * 가장 작은 덮는 블록이 128 B 이하면 256 B 블록 + 32 B 서브리전이 덜 넘칠 수 있으니 둘을 비교하고
 * (같으면 작은 영역), 256 B 이상이면 그 블록이 최적이다. 0 = OK, -1 = len 0 또는 2^32 를 넘음. */
static int mpu_calc(uint32_t start, uint32_t len, region_t *out, uint32_t *over) {
    uint64_t end = (uint64_t)start + len;
    if (len == 0 || end > ((uint64_t)1 << 32)) return -1;
    region_t a, b;
    if (fit_region(start, end, 4, &a) != 0) return -1;
    *out = a;
    if (region_bytes(a.size_f) < 256 && fit_region(start, end, 7, &b) == 0 &&
        enabled_bytes(&b) < enabled_bytes(&a))
        *out = b;
    *over = (uint32_t)(enabled_bytes(out) - len);
    return 0;
}

/* 영역 n 의 RBAR(VALID=1, REGION=n) / RASR 값 만들기 */
static void mpu_encode(const region_t *r, unsigned n, uint32_t *rbar, uint32_t *rasr) {
    *rbar = (r->base & RBAR_ADDR_MASK) | RBAR_VALID | (n & RBAR_REGION_MSK);
    *rasr = (r->xn ? RASR_XN : 0u)
          | ((uint32_t)(r->ap & 7u) << RASR_AP_POS)
          | ((uint32_t)(r->texscb & 0x3Fu) << RASR_SCB_POS)
          | ((uint32_t)r->srd << RASR_SRD_POS)
          | ((uint32_t)(r->size_f & 0x1Fu) << RASR_SIZE_POS)
          | (r->en ? RASR_ENABLE : 0u);
}

/* RBAR / RASR → region_t (base 는 ADDR 필드 그대로 — 정렬 검사는 region_validate 몫) */
static region_t mpu_decode(uint32_t rbar, uint32_t rasr) {
    region_t r;
    r.en     = (rasr & RASR_ENABLE) != 0;
    r.base   = rbar & RBAR_ADDR_MASK;
    r.size_f = (uint8_t)((rasr >> RASR_SIZE_POS) & 0x1Fu);
    r.srd    = (uint8_t)((rasr >> RASR_SRD_POS) & 0xFFu);
    r.texscb = (uint8_t)((rasr >> RASR_SCB_POS) & 0x3Fu);
    r.ap     = (uint8_t)((rasr >> RASR_AP_POS) & 7u);
    r.xn     = (rasr & RASR_XN) != 0;
    return r;
}

/* ======================= 테스트용 (주어짐) ======================= */

/* 독립 참조 모델: 영역 0 → 7 을 다 훑고 "마지막 히트" 를 쓰며, 서브리전은 나눗셈으로, 접근은 바이트마다 */
static bool ref_hit(const region_t *r, uint32_t a) {
    if (!r->en) return false;
    uint64_t sz = region_bytes(r->size_f), lo = r->base;
    if ((uint64_t)a < lo || (uint64_t)a >= lo + sz) return false;
    if (sz < 256) return true;
    return ((r->srd >> (((uint64_t)a - lo) / (sz / 8))) & 1u) == 0;
}
static mm_report_t ref_byte(const mpu_t *m, uint32_t a, acc_t acc, bool priv, bool hf) {
    bool x = (acc == ACC_X), w = (acc == ACC_W);
    if (a >= 0xE0000000u && a < 0xE0100000u)
        return x ? rep_fault(F_DEFAULT_XN, -1, acc, a) : !priv ? rep_fault(F_PPB_UNPRIV, -1, acc, a) : rep_ok(OK_PPB, -1);
    bool dxn = (a >> 29) == 2 || a >= 0xA0000000u;                 /* 0x4000_0000–0x5FFF_FFFF = 상위 3비트 010 */
    if (!m->enable || (hf && !m->hfnmiena))
        return (x && dxn) ? rep_fault(F_DEFAULT_XN, -1, acc, a) : rep_ok(OK_MPU_OFF, -1);
    int win = -1;
    for (int i = 0; i < MPU_NREG; i++) if (ref_hit(&m->r[i], a)) win = i;
    if (win < 0) {
        if (priv && m->privdefena) return (x && dxn) ? rep_fault(F_DEFAULT_XN, -1, acc, a) : rep_ok(OK_DEFAULT, -1);
        return rep_fault(F_NOMATCH, -1, acc, a);
    }
    static const char *const tbl[8] = { "----", "RW--", "RWR-", "RWRW", "----", "R---", "R-R-", "R-R-" };
    const char *t = tbl[m->r[win].ap & 7u] + (priv ? 0 : 2);        /* "pR pW uR uW" */
    bool ok = w ? t[1] == 'W' : t[0] == 'R';
    if (x && m->r[win].xn) return rep_fault(F_XN, win, acc, a);
    return ok ? rep_ok(OK_REGION, win) : rep_fault(F_AP, win, acc, a);
}
static mm_report_t ref_access(const mpu_t *m, uint32_t a, unsigned size, acc_t acc, bool priv, bool hf) {
    mm_report_t first = ref_byte(m, a, acc, priv, hf);
    for (unsigned i = 0; i < size; i++) {
        mm_report_t r = ref_byte(m, a + i, acc, priv, hf);
        if (!r.allowed) return r;
    }
    return first;
}
static bool same_rep(mm_report_t a, mm_report_t b) {
    return a.allowed == b.allowed && a.why == b.why && a.region == b.region && a.memmanage == b.memmanage &&
           a.iaccviol == b.iaccviol && a.daccviol == b.daccviol && a.mmarvalid == b.mmarvalid && a.mmfar == b.mmfar;
}

static region_t mk(uint32_t base, uint8_t size_f, uint8_t ap, bool xn, uint8_t srd) {
    region_t r; memset(&r, 0, sizeof r);
    r.en = true; r.base = base; r.size_f = size_f; r.ap = ap; r.xn = xn; r.srd = srd;
    return r;
}
static mpu_t mpu_new(void) { mpu_t m; memset(&m, 0, sizeof m); m.enable = true; m.privdefena = true; return m; }

/* 무작위지만 "검증 통과" 하는 영역 (작은 크기 위주, 주소는 SRAM 근처에 몰리게) */
static region_t rand_region(void) {
    static const uint8_t aps[7] = { 0, 1, 2, 3, 5, 6, 7 };
    uint32_t r = xorshift32();
    uint8_t f = (r & 3u) == 0 ? (uint8_t)(4 + (r >> 2) % 28) : (uint8_t)(4 + (r >> 2) % 10);   /* 32 B–4 GB, 주로 ≤ 32 KB */
    uint64_t sz = region_bytes(f);
    uint32_t a = 0x20000000u + (xorshift32() & 0x0003FFFFu);
    if ((xorshift32() & 7u) == 0) a = xorshift32();
    region_t g = mk((uint32_t)((uint64_t)a & ~(sz - 1)), f, aps[xorshift32() % 7], (xorshift32() & 1u) != 0,
                    sz >= 256 ? (uint8_t)((xorshift32() & 3u) ? xorshift32() : 0) : 0);
    g.en = (xorshift32() % 8) != 0;
    return g;
}

/* 버퍼마다 가장 작은 넘침을 무식하게: 모든 f 에 대해 덮는 블록 + SRD → 켜진 바이트 최소값 */
static uint64_t brute_min_cover(uint32_t start, uint32_t len) {
    uint64_t end = (uint64_t)start + len, best = UINT64_MAX;
    for (unsigned f = 4; f <= 31; f++) {
        uint64_t sz = region_bytes((uint8_t)f), base = (uint64_t)start / sz * sz;
        if (base + sz < end) continue;
        uint64_t cov = sz;
        if (sz >= 256) {
            uint64_t sub = sz / 8, first = (start - base) / sub, last = (end - 1 - base) / sub;
            cov = (last - first + 1) * sub;
        }
        if (cov < best) best = cov;
    }
    return best;
}

int main(void) {
    mpu_t m;
    mm_report_t rp;
    region_t r;
    uint32_t over, rbar, rasr;

    /* ---------------- Part A: 영역 1개 ---------------- */
    CHECK(region_bytes(4) == 32 && region_bytes(9) == 1024 && region_bytes(31) == ((uint64_t)1 << 32));
    CHECK(region_validate(&(region_t){ .en = true, .base = 0x20000000u, .size_f = 4, .ap = 3 }) == CFG_OK);
    CHECK(region_validate(&(region_t){ .en = true, .base = 0x20000000u, .size_f = 3, .ap = 3 }) == CFG_SIZE_BAD);
    CHECK(region_validate(&(region_t){ .en = true, .base = 0x20000010u, .size_f = 4, .ap = 3 }) == CFG_BASE_ALIGN);
    CHECK(region_validate(&(region_t){ .en = true, .base = 0x20000100u, .size_f = 9, .ap = 3 }) == CFG_BASE_ALIGN);
    CHECK(region_validate(&(region_t){ .en = true, .base = 0x20000080u, .size_f = 6, .ap = 3, .srd = 1 }) == CFG_SRD_SMALL);
    CHECK(region_validate(&(region_t){ .en = true, .base = 0x20000100u, .size_f = 7, .ap = 3, .srd = 0x80 }) == CFG_OK);
    CHECK(region_validate(&(region_t){ .en = true, .base = 0x20000000u, .size_f = 4, .ap = 4 }) == CFG_AP_RESERVED);
    CHECK(region_validate(&(region_t){ .en = true, .base = 0x00000000u, .size_f = 31, .ap = 3 }) == CFG_OK);
    CHECK(region_validate(&(region_t){ .en = true, .base = 0x20000000u, .size_f = 31, .ap = 3 }) == CFG_BASE_ALIGN);
    CHECK(region_validate(&(region_t){ .en = false, .base = 0x12345678u, .size_f = 1, .ap = 4 }) == CFG_OK);

    {   /* AP 표 전수: 8 × (특권 R, 특권 W, 비특권 R, 비특권 W) */
        static const char *const want[8] = { "----", "RW--", "RWR-", "RWRW", "----", "R---", "R-R-", "R-R-" };
        int bad = 0;
        for (uint8_t ap = 0; ap < 8; ap++)
            for (int k = 0; k < 4; k++) {
                bool priv = k < 2, wr = (k & 1) != 0;
                bool exp = want[ap][k] != '-';
                if (ap_allows(ap, priv, wr) != exp) bad++;
            }
        CHECK(bad == 0);
        CHECK(ap_allows(2, true, true) && ap_allows(2, false, false) && !ap_allows(2, false, true));
        CHECK(!ap_allows(6, true, true) && ap_allows(6, false, false));
    }

    r = mk(0x20000400u, 9, 3, true, 0);                                   /* 1 KB @ 0x2000_0400 */
    CHECK(!region_hit(&r, 0x200003FFu) && region_hit(&r, 0x20000400u));
    CHECK(region_hit(&r, 0x200007FFu) && !region_hit(&r, 0x20000800u));
    CHECK(!region_hit(&r, 0x00000400u) && !region_hit(&r, 0xA0000400u));  /* 위 비트가 다르면 안 맞음 */
    r = mk(0, 31, 3, false, 0);                                           /* 4 GB: base+size = 2^32 */
    CHECK(region_hit(&r, 0) && region_hit(&r, 0xFFFFFFFFu));
    r = mk(0xFFFFFFE0u, 4, 3, false, 0);                                  /* 주소 공간 맨 끝 32 B */
    CHECK(region_hit(&r, 0xFFFFFFFFu) && !region_hit(&r, 0xFFFFFFDFu) && !region_hit(&r, 0));
    r.en = false;
    CHECK(!region_hit(&r, 0xFFFFFFF0u));

    m = mpu_new();
    m.r[0] = mk(0x08000000u, 18, 6, false, 0);                            /* Flash 512 KB, RO/RO, 실행 가능 */
    m.r[1] = mk(0x20000000u, 16, 3, true, 0);                             /* SRAM 128 KB, RW/RW, XN */
    rp = mpu_check(&m, 0x20001000u, ACC_R, false, false);
    CHECK(rp.allowed && rp.why == OK_REGION && rp.region == 1);
    rp = mpu_check(&m, 0x20001000u, ACC_W, false, false);
    CHECK(rp.allowed && rp.region == 1);
    rp = mpu_check(&m, 0x20001000u, ACC_X, true, false);
    CHECK(!rp.allowed && rp.why == F_XN && rp.region == 1 && rp.memmanage && rp.iaccviol && !rp.mmarvalid);
    rp = mpu_check(&m, 0x08000100u, ACC_X, false, false);
    CHECK(rp.allowed && rp.why == OK_REGION && rp.region == 0);
    rp = mpu_check(&m, 0x08000100u, ACC_W, true, false);
    CHECK(!rp.allowed && rp.why == F_AP && rp.region == 0 && rp.daccviol && rp.mmarvalid && rp.mmfar == 0x08000100u);

    /* ---------------- Part B: 겹침 · 서브리전 · 기본 맵 · 걸침 ---------------- */
    m = mpu_new();
    m.r[0] = mk(0x20000000u, 15, 3, true, 0);                             /* 64 KB RW/RW */
    m.r[1] = mk(0x20001000u, 9, 6, true, 0);                              /* 1 KB RO/RO 가 위에 겹침 */
    rp = mpu_check(&m, 0x20001000u, ACC_W, true, false);
    CHECK(!rp.allowed && rp.why == F_AP && rp.region == 1);               /* 번호 큰 쪽(작은 영역)이 이김 */
    m.r[0] = mk(0x20001000u, 9, 6, true, 0);
    m.r[1] = mk(0x20000000u, 15, 3, true, 0);
    rp = mpu_check(&m, 0x20001000u, ACC_W, true, false);
    CHECK(rp.allowed && rp.region == 1);                                  /* 크기가 아니라 번호 — 큰 영역이 덮어씀 */

    m = mpu_new();
    m.r[0] = mk(0x20000000u, 15, 1, true, 0);                             /* 64 KB 특권 전용 */
    m.r[1] = mk(0x20002000u, 12, 3, true, 0x30);                          /* 8 KB RW/RW, 서브리전 4·5 끔 */
    rp = mpu_check(&m, 0x20002FFCu, ACC_W, false, false);
    CHECK(rp.allowed && rp.region == 1);
    rp = mpu_check(&m, 0x20003000u, ACC_W, false, false);                 /* 구멍 → 영역 0 으로 떨어짐 */
    CHECK(!rp.allowed && rp.why == F_AP && rp.region == 0 && rp.mmfar == 0x20003000u);
    rp = mpu_check(&m, 0x20003000u, ACC_W, true, false);
    CHECK(rp.allowed && rp.region == 0);
    CHECK(mpu_check(&m, 0x20003800u, ACC_W, false, false).region == 1);   /* 서브리전 6 은 다시 켜짐 */
    m.r[0].en = false;                                                     /* 구멍 아래 아무것도 없음 */
    rp = mpu_check(&m, 0x20003000u, ACC_W, false, false);
    CHECK(!rp.allowed && rp.why == F_NOMATCH && rp.region == -1 && rp.mmarvalid);
    rp = mpu_check(&m, 0x20003000u, ACC_W, true, false);
    CHECK(rp.allowed && rp.why == OK_DEFAULT);
    m.privdefena = false;
    CHECK(mpu_check(&m, 0x20003000u, ACC_W, true, false).why == F_NOMATCH);

    r = mk(0, 31, 3, false, 0x01);                                        /* 4 GB 영역, 서브리전 0(512 MB) 끔 */
    CHECK(!region_hit(&r, 0x1FFFFFFFu) && region_hit(&r, 0x20000000u) && region_hit(&r, 0xFFFFFFFFu));

    m = mpu_new();
    m.enable = false;
    CHECK(mpu_check(&m, 0x20000000u, ACC_W, false, false).why == OK_MPU_OFF);
    rp = mpu_check(&m, 0x40000000u, ACC_X, true, false);                  /* MPU 꺼져도 기본 맵 XN */
    CHECK(!rp.allowed && rp.why == F_DEFAULT_XN && rp.iaccviol && !rp.mmarvalid);
    CHECK(mpu_check(&m, 0x60000000u, ACC_X, false, false).allowed);       /* External RAM 은 실행 가능 */
    CHECK(!mpu_check(&m, 0xA0000000u, ACC_X, true, false).allowed && mpu_check(&m, 0x9FFFFFFCu, ACC_X, true, false).allowed);

    m = mpu_new();
    m.r[0] = mk(0, 31, 0, true, 0);                                       /* 전 주소 접근 금지 */
    rp = mpu_check(&m, 0xE000ED94u, ACC_R, true, false);                  /* PPB 는 MPU 무관 */
    CHECK(rp.allowed && rp.why == OK_PPB);
    rp = mpu_check(&m, 0xE000ED94u, ACC_R, false, false);
    CHECK(!rp.allowed && rp.why == F_PPB_UNPRIV && !rp.memmanage && !rp.daccviol);
    rp = mpu_check(&m, 0xE0100000u, ACC_R, true, false);                  /* PPB 바로 위(Vendor)는 MPU 적용 */
    CHECK(!rp.allowed && rp.why == F_AP && rp.region == 0);
    rp = mpu_check(&m, 0x20000000u, ACC_R, true, true);                   /* HardFault 안, HFNMIENA=0 */
    CHECK(rp.allowed && rp.why == OK_MPU_OFF);
    m.hfnmiena = true;
    CHECK(!mpu_check(&m, 0x20000000u, ACC_R, true, true).allowed);

    m = mpu_new();
    m.r[0] = mk(0x20000000u, 9, 3, true, 0);                              /* [0x000, 0x400) RW/RW */
    m.r[1] = mk(0x20000400u, 9, 1, true, 0);                              /* [0x400, 0x800) 특권 전용 */
    rp = mpu_access(&m, 0x200003FEu, 4, ACC_W, false, false);             /* 2바이트는 OK, 2바이트는 금지 */
    CHECK(!rp.allowed && rp.region == 1 && rp.mmfar == 0x20000400u);
    rp = mpu_access(&m, 0x200003FEu, 2, ACC_W, false, false);
    CHECK(rp.allowed && rp.region == 0);
    rp = mpu_access(&m, 0x200003E0u, 64, ACC_R, false, false);            /* LDM 16워드 */
    CHECK(!rp.allowed && rp.mmfar == 0x20000400u);
    rp = mpu_access(&m, 0x200003C1u, 63, ACC_R, false, false);            /* 끝 = 0x2000_0400 직전 */
    CHECK(rp.allowed);

    {   /* 독립 참조 모델과 무작위 비교: 2000 구성 × 100 접근 */
        int bad = 0, nok = 0, nfault = 0;
        for (int c = 0; c < 2000; c++) {
            mpu_t q = mpu_new();
            q.enable = (xorshift32() % 16) != 0;
            q.privdefena = (xorshift32() & 1u) != 0;
            q.hfnmiena = (xorshift32() & 1u) != 0;
            for (int i = 0; i < MPU_NREG; i++) q.r[i] = rand_region();
            for (int t = 0; t < 100; t++) {
                const region_t *g = &q.r[xorshift32() % MPU_NREG];
                uint32_t a;
                switch (xorshift32() % 4) {
                case 0:  a = g->base + (uint32_t)(region_bytes(g->size_f) - 1 - (xorshift32() % 40)); break;
                case 1:  a = g->base - 40 + xorshift32() % 80; break;
                case 2:  a = 0x20000000u + (xorshift32() & 0x0003FFFFu); break;
                default: a = xorshift32(); break;
                }
                unsigned size = 1 + xorshift32() % 64;
                if ((uint64_t)a + size > ((uint64_t)1 << 32)) a -= size;
                acc_t acc = (acc_t)(xorshift32() % 3);
                bool priv = (xorshift32() & 1u) != 0, hf = (xorshift32() % 8) == 0;
                mm_report_t x = mpu_access(&q, a, size, acc, priv, hf), y = ref_access(&q, a, size, acc, priv, hf);
                if (!same_rep(x, y)) bad++;
                if (y.allowed) nok++; else nfault++;
            }
        }
        CHECK(bad == 0);
        CHECK(nok > 20000 && nfault > 20000);                             /* 양쪽 다 충분히 나왔다 */
    }

    /* ---------------- Part C: 계산기 ---------------- */
    CHECK(mpu_calc(0x20000000u, 0x6000, &r, &over) == 0 && r.base == 0x20000000u && r.size_f == 14 &&
          r.srd == 0xC0 && over == 0);                                     /* 24 KB = 32 KB 영역의 6/8 */
    CHECK(mpu_calc(0x20001000u, 0x6000, &r, &over) == 0 && r.base == 0x20000000u && r.size_f == 14 &&
          r.srd == 0x81 && over == 0);                                     /* 가운데 6조각 */
    CHECK(mpu_calc(0x20007000u, 0x2000, &r, &over) == 0 && r.base == 0x20000000u && r.size_f == 15 &&
          r.srd == 0xE7 && over == 0x2000);                                /* 0x2000_8000 경계를 넘는 8 KB → 64 KB 영역 */
    CHECK(mpu_calc(0x20000020u, 64, &r, &over) == 0 && r.size_f == 7 && r.base == 0x20000000u &&
          r.srd == 0xF9 && over == 0);                                     /* 128 B(넘침 64) 대신 256 B + 서브리전 */
    CHECK(mpu_calc(0x20000040u, 64, &r, &over) == 0 && r.size_f == 5 && r.base == 0x20000040u &&
          r.srd == 0 && over == 0);                                        /* 정렬된 64 B 는 그대로 (동률 → 작은 영역) */
    CHECK(mpu_calc(0x20004000u, 32, &r, &over) == 0 && r.size_f == 4 && r.base == 0x20004000u && over == 0);
    CHECK(mpu_calc(0x80000000u, 0x80000000u, &r, &over) == 0 && r.size_f == 30 && r.base == 0x80000000u && over == 0);
    CHECK(mpu_calc(0x7FFFFFF0u, 0x20, &r, &over) == 0 && r.size_f == 31 && r.base == 0 &&
          r.srd == 0xE7 && over == 0x3FFFFFE0u);                           /* 2 GB 경계를 넘는 32 B → 4 GB 영역 */
    CHECK(mpu_calc(0x20000000u, 0, &r, &over) == -1);
    CHECK(mpu_calc(0xFFFFFFF0u, 0x20, &r, &over) == -1);
    CHECK(mpu_calc(0xFFFFFFE0u, 0x20, &r, &over) == 0 && r.base == 0xFFFFFFE0u && over == 0);

    {   /* 무작위 버퍼: 결과 영역은 검증 통과 + 버퍼 전체를 덮고 + 넘침이 무식한 최솟값과 같다 */
        int bad = 0;
        for (int t = 0; t < 20000; t++) {
            uint32_t start = 0x20000000u + (xorshift32() & 0x000FFFFFu), len = 1 + (xorshift32() & 0x3FFFu);
            if (t % 4 == 0) start &= ~31u;
            if (t % 8 == 1) len = 1 + (xorshift32() & 0xFFu);
            if (mpu_calc(start, len, &r, &over) != 0) { bad++; continue; }
            r.ap = 3;
            if (region_validate(&r) != CFG_OK) bad++;
            for (uint64_t a = start; a < (uint64_t)start + len; a = (a | 31u) + 1)
                if (!region_hit(&r, (uint32_t)a)) { bad++; break; }
            if (!region_hit(&r, start + len - 1)) bad++;
            if ((uint64_t)over + len != brute_min_cover(start, len)) bad++;
        }
        CHECK(bad == 0);
    }

    /* ---------------- Part C: RBAR / RASR ---------------- */
    r = mk(0x20004000u, 4, 0, true, 0);                                   /* 스택 가드 */
    mpu_encode(&r, 7, &rbar, &rasr);
    CHECK(rbar == 0x20004017u && rasr == 0x10000009u);
    r = mk(0x08000000u, 18, 6, false, 0); r.texscb = 0x02;                /* Flash 512 KB, RO/RO, C=1 */
    mpu_encode(&r, 0, &rbar, &rasr);
    CHECK(rbar == 0x08000010u && rasr == 0x06020025u);
    r = mk(0x20000000u, 14, 3, true, 0xC0);
    mpu_encode(&r, 3, &rbar, &rasr);
    CHECK(rasr == 0x1300C01Du);
    {
        int bad = 0;
        for (int t = 0; t < 10000; t++) {
            region_t a; memset(&a, 0, sizeof a);
            a.en = xorshift32() & 1u; a.size_f = (uint8_t)(xorshift32() % 32); a.base = xorshift32() & RBAR_ADDR_MASK;
            a.srd = (uint8_t)xorshift32(); a.ap = (uint8_t)(xorshift32() & 7u); a.xn = xorshift32() & 1u;
            a.texscb = (uint8_t)(xorshift32() & 0x3Fu);
            unsigned n = xorshift32() % 8;
            mpu_encode(&a, n, &rbar, &rasr);
            region_t b = mpu_decode(rbar, rasr);
            if ((rbar & RBAR_REGION_MSK) != n || !(rbar & RBAR_VALID) || memcmp(&a, &b, sizeof a) != 0) bad++;
        }
        CHECK(bad == 0);
    }

    /* ---------------- Part C: RTOS 태스크 구성 ---------------- */
    {
        region_t want[MPU_NREG];
        memset(want, 0, sizeof want);
        want[0] = mk(0x08000000u, 18, 6, false, 0); want[0].texscb = 0x02;   /* Flash: RO/RO, 실행 가능 */
        want[1] = mk(0x20000000u, 16, 1, true, 0);  want[1].texscb = 0x06;   /* SRAM 128 KB: 특권 RW, XN */
        want[2] = mk(0x40000000u, 28, 1, true, 0);  want[2].texscb = 0x01;   /* 주변장치 512 MB: 특권 RW, XN, Device */
        CHECK(mpu_calc(0x20004000u, 0x1800, &want[3], &over) == 0 && over == 0); /* 태스크 A 스택+데이터 6 KB */
        want[3].ap = 3; want[3].xn = true; want[3].texscb = 0x06;
        want[4] = mk(0x20008000u, 9, 2, true, 0);                            /* 공유 표: 특권 RW, 비특권 RO */
        want[5] = mk(0x40004400u, 9, 3, true, 0);   want[5].texscb = 0x01;   /* 태스크 A 의 UART 창 */
        CHECK(mpu_calc(0x20004000u, 32, &want[7], &over) == 0);              /* 스택 가드 32 B (스택 바닥) */
        want[7].ap = 0; want[7].xn = true;

        m = mpu_new();
        int bad = 0;
        for (unsigned i = 0; i < MPU_NREG; i++) {                           /* "레지스터에 쓰고" 다시 읽은 값으로 */
            mpu_encode(&want[i], i, &rbar, &rasr);
            m.r[rbar & RBAR_REGION_MSK] = mpu_decode(rbar, rasr);
            if (region_validate(&m.r[i]) != CFG_OK) bad++;
        }
        CHECK(bad == 0 && m.r[3].size_f == 12 && m.r[3].srd == 0xC0);

        CHECK(mpu_check(&m, 0x08001234u, ACC_X, false, false).region == 0);
        rp = mpu_check(&m, 0x08001234u, ACC_W, false, false);
        CHECK(!rp.allowed && rp.why == F_AP && rp.region == 0 && rp.mmfar == 0x08001234u);
        CHECK(mpu_access(&m, 0x20004100u, 4, ACC_W, false, false).region == 3);
        rp = mpu_access(&m, 0x20004010u, 4, ACC_W, false, false);           /* 스택 넘침 → 가드 */
        CHECK(!rp.allowed && rp.region == 7 && rp.why == F_AP && rp.daccviol && rp.mmfar == 0x20004010u);
        rp = mpu_access(&m, 0x20004010u, 4, ACC_W, true, false);            /* 특권(ISR)도 막는다 (AP=000) */
        CHECK(!rp.allowed && rp.region == 7);
        rp = mpu_access(&m, 0x2000401Eu, 4, ACC_W, false, false);           /* 가드 위끝을 걸친 push */
        CHECK(!rp.allowed && rp.region == 7 && rp.mmfar == 0x2000401Eu);
        rp = mpu_access(&m, 0x200057FCu, 8, ACC_W, false, false);           /* STRD: 뒤 4바이트가 꺼진 서브리전 */
        CHECK(!rp.allowed && rp.region == 1 && rp.mmfar == 0x20005800u);
        rp = mpu_access(&m, 0x20003FF0u, 4, ACC_W, true, false);            /* 큰 프레임이 가드를 건너뜀 */
        CHECK(rp.allowed && rp.region == 1);                                /* → 특권 코드는 조용히 오염 */
        rp = mpu_check(&m, 0x20004100u, ACC_X, false, false);
        CHECK(!rp.allowed && rp.why == F_XN && rp.region == 3 && rp.iaccviol && !rp.mmarvalid);
        CHECK(mpu_check(&m, 0x20008010u, ACC_R, false, false).region == 4 &&
              mpu_check(&m, 0x20008010u, ACC_W, false, false).why == F_AP &&
              mpu_check(&m, 0x20008010u, ACC_W, true, false).allowed);
        CHECK(mpu_access(&m, 0x40004404u, 4, ACC_W, false, false).region == 5);
        rp = mpu_access(&m, 0x40004800u, 4, ACC_W, false, false);           /* 창 밖 주변장치 */
        CHECK(!rp.allowed && rp.region == 2 && rp.why == F_AP);
        CHECK(mpu_access(&m, 0x40004800u, 4, ACC_W, true, false).region == 2);
        CHECK(mpu_check(&m, 0x60000000u, ACC_R, true, false).why == OK_DEFAULT &&
              mpu_check(&m, 0x60000000u, ACC_R, false, false).why == F_NOMATCH);
        CHECK(mpu_check(&m, 0xA0000000u, ACC_X, true, false).why == F_DEFAULT_XN);
        CHECK(mpu_check(&m, 0x20004010u, ACC_R, true, true).why == OK_MPU_OFF);   /* HardFault 핸들러 덤프 */
        m.hfnmiena = true;
        CHECK(mpu_check(&m, 0x20004010u, ACC_R, true, true).why == F_AP);
        CHECK(mpu_check(&m, 0xE000ED28u, ACC_R, false, false).why == F_PPB_UNPRIV &&
              mpu_check(&m, 0xE000ED28u, ACC_R, true, false).why == OK_PPB);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
