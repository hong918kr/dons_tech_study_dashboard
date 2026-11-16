/* PCIE-B13 NVMe 컨트롤러 초기화 시퀀스: CC.EN 과 CSTS.RDY
 *
 * 호스트 드라이버(nvme_* 함수)가 mock NVMe 컨트롤러(별도 pthread = SSD 펌웨어 역할)를 상대로 이 순서를 지킨다:
 *   CAP 읽기 → 설정 검사(MPS/CSS/큐 크기/정렬 — 레지스터를 건드리기 전에!) → disable(CC.EN=0) → CSTS.RDY=0 대기
 *   → AQA/ASQ/ACQ → CC(CSS=NVM, MPS, AMS=RR, SHN=0, IOSQES=6, IOCQES=4, EN=0) → CC.EN=1 → CSTS.RDY=1 대기
 * 대기는 전부 CAP.TO(500 ms 단위) 로 정한 타임아웃 안에서. 오프셋·비트 = Linux include/linux/nvme.h (7.0 헤더) 와 같은 이름·값.
 *
 * 시간 축척: CAP.TO 1 단위 = 실제 500 ms → 이 시뮬레이션에서는 10 ms (SIM_TO_UNIT_US, 50배 빠름).
 *           판정은 시간 측정이 아니라 반환 코드 · 단계(stage) · mock 의 쓰기 로그 / 이벤트 로그 / 위반 로그로 한다.
 * 공유 레지스터는 _Atomic uint32_t, 로그는 mutex — TSan 깨끗해야 한다.
 *
 * Part A: nvme_rdy_budget_us, nvme_wait_rdy, nvme_disable, nvme_check_cfg, nvme_init (정상 경로)
 * Part B: 오류 주입 — RDY 안 옴(timeout), disable 안 끝남, CFS, 장치 사라짐(0xFFFFFFFF), 잘못된 MPS/CSS/큐/정렬,
 *         EN=1 상태에서의 레지스터 쓰기 (mock 이 위반으로 기록)
 * Part C: 컨트롤러(SSD FW) 쪽 — EN 0→1 때 설정 검사 fw_validate_enable, 버그 있는 드라이버 → CFS → 정상 init 으로 복구
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 *       cc -std=c11 -g -O1 -fsanitize=thread solution.c -o sol_tsan && ./sol_tsan
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ---------------- NVMe BAR0 레지스터 (Linux include/linux/nvme.h 와 같은 이름·값) ---------------- */
#define NVME_REG_CAP   0x0000u   /* 64-bit, RO */
#define NVME_REG_VS    0x0008u
#define NVME_REG_CC    0x0014u
#define NVME_REG_CSTS  0x001cu   /* RO (NSSRO 만 RW1C — 이 mock 은 무시) */
#define NVME_REG_AQA   0x0024u
#define NVME_REG_ASQ   0x0028u   /* 64-bit */
#define NVME_REG_ACQ   0x0030u   /* 64-bit */

#define NVME_CAP_MQES(cap)    ((cap) & 0xffff)
#define NVME_CAP_TIMEOUT(cap) (((cap) >> 24) & 0xff)
#define NVME_CAP_CSS(cap)     (((cap) >> 37) & 0xff)
#define NVME_CAP_MPSMIN(cap)  (((cap) >> 48) & 0xf)
#define NVME_CAP_MPSMAX(cap)  (((cap) >> 52) & 0xf)
#define NVME_CAP_AMS(cap)     (((cap) >> 17) & 0x3)   /* 헤더에 매크로 없음 — 스펙 CAP.AMS bits 18:17 */
#define NVME_CAP_CSS_NVM      (1u << 0)

#define NVME_CC_ENABLE        (1u << 0)
#define NVME_CC_CSS_SHIFT     4
#define NVME_CC_CSS_MASK      (7u << NVME_CC_CSS_SHIFT)
#define NVME_CC_CSS_NVM       (0u << NVME_CC_CSS_SHIFT)
#define NVME_CC_MPS_SHIFT     7
#define NVME_CC_MPS_MASK      (0xfu << NVME_CC_MPS_SHIFT)
#define NVME_CC_AMS_SHIFT     11
#define NVME_CC_AMS_MASK      (7u << NVME_CC_AMS_SHIFT)
#define NVME_CC_AMS_RR        (0u << NVME_CC_AMS_SHIFT)
#define NVME_CC_SHN_SHIFT     14
#define NVME_CC_SHN_MASK      (3u << NVME_CC_SHN_SHIFT)
#define NVME_CC_SHN_NONE      (0u << NVME_CC_SHN_SHIFT)
#define NVME_CC_IOSQES_SHIFT  16
#define NVME_CC_IOCQES_SHIFT  20
#define NVME_NVM_IOSQES       6        /* 2^6 = 64 B SQ 엔트리 */
#define NVME_NVM_IOCQES       4        /* 2^4 = 16 B CQ 엔트리 */
#define NVME_CC_IOSQES        (NVME_NVM_IOSQES << NVME_CC_IOSQES_SHIFT)
#define NVME_CC_IOCQES        (NVME_NVM_IOCQES << NVME_CC_IOCQES_SHIFT)

#define NVME_CSTS_RDY         (1u << 0)
#define NVME_CSTS_CFS         (1u << 1)
#define NVME_CSTS_SHST_OCCUR  (1u << 2)
#define NVME_CSTS_SHST_CMPLT  (2u << 2)
#define NVME_CSTS_SHST_MASK   (3u << 2)

#define NVME_AQ_MAX_ENTRIES   4096u    /* AQA.ASQS/ACQS 는 12비트, 0-based → 2..4096 */

/* ---------------- 시간 (시뮬레이션 축척) ---------------- */
#define SIM_TO_UNIT_US 10000u  /* CAP.TO 1 단위: 실제 500 ms → 시뮬 10 ms */
#define HOST_POLL_US   50u     /* 호스트가 CSTS 를 읽는 간격 (Linux 는 msleep(100) 수준) */
#define MOCK_TICK_US   20u

static uint64_t now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}
static void sleep_us(uint32_t us) {
    struct timespec d = { (time_t)(us / 1000000u), (long)(us % 1000000u) * 1000L };
    nanosleep(&d, NULL);
}

/* ======================= mock NVMe 컨트롤러 (주어짐, Part C 의 fw_validate_enable 만 TODO) ======================= */

enum { F_NONE = 0, F_NO_READY = 1 << 0, F_CFS_ON_ENABLE = 1 << 1, F_DISABLE_STUCK = 1 << 2,
       F_GONE_ON_ENABLE = 1 << 3, F_SHN_STUCK = 1 << 4, F_CFS_ONCE = 1 << 5 };
enum { EV_EN_SEEN = 1, EV_READY, EV_CFS, EV_DISABLED, EV_GONE, EV_BAD_CFG, EV_SHN_START, EV_SHN_DONE };
enum { V_ADMINQ_WHILE_EN = 1, V_CC_CFG_WHILE_EN, V_EN_WHILE_RDY, V_RO_WRITE };
enum { BAD_NONE = 0, BAD_MPS, BAD_CSS, BAD_AMS, BAD_AQA, BAD_ASQ, BAD_ACQ };
enum { PEND_NONE = 0, PEND_READY, PEND_DISABLE };

#define LOG_MAX 64
typedef struct {
    _Atomic uint32_t reg[0x40 / 4];   /* BAR0 0x00..0x3F: CAP lo/hi, VS, ..., CC, CSTS, AQA, ASQ lo/hi, ACQ lo/hi */
    _Atomic int gone;                 /* 1 → 모든 읽기 0xFFFFFFFF, 쓰기 사라짐 (surprise removal / link down) */
    _Atomic int stop;
    _Atomic int bad_reason;           /* 마지막 EN 0→1 때 fw_validate_enable 결과 */
    unsigned faults;                  /* 시작 전에만 설정 (스레드 생성이 동기화 지점) */
    uint32_t ready_delay_us, disable_delay_us, shn_delay_us;
    pthread_t th;
    pthread_mutex_t mu;               /* 아래 로그 3개 보호 */
    struct { uint16_t off; uint32_t val; } wlog[LOG_MAX]; int nw;   /* 호스트 쓰기 순서 */
    int ev[LOG_MAX]; int nev;                                        /* 컨트롤러 이벤트 */
    int viol[LOG_MAX]; int nviol;                                    /* 규칙 위반 */
    /* 아래는 컨트롤러 스레드 전용 */
    uint32_t seen_cc; int pending; uint64_t due; int shn_pending; uint64_t shn_due; int cfs_once_used;
} mock_ctrl;

static uint32_t reg_ld(const mock_ctrl *m, uint32_t off) {
    return atomic_load(&((mock_ctrl *)m)->reg[off / 4]);
}
static uint64_t reg_ld64(const mock_ctrl *m, uint32_t off) {
    return (uint64_t)reg_ld(m, off) | ((uint64_t)reg_ld(m, off + 4) << 32);
}
static void push(mock_ctrl *m, int *arr, int *n, int v) {
    pthread_mutex_lock(&m->mu);
    if (*n < LOG_MAX) arr[(*n)++] = v;
    pthread_mutex_unlock(&m->mu);
}
static void mock_ev(mock_ctrl *m, int e) { push(m, m->ev, &m->nev, e); }

/* ---- 호스트가 쓰는 MMIO 접근 (32비트 단위, 실제 BAR0 접근처럼) ---- */
static uint32_t mmio_rd32(mock_ctrl *m, uint32_t off) {
    if (atomic_load(&m->gone)) return 0xFFFFFFFFu;   /* 링크가 죽으면 non-posted 읽기는 all-1s (B03/B08) */
    return reg_ld(m, off);
}
static uint64_t mmio_rd64(mock_ctrl *m, uint32_t off) {   /* lo → hi 두 번 읽기. CAP 는 상수라 찢어짐 없음 */
    uint64_t lo = mmio_rd32(m, off);
    return lo | ((uint64_t)mmio_rd32(m, off + 4) << 32);
}
static void mmio_wr32(mock_ctrl *m, uint32_t off, uint32_t v) {
    if (atomic_load(&m->gone)) return;               /* posted write 는 조용히 사라진다 */
    pthread_mutex_lock(&m->mu);
    if (m->nw < LOG_MAX) { m->wlog[m->nw].off = (uint16_t)off; m->wlog[m->nw].val = v; m->nw++; }
    uint32_t cc = reg_ld(m, NVME_REG_CC), csts = reg_ld(m, NVME_REG_CSTS);
    int bad = 0;
    if (off == NVME_REG_CAP || off == NVME_REG_CAP + 4 || off == NVME_REG_VS || off == NVME_REG_CSTS) {
        bad = V_RO_WRITE;
    } else if (off >= NVME_REG_AQA && off < NVME_REG_ACQ + 8) {
        if ((cc & NVME_CC_ENABLE) || (csts & NVME_CSTS_RDY)) bad = V_ADMINQ_WHILE_EN;
    } else if (off == NVME_REG_CC) {
        uint32_t cfg = NVME_CC_CSS_MASK | NVME_CC_MPS_MASK | NVME_CC_AMS_MASK;
        if ((cc & NVME_CC_ENABLE) && (v & NVME_CC_ENABLE) && ((cc ^ v) & cfg)) bad = V_CC_CFG_WHILE_EN;
        if (!(cc & NVME_CC_ENABLE) && (v & NVME_CC_ENABLE) && (csts & NVME_CSTS_RDY)) bad = V_EN_WHILE_RDY;
    }
    if (bad && m->nviol < LOG_MAX) m->viol[m->nviol++] = bad;
    if (bad != V_RO_WRITE) atomic_store(&m->reg[off / 4], v);   /* 위반이어도 쓰기는 반영 (실제 HW 처럼) */
    pthread_mutex_unlock(&m->mu);
}

/* ---------------- Part C: 컨트롤러(SSD FW) 쪽 — EN 0→1 을 본 순간의 설정 검사 ---------------- */

/* 이 mock 의 정책(실제 컨트롤러는 제각각): 첫 번째로 걸린 이유를 돌려준다. 순서 = MPS, CSS, AMS, AQA, ASQ, ACQ.
 *   MPS : CAP.MPSMIN ≤ CC.MPS ≤ CAP.MPSMAX
 *   CSS : CC.CSS == 0(NVM) 이고 CAP.CSS bit0 = 1 (이 mock 은 NVM 명령 집합만)
 *   AMS : 0(RR) 항상 OK, 1(WRR) 은 CAP.AMS bit0, 7(VS) 는 CAP.AMS bit1, 그 외 값은 reserved → BAD
 *   AQA : ASQS, ACQS (0-based) 가 0 이면 1 엔트리 → 최소 2 위반
 *   ASQ/ACQ : 0 이 아니고 CC.MPS 페이지 크기(2^(12+MPS)) 정렬 */
static int fw_validate_enable(const mock_ctrl *m) {
    uint64_t cap = reg_ld64(m, NVME_REG_CAP);
    uint32_t cc = reg_ld(m, NVME_REG_CC), aqa = reg_ld(m, NVME_REG_AQA);
    uint64_t asq = reg_ld64(m, NVME_REG_ASQ), acq = reg_ld64(m, NVME_REG_ACQ);
    uint32_t mps = (cc & NVME_CC_MPS_MASK) >> NVME_CC_MPS_SHIFT;
    uint32_t css = (cc & NVME_CC_CSS_MASK) >> NVME_CC_CSS_SHIFT;
    uint32_t ams = (cc & NVME_CC_AMS_MASK) >> NVME_CC_AMS_SHIFT;
    if (mps < NVME_CAP_MPSMIN(cap) || mps > NVME_CAP_MPSMAX(cap)) return BAD_MPS;
    if (css != 0 || !(NVME_CAP_CSS(cap) & NVME_CAP_CSS_NVM)) return BAD_CSS;
    if (!(ams == 0 || (ams == 1 && (NVME_CAP_AMS(cap) & 1u)) || (ams == 7 && (NVME_CAP_AMS(cap) & 2u)))) return BAD_AMS;
    if ((aqa & 0xFFFu) == 0 || ((aqa >> 16) & 0xFFFu) == 0) return BAD_AQA;
    uint64_t pmask = (1ull << (12 + mps)) - 1;
    if (asq == 0 || (asq & pmask)) return BAD_ASQ;
    if (acq == 0 || (acq & pmask)) return BAD_ACQ;
    return BAD_NONE;
}

static void mock_apply(mock_ctrl *m, int what) {
    if (what == PEND_READY) {
        /* 이벤트를 먼저 기록하고 레지스터를 바꾼다 → 호스트가 변화를 봤다면 이벤트는 이미 로그에 있다 */
        if (m->faults & F_GONE_ON_ENABLE) { mock_ev(m, EV_GONE); atomic_store(&m->gone, 1); return; }
        if (m->faults & F_NO_READY) return;                          /* FW 가 멈춤: RDY 영영 안 옴 */
        if ((m->faults & F_CFS_ON_ENABLE) || ((m->faults & F_CFS_ONCE) && !m->cfs_once_used)) {
            m->cfs_once_used = 1;
            mock_ev(m, EV_CFS); atomic_fetch_or(&m->reg[NVME_REG_CSTS / 4], NVME_CSTS_CFS); return;
        }
        int r = fw_validate_enable(m);
        atomic_store(&m->bad_reason, r);
        if (r != BAD_NONE) {
            mock_ev(m, EV_BAD_CFG); atomic_fetch_or(&m->reg[NVME_REG_CSTS / 4], NVME_CSTS_CFS); return;
        }
        mock_ev(m, EV_READY); atomic_fetch_or(&m->reg[NVME_REG_CSTS / 4], NVME_CSTS_RDY);
    } else if (what == PEND_DISABLE) {
        if (m->faults & F_DISABLE_STUCK) return;                     /* reset 이 안 끝남: RDY 가 1 에 붙음 */
        /* controller reset: 큐 상태 버림, RDY·CFS·SHST 0 (이 mock 의 정책) */
        mock_ev(m, EV_DISABLED);
        atomic_fetch_and(&m->reg[NVME_REG_CSTS / 4], ~(NVME_CSTS_RDY | NVME_CSTS_CFS | NVME_CSTS_SHST_MASK));
    }
}

static void *mock_thread(void *arg) {
    mock_ctrl *m = arg;
    while (!atomic_load(&m->stop)) {
        uint64_t now = now_us();
        uint32_t cc = reg_ld(m, NVME_REG_CC);
        if ((cc ^ m->seen_cc) & NVME_CC_ENABLE) {
            if (cc & NVME_CC_ENABLE) { mock_ev(m, EV_EN_SEEN); m->pending = PEND_READY; m->due = now + m->ready_delay_us; }
            else { m->pending = PEND_DISABLE; m->due = now + m->disable_delay_us; }
        }
        if (((cc ^ m->seen_cc) & NVME_CC_SHN_MASK) && (cc & NVME_CC_SHN_MASK) && (cc & NVME_CC_ENABLE)) {
            mock_ev(m, EV_SHN_START);
            atomic_fetch_and(&m->reg[NVME_REG_CSTS / 4], ~NVME_CSTS_SHST_MASK);
            atomic_fetch_or(&m->reg[NVME_REG_CSTS / 4], NVME_CSTS_SHST_OCCUR);
            m->shn_pending = 1; m->shn_due = now + m->shn_delay_us;
        }
        m->seen_cc = cc;
        if (m->pending && now >= m->due) { mock_apply(m, m->pending); m->pending = PEND_NONE; }
        if (m->shn_pending && now >= m->shn_due) {
            m->shn_pending = 0;
            if (!(m->faults & F_SHN_STUCK)) {
                mock_ev(m, EV_SHN_DONE);
                atomic_fetch_and(&m->reg[NVME_REG_CSTS / 4], ~NVME_CSTS_SHST_MASK);
                atomic_fetch_or(&m->reg[NVME_REG_CSTS / 4], NVME_CSTS_SHST_CMPLT);
            }
        }
        sleep_us(MOCK_TICK_US);
    }
    return NULL;
}

static void mock_init(mock_ctrl *m, uint64_t cap, uint32_t cc0, uint32_t csts0, unsigned faults) {
    for (unsigned i = 0; i < 0x40 / 4; i++) atomic_init(&m->reg[i], 0);
    atomic_init(&m->reg[NVME_REG_CAP / 4], (uint32_t)cap);
    atomic_init(&m->reg[NVME_REG_CAP / 4 + 1], (uint32_t)(cap >> 32));
    atomic_init(&m->reg[NVME_REG_VS / 4], 0x00010400u);              /* 1.4.0 */
    atomic_init(&m->reg[NVME_REG_CC / 4], cc0);
    atomic_init(&m->reg[NVME_REG_CSTS / 4], csts0);
    atomic_init(&m->gone, 0); atomic_init(&m->stop, 0); atomic_init(&m->bad_reason, BAD_NONE);
    m->faults = faults;
    m->ready_delay_us = 1000; m->disable_delay_us = 500; m->shn_delay_us = 800;
    pthread_mutex_init(&m->mu, NULL);
    m->nw = m->nev = m->nviol = 0;
    m->seen_cc = cc0; m->pending = PEND_NONE; m->due = 0; m->shn_pending = 0; m->shn_due = 0; m->cfs_once_used = 0;
}
static void mock_start(mock_ctrl *m) { pthread_create(&m->th, NULL, mock_thread, m); }
static void mock_stop(mock_ctrl *m) {
    atomic_store(&m->stop, 1);
    pthread_join(m->th, NULL);
    pthread_mutex_destroy(&m->mu);
}

/* ======================= 호스트 드라이버 ======================= */

enum { NV_OK = 0, NV_ERR_PARAM = -1, NV_ERR_MPS = -2, NV_ERR_CSS = -3, NV_ERR_QSIZE = -4, NV_ERR_ALIGN = -5,
       NV_ERR_TO_DISABLE = -6, NV_ERR_TO_ENABLE = -7, NV_ERR_CFS = -8, NV_ERR_GONE = -9 };
enum { ST_NONE = 0, ST_CAP, ST_CHECK, ST_DISABLE, ST_ADMINQ, ST_CONFIG, ST_ENABLE, ST_READY };

typedef struct {
    uint64_t asq, acq;              /* Admin SQ/CQ 의 물리(DMA) 주소 */
    uint32_t sq_depth, cq_depth;    /* 엔트리 수 (1-based), 2..4096 */
    uint32_t page_size;             /* 호스트 메모리 페이지 = 2^(12+CC.MPS) 바이트 */
} nvme_admin_cfg;

typedef struct {
    uint64_t cap;
    uint32_t budget_us;   /* 마지막 대기의 타임아웃 예산 */
    uint32_t polls;       /* 마지막 대기에서 CSTS 를 읽은 횟수 */
    uint32_t last_csts;   /* 마지막 대기에서 마지막으로 읽은 CSTS */
    int stage;            /* 끝난(또는 실패한) 단계 */
    int reset_done;       /* CFS 를 보고 reset(EN=0) 까지 했나 */
    int reset_rc;         /* 그 reset 의 RDY=0 대기 결과 */
} nvme_report;

/* A1: CAP.TO(500 ms 단위) → 대기 예산(시뮬 µs). 실제 드라이버라면 TO × 500 ms. */
static uint32_t nvme_rdy_budget_us(uint64_t cap) {
    return (uint32_t)NVME_CAP_TIMEOUT(cap) * SIM_TO_UNIT_US;
}

/* A2: CSTS 폴링. want=1: RDY=1 이면 OK, CFS=1 이면 NV_ERR_CFS.  want=0: RDY=0 **그리고** CFS=0 이면 OK
 *     (reset 이 끝나야 CFS 가 풀린다 — RDY 가 원래 0 이던 CFS 상태에서 reset 완료를 알려 주는 유일한 신호).
 *     0xFFFFFFFF → NV_ERR_GONE (CFS·RDY 보다 먼저 검사). 예산이 다 되면 want ? NV_ERR_TO_ENABLE : NV_ERR_TO_DISABLE. 만료 뒤에도 **마지막 한 번은 읽고** 판정한다.
 *     r->budget_us / polls / last_csts 를 채운다. */
static int nvme_wait_rdy(mock_ctrl *m, int want, uint32_t budget_us, nvme_report *r) {
    uint64_t deadline = now_us() + budget_us;
    r->budget_us = budget_us; r->polls = 0;
    for (;;) {
        int expired = now_us() >= deadline;          /* 시간 먼저 보고, 그다음 읽는다 */
        uint32_t csts = mmio_rd32(m, NVME_REG_CSTS);
        r->polls++; r->last_csts = csts;
        if (csts == 0xFFFFFFFFu) return NV_ERR_GONE;
        if (want && (csts & NVME_CSTS_CFS)) return NV_ERR_CFS;
        if (want ? (csts & NVME_CSTS_RDY) != 0 : (csts & (NVME_CSTS_RDY | NVME_CSTS_CFS)) == 0) return NV_OK;
        if (expired) return want ? NV_ERR_TO_ENABLE : NV_ERR_TO_DISABLE;
        sleep_us(HOST_POLL_US);
    }
}

/* A3: 컨트롤러를 끈다. CC.EN=1 이면 EN(과 SHN)만 지워 쓰고 RDY=0(+CFS=0) 대기. EN=0 인데 RDY 나 CFS 가 1 이면 대기만.
 *     EN=0, RDY=0, CFS=0 이면 아무것도 쓰지 않고 NV_OK. r->stage = ST_DISABLE. */
static int nvme_disable(mock_ctrl *m, uint64_t cap, nvme_report *r) {
    r->stage = ST_DISABLE;
    uint32_t cc = mmio_rd32(m, NVME_REG_CC), csts = mmio_rd32(m, NVME_REG_CSTS);
    if (cc == 0xFFFFFFFFu || csts == 0xFFFFFFFFu) return NV_ERR_GONE;
    if (cc & NVME_CC_ENABLE) mmio_wr32(m, NVME_REG_CC, cc & ~(NVME_CC_ENABLE | NVME_CC_SHN_MASK));
    else if (!(csts & (NVME_CSTS_RDY | NVME_CSTS_CFS))) return NV_OK;
    return nvme_wait_rdy(m, 0, nvme_rdy_budget_us(cap), r);
}

/* A4/B: 레지스터를 하나도 건드리기 전에 설정을 검사하고 CC 값(EN=0)을 만든다. 검사 순서:
 *   page_size: 2의 거듭제곱, 2^(12+MPSMIN) ≤ page ≤ 2^(12+MPSMAX)     → 아니면 NV_ERR_MPS
 *   CAP.CSS bit0 (NVM 명령 집합)                                      → 없으면 NV_ERR_CSS
 *   sq_depth, cq_depth ∈ [2, 4096]                                    → 아니면 NV_ERR_QSIZE
 *   asq, acq 가 page_size 정렬                                        → 아니면 NV_ERR_ALIGN
 *   *cc_out = CSS_NVM | MPS | AMS_RR | SHN_NONE | IOSQES(6) | IOCQES(4)   (실패 시 *cc_out 안 건드림) */
static int nvme_check_cfg(uint64_t cap, const nvme_admin_cfg *c, uint32_t *cc_out) {
    if (!c || !cc_out) return NV_ERR_PARAM;
    uint32_t ps = c->page_size, sh = 0;
    if (ps == 0 || (ps & (ps - 1))) return NV_ERR_MPS;
    while ((1u << sh) != ps) sh++;
    if (sh < 12 || sh - 12 < NVME_CAP_MPSMIN(cap) || sh - 12 > NVME_CAP_MPSMAX(cap)) return NV_ERR_MPS;
    if (!(NVME_CAP_CSS(cap) & NVME_CAP_CSS_NVM)) return NV_ERR_CSS;
    if (c->sq_depth < 2 || c->sq_depth > NVME_AQ_MAX_ENTRIES || c->cq_depth < 2 || c->cq_depth > NVME_AQ_MAX_ENTRIES)
        return NV_ERR_QSIZE;
    if ((c->asq & (ps - 1u)) || (c->acq & (ps - 1u))) return NV_ERR_ALIGN;
    *cc_out = NVME_CC_CSS_NVM | ((sh - 12) << NVME_CC_MPS_SHIFT) | NVME_CC_AMS_RR | NVME_CC_SHN_NONE |
              NVME_CC_IOSQES | NVME_CC_IOCQES;
    return NV_OK;
}

/* A5: 전체 시퀀스. stage 를 단계마다 갱신. CFS 를 보면 reset(CC.EN=0 쓰고 RDY=0 대기) 후 NV_ERR_CFS.
 *   ST_CAP: CAP 읽기 (all-1s → NV_ERR_GONE)  ST_CHECK: nvme_check_cfg  ST_DISABLE: nvme_disable
 *   ST_ADMINQ: AQA, ASQ lo, ASQ hi, ACQ lo, ACQ hi   ST_CONFIG: CC(EN=0)   ST_ENABLE: CC|EN, RDY=1 대기   ST_READY */
static int nvme_init(mock_ctrl *m, const nvme_admin_cfg *c, nvme_report *r) {
    memset(r, 0, sizeof *r);
    r->stage = ST_CAP;
    uint64_t cap = mmio_rd64(m, NVME_REG_CAP);
    r->cap = cap;
    if (cap == ~0ull) return NV_ERR_GONE;
    r->stage = ST_CHECK;
    uint32_t cc = 0;
    int rc = nvme_check_cfg(cap, c, &cc);
    if (rc) return rc;
    rc = nvme_disable(m, cap, r);
    if (rc) return rc;
    r->stage = ST_ADMINQ;
    mmio_wr32(m, NVME_REG_AQA, ((c->cq_depth - 1u) << 16) | (c->sq_depth - 1u));
    mmio_wr32(m, NVME_REG_ASQ, (uint32_t)c->asq);
    mmio_wr32(m, NVME_REG_ASQ + 4, (uint32_t)(c->asq >> 32));
    mmio_wr32(m, NVME_REG_ACQ, (uint32_t)c->acq);
    mmio_wr32(m, NVME_REG_ACQ + 4, (uint32_t)(c->acq >> 32));
    r->stage = ST_CONFIG;
    mmio_wr32(m, NVME_REG_CC, cc);
    r->stage = ST_ENABLE;
    mmio_wr32(m, NVME_REG_CC, cc | NVME_CC_ENABLE);
    rc = nvme_wait_rdy(m, 1, nvme_rdy_budget_us(cap), r);
    if (rc == NV_ERR_CFS) {
        nvme_report tmp = *r;
        mmio_wr32(m, NVME_REG_CC, cc);                 /* EN=0 → controller reset */
        r->reset_rc = nvme_wait_rdy(m, 0, nvme_rdy_budget_us(cap), &tmp);
        r->reset_done = 1;
        return NV_ERR_CFS;
    }
    if (rc) return rc;
    r->stage = ST_READY;
    return NV_OK;
}

/* ======================= 테스트 하네스 ======================= */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t make_cap(uint32_t mqes, uint32_t to, uint32_t css, uint32_t mpsmin, uint32_t mpsmax, uint32_t ams) {
    return (uint64_t)mqes | (1ull << 16) /* CQR */ | ((uint64_t)ams << 17) | ((uint64_t)to << 24) |
           ((uint64_t)css << 37) | ((uint64_t)mpsmin << 48) | ((uint64_t)mpsmax << 52);
}
static const char *rc_name(int rc) {
    static const char *n[] = { "OK", "ERR_PARAM", "ERR_MPS", "ERR_CSS", "ERR_QSIZE", "ERR_ALIGN",
                               "ERR_TO_DISABLE", "ERR_TO_ENABLE", "ERR_CFS", "ERR_GONE" };
    return (rc <= 0 && rc >= -9) ? n[-rc] : "?";
}
static const char *st_name(int s) {
    static const char *n[] = { "NONE", "CAP", "CHECK", "DISABLE", "ADMINQ", "CONFIG", "ENABLE", "READY" };
    return (s >= 0 && s <= 7) ? n[s] : "?";
}
static int count_of(mock_ctrl *m, const int *arr, const int *n, int v) {
    pthread_mutex_lock(&m->mu);
    int c = 0;
    for (int i = 0; i < *n; i++) c += arr[i] == v;
    pthread_mutex_unlock(&m->mu);
    return c;
}
static int n_ev(mock_ctrl *m, int e) { return count_of(m, m->ev, &m->nev, e); }
static int n_viol(mock_ctrl *m, int v) { return count_of(m, m->viol, &m->nviol, v); }
static int wait_ev(mock_ctrl *m, int e, int n) {   /* 하네스 동기화: 이벤트가 n 번 기록될 때까지 (최대 2 s) */
    for (int i = 0; i < 2000; i++) { if (n_ev(m, e) >= n) return 1; sleep_us(1000); }
    return 0;
}
static int ev_seq(mock_ctrl *m, const int *seq, int n) {   /* 이벤트 로그가 정확히 seq 인가 */
    pthread_mutex_lock(&m->mu);
    int ok = m->nev == n;
    for (int i = 0; ok && i < n; i++) ok = m->ev[i] == seq[i];
    pthread_mutex_unlock(&m->mu);
    return ok;
}
static int wlog_is(mock_ctrl *m, const uint32_t (*exp)[2], int n) {   /* 쓰기 로그가 정확히 exp 인가 */
    pthread_mutex_lock(&m->mu);
    int ok = m->nw == n;
    for (int i = 0; ok && i < n; i++) ok = m->wlog[i].off == exp[i][0] && m->wlog[i].val == exp[i][1];
    pthread_mutex_unlock(&m->mu);
    return ok;
}
static int wrote_off(mock_ctrl *m, uint32_t off) {
    pthread_mutex_lock(&m->mu);
    int c = 0;
    for (int i = 0; i < m->nw; i++) c += m->wlog[i].off == off;
    pthread_mutex_unlock(&m->mu);
    return c;
}
static int n_writes(mock_ctrl *m) { pthread_mutex_lock(&m->mu); int n = m->nw; pthread_mutex_unlock(&m->mu); return n; }
static uint32_t last_write_val(mock_ctrl *m) {
    pthread_mutex_lock(&m->mu); uint32_t v = m->nw ? m->wlog[m->nw - 1].val : 0; pthread_mutex_unlock(&m->mu); return v;
}
static void show(const char *tag, int rc, const nvme_report *r, mock_ctrl *m) {
    printf("%-28s rc=%-15s stage=%-8s polls=%-3u budget=%5u us  last CSTS=0x%08X  writes=%d\n", tag, rc_name(rc),
           st_name(r->stage), r->polls, r->budget_us, r->last_csts, n_writes(m));
}

static mock_ctrl g_m;
#define CAP_LAB  make_cap(0x3FF, 0x28, NVME_CAP_CSS_NVM, 0, 4, 0)   /* 가상 컨트롤러: MQES 1023, TO 20 s, 4K..64K */
#define CAP_FAST make_cap(0x3FF, 0x05, NVME_CAP_CSS_NVM, 0, 4, 0)   /* TO = 5 (2.5 s) — 타임아웃 테스트용 */
#define CC_ON    (0x00460000u | NVME_CC_ENABLE)

int main(void) {
    const nvme_admin_cfg good = { 0x0000000123456000ull, 0x0000000123457000ull, 32, 32, 4096 };
    nvme_report r;
    int rc;

    /* ---------------- Part A ---------------- */
    CHECK(CAP_LAB == 0x00400020280103FFull);                       /* 비트 위치 검산 (풀이 표) */
    CHECK(nvme_rdy_budget_us(CAP_LAB) == 400000u);                 /* TO 0x28 = 40 단위 = 실제 20 s, 시뮬 0.4 s */
    CHECK(nvme_rdy_budget_us(make_cap(0, 0xFF, 1, 0, 0, 0)) == 2550000u);
    CHECK(nvme_rdy_budget_us(make_cap(0xFFFF, 0, 1, 0xF, 0xF, 3)) == 0u);   /* 다른 필드가 섞이지 않는다 */

    {   /* A: CC 값 만들기 + page_size 전수 (2^0..2^31): 받아들이는 건 2^12..2^16 (MPSMIN 0, MPSMAX 4) 뿐 */
        uint32_t cc = 0xDEADBEEF;
        CHECK(nvme_check_cfg(CAP_LAB, &good, &cc) == NV_OK && cc == 0x00460000u);
        nvme_admin_cfg g16 = good; g16.page_size = 16384; g16.asq = 0x123458000ull; g16.acq = 0x12345C000ull;
        CHECK(nvme_check_cfg(CAP_LAB, &g16, &cc) == NV_OK && cc == 0x00460100u);   /* MPS=2 → bit 8 */
        int okcnt = 0, badrc = 0;
        for (unsigned s = 0; s < 32; s++) {
            nvme_admin_cfg t = good; t.page_size = 1u << s; t.asq = 1ull << 32; t.acq = 2ull << 32;
            uint32_t v = 0;
            int x = nvme_check_cfg(CAP_LAB, &t, &v);
            if (x == NV_OK) { okcnt++; if (v != (0x00460000u | ((s - 12u) << 7))) badrc++; }
            else if (x != NV_ERR_MPS) badrc++;
        }
        CHECK(okcnt == 5 && badrc == 0);
    }

    /* A: 처음 켜진(EN=0, RDY=0) 컨트롤러 → 정확한 쓰기 순서 7개, 이벤트 EN_SEEN → READY */
    mock_init(&g_m, CAP_LAB, 0, 0, F_NONE); mock_start(&g_m);
    rc = nvme_init(&g_m, &good, &r);
    show("A fresh controller", rc, &r, &g_m);
    {
        static const uint32_t exp[][2] = {
            { NVME_REG_AQA, 0x001F001Fu }, { NVME_REG_ASQ, 0x23456000u }, { NVME_REG_ASQ + 4, 0x1u },
            { NVME_REG_ACQ, 0x23457000u }, { NVME_REG_ACQ + 4, 0x1u }, { NVME_REG_CC, 0x00460000u },
            { NVME_REG_CC, CC_ON } };
        static const int evs[] = { EV_EN_SEEN, EV_READY };
        CHECK(rc == NV_OK && r.stage == ST_READY && r.cap == CAP_LAB);
        CHECK(wlog_is(&g_m, exp, 7));
        CHECK(ev_seq(&g_m, evs, 2));
        CHECK(reg_ld(&g_m, NVME_REG_CSTS) == NVME_CSTS_RDY && reg_ld(&g_m, NVME_REG_CC) == CC_ON);
        CHECK(mmio_rd64(&g_m, NVME_REG_CAP) == CAP_LAB);                 /* 64비트 = 32비트 두 번 (lo → hi) */
        CHECK(reg_ld64(&g_m, NVME_REG_ASQ) == good.asq && reg_ld64(&g_m, NVME_REG_ACQ) == good.acq);
        CHECK(r.last_csts == NVME_CSTS_RDY && r.budget_us == 400000u);
        CHECK(g_m.nviol == 0);
    }
    mock_stop(&g_m);

    /* A: 이미 켜진(EN=1, RDY=1 — UEFI/이전 드라이버가 남긴) 컨트롤러 → 먼저 EN=0, RDY=0 대기 */
    mock_init(&g_m, CAP_LAB, CC_ON, NVME_CSTS_RDY, F_NONE); mock_start(&g_m);
    rc = nvme_init(&g_m, &good, &r);
    show("A already enabled", rc, &r, &g_m);
    {
        static const int evs[] = { EV_DISABLED, EV_EN_SEEN, EV_READY };
        CHECK(rc == NV_OK && r.stage == ST_READY);
        CHECK(n_writes(&g_m) == 8 && g_m.wlog[0].off == NVME_REG_CC && g_m.wlog[0].val == 0x00460000u);
        CHECK(g_m.wlog[1].off == NVME_REG_AQA);        /* disable 이 끝난 다음에야 AQA */
        CHECK(ev_seq(&g_m, evs, 3));
        CHECK(g_m.nviol == 0);
    }
    mock_stop(&g_m);

    /* A: EN=0 인데 RDY=1 (앞선 disable 이 아직 진행 중) → CC 를 다시 쓰지 않고 기다리기만 */
    mock_init(&g_m, CAP_LAB, 0x00460000u, NVME_CSTS_RDY, F_NONE);
    g_m.seen_cc = CC_ON;                                /* 스레드가 "EN 1→0" 을 막 보게 만든다 */
    mock_start(&g_m);
    memset(&r, 0, sizeof r);
    rc = nvme_disable(&g_m, CAP_LAB, &r);
    CHECK(rc == NV_OK && n_writes(&g_m) == 0 && r.stage == ST_DISABLE);
    CHECK(n_ev(&g_m, EV_DISABLED) == 1 && reg_ld(&g_m, NVME_REG_CSTS) == 0);   /* 컨트롤러가 끄기를 끝낸 뒤에 OK */
    rc = nvme_disable(&g_m, CAP_LAB, &r);              /* 이미 꺼짐 → 쓰기 없음, 대기 없음 */
    CHECK(rc == NV_OK && n_writes(&g_m) == 0);
    mock_stop(&g_m);

    /* ---------------- Part B: 오류 주입 ---------------- */

    /* B1: RDY 가 영영 안 옴 → 정확히 NV_ERR_TO_ENABLE, 예산 = TO × 단위, 마지막 CSTS 는 RDY=0 */
    mock_init(&g_m, CAP_FAST, 0, 0, F_NO_READY); mock_start(&g_m);
    rc = nvme_init(&g_m, &good, &r);
    show("B1 RDY never comes", rc, &r, &g_m);
    {
        static const int evs[] = { EV_EN_SEEN };
        CHECK(rc == NV_ERR_TO_ENABLE && r.stage == ST_ENABLE);
        CHECK(r.budget_us == 50000u && r.polls >= 2 && r.last_csts == 0);
        CHECK(n_writes(&g_m) == 7 && wait_ev(&g_m, EV_EN_SEEN, 1) && ev_seq(&g_m, evs, 1));
    }
    mock_stop(&g_m);

    /* B2: disable 이 끝나지 않음 (RDY 가 1 에 붙음) → NV_ERR_TO_DISABLE, AQA 는 절대 안 씀 */
    mock_init(&g_m, CAP_FAST, CC_ON, NVME_CSTS_RDY, F_DISABLE_STUCK); mock_start(&g_m);
    rc = nvme_init(&g_m, &good, &r);
    show("B2 disable stuck", rc, &r, &g_m);
    CHECK(rc == NV_ERR_TO_DISABLE && r.stage == ST_DISABLE);
    CHECK(n_writes(&g_m) == 1 && wrote_off(&g_m, NVME_REG_AQA) == 0 && (r.last_csts & NVME_CSTS_RDY));
    CHECK(g_m.nviol == 0);
    mock_stop(&g_m);

    /* B3: EN 뒤 컨트롤러가 CFS → 호스트는 NV_ERR_CFS + reset(EN=0) 하고 RDY=0 확인, CFS 는 reset 으로 지워짐 */
    mock_init(&g_m, CAP_LAB, 0, 0, F_CFS_ON_ENABLE); mock_start(&g_m);
    rc = nvme_init(&g_m, &good, &r);
    show("B3 CFS on enable", rc, &r, &g_m);
    {
        static const int evs[] = { EV_EN_SEEN, EV_CFS, EV_DISABLED };
        CHECK(rc == NV_ERR_CFS && r.stage == ST_ENABLE && (r.last_csts & NVME_CSTS_CFS));
        CHECK(r.reset_done == 1 && r.reset_rc == NV_OK);
        CHECK(n_writes(&g_m) == 8 && last_write_val(&g_m) == 0x00460000u);   /* 마지막 쓰기 = CC, EN=0 */
        CHECK(ev_seq(&g_m, evs, 3) && reg_ld(&g_m, NVME_REG_CSTS) == 0);
    }
    mock_stop(&g_m);

    /* B4: 켜질 때부터 치명 상태(EN=1, RDY=1, CFS=1) → init 의 disable 이 곧 복구. 결과 OK */
    mock_init(&g_m, CAP_LAB, CC_ON, NVME_CSTS_RDY | NVME_CSTS_CFS, F_NONE); mock_start(&g_m);
    rc = nvme_init(&g_m, &good, &r);
    show("B4 CFS already set", rc, &r, &g_m);
    {
        static const int evs[] = { EV_DISABLED, EV_EN_SEEN, EV_READY };
        CHECK(rc == NV_OK && ev_seq(&g_m, evs, 3) && reg_ld(&g_m, NVME_REG_CSTS) == NVME_CSTS_RDY);
    }
    mock_stop(&g_m);

    /* B5: 설정 오류는 레지스터를 하나도 건드리기 전에 거절 (쓰기 0, stage = CHECK, CC 그대로) */
    {
        struct { const char *name; uint64_t cap; nvme_admin_cfg c; int want; } cs[] = {
            { "page 64K > MPSMAX 16K", make_cap(0x3FF, 0x28, 1, 0, 2, 0), { 0x100000000ull, 0x100010000ull, 32, 32, 65536 }, NV_ERR_MPS },
            { "page 2K < 4K",          CAP_LAB, { 0x123456000ull, 0x123457000ull, 32, 32, 2048 },  NV_ERR_MPS },
            { "page 6144 not pow2",    CAP_LAB, { 0x123456000ull, 0x123457000ull, 32, 32, 6144 },  NV_ERR_MPS },
            { "page 0",                CAP_LAB, { 0x123456000ull, 0x123457000ull, 32, 32, 0 },     NV_ERR_MPS },
            { "page 4K < MPSMIN 8K",   make_cap(0x3FF, 0x28, 1, 1, 4, 0), good,                NV_ERR_MPS },
            { "CAP.CSS no NVM",        make_cap(0x3FF, 0x28, 0x40, 0, 4, 0), good,                NV_ERR_CSS },
            { "sq depth 1",            CAP_LAB, { 0x123456000ull, 0x123457000ull, 1, 32, 4096 },   NV_ERR_QSIZE },
            { "cq depth 4097",         CAP_LAB, { 0x123456000ull, 0x123457000ull, 32, 4097, 4096 }, NV_ERR_QSIZE },
            { "cq depth 0",            CAP_LAB, { 0x123456000ull, 0x123457000ull, 32, 0, 4096 },   NV_ERR_QSIZE },
            { "ASQ +0x800",            CAP_LAB, { 0x123456800ull, 0x123457000ull, 32, 32, 4096 },  NV_ERR_ALIGN },
            { "ACQ 4K-aligned, page 16K", CAP_LAB, { 0x123458000ull, 0x123457000ull, 32, 32, 16384 }, NV_ERR_ALIGN },
        };
        int n = (int)(sizeof cs / sizeof cs[0]), bad = 0;
        for (int i = 0; i < n; i++) {
            mock_init(&g_m, cs[i].cap, CC_ON, NVME_CSTS_RDY, F_NONE); mock_start(&g_m);
            rc = nvme_init(&g_m, &cs[i].c, &r);
            int ok = rc == cs[i].want && r.stage == ST_CHECK && n_writes(&g_m) == 0 && reg_ld(&g_m, NVME_REG_CC) == CC_ON;
            if (!ok) { bad++; printf("  B5 %-26s rc=%s stage=%s writes=%d\n", cs[i].name, rc_name(rc), st_name(r.stage), n_writes(&g_m)); }
            mock_stop(&g_m);
        }
        printf("B5 config rejections: %d cases, bad=%d\n", n, bad);
        CHECK(bad == 0);
    }

    /* B6: 경계 안쪽 값은 통과 — depth 2 / 4096 → AQA 0x00010001 / 0x0FFF0FFF */
    {
        nvme_admin_cfg c2 = good; c2.sq_depth = 2; c2.cq_depth = 4096;
        mock_init(&g_m, CAP_LAB, 0, 0, F_NONE); mock_start(&g_m);
        rc = nvme_init(&g_m, &c2, &r);
        CHECK(rc == NV_OK && reg_ld(&g_m, NVME_REG_AQA) == 0x0FFF0001u);
        mock_stop(&g_m);
    }

    /* B7: 장치가 사라짐 (CAP 이 all-1s) → NV_ERR_GONE at ST_CAP, 쓰기 0 */
    mock_init(&g_m, CAP_LAB, 0, 0, F_NONE); atomic_store(&g_m.gone, 1); mock_start(&g_m);
    rc = nvme_init(&g_m, &good, &r);
    CHECK(rc == NV_ERR_GONE && r.stage == ST_CAP && n_writes(&g_m) == 0);
    mock_stop(&g_m);

    /* B8: EN=1 직후 링크가 죽음 → RDY 대기 중 CSTS = 0xFFFFFFFF → NV_ERR_GONE (타임아웃까지 기다리지 않는다) */
    mock_init(&g_m, CAP_LAB, 0, 0, F_GONE_ON_ENABLE); mock_start(&g_m);
    rc = nvme_init(&g_m, &good, &r);
    show("B8 surprise removal", rc, &r, &g_m);
    CHECK(rc == NV_ERR_GONE && r.stage == ST_ENABLE && r.last_csts == 0xFFFFFFFFu && n_ev(&g_m, EV_GONE) == 1);
    mock_stop(&g_m);

    /* B9: 예산 0 이어도 정확히 한 번은 읽고 판정 */
    mock_init(&g_m, CAP_LAB, 0, 0, F_NONE); mock_start(&g_m);
    rc = nvme_wait_rdy(&g_m, 0, 0, &r);
    CHECK(rc == NV_OK && r.polls == 1);
    rc = nvme_wait_rdy(&g_m, 1, 0, &r);
    CHECK(rc == NV_ERR_TO_ENABLE && r.polls == 1 && r.budget_us == 0);
    mock_stop(&g_m);

    /* B10: 버그 있는 드라이버 순서는 mock 이 위반으로 기록한다 (정상 init 은 위의 모든 경우에서 0건) */
    mock_init(&g_m, CAP_LAB, CC_ON, NVME_CSTS_RDY, F_DISABLE_STUCK); mock_start(&g_m);
    mmio_wr32(&g_m, NVME_REG_AQA, 0x001F001Fu);                         /* EN=1 인데 AQA */
    mmio_wr32(&g_m, NVME_REG_ASQ + 4, 0x2u);                            /* EN=1 인데 ASQ hi */
    mmio_wr32(&g_m, NVME_REG_CC, CC_ON | (2u << NVME_CC_MPS_SHIFT));    /* EN=1 인데 MPS 바꿈 */
    mmio_wr32(&g_m, NVME_REG_CSTS, 0x0u);                               /* RO 레지스터 쓰기 → 무시 */
    CHECK(reg_ld(&g_m, NVME_REG_CSTS) == NVME_CSTS_RDY);                /* RO 쓰기는 반영 안 됨 */
    mmio_wr32(&g_m, NVME_REG_CC, 0x00460000u);                          /* EN 1→0 ... */
    mmio_wr32(&g_m, NVME_REG_CC, CC_ON);                                /* ... RDY=0 기다리지 않고 바로 EN=1 */
    CHECK(n_viol(&g_m, V_ADMINQ_WHILE_EN) == 2 && n_viol(&g_m, V_CC_CFG_WHILE_EN) == 1);
    CHECK(n_viol(&g_m, V_RO_WRITE) == 1 && n_viol(&g_m, V_EN_WHILE_RDY) == 1 && g_m.nviol == 5);
    mock_stop(&g_m);

    /* ---------------- Part C: 컨트롤러(SSD FW) 쪽 ---------------- */
    {   /* C1: fw_validate_enable 단위 테스트 (스레드 없이 레지스터만 세팅) */
        struct { const char *name; uint64_t cap; uint32_t cc, aqa; uint64_t asq, acq; int want; } vs[] = {
            { "good",               CAP_LAB, 0x00460000u, 0x001F001Fu, 0x123456000ull, 0x123457000ull, BAD_NONE },
            { "ASQS=0 (1 entry)",   CAP_LAB, 0x00460000u, 0x001F0000u, 0x123456000ull, 0x123457000ull, BAD_AQA },
            { "ACQS=0 (1 entry)",   CAP_LAB, 0x00460000u, 0x0000001Fu, 0x123456000ull, 0x123457000ull, BAD_AQA },
            { "ASQ=0",              CAP_LAB, 0x00460000u, 0x001F001Fu, 0x0ull,         0x123457000ull, BAD_ASQ },
            { "ASQ 4K but MPS=16K", CAP_LAB, 0x00460100u, 0x001F001Fu, 0x123456000ull, 0x123458000ull, BAD_ASQ },
            { "ACQ +0x40",          CAP_LAB, 0x00460000u, 0x001F001Fu, 0x123456000ull, 0x123457040ull, BAD_ACQ },
            { "MPS=5 > MPSMAX 4",   CAP_LAB, 0x00460280u, 0x001F001Fu, 0x100000000ull, 0x200000000ull, BAD_MPS },
            { "CSS=6 (CSI)",        CAP_LAB, 0x00460060u, 0x001F001Fu, 0x123456000ull, 0x123457000ull, BAD_CSS },
            { "CAP.CSS no NVM",     make_cap(0x3FF, 0x28, 0x40, 0, 4, 0), 0x00460000u, 0x001F001Fu, 0x123456000ull, 0x123457000ull, BAD_CSS },
            { "AMS=WRR, CAP no",    CAP_LAB, 0x00460800u, 0x001F001Fu, 0x123456000ull, 0x123457000ull, BAD_AMS },
            { "AMS=WRR, CAP yes",   make_cap(0x3FF, 0x28, 1, 0, 4, 1), 0x00460800u, 0x001F001Fu, 0x123456000ull, 0x123457000ull, BAD_NONE },
            { "AMS=2 reserved",     make_cap(0x3FF, 0x28, 1, 0, 4, 3), 0x00461000u, 0x001F001Fu, 0x123456000ull, 0x123457000ull, BAD_AMS },
            { "AMS=VS, CAP bit1",   make_cap(0x3FF, 0x28, 1, 0, 4, 2), 0x00463800u, 0x001F001Fu, 0x123456000ull, 0x123457000ull, BAD_NONE },
        };
        int n = (int)(sizeof vs / sizeof vs[0]), bad = 0;
        for (int i = 0; i < n; i++) {
            mock_init(&g_m, vs[i].cap, vs[i].cc, 0, F_NONE);
            atomic_store(&g_m.reg[NVME_REG_AQA / 4], vs[i].aqa);
            atomic_store(&g_m.reg[NVME_REG_ASQ / 4], (uint32_t)vs[i].asq);
            atomic_store(&g_m.reg[NVME_REG_ASQ / 4 + 1], (uint32_t)(vs[i].asq >> 32));
            atomic_store(&g_m.reg[NVME_REG_ACQ / 4], (uint32_t)vs[i].acq);
            atomic_store(&g_m.reg[NVME_REG_ACQ / 4 + 1], (uint32_t)(vs[i].acq >> 32));
            int got = fw_validate_enable(&g_m);
            if (got != vs[i].want) { bad++; printf("  C1 %-20s got %d want %d\n", vs[i].name, got, vs[i].want); }
            pthread_mutex_destroy(&g_m.mu);
        }
        printf("C1 fw_validate_enable: %d cases, bad=%d\n", n, bad);
        CHECK(bad == 0);
    }

    /* C2: 버그 있는 bring-up 드라이버 (ACQ 를 안 씀) → 컨트롤러가 CFS + 이유 BAD_ACQ.
     *     같은 장치에 정상 nvme_init → disable 이 CFS 를 지우고 정상 기동 */
    mock_init(&g_m, CAP_LAB, 0, 0, F_NONE); mock_start(&g_m);
    mmio_wr32(&g_m, NVME_REG_AQA, 0x001F001Fu);
    mmio_wr32(&g_m, NVME_REG_ASQ, 0x23456000u);
    mmio_wr32(&g_m, NVME_REG_ASQ + 4, 0x1u);
    mmio_wr32(&g_m, NVME_REG_CC, 0x00460000u);
    mmio_wr32(&g_m, NVME_REG_CC, CC_ON);
    rc = nvme_wait_rdy(&g_m, 1, nvme_rdy_budget_us(CAP_LAB), &r);
    printf("C2 buggy driver (no ACQ): rc=%s CSTS=0x%08X bad_reason=%d\n", rc_name(rc), r.last_csts, atomic_load(&g_m.bad_reason));
    CHECK(rc == NV_ERR_CFS && r.last_csts == NVME_CSTS_CFS && atomic_load(&g_m.bad_reason) == BAD_ACQ);
    CHECK(n_ev(&g_m, EV_BAD_CFG) == 1 && g_m.nviol == 0);
    rc = nvme_init(&g_m, &good, &r);
    show("C2 then correct init", rc, &r, &g_m);
    CHECK(rc == NV_OK && reg_ld(&g_m, NVME_REG_CSTS) == NVME_CSTS_RDY && atomic_load(&g_m.bad_reason) == BAD_NONE);
    CHECK(n_ev(&g_m, EV_DISABLED) == 1 && n_ev(&g_m, EV_READY) == 1 && g_m.nviol == 0);
    mock_stop(&g_m);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
