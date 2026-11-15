/* COMM-A03 Ethernet PHY와 MDIO: Clause 22 레지스터와 링크 상태
 *
 * 계층 (아래 → 위):
 *   MDIO 선   : MDC(STA = MAC 쪽이 구동) + MDIO(양방향, 풀업, 3상태). 시간 단위 = MDC 반주기 200 ns (2.5 MHz).
 *   GPIO HAL  : gpio_mdc / gpio_mdio_dir / gpio_mdio_set / gpio_mdio_get (Linux mdiobb_ops 와 같은 모양) — 주어짐
 *   mock PHY  : MDC 상승 에지마다 MDIO 를 샘플하는 프레임 상태 기계 + Clause 22 레지스터
 *               (BMCR 0, BMSR 1, PHYID1/2 2·3, ANAR 4, ANLPAR 5, ANER 6, 벤더 0x10..0x1F) — 주어짐.
 *               링크 파트너(없음 / 오토네고 / 강제 속도), 링크 끊김(flap) 일정, 장애 플래그를 가진다.
 *   driver    : 이 레슨의 TODO.
 * 이 모델의 타이밍 규칙 (802.3 22.2.4 / 22.3 요약 — 세부 수치는 스펙 확인 권장):
 *   - MDIO 는 누가 구동하든 MDC 상승 에지에서 샘플된다.
 *   - STA 는 MDC low 동안 MDIO 를 바꾸고, PHY 는 상승 에지 직후(실제 tCO 0..300 ns) 바꾼다.
 *   - STA 는 PHY 가 낸 비트를 "다음 상승 에지 직전"에 읽는다 (= 그 에지에서 샘플).
 * Part A: mdio_read / mdio_write 프레임 비트뱅 (PRE 32x1, ST 01, OP 10/01, PHYAD 5, REGAD 5, TA, DATA 16 MSB first)
 *         + phyid_decode (PHYID1/2 → OUI · model · revision)
 * Part B: phy_soft_reset (BMCR.15 자체 클리어 + 타임아웃), phy_read_link (BMSR.2 latch-low 두 번 읽기 + flap 감지),
 *         an_resolve (ANAR & ANLPAR → 최고 공통 모드), phy_probe (32 주소 스캔 + 버스 고장 판별)
 * Part C: phy_bringup (ID → reset → ANAR → AN 재시작 → 링크 폴링 → 해석) + phy_format_status (Linux 식 상태 줄)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
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

static const char *phy_err_name(int rc) {
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

static bool phy_in_flap(const MockPhy *p, uint64_t t) {
    for (int i = p->flap_i; i < p->nflaps && p->flaps[i].start_ns <= t; i++)
        if (t < p->flaps[i].end_ns) return true;
    return false;
}
static bool phy_link_now(const MockPhy *p) { return p->established && !p->in_reset && !phy_in_flap(p, g_now_ns); }

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
static int mdio_read(MdioBus *b, int phy, int reg) {
    (void)b; (void)phy; (void)reg;
    return MDIO_ENOACK;                             /* TODO(A1) — 선을 건드리지 않는 스텁 */
}

/* TODO(A2) mdio_write: PRE, ST 01, OP 01, PHYAD, REGAD, TA = 10 (STA 가 구동), DATA 16. 끝나면 선을 놓는다. */
static int mdio_write(MdioBus *b, int phy, int reg, uint16_t val) {
    (void)b; (void)phy; (void)reg; (void)val;
    return MDIO_EINVAL;                             /* TODO(A2) */
}

typedef struct { uint32_t phy_id, oui; uint8_t model, rev; } PhyIdInfo;
/* TODO(A3) phyid_decode: PHYID1 = OUI 비트 3..18, PHYID2[15:10] = OUI 비트 19..24, [9:4] model, [3:0] rev.
 * OUI 비트 번호는 "전송 순서" (각 옥텟의 LSB 가 먼저). oui 는 사람이 쓰는 꼴 00-10-A1 → 0x0010A1 로 돌려준다. */
static void phyid_decode(uint16_t id1, uint16_t id2, PhyIdInfo *o) {
    (void)id1; (void)id2;
    if (o) memset(o, 0, sizeof *o);                 /* TODO(A3) */
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
static int phy_soft_reset(PhyDev *d) {
    (void)d;
    return PHY_ERESET_TIMEOUT;                      /* TODO(B1) */
}

/* TODO(B2) phy_read_link: BMSR 를 두 번 읽는다. 첫 값 = 래치(지난 읽기 뒤 끊김이 있었나), 둘째 값 = 지금 상태.
 * s->dropped = (지난번에 up 이었는데 래치가 0) → d->flaps++. d->was_up = s->up. 읽기 실패는 PHY 코드로. */
static int phy_read_link(PhyDev *d, LinkSample *s) {
    (void)d;
    if (s) memset(s, 0, sizeof *s);
    return PHY_ENODEV;                              /* TODO(B2) */
}

/* TODO(B3) an_resolve: (adv & lpa) 의 기술 비트 중 우선순위가 가장 높은 하나 (100FD > 100T4 > 100HD > 10FD > 10HD),
 * 공통이 없으면 0. */
static uint16_t an_resolve(uint16_t adv, uint16_t lpa) {
    (void)adv; (void)lpa;
    return 0;                                       /* TODO(B3) */
}

/* TODO(B4) phy_probe: 주소 0..31 의 PHYID1/2 를 읽어 PHY 목록을 만든다. MDIO_EBUS → PHY_EBUS 즉시.
 * 읽기 실패나 phyid_absent(id) 는 "없음". cap 을 넘으면 PHY_ENOBUF. 하나도 없으면 PHY_ENODEV. */
static int phy_probe(MdioBus *b, ProbeHit *hits, int cap, int *n) {
    (void)b; (void)hits; (void)cap;
    if (n) *n = 0;
    return PHY_ENODEV;                              /* TODO(B4) */
}

/* ================= 7. Part C — bring-up 과 상태 줄 (TODO) ================= */
/* TODO(C1) phy_bringup: ① PHYID 읽기(없으면 ENODEV) ② phy_soft_reset ③ BMSR 능력 → adv = CSMA | (능력 & mask) | pause
 * ④ ANAR 쓰기 + 되읽기(다르면 EVERIFY) ⑤ BMCR = ANENABLE|ANRESTART ⑥ phy_read_link 폴링(up && an_done),
 * an_timeout_us 지나면 ANLPAR 를 보고 NOCOMMON / AN_TIMEOUT ⑦ ANLPAR·ANER 읽고 해석 (속도, 이중, PD, pause).
 * 단계마다 phy_log. adv 에 기술 비트가 하나도 없으면 PHY_EINVAL. */
static int phy_bringup(PhyDev *d, uint16_t adv_mask, PhyResult *r) {
    (void)d; (void)adv_mask;
    if (r) { memset(r, 0, sizeof *r); r->pause = "off"; }
    return PHY_ENODEV;                              /* TODO(C1) */
}

/* TODO(C2) phy_format_status: "phy1: Link is Up - 100Mbps/Full - flow control rx/tx" (PD 면 꼬리표 추가),
 * 링크 없음은 "phy1: Link is Down". 반환 = 글자 수, cap 이 모자라면 buf = "" 로 두고 PHY_ENOBUF. */
static int phy_format_status(const PhyResult *r, char *buf, size_t cap) {
    (void)r;
    if (buf && cap) buf[0] = '\0';
    return 0;                                       /* TODO(C2) */
}

/* ================= 8. 테스트 하네스 ================= */
static int g_total, g_pass;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0xA03C0DE5u;
static uint32_t rnd(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

static MockPhy g_p1, g_p2, g_p3;
static MdioBus g_bus;
static Log g_log;
#define MS(x) ((uint64_t)(x) * 1000000u)

static void mock_phy_init(MockPhy *p, uint8_t addr, uint16_t id1, uint16_t id2) {
    memset(p, 0, sizeof *p);
    p->addr = addr; p->id1 = id1; p->id2 = id2;
    p->caps = BMSR_100FULL | BMSR_100HALF | BMSR_10FULL | BMSR_10HALF | BMSR_ANEGCAPABLE | BMSR_ERCAP;
    p->reset_us = 5000; p->an_ms = 1500; p->force_ms = 20;       /* mock 값: 리셋 5 ms, AN 1.5 s */
    p->lp = LP_AUTONEG; p->lp_speed = 100; p->lp_full = true;
    p->lp_adv = ADVERTISE_10HALF | ADVERTISE_10FULL | ADVERTISE_100HALF | ADVERTISE_100FULL | ADVERTISE_PAUSE_CAP;
    p->last_upd_ns = g_now_ns;
    phy_defaults(p); phy_an_start(p, g_now_ns);                 /* 전원 인가 = 리셋 직후와 같음 */
}
/* phy1 = KSZ8081 의 ID 값을 빌린 mock, phy3 = RTL8201F 의 ID 값을 빌린 mock (케이블 없음). 동작은 이 레슨의 mock */
static void world_reset(void) {
    g_now_ns = 0;
    memset(&g_bus, 0, sizeof g_bus);
    g_bus.half_ns = 200; g_bus.preamble_bits = 32;
    mock_phy_init(&g_p1, 1, 0x0022, 0x1560);
    mock_phy_init(&g_p3, 3, 0x001C, 0xC816); g_p3.lp = LP_NONE; g_p3.established = false;
    g_bus.phy[0] = &g_p1; g_bus.phy[1] = &g_p3; g_bus.nphy = 2;
    log_clear(&g_log);
}
static PhyDev dev_at(int addr, PhyCfg cfg) {
    PhyDev d; memset(&d, 0, sizeof d);
    d.bus = &g_bus; d.addr = addr; d.cfg = cfg; d.log = &g_log;
    return d;
}
/* 독립 기준 구현들 (TODO 와 다른 방식으로 쓴다) */
static void ref_phyid_encode(uint32_t oui, uint8_t model, uint8_t rev, uint16_t *id1, uint16_t *id2) {
    /* 802.3 22.2.4.3.1: OUI 비트 k (1..24, 옥텟마다 LSB 부터 = 전송 순서). 3..18 → PHYID1[15..0], 19..24 → PHYID2[15..10] */
    unsigned a = 0, c = 0;
    for (int k = 3; k <= 24; k++) {
        int octet = (k - 1) / 8, pos = (k - 1) % 8;
        unsigned bit = (oui >> (8 * (2 - octet) + pos)) & 1u;
        if (k <= 18) a |= bit << (15 - (k - 3)); else c |= bit << (15 - (k - 19));
    }
    *id1 = (uint16_t)a; *id2 = (uint16_t)(c | (unsigned)(model & 0x3F) << 4 | (rev & 0xFu));
}
static uint16_t ref_nway(uint16_t neg) {          /* Linux mii_nway_result 모양 + "공통 없음 = 0" */
    if (!(neg & ADV_TECH)) return 0;
    if (neg & ADVERTISE_100FULL) return ADVERTISE_100FULL;
    if (neg & ADVERTISE_100BASE4) return ADVERTISE_100BASE4;
    if (neg & ADVERTISE_100HALF) return ADVERTISE_100HALF;
    if (neg & ADVERTISE_10FULL) return ADVERTISE_10FULL;
    return ADVERTISE_10HALF;
}
static bool naive_link_second_only(MdioBus *b, int addr) {   /* 두 번 읽고 둘째만 본다 → flap 을 놓친다 */
    (void)mdio_read(b, addr, MII_BMSR);
    return (mdio_read(b, addr, MII_BMSR) & BMSR_LSTATUS) != 0;
}
static bool naive_link_single(MdioBus *b, int addr) {        /* 한 번만 읽는다 → 지금 up 인데 down 이라 한다 */
    return (mdio_read(b, addr, MII_BMSR) & BMSR_LSTATUS) != 0;
}
static int monitor_step(PhyDev *d) {             /* 주기 감시 (Linux phylib 의 1 s 폴링 자리) — 주어짐 */
    LinkSample s;
    int rc = phy_read_link(d, &s);
    if (rc != PHY_OK) { phy_log(d, "monitor: %s", phy_err_name(rc)); return rc; }
    if (s.dropped) phy_log(d, "Link is Down (latched: dropped since last poll)");
    if (s.dropped && s.up) phy_log(d, "Link is Up (again)");
    else if (!s.dropped && s.up != s.latched_up && s.up) phy_log(d, "Link is Up");
    return PHY_OK;
}
static void flap_add(MockPhy *p, uint64_t s, uint64_t e) {
    if (p->nflaps < MAX_FLAPS) { p->flaps[p->nflaps].start_ns = s; p->flaps[p->nflaps].end_ns = e; p->nflaps++; }
}

int main(void) {
    char line[256];

    /* ---------- 0. mock 자체 점검 ---------- */
    world_reset();
    CHECK(caps_to_adv(g_p1.caps) == (ADVERTISE_10HALF | ADVERTISE_10FULL | ADVERTISE_100HALF | ADVERTISE_100FULL));
    CHECK(bus_line(&g_bus) == 1);                                /* 아무도 안 구동 → 풀업 */

    /* ---------- Part A: 프레임 비트 ---------- */
    printf("== Part A: MDIO frames ==\n");
    sim_sleep_until(&g_bus, MS(1600));                           /* 전원 AN(1.5 s) 끝난 뒤 */
    int v1 = mdio_read(&g_bus, 1, MII_BMSR);                     /* 첫 읽기: latch-low 라 link 0 */
    g_bus.trace = true; g_bus.tr_n = 0;
    int v2 = mdio_read(&g_bus, 1, MII_BMSR);
    g_bus.trace = false;
    CHECK(v1 == 0x7829);
    CHECK(v2 == 0x782D);
    printf("BMSR read #1 = 0x%04X, #2 = 0x%04X (link bit latched low until read)\n", (unsigned)(v1 & 0xFFFF), (unsigned)(v2 & 0xFFFF));
    printf("read  phy1 reg1 bits: %.32s %.2s %.2s %.5s %.5s %.2s %.16s\n", g_bus.tr_bits, g_bus.tr_bits + 32,
           g_bus.tr_bits + 34, g_bus.tr_bits + 36, g_bus.tr_bits + 41, g_bus.tr_bits + 46, g_bus.tr_bits + 48);
    printf("                who : %.32s %.2s %.2s %.5s %.5s %.2s %.16s\n", g_bus.tr_who, g_bus.tr_who + 32,
           g_bus.tr_who + 34, g_bus.tr_who + 36, g_bus.tr_who + 41, g_bus.tr_who + 46, g_bus.tr_who + 48);
    CHECK(g_bus.tr_n == 64);
    CHECK(strcmp(g_bus.tr_bits, "11111111111111111111111111111111" "01" "10" "00001" "00001" "10" "0111100000101101") == 0);
    CHECK(strcmp(g_bus.tr_who,  "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS" "SS" "SS" "SSSSS" "SSSSS" "zP" "PPPPPPPPPPPPPPPP") == 0);
    g_bus.trace = true; g_bus.tr_n = 0;
    CHECK(mdio_write(&g_bus, 1, 0x10, 0xA5C3) == MDIO_OK);
    g_bus.trace = false;
    printf("write phy1 reg16 bits: %.32s %.2s %.2s %.5s %.5s %.2s %.16s\n", g_bus.tr_bits, g_bus.tr_bits + 32,
           g_bus.tr_bits + 34, g_bus.tr_bits + 36, g_bus.tr_bits + 41, g_bus.tr_bits + 46, g_bus.tr_bits + 48);
    CHECK(strcmp(g_bus.tr_bits, "11111111111111111111111111111111" "01" "01" "00001" "10000" "10" "1010010111000011") == 0);
    CHECK(strspn(g_bus.tr_who, "S") == 64);
    CHECK(g_p1.vendor[0] == 0xA5C3 && mdio_read(&g_bus, 1, 0x10) == 0xA5C3);
    CHECK(mdio_read(&g_bus, 32, 0) == MDIO_EINVAL && mdio_read(&g_bus, 1, 32) == MDIO_EINVAL && mdio_read(&g_bus, -1, 0) == MDIO_EINVAL);
    CHECK(mdio_write(&g_bus, 1, 40, 0) == MDIO_EINVAL && mdio_read(NULL, 1, 1) == MDIO_EINVAL);
    {   /* 한 프레임 = 64 MDC = 25.6 us @ 2.5 MHz */
        unsigned long e0 = g_bus.edges; uint64_t t0 = g_now_ns;
        (void)mdio_read(&g_bus, 1, MII_PHYSID1);
        CHECK(g_bus.edges - e0 == 64 && g_now_ns - t0 == 25600);
        printf("one frame: %lu MDC edges, %llu ns\n", g_bus.edges - e0, (unsigned long long)(g_now_ns - t0));
    }
    {   /* 벤더 레지스터 0x1F: 16비트 값 65536개 전부 쓰기→읽기 */
        unsigned bad = 0;
        for (uint32_t v = 0; v < 65536; v++) {
            if (mdio_write(&g_bus, 1, 0x1F, (uint16_t)v) != MDIO_OK) bad++;
            if (mdio_read(&g_bus, 1, 0x1F) != (int)v) bad++;
        }
        CHECK(bad == 0);
        printf("vendor reg 0x1F: 65536 write/read round trips, bad=%u\n", bad);
    }
    {   /* 32 주소 × 레지스터 0..6: 있는 곳(1,3)만 응답, 값은 mock 레지스터와 같음 */
        unsigned bad = 0;
        for (int a = 0; a < 32; a++) for (int r = 0; r <= 6; r++) {
            int v = mdio_read(&g_bus, a, r);
            if (a == 1 || a == 3) {
                MockPhy *p = a == 1 ? &g_p1 : &g_p3;
                if (r == MII_BMSR) { if (v < 0 || (v & 0xFFDB) != p->caps) bad++; }   /* 동적 비트(5, 2) 제외 */
                else if (v != phy_reg_read(p, r)) bad++;
            } else if (v != MDIO_ENOACK) bad++;
        }
        CHECK(bad == 0);
    }
    {   /* 읽기 전용 레지스터에 쓰기 → 무시 */
        (void)mdio_write(&g_bus, 1, MII_PHYSID1, 0x1234); (void)mdio_write(&g_bus, 1, MII_BMSR, 0);
        (void)mdio_write(&g_bus, 1, MII_LPA, 0xFFFF);
        CHECK(mdio_read(&g_bus, 1, MII_PHYSID1) == 0x0022 && (mdio_read(&g_bus, 1, MII_BMSR) & 0xFFDB) == g_p1.caps);
        CHECK(mdio_read(&g_bus, 1, MII_LPA) == 0x45E1);
    }
    CHECK(g_bus.contention == 0);                                /* 올바른 TA 처리 = 버스 싸움 0 */
    CHECK(g_p1.frames_ignored == 0);

    /* PHY ID 해석 */
    {
        static const struct { const char *name; uint16_t id1, id2; uint32_t oui; uint8_t model, rev; } known[] = {
            { "KSZ8081  (Micrel)",  0x0022, 0x1560, 0x0010A1, 0x16, 0 },
            { "RTL8201F (Realtek)", 0x001C, 0xC816, 0x00E04C, 0x01, 6 },
            { "RTL8211F (Realtek)", 0x001C, 0xC916, 0x00E04C, 0x11, 6 },
        };
        for (size_t i = 0; i < sizeof known / sizeof known[0]; i++) {
            PhyIdInfo o; memset(&o, 0xEE, sizeof o);
            phyid_decode(known[i].id1, known[i].id2, &o);
            CHECK(o.phy_id == ((uint32_t)known[i].id1 << 16 | known[i].id2));
            CHECK(o.oui == known[i].oui && o.model == known[i].model && o.rev == known[i].rev);
            printf("%s PHYID %04X %04X -> OUI %02X-%02X-%02X model 0x%02X rev %u\n", known[i].name, known[i].id1,
                   known[i].id2, (unsigned)(o.oui >> 16), (unsigned)(o.oui >> 8 & 0xFF), (unsigned)(o.oui & 0xFF),
                   (unsigned)o.model, (unsigned)o.rev);
        }
        /* 전수: OUI 비트 1·2 (= 첫 옥텟의 LSB 두 개) 가 0 인 모든 OUI 2^22 개 */
        unsigned bad = 0;
        for (uint32_t k = 0; k < (1u << 22); k++) {
            uint32_t oui = ((k >> 16) << 18) | (k & 0xFFFFu);     /* 첫 옥텟 = (k>>16)<<2 */
            uint8_t model = (uint8_t)(k & 0x3F), rev = (uint8_t)((k >> 6) & 0xF);
            uint16_t a, c; ref_phyid_encode(oui, model, rev, &a, &c);
            PhyIdInfo o; phyid_decode(a, c, &o);
            if (o.oui != oui || o.model != model || o.rev != rev) bad++;
        }
        CHECK(bad == 0);
        printf("phyid decode vs per-bit reference: 4194304 OUIs, bad=%u\n", bad);
    }

    /* ---------- Part B: 장애와 경계 ---------- */
    printf("\n== Part B: reset / latch-low / resolution / probe ==\n");
    {   /* an_resolve: 기술 5비트 × 5비트 전수 (+ pause/selector 비트가 섞여도 무시) */
        unsigned bad = 0;
        for (unsigned x = 0; x < 32; x++) for (unsigned y = 0; y < 32; y++) {
            uint16_t ax = (uint16_t)(x << 5 | ADVERTISE_CSMA | ADVERTISE_PAUSE_CAP), ly = (uint16_t)(y << 5 | ADVERTISE_LPACK | ADVERTISE_CSMA);
            if (an_resolve(ax, ly) != ref_nway((uint16_t)(ax & ly))) bad++;
        }
        CHECK(bad == 0);
        CHECK(an_resolve(0x01E1, 0x45E1) == ADVERTISE_100FULL && an_resolve(0x0061, 0x4181) == 0);
        CHECK(an_resolve(0x03E1, 0x4281) == ADVERTISE_100BASE4);
    }
    {   /* 리셋: 정상 5 ms */
        world_reset(); PhyDev d = dev_at(1, PHY_CFG_DEFAULT);
        (void)mdio_write(&g_bus, 1, 0x10, 0x1234);
        CHECK(phy_soft_reset(&d) == PHY_OK);
        CHECK(d.reset_polls == 5 && g_p1.resets == 1 && g_p1.vendor[0] == 0);   /* 1 ms 마다, 5번째에 끝 */
        CHECK(mdio_read(&g_bus, 1, MII_BMCR) == BMCR_ANENABLE);
        /* 리셋이 안 끝나는 PHY */
        world_reset(); d = dev_at(1, PHY_CFG_DEFAULT); g_p1.reset_stuck = true;
        uint64_t t0 = g_now_ns;
        CHECK(phy_soft_reset(&d) == PHY_ERESET_TIMEOUT);
        CHECK(d.reset_polls == 488 && g_now_ns - t0 >= MS(500) && g_now_ns - t0 < MS(501));   /* 폴 1회 = 1 ms + 25.6 us */
        /* 리셋 중 MDIO 무응답 PHY: ENOACK 를 "아직"으로 */
        world_reset(); d = dev_at(1, PHY_CFG_DEFAULT); g_p1.dead_in_reset = true; g_p1.reset_us = 20000;
        CHECK(phy_soft_reset(&d) == PHY_OK && d.reset_polls == 20);
        world_reset(); d = dev_at(1, PHY_CFG_DEFAULT); g_p1.dead_in_reset = true; g_p1.reset_stuck = true;
        CHECK(phy_soft_reset(&d) == PHY_ENODEV);
        world_reset(); d = dev_at(1, PHY_CFG_DEFAULT); d.cfg.reset_poll_us = 0;
        CHECK(phy_soft_reset(&d) == PHY_EINVAL && g_p1.resets == 0);
    }
    {   /* latch-low: 두 폴링 사이 5 ms 끊김 */
        world_reset(); PhyDev d = dev_at(1, PHY_CFG_DEFAULT);
        sim_sleep_until(&g_bus, MS(2000));
        LinkSample s;
        CHECK(phy_read_link(&d, &s) == PHY_OK && !s.latched_up && s.up && s.an_done && !s.dropped);  /* 처음: 래치 0, 지금 1 */
        flap_add(&g_p1, MS(2003), MS(2008));
        sim_sleep_until(&g_bus, MS(2010));
        CHECK(phy_read_link(&d, &s) == PHY_OK && !s.latched_up && s.up && s.dropped && d.flaps == 1);
        sim_sleep_until(&g_bus, MS(2020));
        CHECK(phy_read_link(&d, &s) == PHY_OK && s.latched_up && s.up && !s.dropped && d.flaps == 1);
        /* 순진한 두 방식 */
        flap_add(&g_p1, MS(2023), MS(2028));
        sim_sleep_until(&g_bus, MS(2030));
        CHECK(naive_link_second_only(&g_bus, 1) == true);        /* 끊김을 놓침 */
        flap_add(&g_p1, MS(2033), MS(2038));
        sim_sleep_until(&g_bus, MS(2040));
        CHECK(naive_link_single(&g_bus, 1) == false);            /* 지금은 up 인데 down 으로 보고 */
        CHECK(phy_link_now(&g_p1));
        /* 끊김 도중의 폴링 */
        flap_add(&g_p1, MS(2045), MS(2055));
        sim_sleep_until(&g_bus, MS(2050));
        (void)phy_read_link(&d, &s);
        sim_sleep_until(&g_bus, MS(2051));
        CHECK(phy_read_link(&d, &s) == PHY_OK && !s.up && !s.an_done && !d.was_up);
        sim_sleep_until(&g_bus, MS(2060));
        CHECK(phy_read_link(&d, &s) == PHY_OK && s.up && !s.dropped);   /* down 에서 up: drop 아님 */
        CHECK(phy_read_link(NULL, &s) == PHY_EINVAL && phy_read_link(&d, NULL) == PHY_EINVAL);
    }
    {   /* 랜덤: flap 일정 + 폴링 간격 무작위 (ms 격자), 기준 = 구간 교차 계산 */
        world_reset(); PhyDev d = dev_at(1, PHY_CFG_DEFAULT);
        sim_sleep_until(&g_bus, MS(2000));
        LinkSample s; (void)phy_read_link(&d, &s);
        uint64_t t = 2001;
        while (g_p1.nflaps < MAX_FLAPS) {                        /* ms 단위로 겹치지 않는 끊김 */
            t += 1 + rnd() % 120; uint64_t len = 1 + rnd() % 30;
            flap_add(&g_p1, MS(t), MS(t + len)); t += len;
        }
        uint64_t prev = 2000, last_end = t;
        bool was_up = true; unsigned bad = 0, polls = 0, drops_ref = 0, f0 = d.flaps;
        for (uint64_t now = 2000; now + 25 < last_end; polls++) {
            now += 1 + rnd() % 20;
            sim_sleep_until(&g_bus, MS(now));
            if (phy_read_link(&d, &s) != PHY_OK) { bad++; break; }
            bool up_ref = true, touched = false;
            for (int i = 0; i < g_p1.nflaps; i++) {
                uint64_t fs = g_p1.flaps[i].start_ns / 1000000u, fe = g_p1.flaps[i].end_ns / 1000000u;
                if (fs <= now && now < fe) up_ref = false;
                if (fs <= now && fe > prev) touched = true;
            }
            bool drop_ref = was_up && touched;
            drops_ref += drop_ref;
            if (s.up != up_ref || s.dropped != drop_ref) bad++;
            was_up = up_ref; prev = now;
        }
        CHECK(bad == 0 && polls > 3000);
        CHECK(d.flaps - f0 == drops_ref);
        printf("random flaps: %d flaps, %u polls, drops detected %u (reference %u), mismatches %u\n",
               g_p1.nflaps, polls, d.flaps - f0, drops_ref, bad);
    }
    {   /* 프리앰블 */
        world_reset();
        (void)mdio_write(&g_bus, 1, 0x10, 0x0000);              /* 마지막 비트 0 → PHY 의 1 카운트 = 0 */
        g_bus.preamble_bits = 31;
        CHECK(mdio_read(&g_bus, 1, MII_PHYSID1) == MDIO_ENOACK);  /* 31개 → 무시 */
        CHECK(mdio_read(&g_bus, 1, MII_PHYSID1) == 0x0022);       /* 직전 프레임 꼬리의 1(Z) 이 더해져 통과 — 간헐 버그 */
        g_bus.preamble_bits = 0;
        CHECK(mdio_read(&g_bus, 1, MII_PHYSID1) == MDIO_ENOACK);
        g_p1.caps |= BMSR_MFPS;                                  /* preamble suppression 지원 PHY */
        g_bus.preamble_bits = 1;
        (void)mdio_write(&g_bus, 1, 0x10, 0x0000);
        CHECK(mdio_read(&g_bus, 1, MII_PHYSID1) == 0x0022);
        g_bus.preamble_bits = 32;
    }
    {   /* TA 를 구동하지 않는 PHY (addr 2) + ignore_ta_mask, 그 뒤 다른 PHY 정상 */
        world_reset();
        mock_phy_init(&g_p2, 2, 0x0022, 0x1561); g_p2.ta_broken = true;
        g_bus.phy[2] = &g_p2; g_bus.nphy = 3;
        CHECK(mdio_read(&g_bus, 2, MII_PHYSID1) == MDIO_ENOACK);
        CHECK(mdio_read(&g_bus, 1, MII_PHYSID1) == 0x0022 && g_bus.contention == 0);
        g_bus.ignore_ta_mask = 1u << 2;
        CHECK(mdio_read(&g_bus, 2, MII_PHYSID2) == 0x1561);
        CHECK(mdio_read(&g_bus, 5, MII_PHYSID1) == MDIO_ENOACK);  /* 마스크 밖 */
        g_bus.ignore_ta_mask = 0xFFFFFFFFu;                       /* 전부 무시 → 없는 주소는 0xFFFF */
        CHECK(mdio_read(&g_bus, 5, MII_PHYSID1) == 0xFFFF);
        ProbeHit hits[8]; int n = -1;
        CHECK(phy_probe(&g_bus, hits, 8, &n) == PHY_OK && n == 3);   /* 0xFFFFFFFF 는 "없음"으로 걸러짐 */
        g_bus.ignore_ta_mask = 0;
        CHECK(phy_probe(&g_bus, hits, 8, &n) == PHY_OK && n == 2 && hits[0].addr == 1 && hits[1].addr == 3);
        CHECK(hits[0].id.oui == 0x0010A1 && hits[1].id.oui == 0x00E04C && hits[1].id.model == 0x01);
        CHECK(phy_probe(&g_bus, hits, 1, &n) == PHY_ENOBUF && n == 1 && hits[0].addr == 1);
        CHECK(phy_probe(&g_bus, NULL, 1, &n) == PHY_EINVAL && n == 0);
        g_bus.nphy = 0;
        CHECK(phy_probe(&g_bus, hits, 8, &n) == PHY_ENODEV && n == 0);
        g_bus.nphy = 3; g_bus.stuck_low = true;
        CHECK(mdio_read(&g_bus, 1, MII_PHYSID1) == MDIO_EBUS);
        CHECK(phy_probe(&g_bus, hits, 8, &n) == PHY_EBUS);
        printf("probe: phy1 %08X, phy3 %08X; stuck-low MDIO -> %s\n", (unsigned)0x00221560u, (unsigned)0x001CC816u,
               phy_err_name(PHY_EBUS));
    }

    /* ---------- Part C: bring-up ---------- */
    printf("\n== Part C: bring-up ==\n");
    {
        world_reset(); PhyDev d1 = dev_at(1, PHY_CFG_DEFAULT), d3 = dev_at(3, PHY_CFG_DEFAULT);
        PhyResult r;
        int rc = phy_bringup(&d1, ADV_ALL, &r);
        CHECK(rc == PHY_OK && r.link && r.speed == 100 && r.full && !r.parallel && strcmp(r.pause, "rx/tx") == 0);
        CHECK(r.adv == 0x0DE1 && r.lpa == 0x45E1 && r.exp == EXPANSION_NWAY && r.hcd == ADVERTISE_100FULL);
        CHECK(g_p1.an_starts == 3 && g_p1.resets == 1);           /* 전원, 리셋 뒤 자동, 우리 재시작 */
        CHECK(phy_format_status(&r, line, sizeof line) == (int)strlen("phy1: Link is Up - 100Mbps/Full - flow control rx/tx"));
        CHECK(strcmp(line, "phy1: Link is Up - 100Mbps/Full - flow control rx/tx") == 0);
        printf("%s\n", line);
        int rc3 = phy_bringup(&d3, ADV_ALL, &r);
        CHECK(rc3 == PHY_EAN_TIMEOUT && !r.link && r.lpa == 0);
        CHECK(phy_format_status(&r, line, sizeof line) > 0 && strcmp(line, "phy3: Link is Down") == 0);
        printf("%s\nbring-up log:\n%s", line, g_log.s);
        CHECK(!g_log.overflow && strstr(g_log.s, "phy1 link: up after") && strstr(g_log.s, "phy3 aneg: no link after"));
        char small[10];
        r.link = true; r.speed = 100; r.full = true; r.pause = "off";
        CHECK(phy_format_status(&r, small, sizeof small) == PHY_ENOBUF && small[0] == '\0');
        CHECK(phy_format_status(&r, small, 0) == PHY_EINVAL && phy_format_status(NULL, line, sizeof line) == PHY_EINVAL);
    }
    {   /* 장애 표 */
        printf("\nfault table:\n%-34s %-20s %-30s %9s %7s\n", "scenario", "return", "status", "t (ms)", "frames");
        enum { NSC = 15 };
        static const char *names[NSC] = {
            "normal", "reset never ends", "MDIO dead during reset", "dead in reset + stuck", "cable unplugged",
            "no common mode (we 10 only)", "partner 10HD only", "partner forced 100 (PD)", "partner forced 10, we 100 only",
            "wrong PHYAD (5)", "PHY ignores TA (addr 2)", "same + ignore_ta_mask", "MDIO stuck low",
            "preamble 31 bits", "empty advertise mask" };
        static const int expect[NSC] = { PHY_OK, PHY_ERESET_TIMEOUT, PHY_OK, PHY_ENODEV, PHY_EAN_TIMEOUT,
            PHY_EAN_NOCOMMON, PHY_OK, PHY_OK, PHY_EAN_TIMEOUT, PHY_ENODEV, PHY_ENODEV, PHY_OK, PHY_EBUS,
            PHY_ENODEV, PHY_EINVAL };
        for (int sc = 0; sc < NSC; sc++) {
            world_reset();
            int addr = 1; uint16_t mask = ADV_ALL;
            switch (sc) {
            case 1: g_p1.reset_stuck = true; break;
            case 2: g_p1.dead_in_reset = true; g_p1.reset_us = 20000; break;
            case 3: g_p1.dead_in_reset = true; g_p1.reset_stuck = true; break;
            case 4: g_p1.lp = LP_NONE; break;
            case 5: g_p1.lp_adv = ADVERTISE_100FULL; mask = ADVERTISE_10HALF | ADVERTISE_10FULL; break;
            case 6: g_p1.lp_adv = ADVERTISE_10HALF; break;
            case 7: g_p1.lp = LP_FORCED; g_p1.lp_speed = 100; break;
            case 8: g_p1.lp = LP_FORCED; g_p1.lp_speed = 10; mask = ADVERTISE_100HALF | ADVERTISE_100FULL; break;
            case 9: addr = 5; break;
            case 10: case 11:
                mock_phy_init(&g_p2, 2, 0x0022, 0x1561); g_p2.ta_broken = true;
                g_bus.phy[2] = &g_p2; g_bus.nphy = 3; addr = 2;
                if (sc == 11) g_bus.ignore_ta_mask = 1u << 2;
                break;
            case 12: g_bus.stuck_low = true; break;
            case 13: g_bus.preamble_bits = 31; break;
            case 14: mask = ADVERTISE_PAUSE_CAP; break;
            default: break;
            }
            PhyDev d = dev_at(addr, PHY_CFG_DEFAULT);
            PhyResult r; uint64_t t0 = g_now_ns; unsigned long e0 = g_bus.edges;
            int rc = phy_bringup(&d, mask, &r);
            CHECK(rc == expect[sc]);
            if (phy_format_status(&r, line, sizeof line) < 0) snprintf(line, sizeof line, "(n/a)");
            const char *st = strchr(line, ':'); st = st ? st + 2 : line;
            printf("%-34s %-20s %-30.30s %9.1f %7lu\n", names[sc], phy_err_name(rc), rc == PHY_OK ? st : "-",
                   (double)(g_now_ns - t0) / 1e6, (g_bus.edges - e0 + 32) / 64);
            if (sc == 6) CHECK(r.speed == 10 && !r.full && r.hcd == ADVERTISE_10HALF && strcmp(r.pause, "off") == 0);
            if (sc == 7) CHECK(r.speed == 100 && !r.full && r.parallel && r.lpa == ADVERTISE_100HALF);
            if (sc == 5) CHECK(r.lpa == (ADVERTISE_100FULL | ADVERTISE_CSMA | ADVERTISE_LPACK));
            if (sc == 11) CHECK(r.link && r.speed == 100 && r.full);
            if (sc == 1) CHECK(g_now_ns - t0 > MS(500) && g_now_ns - t0 < MS(502));
            if (sc == 4) CHECK(g_now_ns - t0 > MS(3000) && g_now_ns - t0 < MS(3020));
            if (sc == 7) {
                CHECK(phy_format_status(&r, line, sizeof line) > 0);
                CHECK(strstr(line, "100Mbps/Half") && strstr(line, "parallel detect"));
            }
        }
    }
    {   /* 전수: 우리 광고 마스크 16 × 파트너 기술 32 (T4 포함) = 512 bring-up, 짧은 타이밍 */
        unsigned bad = 0, ok = 0, nocommon = 0, inval = 0;
        PhyCfg fast = { 50000, 1000, 100000, 5000 };
        for (unsigned m = 0; m < 16; m++) for (unsigned pa = 0; pa < 32; pa++) {
            world_reset();
            g_p1.an_ms = 20; g_p1.reset_us = 2000; g_p1.lp_adv = (uint16_t)(pa << 5);
            PhyDev d = dev_at(1, fast); d.log = NULL;
            uint16_t mask = (uint16_t)(m << 5);
            PhyResult r;
            int rc = phy_bringup(&d, mask, &r);
            uint16_t want = ref_nway((uint16_t)(mask & (pa << 5)));
            if (m == 0) { inval++; if (rc != PHY_EINVAL) bad++; continue; }
            if (want == 0) { nocommon++; if (rc != (pa ? PHY_EAN_NOCOMMON : PHY_EAN_TIMEOUT)) bad++; continue; }
            ok++;
            if (rc != PHY_OK || r.hcd != want || r.speed != ((want & 0x0380) ? 100 : 10) ||
                r.full != ((want & (ADVERTISE_100FULL | ADVERTISE_10FULL)) != 0) || r.parallel) bad++;
        }
        CHECK(bad == 0);
        printf("\nexhaustive bring-up: 16 masks x 32 partner pages: ok %u, no-common/timeout %u, EINVAL %u, bad %u\n",
               ok, nocommon, inval, bad);
        /* parallel detection 전수: 마스크 16 × 강제 속도 2 */
        bad = 0;
        for (unsigned m = 1; m < 16; m++) for (int sp = 0; sp < 2; sp++) {
            world_reset();
            g_p1.an_ms = 20; g_p1.reset_us = 2000; g_p1.lp = LP_FORCED; g_p1.lp_speed = sp ? 100 : 10;
            PhyDev d = dev_at(1, fast); d.log = NULL;
            uint16_t mask = (uint16_t)(m << 5);
            bool can = sp ? (mask & ADVERTISE_100HALF) : (mask & ADVERTISE_10HALF);
            PhyResult r;
            int rc = phy_bringup(&d, mask, &r);
            if (can) { if (rc != PHY_OK || !r.parallel || r.full || r.speed != (sp ? 100 : 10)) bad++; }
            else if (rc != PHY_EAN_TIMEOUT) bad++;
        }
        CHECK(bad == 0);
    }
    {   /* 능력표에 없는 기술은 광고하지 않음 / ANAR 쓰기가 안 먹는 PHY → EVERIFY */
        world_reset(); PhyDev d = dev_at(1, PHY_CFG_DEFAULT);
        g_p1.caps = (uint16_t)(g_p1.caps & ~BMSR_100FULL);
        PhyResult r;
        CHECK(phy_bringup(&d, ADV_ALL, &r) == PHY_OK && r.adv == 0x0CE1 && r.hcd == ADVERTISE_100HALF && !r.full);
        world_reset(); d = dev_at(1, PHY_CFG_DEFAULT);
        g_p1.anar_ro = true;                                     /* 기본값 0x01E1 에 묶임 */
        CHECK(phy_bringup(&d, ADVERTISE_10HALF | ADVERTISE_10FULL, &r) == PHY_EVERIFY && !r.link);
        CHECK(strstr(g_log.s, "adv: wrote 0x0061 read 0x01E1") != NULL);
    }
    {   /* 운영 중 감시: flap 하나 → Down/Up 로그, 그 뒤 조용 */
        world_reset(); PhyDev d = dev_at(1, PHY_CFG_DEFAULT);
        PhyResult r;
        CHECK(phy_bringup(&d, ADV_ALL, &r) == PHY_OK);
        log_clear(&g_log);
        uint64_t t = g_now_ns / 1000000u + 1;
        flap_add(&g_p1, MS(t + 1500), MS(t + 1800));
        for (int k = 1; k <= 4; k++) { sim_sleep_until(&g_bus, MS(t + 1000u * (uint64_t)k)); CHECK(monitor_step(&d) == PHY_OK); }
        printf("\nmonitor (1 s polls, 300 ms outage at +1.5 s):\n%s", g_log.s);
        CHECK(d.flaps == 1 && strstr(g_log.s, "Link is Down") && strstr(g_log.s, "Link is Up (again)"));
    }

    printf("\n%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
