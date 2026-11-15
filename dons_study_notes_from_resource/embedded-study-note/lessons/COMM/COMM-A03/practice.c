/* COMM-A03 추가 연습 — solution.c 의 모델·드라이버(0~7절)를 그대로 포함하고 연습 1~5 를 검증한다
 *   연습 1: pause 해석 (802.3 Table 28B-3) 전수 + 대칭성
 *   연습 2: Clause 45 프레임 (ST=00) — C22 PHY 는 무시해야 한다 / C22 레지스터 13·14 로 MMD 간접 접근 순서
 *   연습 3: 강제 모드(AN 끔)와 duplex mismatch 판별
 *   연습 4: MDIO 시간 예산 (프레임 시간, preamble suppression, 비트뱅 CPU 점유)
 *   연습 5: TA 에서 선을 안 놓는 순진한 읽기 → 버스 싸움, TA1(Z) 검사로 잡기
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GIVEN __attribute__((unused))   /* 주어진 도우미 — starter 의 스텁 상태에서는 아직 안 불릴 수 있음 */

/* ================= 0. Clause 22 레지스터와 비트 (이름은 Linux include/uapi/linux/mii.h 와 같게) ================= */
enum { MII_BMCR = 0, MII_BMSR = 1, MII_PHYSID1 = 2, MII_PHYSID2 = 3,
       MII_ADVERTISE = 4, MII_LPA = 5, MII_EXPANSION = 6, MII_VENDOR = 0x10 };
#define BMCR_RESET           0x8000u  /* 쓰면 리셋, 끝나면 PHY 가 0 으로 (self-clearing) */
#define BMCR_SPEED100        0x2000u
#define BMCR_ANENABLE        0x1000u
#define BMCR_ANRESTART       0x0200u  /* self-clearing */
#define BMCR_FULLDPLX        0x0100u
#define BMSR_100BASE4        0x8000u
#define BMSR_100FULL         0x4000u
#define BMSR_100HALF         0x2000u
#define BMSR_10FULL          0x1000u
#define BMSR_10HALF          0x0800u
#define BMSR_MFPS            0x0040u  /* preamble suppression 허용 (Linux 헤더에선 RESV 로 묶임) */
#define BMSR_ANEGCOMPLETE    0x0020u
#define BMSR_ANEGCAPABLE     0x0008u
#define BMSR_LSTATUS         0x0004u  /* link status — LATCHING LOW */
#define BMSR_ERCAP           0x0001u
#define ADVERTISE_CSMA       0x0001u  /* selector 00001 = IEEE 802.3 */
#define ADVERTISE_SLCT       0x001Fu
#define ADVERTISE_10HALF     0x0020u
#define ADVERTISE_10FULL     0x0040u
#define ADVERTISE_100HALF    0x0080u
#define ADVERTISE_100FULL    0x0100u
#define ADVERTISE_100BASE4   0x0200u
#define ADVERTISE_PAUSE_CAP  0x0400u
#define ADVERTISE_PAUSE_ASYM 0x0800u
#define ADVERTISE_LPACK      0x4000u
#define ADV_TECH  (ADVERTISE_10HALF | ADVERTISE_10FULL | ADVERTISE_100HALF | ADVERTISE_100FULL | ADVERTISE_100BASE4)
#define ADV_ALL   (ADV_TECH | ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM)
#define EXPANSION_NWAY       0x0001u  /* 링크 파트너가 오토네고 가능 (LP AN able) */

/* BMSR 능력 비트 15..11 (T4,100F,100H,10F,10H) 와 ANAR 비트 9..5 는 같은 순서 → 6비트 시프트 */
static uint16_t caps_to_adv(uint16_t bmsr) { return (uint16_t)((bmsr >> 6) & ADV_TECH); }

/* MDIO 계층 반환값: 0..0xFFFF = 읽은 값, 음수 = 에러 */
enum { MDIO_OK = 0, MDIO_ENOACK = -1, MDIO_EBUS = -2, MDIO_EINVAL = -3 };
/* PHY 드라이버 계층 반환값 */
enum { PHY_OK = 0, PHY_ENODEV = -20, PHY_EBUS = -21, PHY_ERESET_TIMEOUT = -22, PHY_EAN_TIMEOUT = -23,
       PHY_EAN_NOCOMMON = -24, PHY_EVERIFY = -25, PHY_EINVAL = -26, PHY_ENOBUF = -27 };

static GIVEN const char *phy_err_name(int rc) {
    switch (rc) {
    case PHY_OK: return "PHY_OK";               case PHY_ENODEV: return "PHY_ENODEV";
    case PHY_EBUS: return "PHY_EBUS";           case PHY_ERESET_TIMEOUT: return "PHY_ERESET_TIMEOUT";
    case PHY_EAN_TIMEOUT: return "PHY_EAN_TIMEOUT"; case PHY_EAN_NOCOMMON: return "PHY_EAN_NOCOMMON";
    case PHY_EVERIFY: return "PHY_EVERIFY";     case PHY_EINVAL: return "PHY_EINVAL";
    case PHY_ENOBUF: return "PHY_ENOBUF";       default: return "?";
    }
}

/* ================= 1. 시뮬레이션 시간 + 로그 (malloc 없음) ================= */
static uint64_t g_now_ns;
typedef struct { char s[8192]; size_t len; bool overflow; } Log;
static void log_clear(Log *l) { if (l) { l->len = 0; l->s[0] = '\0'; l->overflow = false; } }
__attribute__((format(printf, 2, 3)))
static void log_printf(Log *l, const char *fmt, ...) {
    if (!l) return;
    size_t room = sizeof l->s - l->len;
    va_list ap; va_start(ap, fmt);
    int k = vsnprintf(l->s + l->len, room, fmt, ap);
    va_end(ap);
    if (k < 0) return;
    if ((size_t)k >= room) { l->len = sizeof l->s - 1; l->overflow = true; } else l->len += (size_t)k;
}

/* ================= 2. mock PHY (주어짐) ================= */
typedef enum { LP_NONE, LP_AUTONEG, LP_FORCED } LpKind;   /* 케이블 없음 / 오토네고 파트너 / 강제 속도 파트너 */
typedef struct { uint64_t start_ns, end_ns; } Flap;        /* [start, end) 동안 링크 끊김. 정렬 + 겹치지 않게 */
#define MAX_FLAPS 1024
enum { F_IDLE, F_ST2, F_OP, F_PHYAD, F_REGAD, F_RD, F_WTA, F_WDATA, F_SKIP };

typedef struct MockPhy {
    uint8_t  addr; uint16_t id1, id2, caps;                 /* caps = BMSR 고정 비트 */
    uint16_t bmcr, anar, anlpar, aner, an_adv, vendor[16];
    bool     in_reset, an_running, an_complete, established, link_latched, forced_running;
    uint64_t reset_done_ns, an_done_ns, force_done_ns, last_upd_ns;
    uint32_t reset_us, an_ms, force_ms;                     /* 리셋 시간, AN 시간, 강제 모드 링크 시간 (mock 값) */
    LpKind   lp; uint16_t lp_adv; int lp_speed; bool lp_full;
    bool     reset_stuck, dead_in_reset, ta_broken, anar_ro; /* 장애 주입 (anar_ro: ANAR 쓰기가 안 먹음) */
    Flap     flaps[MAX_FLAPS]; int nflaps, flap_i;
    int      fs, cnt, ones, op, fr_addr, regad, ta; uint32_t sh; uint16_t out;   /* MDIO 프레임 FSM */
    bool     drv_en; int drv_v;                             /* PHY 의 MDIO 출력 (3상태) */
    unsigned frames_rd, frames_wr, frames_ignored, resets, an_starts;
} MockPhy;

static GIVEN bool phy_in_flap(const MockPhy *p, uint64_t t) {
    for (int i = p->flap_i; i < p->nflaps && p->flaps[i].start_ns <= t; i++)
        if (t < p->flaps[i].end_ns) return true;
    return false;
}
static GIVEN bool phy_link_now(const MockPhy *p) { return p->established && !p->in_reset && !phy_in_flap(p, g_now_ns); }

static void phy_an_start(MockPhy *p, uint64_t t) {
    p->an_running = true; p->an_adv = p->anar; p->an_done_ns = t + (uint64_t)p->an_ms * 1000000u;
    p->an_complete = p->established = p->link_latched = p->forced_running = false;
    p->anlpar = p->aner = 0; p->an_starts++;
}
static void phy_defaults(MockPhy *p) {          /* 리셋 / 전원 인가 직후 값 (mock 정의) */
    p->bmcr = (p->caps & BMSR_ANEGCAPABLE) ? BMCR_ANENABLE : 0;
    p->anar = (uint16_t)(ADVERTISE_CSMA | caps_to_adv(p->caps));
    p->anlpar = p->aner = 0; memset(p->vendor, 0, sizeof p->vendor);
    p->an_running = p->an_complete = p->established = p->link_latched = p->forced_running = false;
}
static void phy_an_finish(MockPhy *p) {
    p->an_running = false;
    if (p->lp == LP_AUTONEG) {                  /* base page 교환 완료 → ANLPAR 채움, 공통 모드가 있으면 링크 */
        p->anlpar = (uint16_t)(p->lp_adv | ADVERTISE_CSMA | ADVERTISE_LPACK);
        p->aner = EXPANSION_NWAY;
        if (p->an_adv & p->lp_adv & ADV_TECH) p->an_complete = p->established = true;
        /* 공통 모드 없음: 페이지는 받았지만 AN 완료 안 됨 (HCD 없음 — 실제 PHY 는 AN 을 계속 다시 돈다) */
    } else if (p->lp == LP_FORCED) {            /* parallel detection: 상대 신호로 속도만 알아냄 → 반이중 */
        uint16_t hd = p->lp_speed == 100 ? ADVERTISE_100HALF : ADVERTISE_10HALF;
        if (p->an_adv & hd) {                   /* mock 정의: 그 속도의 반이중을 광고하고 있을 때만 */
            p->anlpar = hd;
            p->aner = 0; p->an_complete = p->established = true;
        }
    }                                           /* LP_NONE: 케이블 없음 → 영원히 미완료 */
}
static void phy_forced_finish(MockPhy *p) {
    p->forced_running = false;
    int sp = (p->bmcr & BMCR_SPEED100) ? 100 : 10;
    uint16_t lp_sp = sp == 100 ? (ADVERTISE_100HALF | ADVERTISE_100FULL) : (ADVERTISE_10HALF | ADVERTISE_10FULL);
    if ((p->lp == LP_FORCED && p->lp_speed == sp) || (p->lp == LP_AUTONEG && (p->lp_adv & lp_sp))) p->established = true;
}
static void phy_update(MockPhy *p) {            /* 시간 경과 반영: MDC 에지마다, sleep 마다 불린다 */
    uint64_t now = g_now_ns;
    if (p->in_reset && !p->reset_stuck && now >= p->reset_done_ns) {
        p->in_reset = false; phy_defaults(p);
        if (p->bmcr & BMCR_ANENABLE) phy_an_start(p, p->reset_done_ns);   /* 리셋 뒤 AN 자동 시작 */
    }
    if (p->an_running && now >= p->an_done_ns) phy_an_finish(p);
    if (p->forced_running && now >= p->force_done_ns) phy_forced_finish(p);
    for (int i = p->flap_i; i < p->nflaps && p->flaps[i].start_ns <= now; i++)
        if (p->flaps[i].end_ns > p->last_upd_ns) p->link_latched = false;     /* 지난 갱신 뒤 끊김이 있었다 */
    while (p->flap_i < p->nflaps && p->flaps[p->flap_i].end_ns <= now) p->flap_i++;
    if (p->in_reset) p->link_latched = false;
    p->last_upd_ns = now;
}
static uint16_t phy_reg_read(MockPhy *p, int reg) {
    switch (reg) {
    case MII_BMCR: return (uint16_t)(p->bmcr | (p->in_reset ? BMCR_RESET : 0));
    case MII_BMSR: {
        uint16_t v = p->caps;
        if (p->an_complete && !p->in_reset && !phy_in_flap(p, g_now_ns)) v |= BMSR_ANEGCOMPLETE;
        if (p->link_latched) v |= BMSR_LSTATUS;
        p->link_latched = phy_link_now(p);      /* latch-low: 읽고 나면 현재 상태로 다시 장전 */
        return v;
    }
    case MII_PHYSID1: return p->id1;
    case MII_PHYSID2: return p->id2;
    case MII_ADVERTISE: return p->anar;
    case MII_LPA: return p->anlpar;
    case MII_EXPANSION: return p->aner;
    default: return reg >= MII_VENDOR ? p->vendor[reg - MII_VENDOR] : 0;
    }
}
static void phy_reg_write(MockPhy *p, int reg, uint16_t v) {
    if (p->in_reset) return;                    /* mock 정의: 리셋 중 쓰기는 무시 */
    if (reg == MII_BMCR) {
        if (v & BMCR_RESET) {
            p->in_reset = true; p->reset_done_ns = g_now_ns + (uint64_t)p->reset_us * 1000u; p->resets++;
            p->established = p->link_latched = p->an_running = p->forced_running = p->an_complete = false;
            return;
        }
        uint16_t old = p->bmcr;
        p->bmcr = (uint16_t)(v & (BMCR_ANENABLE | BMCR_SPEED100 | BMCR_FULLDPLX));
        if (p->bmcr & BMCR_ANENABLE) {
            if ((v & BMCR_ANRESTART) || !(old & BMCR_ANENABLE)) phy_an_start(p, g_now_ns);
        } else if ((old & BMCR_ANENABLE) || ((old ^ p->bmcr) & (BMCR_SPEED100 | BMCR_FULLDPLX))) {
            p->established = p->link_latched = p->an_running = p->an_complete = false;
            p->forced_running = true; p->force_done_ns = g_now_ns + (uint64_t)p->force_ms * 1000000u;
        }
    } else if (reg == MII_ADVERTISE) {          /* 지원 안 하는 기술 비트는 0 으로 남음 (mock 정의) */
        if (!p->anar_ro) p->anar = (uint16_t)(v & (ADVERTISE_SLCT | caps_to_adv(p->caps) | ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM));
    } else if (reg >= MII_VENDOR) {
        p->vendor[reg - MII_VENDOR] = v;
    }                                           /* BMSR, PHYID, ANLPAR, ANER: 읽기 전용 → 무시 */
}
static void phy_mdc_rise(MockPhy *p, int v) {   /* MDC 상승 에지: v = 그 순간 MDIO 선의 값 */
    switch (p->fs) {
    case F_IDLE:
        if (v) { if (p->ones < 1000) p->ones++; }
        else {
            int need = (p->caps & BMSR_MFPS) ? 1 : 32;              /* 프리앰블 32개 (억제 지원이면 1, mock 정의) */
            if (p->ones >= need) p->fs = F_ST2;
            p->ones = 0;
        }
        break;
    case F_ST2:
        if (v) { p->fs = F_OP; p->cnt = 0; p->sh = 0; }
        else { p->fs = F_IDLE; p->frames_ignored++; }               /* ST = 00 → Clause 45 프레임: 무시 */
        break;
    case F_OP:
        p->sh = p->sh << 1 | (uint32_t)v;
        if (++p->cnt == 2) {
            p->op = (int)p->sh;
            if (p->op == 2 || p->op == 1) { p->fs = F_PHYAD; p->cnt = 0; p->sh = 0; }
            else { p->fs = F_IDLE; p->frames_ignored++; }
        }
        break;
    case F_PHYAD:
        p->sh = p->sh << 1 | (uint32_t)v;
        if (++p->cnt == 5) { p->fr_addr = (int)p->sh; p->fs = F_REGAD; p->cnt = 0; p->sh = 0; }
        break;
    case F_REGAD:
        p->sh = p->sh << 1 | (uint32_t)v;
        if (++p->cnt == 5) {
            p->regad = (int)p->sh; p->cnt = 0; p->sh = 0;
            bool mine = p->fr_addr == p->addr && !(p->in_reset && p->dead_in_reset);
            if (!mine) p->fs = F_SKIP;
            else if (p->op == 2) { p->out = phy_reg_read(p, p->regad); p->fs = F_RD; }
            else { p->fs = F_WTA; p->ta = 0; }
        }
        break;
    case F_RD:                                                       /* 첫 TA 비트 동안은 Z, 그 다음 에지부터 구동 */
        p->cnt++;
        if (p->cnt == 1) { p->drv_en = !p->ta_broken; p->drv_v = 0; } /* TA 두 번째 비트 = 0 */
        else if (p->cnt <= 17) { p->drv_en = true; p->drv_v = (p->out >> (17 - p->cnt)) & 1; }   /* D15..D0 */
        else { p->drv_en = false; p->fs = F_IDLE; p->ones = 0; p->frames_rd++; }
        break;
    case F_WTA:
        p->ta = p->ta << 1 | v;
        if (++p->cnt == 2) { p->fs = F_WDATA; p->cnt = 0; p->sh = 0; }
        break;
    case F_WDATA:
        p->sh = p->sh << 1 | (uint32_t)v;
        if (++p->cnt == 16) {
            if (p->ta == 2) { phy_reg_write(p, p->regad, (uint16_t)p->sh); p->frames_wr++; }
            else p->frames_ignored++;                               /* TA != 10 인 쓰기는 무시 (mock 정의) */
            p->fs = F_IDLE; p->ones = 0;
        }
        break;
    default:                                                         /* F_SKIP: 남의 프레임 — TA+DATA 18비트를 흘려보냄 */
        if (++p->cnt == 18) { p->fs = F_IDLE; p->ones = 0; }
        break;
    }
}

/* ================= 3. MDIO 버스 + GPIO HAL (주어짐) ================= */
#define MAX_PHYS 4
typedef struct {
    int mdc, sta_en, sta_v;
    MockPhy *phy[MAX_PHYS]; int nphy;
    bool stuck_low;                 /* 장애: MDIO 가 GND 에 붙음 */
    uint32_t half_ns;               /* MDC 반주기 (200 ns = 2.5 MHz) */
    unsigned preamble_bits;         /* 드라이버가 보낼 프리앰블 수 (표준 32) */
    uint32_t ignore_ta_mask;        /* Linux mii_bus.phy_ignore_ta_mask 와 같은 역할 */
    unsigned long edges, contention;
    bool trace; char tr_bits[80], tr_who[80]; unsigned tr_n;
} MdioBus;

static int bus_line(const MdioBus *b) {         /* 풀업 + 3상태 드라이버들. 충돌은 contention 으로 따로 센다 */
    if (b->stuck_low) return 0;
    int v = 1;
    if (b->sta_en) v &= b->sta_v;
    for (int i = 0; i < b->nphy; i++) if (b->phy[i]->drv_en) v &= b->phy[i]->drv_v;
    return v;
}
static void bus_update_phys(MdioBus *b) { for (int i = 0; i < b->nphy; i++) phy_update(b->phy[i]); }
static void bus_rise(MdioBus *b) {
    int v = bus_line(b);                        /* 모든 PHY 가 같은 값을 샘플한 뒤 출력을 바꾼다 */
    if (b->trace && b->tr_n + 1 < sizeof b->tr_bits) {
        bool phy_drv = false;
        for (int i = 0; i < b->nphy; i++) phy_drv |= b->phy[i]->drv_en;
        b->tr_bits[b->tr_n] = (char)('0' + v);
        b->tr_who[b->tr_n] = b->sta_en ? (phy_drv ? 'X' : 'S') : (phy_drv ? 'P' : 'z');
        b->tr_n++; b->tr_bits[b->tr_n] = b->tr_who[b->tr_n] = '\0';
    }
    b->edges++;
    for (int i = 0; i < b->nphy; i++) { phy_update(b->phy[i]); phy_mdc_rise(b->phy[i], v); }
}
static void gpio_mdc(MdioBus *b, int v) { if (!b->mdc && v) { b->mdc = 1; bus_rise(b); } else b->mdc = v ? 1 : 0; }
static GIVEN void gpio_mdio_dir(MdioBus *b, int out) { b->sta_en = out ? 1 : 0; }
static void gpio_mdio_set(MdioBus *b, int v) { b->sta_v = v ? 1 : 0; }
static int  gpio_mdio_get(const MdioBus *b) { return bus_line(b); }
static void gpio_delay(MdioBus *b) {
    g_now_ns += b->half_ns;
    if (b->sta_en) for (int i = 0; i < b->nphy; i++) if (b->phy[i]->drv_en) b->contention++;   /* 버스 싸움 */
}
static GIVEN void sim_sleep_us(MdioBus *b, uint32_t us) { g_now_ns += (uint64_t)us * 1000u; bus_update_phys(b); }
static GIVEN void sim_sleep_until(MdioBus *b, uint64_t t) { if (t > g_now_ns) g_now_ns = t; bus_update_phys(b); }

/* 비트 단위 원시 함수 (mdiobb_send_bit / get_bit 과 같은 역할, 샘플 시점만 이 모델의 규칙) */
static GIVEN void mdio_bit_out(MdioBus *b, int v) {
    gpio_mdio_set(b, v); gpio_delay(b); gpio_mdc(b, 1); gpio_delay(b); gpio_mdc(b, 0);
}
static GIVEN int mdio_bit_in(MdioBus *b) {      /* low 구간 끝(= 상승 에지 직전)에 읽고 클럭을 올린다 */
    gpio_delay(b); int v = gpio_mdio_get(b); gpio_mdc(b, 1); gpio_delay(b); gpio_mdc(b, 0); return v;
}
static GIVEN void mdio_bits_out(MdioBus *b, uint32_t val, int n) {   /* MSB first */
    for (int i = n - 1; i >= 0; i--) mdio_bit_out(b, (int)(val >> i) & 1);
}

/* ================= 4. Part A — MDIO 프레임 (TODO) ================= */
/* TODO(A1) mdio_read: Clause 22 읽기 프레임. 반환 0..0xFFFF, 또는 MDIO_ENOACK(TA 를 아무도 0 으로 안 끌어내림)
 * / MDIO_EBUS(TA 첫 비트 = Z 구간인데 0 → 선이 low 에 붙음) / MDIO_EINVAL. 실패해도 16 데이터 클럭은 끝까지 보낸다. */
static GIVEN int mdio_read(MdioBus *b, int phy, int reg) {
    if (!b || phy < 0 || phy > 31 || reg < 0 || reg > 31) return MDIO_EINVAL;
    gpio_mdio_dir(b, 1);
    for (unsigned i = 0; i < b->preamble_bits; i++) mdio_bit_out(b, 1);   /* PRE: 32 x 1 */
    mdio_bits_out(b, 0x1, 2);                       /* ST = 01 */
    mdio_bits_out(b, 0x2, 2);                       /* OP = 10 (read) */
    mdio_bits_out(b, (uint32_t)phy, 5);             /* PHYAD */
    mdio_bits_out(b, (uint32_t)reg, 5);             /* REGAD */
    gpio_mdio_dir(b, 0);                            /* TA 첫 비트 전에 선을 놓는다 (안 놓으면 버스 싸움) */
    int z  = mdio_bit_in(b);                        /* TA1: 아무도 안 구동 → 풀업 1 이어야 함 */
    int ta = mdio_bit_in(b);                        /* TA2: PHY 가 0 을 구동 */
    uint16_t v = 0;
    for (int i = 0; i < 16; i++) v = (uint16_t)(v << 1 | mdio_bit_in(b));   /* 실패해도 끝까지: PHY FSM 동기 */
    if (z == 0) return MDIO_EBUS;
    if (ta != 0 && !((b->ignore_ta_mask >> phy) & 1u)) return MDIO_ENOACK;
    return v;
}

/* TODO(A2) mdio_write: PRE, ST 01, OP 01, PHYAD, REGAD, TA = 10 (STA 가 구동), DATA 16. 끝나면 선을 놓는다. */
static GIVEN int mdio_write(MdioBus *b, int phy, int reg, uint16_t val) {
    if (!b || phy < 0 || phy > 31 || reg < 0 || reg > 31) return MDIO_EINVAL;
    gpio_mdio_dir(b, 1);
    for (unsigned i = 0; i < b->preamble_bits; i++) mdio_bit_out(b, 1);
    mdio_bits_out(b, 0x1, 2);                       /* ST = 01 */
    mdio_bits_out(b, 0x1, 2);                       /* OP = 01 (write) */
    mdio_bits_out(b, (uint32_t)phy, 5);
    mdio_bits_out(b, (uint32_t)reg, 5);
    mdio_bits_out(b, 0x2, 2);                       /* TA = 10 */
    mdio_bits_out(b, val, 16);
    gpio_mdio_dir(b, 0);
    return MDIO_OK;
}

typedef struct { uint32_t phy_id, oui; uint8_t model, rev; } PhyIdInfo;
/* TODO(A3) phyid_decode: PHYID1 = OUI 비트 3..18, PHYID2[15:10] = OUI 비트 19..24, [9:4] model, [3:0] rev.
 * OUI 비트 번호는 "전송 순서" (각 옥텟의 LSB 가 먼저). oui 는 사람이 쓰는 꼴 00-10-A1 → 0x0010A1 로 돌려준다. */
static GIVEN void phyid_decode(uint16_t id1, uint16_t id2, PhyIdInfo *o) {
    if (!o) return;
    o->phy_id = (uint32_t)id1 << 16 | id2;
    uint32_t s = (uint32_t)id1 << 6 | (uint32_t)(id2 >> 10);   /* OUI 비트 3..24 (비트 3 이 bit21), 비트 1·2 = 0 */
    uint32_t oui = 0;
    for (int j = 0; j < 3; j++) {                   /* 24비트 열을 옥텟 단위로 잘라 비트 순서를 뒤집는다 */
        uint8_t c = (uint8_t)(s >> (16 - 8 * j)), r = 0;
        for (int i = 0; i < 8; i++) if ((c >> i) & 1) r |= (uint8_t)(0x80u >> i);
        oui = oui << 8 | r;
    }
    o->oui = oui;
    o->model = (uint8_t)((id2 >> 4) & 0x3F);
    o->rev = (uint8_t)(id2 & 0xF);
}

/* ================= 5. PHY 드라이버 공통 (주어짐) ================= */
typedef struct { uint32_t reset_timeout_us, reset_poll_us, an_timeout_us, link_poll_us; } PhyCfg;
static const PhyCfg PHY_CFG_DEFAULT = { 500000, 1000, 3000000, 10000 };   /* 0.5 s / 1 ms / 3 s / 10 ms (이 레슨의 정의) */
typedef struct {
    MdioBus *bus; int addr; PhyCfg cfg; Log *log;
    bool was_up; unsigned flaps, reset_polls, link_polls;
} PhyDev;
typedef struct { bool latched_up, up, an_done, dropped; } LinkSample;
typedef struct { int addr; PhyIdInfo id; } ProbeHit;
typedef struct {
    int addr; PhyIdInfo id; uint16_t adv, lpa, exp, hcd;
    int speed; bool full, link, parallel; const char *pause;
    uint64_t t_reset_us, t_link_us; unsigned polls;
} PhyResult;

static GIVEN int phy_map_mdio(int rc) {
    return rc == MDIO_ENOACK ? PHY_ENODEV : rc == MDIO_EBUS ? PHY_EBUS : rc == MDIO_EINVAL ? PHY_EINVAL : PHY_OK;
}
static GIVEN bool phyid_absent(uint32_t id) { return (id & 0x1FFFFFFFu) == 0x1FFFFFFFu || id == 0; }   /* Linux: 거의 다 F 면 없음 */
static GIVEN const char *adv_name(uint16_t bit) {
    switch (bit) {
    case ADVERTISE_100FULL: return "100FD"; case ADVERTISE_100BASE4: return "100T4"; case ADVERTISE_100HALF: return "100HD";
    case ADVERTISE_10FULL: return "10FD";   case ADVERTISE_10HALF: return "10HD";     default: return "none";
    }
}
__attribute__((format(printf, 2, 3)))
static GIVEN void phy_log(PhyDev *d, const char *fmt, ...) {
    if (!d || !d->log) return;
    char line[256];
    va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    log_printf(d->log, "[%9.3f ms] phy%d %s\n", (double)g_now_ns / 1e6, d->addr, line);
}

/* ================= 6. Part B — 리셋, 링크(latch-low), 해석, 탐색 (TODO) ================= */
/* TODO(B1) phy_soft_reset: BMCR = RESET 쓰기 → reset_poll_us 마다 BMCR 읽기, bit15 == 0 이면 OK.
 * 리셋 중 응답 없음(ENOACK)은 "아직"으로 보고 계속 폴링. MDIO_EBUS → PHY_EBUS 즉시.
 * reset_timeout_us 가 지나면: 한 번이라도 응답했으면 PHY_ERESET_TIMEOUT, 끝까지 무응답이면 PHY_ENODEV. */
static GIVEN int phy_soft_reset(PhyDev *d) {
    if (!d || !d->bus || d->cfg.reset_poll_us == 0) return PHY_EINVAL;
    int rc = mdio_write(d->bus, d->addr, MII_BMCR, BMCR_RESET);
    if (rc < 0) return phy_map_mdio(rc);
    uint64_t t0 = g_now_ns;
    bool seen = false;
    for (;;) {
        sim_sleep_us(d->bus, d->cfg.reset_poll_us);
        d->reset_polls++;
        int v = mdio_read(d->bus, d->addr, MII_BMCR);
        if (v == MDIO_EBUS) return PHY_EBUS;
        if (v >= 0) { seen = true; if (!(v & BMCR_RESET)) return PHY_OK; }
        if (g_now_ns - t0 >= (uint64_t)d->cfg.reset_timeout_us * 1000u)
            return seen ? PHY_ERESET_TIMEOUT : PHY_ENODEV;
    }
}

/* TODO(B2) phy_read_link: BMSR 를 두 번 읽는다. 첫 값 = 래치(지난 읽기 뒤 끊김이 있었나), 둘째 값 = 지금 상태.
 * s->dropped = (지난번에 up 이었는데 래치가 0) → d->flaps++. d->was_up = s->up. 읽기 실패는 PHY 코드로. */
static GIVEN int phy_read_link(PhyDev *d, LinkSample *s) {
    if (!d || !d->bus || !s) return PHY_EINVAL;
    memset(s, 0, sizeof *s);
    int a = mdio_read(d->bus, d->addr, MII_BMSR);
    if (a < 0) return phy_map_mdio(a);
    int b = mdio_read(d->bus, d->addr, MII_BMSR);
    if (b < 0) return phy_map_mdio(b);
    d->link_polls++;
    s->latched_up = (a & BMSR_LSTATUS) != 0;
    s->up = (b & BMSR_LSTATUS) != 0;
    s->an_done = (b & BMSR_ANEGCOMPLETE) != 0;
    s->dropped = d->was_up && !s->latched_up;
    if (s->dropped) d->flaps++;
    d->was_up = s->up;
    return PHY_OK;
}

/* TODO(B3) an_resolve: (adv & lpa) 의 기술 비트 중 우선순위가 가장 높은 하나 (100FD > 100T4 > 100HD > 10FD > 10HD),
 * 공통이 없으면 0. */
static GIVEN uint16_t an_resolve(uint16_t adv, uint16_t lpa) {
    static const uint16_t order[] = { ADVERTISE_100FULL, ADVERTISE_100BASE4, ADVERTISE_100HALF,
                                      ADVERTISE_10FULL, ADVERTISE_10HALF };
    uint16_t c = (uint16_t)(adv & lpa & ADV_TECH);
    for (size_t i = 0; i < sizeof order / sizeof order[0]; i++) if (c & order[i]) return order[i];
    return 0;
}

/* TODO(B4) phy_probe: 주소 0..31 의 PHYID1/2 를 읽어 PHY 목록을 만든다. MDIO_EBUS → PHY_EBUS 즉시.
 * 읽기 실패나 phyid_absent(id) 는 "없음". cap 을 넘으면 PHY_ENOBUF. 하나도 없으면 PHY_ENODEV. */
static GIVEN int phy_probe(MdioBus *b, ProbeHit *hits, int cap, int *n) {
    if (!n) return PHY_EINVAL;
    *n = 0;
    if (!b || !hits || cap < 0) return PHY_EINVAL;
    for (int addr = 0; addr < 32; addr++) {
        int a = mdio_read(b, addr, MII_PHYSID1);
        if (a == MDIO_EBUS) return PHY_EBUS;
        if (a < 0) continue;
        int c = mdio_read(b, addr, MII_PHYSID2);
        if (c == MDIO_EBUS) return PHY_EBUS;
        if (c < 0) continue;
        if (phyid_absent((uint32_t)a << 16 | (uint32_t)c)) continue;
        if (*n >= cap) return PHY_ENOBUF;
        hits[*n].addr = addr;
        phyid_decode((uint16_t)a, (uint16_t)c, &hits[*n].id);
        (*n)++;
    }
    return *n ? PHY_OK : PHY_ENODEV;
}

/* ================= 7. Part C — bring-up 과 상태 줄 (TODO) ================= */
/* TODO(C1) phy_bringup: ① PHYID 읽기(없으면 ENODEV) ② phy_soft_reset ③ BMSR 능력 → adv = CSMA | (능력 & mask) | pause
 * ④ ANAR 쓰기 + 되읽기(다르면 EVERIFY) ⑤ BMCR = ANENABLE|ANRESTART ⑥ phy_read_link 폴링(up && an_done),
 * an_timeout_us 지나면 ANLPAR 를 보고 NOCOMMON / AN_TIMEOUT ⑦ ANLPAR·ANER 읽고 해석 (속도, 이중, PD, pause).
 * 단계마다 phy_log. adv 에 기술 비트가 하나도 없으면 PHY_EINVAL. */
static GIVEN int phy_bringup(PhyDev *d, uint16_t adv_mask, PhyResult *r) {
    if (!d || !d->bus || !r || d->cfg.link_poll_us == 0) return PHY_EINVAL;
    memset(r, 0, sizeof *r);
    r->addr = d->addr; r->pause = "off";
    uint64_t t0 = g_now_ns;
    int a = mdio_read(d->bus, d->addr, MII_PHYSID1);
    if (a < 0) { phy_log(d, "probe: no answer (%s)", phy_err_name(phy_map_mdio(a))); return phy_map_mdio(a); }
    int c = mdio_read(d->bus, d->addr, MII_PHYSID2);
    if (c < 0) return phy_map_mdio(c);
    if (phyid_absent((uint32_t)a << 16 | (uint32_t)c)) { phy_log(d, "probe: id %04X%04X = absent", (unsigned)a, (unsigned)c); return PHY_ENODEV; }
    phyid_decode((uint16_t)a, (uint16_t)c, &r->id);
    phy_log(d, "probe: id 0x%08X OUI %02X-%02X-%02X model 0x%02X rev %u", (unsigned)r->id.phy_id,
            (unsigned)(r->id.oui >> 16), (unsigned)(r->id.oui >> 8 & 0xFF), (unsigned)(r->id.oui & 0xFF),
            (unsigned)r->id.model, (unsigned)r->id.rev);
    unsigned p0 = d->reset_polls;
    int rc = phy_soft_reset(d);
    if (rc != PHY_OK) { phy_log(d, "reset: FAILED %s after %u polls", phy_err_name(rc), d->reset_polls - p0); return rc; }
    r->t_reset_us = (g_now_ns - t0) / 1000u;
    phy_log(d, "reset: BMCR.15 cleared after %u polls", d->reset_polls - p0);
    int bmsr = mdio_read(d->bus, d->addr, MII_BMSR);
    if (bmsr < 0) return phy_map_mdio(bmsr);
    uint16_t adv = (uint16_t)(ADVERTISE_CSMA | (caps_to_adv((uint16_t)bmsr) & adv_mask)
                              | (adv_mask & (ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM)));
    if (!(adv & ADV_TECH)) { phy_log(d, "adv: nothing to advertise (mask 0x%04X)", (unsigned)adv_mask); return PHY_EINVAL; }
    rc = mdio_write(d->bus, d->addr, MII_ADVERTISE, adv);
    if (rc < 0) return phy_map_mdio(rc);
    int rb = mdio_read(d->bus, d->addr, MII_ADVERTISE);
    if (rb < 0) return phy_map_mdio(rb);
    if (rb != adv) { phy_log(d, "adv: wrote 0x%04X read 0x%04X", (unsigned)adv, (unsigned)rb); return PHY_EVERIFY; }
    r->adv = adv;
    phy_log(d, "adv: ANAR=0x%04X (BMSR=0x%04X)", (unsigned)adv, (unsigned)bmsr);
    rc = mdio_write(d->bus, d->addr, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
    if (rc < 0) return phy_map_mdio(rc);
    phy_log(d, "aneg: restart");
    uint64_t t_an = g_now_ns;
    LinkSample s;
    for (;;) {
        rc = phy_read_link(d, &s);
        if (rc != PHY_OK) return rc;
        r->polls++;
        if (s.up && s.an_done) break;
        if (g_now_ns - t_an >= (uint64_t)d->cfg.an_timeout_us * 1000u) {
            int lp = mdio_read(d->bus, d->addr, MII_LPA);
            if (lp < 0) return phy_map_mdio(lp);
            r->lpa = (uint16_t)lp;
            if ((lp & ADV_TECH) && !(adv & lp & ADV_TECH)) {
                phy_log(d, "aneg: partner page 0x%04X has nothing in common with 0x%04X", (unsigned)lp, (unsigned)adv);
                return PHY_EAN_NOCOMMON;
            }
            phy_log(d, "aneg: no link after %u polls (LPA=0x%04X)", r->polls, (unsigned)lp);
            return PHY_EAN_TIMEOUT;
        }
        sim_sleep_us(d->bus, d->cfg.link_poll_us);
    }
    int lp = mdio_read(d->bus, d->addr, MII_LPA);
    if (lp < 0) return phy_map_mdio(lp);
    int ex = mdio_read(d->bus, d->addr, MII_EXPANSION);
    if (ex < 0) return phy_map_mdio(ex);
    r->lpa = (uint16_t)lp; r->exp = (uint16_t)ex;
    r->hcd = an_resolve(adv, r->lpa);
    if (!r->hcd) return PHY_EAN_NOCOMMON;
    r->speed = (r->hcd & (ADVERTISE_100FULL | ADVERTISE_100BASE4 | ADVERTISE_100HALF)) ? 100 : 10;
    r->full = (r->hcd & (ADVERTISE_100FULL | ADVERTISE_10FULL)) != 0;
    r->parallel = !(r->exp & EXPANSION_NWAY);
    if (r->full) {                                  /* 802.3 Table 28B-3 (Linux mii_resolve_flowctrl_fdx 와 같음) */
        if (adv & r->lpa & ADVERTISE_PAUSE_CAP) r->pause = "rx/tx";
        else if (adv & r->lpa & ADVERTISE_PAUSE_ASYM)
            r->pause = (adv & ADVERTISE_PAUSE_CAP) ? "rx" : (r->lpa & ADVERTISE_PAUSE_CAP) ? "tx" : "off";
    }
    r->link = true;
    r->t_link_us = (g_now_ns - t0) / 1000u;
    phy_log(d, "link: up after %u polls, LPA=0x%04X ANER=0x%04X -> %s%s", r->polls, (unsigned)r->lpa,
            (unsigned)r->exp, adv_name(r->hcd), r->parallel ? " (parallel detect)" : "");
    return PHY_OK;
}

/* TODO(C2) phy_format_status: "phy1: Link is Up - 100Mbps/Full - flow control rx/tx" (PD 면 꼬리표 추가),
 * 링크 없음은 "phy1: Link is Down". 반환 = 글자 수, cap 이 모자라면 buf = "" 로 두고 PHY_ENOBUF. */
static GIVEN int phy_format_status(const PhyResult *r, char *buf, size_t cap) {
    if (!r || !buf || cap == 0) return PHY_EINVAL;
    int k;
    if (!r->link) k = snprintf(buf, cap, "phy%d: Link is Down", r->addr);
    else k = snprintf(buf, cap, "phy%d: Link is Up - %dMbps/%s - flow control %s%s", r->addr, r->speed,
                      r->full ? "Full" : "Half", r->pause,
                      r->parallel ? " [parallel detect: partner not autonegotiating, check its duplex]" : "");
    if (k < 0) { buf[0] = '\0'; return PHY_EINVAL; }
    if ((size_t)k >= cap) { buf[0] = '\0'; return PHY_ENOBUF; }
    return k;
}

/* ================= 8. 추가 연습 ================= */
static int g_total, g_pass;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static MockPhy g_p1;
static MdioBus g_bus;
static Log g_log;
#define MS(x) ((uint64_t)(x) * 1000000u)

static void world_reset(void) {
    g_now_ns = 0;
    memset(&g_bus, 0, sizeof g_bus);
    g_bus.half_ns = 200; g_bus.preamble_bits = 32;
    memset(&g_p1, 0, sizeof g_p1);
    g_p1.addr = 1; g_p1.id1 = 0x0022; g_p1.id2 = 0x1560;
    g_p1.caps = BMSR_100FULL | BMSR_100HALF | BMSR_10FULL | BMSR_10HALF | BMSR_ANEGCAPABLE | BMSR_ERCAP;
    g_p1.reset_us = 5000; g_p1.an_ms = 1500; g_p1.force_ms = 20;
    g_p1.lp = LP_AUTONEG; g_p1.lp_speed = 100; g_p1.lp_full = true;
    g_p1.lp_adv = ADVERTISE_10HALF | ADVERTISE_10FULL | ADVERTISE_100HALF | ADVERTISE_100FULL | ADVERTISE_PAUSE_CAP;
    phy_defaults(&g_p1); phy_an_start(&g_p1, 0);
    g_bus.phy[0] = &g_p1; g_bus.nphy = 1;
    log_clear(&g_log);
}
static PhyDev dev1(void) {
    PhyDev d; memset(&d, 0, sizeof d);
    d.bus = &g_bus; d.addr = 1; d.cfg = PHY_CFG_DEFAULT; d.log = &g_log;
    return d;
}

/* ---- 연습 1: pause 해석 (802.3 Table 28B-3) ---- */
enum { FC_OFF = 0, FC_RX = 1, FC_TX = 2 };
static int resolve_pause(uint16_t adv, uint16_t lpa) {          /* Linux mii_resolve_flowctrl_fdx 와 같은 논리 */
    if (adv & lpa & ADVERTISE_PAUSE_CAP) return FC_RX | FC_TX;
    if (adv & lpa & ADVERTISE_PAUSE_ASYM) {
        if (adv & ADVERTISE_PAUSE_CAP) return FC_RX;              /* 우리는 PAUSE 를 받아 멈추기만 */
        if (lpa & ADVERTISE_PAUSE_CAP) return FC_TX;              /* 우리는 PAUSE 를 보내기만 */
    }
    return FC_OFF;
}
static const struct { const char *pat; int res; } T28B3[] = {   /* "LP LA RP RA", x = 무관 */
    { "00xx", FC_OFF }, { "010x", FC_OFF }, { "0110", FC_OFF }, { "0111", FC_TX },
    { "100x", FC_OFF }, { "1x1x", FC_RX | FC_TX }, { "1100", FC_OFF }, { "1101", FC_RX },
};
static int table_pause(int lp, int la, int rp, int ra) {
    int v[4] = { lp, la, rp, ra }, hit = -1, nhit = 0;
    for (size_t i = 0; i < sizeof T28B3 / sizeof T28B3[0]; i++) {
        bool m = true;
        for (int k = 0; k < 4; k++) if (T28B3[i].pat[k] != 'x' && T28B3[i].pat[k] - '0' != v[k]) m = false;
        if (m) { hit = T28B3[i].res; nhit++; }
    }
    return nhit == 1 ? hit : -1;                                  /* 표의 행이 정확히 하나만 맞아야 한다 */
}

/* ---- 연습 2: Clause 45 프레임과 C22 간접 접근 (레지스터 13/14) ---- */
enum { C45_ADDR = 0, C45_WRITE = 1, C45_READ_INC = 2, C45_READ = 3 };
static int c45_frame_bits(int op, int prtad, int devad, uint16_t data, char *out, size_t cap) {
    if (!out || cap < 65 || op < 0 || op > 3 || prtad < 0 || prtad > 31 || devad < 0 || devad > 31) return -1;
    size_t n = 0;
    for (int i = 0; i < 32; i++) out[n++] = '1';
    out[n++] = '0'; out[n++] = '0';                               /* ST = 00 → Clause 45 */
    for (int i = 1; i >= 0; i--) out[n++] = (char)('0' + ((op >> i) & 1));
    for (int i = 4; i >= 0; i--) out[n++] = (char)('0' + ((prtad >> i) & 1));
    for (int i = 4; i >= 0; i--) out[n++] = (char)('0' + ((devad >> i) & 1));
    if (op >= C45_READ_INC) { out[n++] = 'z'; out[n++] = 'd'; } else { out[n++] = '1'; out[n++] = '0'; }   /* 읽기 TA = Z, 장치가 0 */
    for (int i = 15; i >= 0; i--) out[n++] = op >= C45_READ_INC ? 'd' : (char)('0' + ((data >> i) & 1));
    out[n] = '\0';
    return (int)n;
}
static void send_bitstring(MdioBus *b, const char *bits) {       /* 'z' = 아무도 안 구동, 'd' = 장치가 구동 → STA 는 놓는다 */
    for (const char *c = bits; *c; c++) {
        if (*c == '0' || *c == '1') { gpio_mdio_dir(b, 1); mdio_bit_out(b, *c - '0'); }
        else { gpio_mdio_dir(b, 0); (void)mdio_bit_in(b); }
    }
    gpio_mdio_dir(b, 0);
}
typedef struct { bool write; int reg; uint16_t val; } MiiOp;
static int mmd_read_seq(int devad, uint16_t reg, MiiOp *ops, int cap) {   /* 802.3 Annex 22D (확인 권장) */
    if (!ops || cap < 4 || devad < 0 || devad > 31) return -1;
    ops[0] = (MiiOp){ true, 13, (uint16_t)devad };                         /* function 00 = address */
    ops[1] = (MiiOp){ true, 14, reg };
    ops[2] = (MiiOp){ true, 13, (uint16_t)(0x4000u | (unsigned)devad) };   /* function 01 = data, no post increment */
    ops[3] = (MiiOp){ false, 14, 0 };
    return 4;
}

/* ---- 연습 3: 강제 모드와 duplex mismatch ---- */
static int phy_force(PhyDev *d, int speed, bool full, uint32_t timeout_ms) {
    if (!d || (speed != 10 && speed != 100)) return PHY_EINVAL;
    uint16_t bmcr = (uint16_t)((speed == 100 ? BMCR_SPEED100 : 0) | (full ? BMCR_FULLDPLX : 0));   /* ANENABLE = 0 */
    int rc = mdio_write(d->bus, d->addr, MII_BMCR, bmcr);
    if (rc < 0) return phy_map_mdio(rc);
    uint64_t t0 = g_now_ns;
    LinkSample s;
    for (;;) {
        rc = phy_read_link(d, &s);
        if (rc != PHY_OK) return rc;
        if (s.up) return PHY_OK;                                  /* 강제 모드: AN complete 는 안 본다 */
        if (g_now_ns - t0 >= MS(timeout_ms)) return PHY_EAN_TIMEOUT;
        sim_sleep_us(d->bus, 10000);
    }
}
typedef struct { bool link; int speed; bool full; } View;
static View partner_view(const MockPhy *p) {                     /* 케이블 반대편이 스스로 결정한 것 (mock 의 규칙) */
    View v = { phy_link_now(p), 0, false };
    if (!v.link) return v;
    bool local_an = (p->bmcr & BMCR_ANENABLE) != 0;
    if (p->lp == LP_FORCED) { v.speed = p->lp_speed; v.full = p->lp_full; }
    else if (!local_an) { v.speed = (p->bmcr & BMCR_SPEED100) ? 100 : 10; v.full = false; }   /* 파트너의 PD → 반이중 */
    else { uint16_t h = an_resolve(p->an_adv, p->lp_adv); v.speed = (h & 0x0380) ? 100 : 10; v.full = (h & 0x0140) != 0; }
    return v;
}

/* ---- 연습 4: MDIO 시간 예산 ---- */
static uint64_t mdio_frame_ns(unsigned preamble_bits, uint32_t mdc_hz) {
    return (uint64_t)(preamble_bits + 32u) * 1000000000u / mdc_hz;
}

/* ---- 연습 5: TA 에서 선을 안 놓는 순진한 읽기 ---- */
static int mdio_read_naive(MdioBus *b, int phy, int reg) {
    gpio_mdio_dir(b, 1);
    for (unsigned i = 0; i < b->preamble_bits; i++) mdio_bit_out(b, 1);
    mdio_bits_out(b, 0x1, 2); mdio_bits_out(b, 0x2, 2);
    mdio_bits_out(b, (uint32_t)phy, 5); mdio_bits_out(b, (uint32_t)reg, 5);
    /* 버그: gpio_mdio_dir(b, 0) 이 없다 — STA 가 마지막 REGAD 비트를 계속 구동 */
    int z = mdio_bit_in(b), ta = mdio_bit_in(b);
    uint16_t v = 0;
    for (int i = 0; i < 16; i++) v = (uint16_t)(v << 1 | mdio_bit_in(b));
    gpio_mdio_dir(b, 0);
    if (z == 0) return MDIO_EBUS;
    if (ta != 0) return MDIO_ENOACK;
    return v;
}

int main(void) {
    /* 연습 1 */
    {
        unsigned bad = 0;
        printf("ex1 pause resolution (LP LA RP RA -> local):");
        for (int m = 0; m < 16; m++) {
            int lp = m >> 3 & 1, la = m >> 2 & 1, rp = m >> 1 & 1, ra = m & 1;
            uint16_t adv = (uint16_t)((lp ? ADVERTISE_PAUSE_CAP : 0) | (la ? ADVERTISE_PAUSE_ASYM : 0));
            uint16_t lpa = (uint16_t)((rp ? ADVERTISE_PAUSE_CAP : 0) | (ra ? ADVERTISE_PAUSE_ASYM : 0));
            int got = resolve_pause(adv, lpa), want = table_pause(lp, la, rp, ra);
            if (got != want) bad++;
            static const char *nm[] = { "off", "rx", "tx", "rx/tx" };
            printf("%s %d%d%d%d=%s", m % 4 ? "" : "\n   ", lp, la, rp, ra, want >= 0 ? nm[want] : "??");
        }
        printf("\n");
        CHECK(bad == 0);
        /* 반대편에서 본 결과는 TX/RX 가 뒤바뀐 것과 같아야 한다 (대칭성) */
        unsigned asym = 0;
        for (int m = 0; m < 16; m++) {
            uint16_t a = (uint16_t)(((m >> 3 & 1) ? ADVERTISE_PAUSE_CAP : 0) | ((m >> 2 & 1) ? ADVERTISE_PAUSE_ASYM : 0));
            uint16_t c = (uint16_t)(((m >> 1 & 1) ? ADVERTISE_PAUSE_CAP : 0) | ((m & 1) ? ADVERTISE_PAUSE_ASYM : 0));
            int x = resolve_pause(a, c), y = resolve_pause(c, a);
            int swapped = ((y & FC_RX) ? FC_TX : 0) | ((y & FC_TX) ? FC_RX : 0);
            if (x != swapped) asym++;
        }
        CHECK(asym == 0);
    }
    /* 연습 2 */
    {
        char bits[80];
        CHECK(c45_frame_bits(C45_ADDR, 1, 7, 0x003C, bits, sizeof bits) == 64);
        CHECK(strcmp(bits + 32, "00" "00" "00001" "00111" "10" "0000000000111100") == 0);
        printf("ex2 C45 address frame: %.32s %.2s %.2s %.5s %.5s %.2s %.16s\n", bits, bits + 32, bits + 34, bits + 36,
               bits + 41, bits + 46, bits + 48);
        world_reset(); sim_sleep_until(&g_bus, MS(1600));
        uint16_t before[7]; for (int r = 0; r < 7; r++) before[r] = r == MII_BMSR ? 0 : phy_reg_read(&g_p1, r);
        unsigned ign0 = g_p1.frames_ignored, rd0 = g_p1.frames_rd, wr0 = g_p1.frames_wr;
        send_bitstring(&g_bus, bits);
        CHECK(c45_frame_bits(C45_READ, 1, 7, 0, bits, sizeof bits) == 64);
        CHECK(strcmp(bits + 32, "00" "11" "00001" "00111" "zd" "dddddddddddddddd") == 0);
        g_bus.trace = true; g_bus.tr_n = 0;
        send_bitstring(&g_bus, bits);
        g_bus.trace = false;
        CHECK(g_p1.frames_ignored == ign0 + 2 && g_p1.frames_rd == rd0 && g_p1.frames_wr == wr0);
        CHECK(strspn(g_bus.tr_bits + 46, "1") == 18);              /* C22 PHY 는 C45 읽기에 응답 안 함 → TA·DATA 모두 1 */
        unsigned same = 0; for (int r = 0; r < 7; r++) same += r == MII_BMSR || phy_reg_read(&g_p1, r) == before[r];
        CHECK(same == 7 && g_bus.contention == 0);
        CHECK(mdio_read(&g_bus, 1, MII_PHYSID1) == 0x0022);        /* 다음 C22 프레임은 정상 */
        CHECK(c45_frame_bits(4, 1, 7, 0, bits, sizeof bits) == -1 && c45_frame_bits(0, 1, 7, 0, bits, 64) == -1);
        MiiOp ops[4];
        CHECK(mmd_read_seq(7, 0x003C, ops, 4) == 4);
        CHECK(ops[0].write && ops[0].reg == 13 && ops[0].val == 0x0007 && ops[1].val == 0x003C);
        CHECK(ops[2].val == 0x4007 && !ops[3].write && ops[3].reg == 14);
        CHECK(mmd_read_seq(7, 0, ops, 3) == -1);
        unsigned long e0 = g_bus.edges;
        for (int i = 0; i < 4; i++) { if (ops[i].write) (void)mdio_write(&g_bus, 1, ops[i].reg, ops[i].val); else (void)mdio_read(&g_bus, 1, ops[i].reg); }
        CHECK(g_bus.edges - e0 == 256);
        printf("ex2 MMD 7.60 via C22 regs 13/14: 4 frames = %lu MDC edges (native C45: 2 frames = 128)\n", g_bus.edges - e0);
    }
    /* 연습 3 */
    {
        printf("ex3 forced mode / duplex:\n");
        struct { const char *name; bool local_an; int lsp; bool lfull; LpKind lp; int psp; bool pfull; uint16_t padv; int want_rc; bool mismatch; } sc[] = {
            { "local forced 100FD, partner AN   ", false, 100, true,  LP_AUTONEG, 0,   false, 0x01E0, PHY_OK, true },
            { "local forced 100FD, partner 100FD", false, 100, true,  LP_FORCED,  100, true,  0,      PHY_OK, false },
            { "local AN, partner forced 100FD   ", true,  0,   false, LP_FORCED,  100, true,  0,      PHY_OK, true },
            { "local AN, partner AN             ", true,  0,   false, LP_AUTONEG, 0,   false, 0x01E0, PHY_OK, false },
            { "local forced 10HD, partner AN 100", false, 10,  false, LP_AUTONEG, 0,   false, 0x0180, PHY_EAN_TIMEOUT, false },
        };
        for (size_t i = 0; i < sizeof sc / sizeof sc[0]; i++) {
            world_reset();
            g_p1.lp = sc[i].lp; g_p1.lp_speed = sc[i].psp; g_p1.lp_full = sc[i].pfull;
            if (sc[i].padv) g_p1.lp_adv = sc[i].padv;
            PhyDev d = dev1(); PhyResult r; int rc; bool lfull = false; int lsp = 0;
            if (sc[i].local_an) { rc = phy_bringup(&d, ADV_TECH, &r); lfull = r.full; lsp = r.speed; }
            else { rc = phy_force(&d, sc[i].lsp, sc[i].lfull, 200); lfull = sc[i].lfull; lsp = sc[i].lsp; }
            View pv = partner_view(&g_p1);
            bool mm = rc == PHY_OK && pv.link && lfull != pv.full;
            CHECK(rc == sc[i].want_rc && mm == sc[i].mismatch);
            if (rc == PHY_OK) CHECK(pv.speed == lsp);
            printf("   %s -> %-15s local %3d/%s partner %3d/%s %s\n", sc[i].name, phy_err_name(rc), rc ? 0 : lsp,
                   rc ? "-   " : lfull ? "Full" : "Half", pv.link ? pv.speed : 0, pv.link ? (pv.full ? "Full" : "Half") : "-   ",
                   mm ? "DUPLEX MISMATCH" : "");
        }
    }
    /* 연습 4 */
    {
        CHECK(mdio_frame_ns(32, 2500000) == 25600 && mdio_frame_ns(1, 2500000) == 13200);
        CHECK(mdio_frame_ns(32, 1000000) == 64000);
        printf("ex4 MDIO time budget (link poll = 2 BMSR reads per PHY per period):\n");
        printf("   %-26s %5s %10s %16s\n", "MDC", "PHYs", "frame(ns)", "busy us / 1 s poll");
        const struct { const char *n; uint32_t hz; unsigned pre; } m[] = {
            { "2.5 MHz controller", 2500000, 32 }, { "2.5 MHz + preamble supp.", 2500000, 1 },
            { "bit-bang GPIO ~500 kHz", 500000, 32 } };
        for (int i = 0; i < 3; i++) for (unsigned n = 1; n <= 8; n *= 8) {
            uint64_t f = mdio_frame_ns(m[i].pre, m[i].hz);
            printf("   %-26s %5u %10llu %16.1f\n", m[i].n, n, (unsigned long long)f, (double)(f * 2 * n) / 1000.0);
        }
        CHECK(mdio_frame_ns(32, 500000) * 2 * 8 == 2048000);       /* 8 PHY 비트뱅 = 1 초에 2.048 ms CPU 점유 */
    }
    /* 연습 5 */
    {
        world_reset(); sim_sleep_until(&g_bus, MS(1600));
        unsigned long c0 = g_bus.contention;
        int v = mdio_read_naive(&g_bus, 1, MII_BMSR);              /* REGAD 마지막 비트 = 1 → STA 가 1 을 구동 */
        printf("ex5 naive read reg1: value 0x%04X, contention half-periods %lu\n", (unsigned)(v & 0xFFFF), g_bus.contention - c0);
        CHECK(v == 0x7829 && g_bus.contention - c0 == 34);           /* 값은 맞아 보이지만 버스 싸움 (wired-AND 모델) */
        c0 = g_bus.contention;
        int w = mdio_read_naive(&g_bus, 1, MII_PHYSID1);           /* 마지막 비트 = 0 → STA 가 0 을 구동 */
        printf("ex5 naive read reg2: rc %d, contention half-periods %lu\n", w, g_bus.contention - c0);
        CHECK(w == MDIO_EBUS && g_bus.contention - c0 > 0);
        c0 = g_bus.contention;
        CHECK(mdio_read(&g_bus, 1, MII_PHYSID1) == 0x0022 && g_bus.contention == c0);
    }
    printf("\n%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
