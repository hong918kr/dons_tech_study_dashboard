/* PCIE-B13 추가 연습 — 한 번에 검증
 *
 * 본문 solution.c 의 mock 컨트롤러 + 호스트 init 을 그대로 담고(자체 완결), 그 위에:
 *   연습 1 normal shutdown (CC.SHN=01 → CSTS.SHST=10) 과 shutdown 뒤 재-init
 *   연습 2 abrupt shutdown (SHN=10), SHST 가 안 끝날 때 타임아웃, EN=0 이면 쓰기 없음
 *   연습 3 CFS 면 다시 시도하는 nvme_init_retry (CFS 한 번 / 매번 / 재시도하면 안 되는 오류)
 *   연습 4 함정 재현 — RDY 비트만 보는 "RDY=0 대기" 는 (EN=1, RDY=0, CFS=1) 에서 reset 완료를 못 기다린다
 *   연습 5 CSTS 값 분류 전수, I/O 큐 깊이(MQES 0-based), CAP.TO 실제 시간
 * 시간 축척은 본문과 같다 (CAP.TO 1 단위 = 시뮬 10 ms).
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 *       cc -std=c11 -g -O1 -fsanitize=thread practice.c -o pr_tsan && ./pr_tsan
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
static int n_writes(mock_ctrl *m) { pthread_mutex_lock(&m->mu); int n = m->nw; pthread_mutex_unlock(&m->mu); return n; }
static uint32_t last_write_val(mock_ctrl *m) {
    pthread_mutex_lock(&m->mu); uint32_t v = m->nw ? m->wlog[m->nw - 1].val : 0; pthread_mutex_unlock(&m->mu); return v;
}

/* ======================= 추가 연습 ======================= */

#define NV_ERR_TO_SHN (-10)
#define NVME_CC_SHN_NORMAL (1u << NVME_CC_SHN_SHIFT)
#define NVME_CC_SHN_ABRUPT (2u << NVME_CC_SHN_SHIFT)

/* 연습 1·2: shutdown 통지. shn = 1(normal) 또는 2(abrupt). EN=0 이면 할 일 없음(쓰기 없이 OK).
 * CC 의 SHN 만 바꿔 쓰고(EN 유지) CSTS.SHST == 10b(complete) 까지 budget_us 안에서 폴링. */
static int nvme_shutdown(mock_ctrl *m, uint32_t shn, uint32_t budget_us, nvme_report *r) {
    if (shn != 1 && shn != 2) return NV_ERR_PARAM;
    uint32_t cc = mmio_rd32(m, NVME_REG_CC);
    if (cc == 0xFFFFFFFFu) return NV_ERR_GONE;
    if (!(cc & NVME_CC_ENABLE)) return NV_OK;
    mmio_wr32(m, NVME_REG_CC, (cc & ~NVME_CC_SHN_MASK) | (shn << NVME_CC_SHN_SHIFT));
    uint64_t deadline = now_us() + budget_us;
    r->budget_us = budget_us; r->polls = 0;
    for (;;) {
        int expired = now_us() >= deadline;
        uint32_t csts = mmio_rd32(m, NVME_REG_CSTS);
        r->polls++; r->last_csts = csts;
        if (csts == 0xFFFFFFFFu) return NV_ERR_GONE;
        if ((csts & NVME_CSTS_SHST_MASK) == NVME_CSTS_SHST_CMPLT) return NV_OK;
        if (expired) return NV_ERR_TO_SHN;
        sleep_us(HOST_POLL_US);
    }
}

/* 연습 3: CFS 일 때만 다시 시도 (nvme_init 이 이미 reset 을 해 둔다). 다른 오류는 즉시 반환. */
static int nvme_init_retry(mock_ctrl *m, const nvme_admin_cfg *c, int max_tries, int *tries, nvme_report *r) {
    int rc = NV_ERR_PARAM;
    *tries = 0;
    while (*tries < max_tries) {
        (*tries)++;
        rc = nvme_init(m, c, r);
        if (rc != NV_ERR_CFS) break;
    }
    return rc;
}

/* 연습 4: 함정 재현용 — RDY 비트만 보는 "RDY=0 대기" (CFS 를 보지 않는다) */
static int wait_rdy0_only(mock_ctrl *m, uint32_t budget_us, nvme_report *r) {
    uint64_t deadline = now_us() + budget_us;
    r->polls = 0;
    for (;;) {
        int expired = now_us() >= deadline;
        uint32_t csts = mmio_rd32(m, NVME_REG_CSTS);
        r->polls++; r->last_csts = csts;
        if (!(csts & NVME_CSTS_RDY)) return NV_OK;
        if (expired) return NV_ERR_TO_DISABLE;
        sleep_us(HOST_POLL_US);
    }
}

/* 연습 5: CSTS 한 값 분류 (우선순위: all-1s > CFS > SHST > RDY) + 숫자 감각 */
enum { CS_GONE, CS_FATAL, CS_SHN_RSVD, CS_SHN_DONE, CS_SHN_BUSY, CS_READY, CS_NOT_READY };
static int csts_classify(uint32_t v) {
    if (v == 0xFFFFFFFFu) return CS_GONE;
    if (v & NVME_CSTS_CFS) return CS_FATAL;
    switch (v & NVME_CSTS_SHST_MASK) {
    case NVME_CSTS_SHST_MASK:  return CS_SHN_RSVD;      /* 11b reserved */
    case NVME_CSTS_SHST_CMPLT: return CS_SHN_DONE;
    case NVME_CSTS_SHST_OCCUR: return CS_SHN_BUSY;
    default: break;
    }
    return (v & NVME_CSTS_RDY) ? CS_READY : CS_NOT_READY;
}
static uint32_t io_queue_depth(uint64_t cap, uint32_t want) {   /* MQES 는 0-based: 최대 엔트리 = MQES + 1 */
    uint32_t max = (uint32_t)NVME_CAP_MQES(cap) + 1u;
    if (want > max) want = max;
    return want < 2u ? 2u : want;
}
static uint32_t cap_to_real_ms(uint64_t cap) { return (uint32_t)NVME_CAP_TIMEOUT(cap) * 500u; }

static mock_ctrl g_m;
#define CAP_LAB  make_cap(0x3FF, 0x28, NVME_CAP_CSS_NVM, 0, 4, 0)
#define CC_ON    (0x00460000u | NVME_CC_ENABLE)

int main(void) {
    const nvme_admin_cfg good = { 0x0000000123456000ull, 0x0000000123457000ull, 32, 32, 4096 };
    nvme_report r;
    int rc, tries;

    /* 연습 1: normal shutdown → SHST 01 → 10, CC = 0x00464001. 그다음 init 으로 재사용 가능 */
    mock_init(&g_m, CAP_LAB, 0, 0, F_NONE); mock_start(&g_m);
    CHECK(nvme_init(&g_m, &good, &r) == NV_OK);
    rc = nvme_shutdown(&g_m, 1, 400000u, &r);
    printf("P1 normal shutdown: rc=%s polls=%u CSTS=0x%08X last CC write=0x%08X\n", rc_name(rc), r.polls, r.last_csts, last_write_val(&g_m));
    {
        static const int evs[] = { EV_EN_SEEN, EV_READY, EV_SHN_START, EV_SHN_DONE };
        CHECK(rc == NV_OK && last_write_val(&g_m) == 0x00464001u);
        CHECK(reg_ld(&g_m, NVME_REG_CSTS) == (NVME_CSTS_SHST_CMPLT | NVME_CSTS_RDY) && ev_seq(&g_m, evs, 4));
    }
    rc = nvme_init(&g_m, &good, &r);                       /* EN 1→0 이 SHST 를 지우고 다시 켠다 */
    CHECK(rc == NV_OK && reg_ld(&g_m, NVME_REG_CSTS) == NVME_CSTS_RDY && g_m.nviol == 0);
    CHECK(nvme_shutdown(&g_m, 3, 400000u, &r) == NV_ERR_PARAM);
    mock_stop(&g_m);

    /* 연습 2: abrupt shutdown → CC = 0x00468001 / SHST 가 안 끝나면 NV_ERR_TO_SHN (SHST=01 에서 멈춤) / EN=0 이면 쓰기 없음 */
    mock_init(&g_m, CAP_LAB, 0, 0, F_NONE); mock_start(&g_m);
    CHECK(nvme_init(&g_m, &good, &r) == NV_OK);
    rc = nvme_shutdown(&g_m, 2, 400000u, &r);
    CHECK(rc == NV_OK && last_write_val(&g_m) == 0x00468001u);
    mock_stop(&g_m);
    mock_init(&g_m, CAP_LAB, 0, 0, F_SHN_STUCK); mock_start(&g_m);
    CHECK(nvme_init(&g_m, &good, &r) == NV_OK);
    rc = nvme_shutdown(&g_m, 1, 50000u, &r);
    printf("P2 shutdown stuck:  rc=%s polls=%u CSTS=0x%08X\n", rc == NV_ERR_TO_SHN ? "ERR_TO_SHN" : rc_name(rc), r.polls, r.last_csts);
    CHECK(rc == NV_ERR_TO_SHN && r.polls >= 2 && wait_ev(&g_m, EV_SHN_START, 1));
    CHECK(reg_ld(&g_m, NVME_REG_CSTS) == (NVME_CSTS_SHST_OCCUR | NVME_CSTS_RDY));   /* SHST=01 에서 멈춤 */
    mock_stop(&g_m);
    mock_init(&g_m, CAP_LAB, 0, 0, F_NONE); mock_start(&g_m);
    CHECK(nvme_shutdown(&g_m, 1, 3000u, &r) == NV_OK && n_writes(&g_m) == 0);
    mock_stop(&g_m);

    /* 연습 3: CFS 한 번 → 두 번째 시도에 성공 / 매번 CFS → max_tries 에서 포기 / MPS 오류는 재시도 안 함 */
    mock_init(&g_m, CAP_LAB, 0, 0, F_CFS_ONCE); mock_start(&g_m);
    rc = nvme_init_retry(&g_m, &good, 3, &tries, &r);
    CHECK(rc == NV_OK && tries == 2 && n_ev(&g_m, EV_CFS) == 1 && n_ev(&g_m, EV_READY) == 1);
    mock_stop(&g_m);
    mock_init(&g_m, CAP_LAB, 0, 0, F_CFS_ON_ENABLE); mock_start(&g_m);
    rc = nvme_init_retry(&g_m, &good, 3, &tries, &r);
    printf("P3 always CFS: rc=%s stage=%s tries=%d EV_CFS=%d EV_DISABLED=%d\n", rc_name(rc), st_name(r.stage), tries, n_ev(&g_m, EV_CFS), n_ev(&g_m, EV_DISABLED));
    CHECK(rc == NV_ERR_CFS && tries == 3 && n_ev(&g_m, EV_CFS) == 3 && r.reset_done == 1);
    mock_stop(&g_m);
    {
        nvme_admin_cfg bad = good; bad.page_size = 1u << 20;
        mock_init(&g_m, CAP_LAB, 0, 0, F_NONE); mock_start(&g_m);
        CHECK(nvme_init_retry(&g_m, &bad, 3, &tries, &r) == NV_ERR_MPS && tries == 1 && n_writes(&g_m) == 0);
        mock_stop(&g_m);
    }

    /* 연습 4: (EN=1, RDY=0, CFS=1) 컨트롤러 — RDY 만 보는 disable 은 reset 완료를 못 기다린다 */
    mock_init(&g_m, CAP_LAB, CC_ON, NVME_CSTS_CFS, F_NONE); g_m.disable_delay_us = 100000; mock_start(&g_m);
    mmio_wr32(&g_m, NVME_REG_CC, 0x00460000u);
    rc = wait_rdy0_only(&g_m, nvme_rdy_budget_us(CAP_LAB), &r);
    int polls0 = (int)r.polls;
    mmio_wr32(&g_m, NVME_REG_AQA, 0x001F001Fu);
    mmio_wr32(&g_m, NVME_REG_ASQ, 0x23456000u); mmio_wr32(&g_m, NVME_REG_ASQ + 4, 1u);
    mmio_wr32(&g_m, NVME_REG_ACQ, 0x23457000u); mmio_wr32(&g_m, NVME_REG_ACQ + 4, 1u);
    mmio_wr32(&g_m, NVME_REG_CC, 0x00460000u);
    mmio_wr32(&g_m, NVME_REG_CC, CC_ON);
    int rc_buggy = nvme_wait_rdy(&g_m, 1, nvme_rdy_budget_us(CAP_LAB), &r);
    printf("P4 RDY-only disable: wait0 rc=%s polls=%d -> enable rc=%s CSTS=0x%08X\n", rc_name(rc), polls0, rc_name(rc_buggy), r.last_csts);
    CHECK(rc == NV_OK && polls0 == 1 && rc_buggy == NV_ERR_CFS);
    mock_stop(&g_m);
    mock_init(&g_m, CAP_LAB, CC_ON, NVME_CSTS_CFS, F_NONE); g_m.disable_delay_us = 100000; mock_start(&g_m);
    rc = nvme_init(&g_m, &good, &r);
    printf("P4 fixed (RDY=0 && CFS=0): rc=%s EV_DISABLED=%d\n", rc_name(rc), n_ev(&g_m, EV_DISABLED));
    CHECK(rc == NV_OK && n_ev(&g_m, EV_DISABLED) == 1 && reg_ld(&g_m, NVME_REG_CSTS) == NVME_CSTS_RDY);
    mock_stop(&g_m);

    /* 연습 5: CSTS 분류 전수 (하위 6비트 64개 + all-1s), 큐 깊이, CAP.TO 실제 시간 */
    {
        int cnt[7] = { 0 };
        for (uint32_t v = 0; v < 64; v++) cnt[csts_classify(v)]++;
        cnt[csts_classify(0xFFFFFFFFu)]++;
        printf("P5 CSTS classes: GONE=%d FATAL=%d SHN_RSVD=%d SHN_DONE=%d SHN_BUSY=%d READY=%d NOT_READY=%d\n",
               cnt[0], cnt[1], cnt[2], cnt[3], cnt[4], cnt[5], cnt[6]);
        CHECK(cnt[CS_GONE] == 1 && cnt[CS_FATAL] == 32 && cnt[CS_SHN_RSVD] == 8 && cnt[CS_SHN_DONE] == 8);
        CHECK(cnt[CS_SHN_BUSY] == 8 && cnt[CS_READY] == 4 && cnt[CS_NOT_READY] == 4);
        CHECK(csts_classify(0x9) == CS_SHN_DONE && csts_classify(0x3) == CS_FATAL && csts_classify(0x21) == CS_READY);
        CHECK(io_queue_depth(CAP_LAB, 4096) == 1024 && io_queue_depth(CAP_LAB, 1) == 2 && io_queue_depth(CAP_LAB, 256) == 256);
        CHECK(io_queue_depth(make_cap(0xFFFF, 1, 1, 0, 0, 0), 70000) == 65536);
        CHECK(cap_to_real_ms(make_cap(0, 1, 1, 0, 0, 0)) == 500 && cap_to_real_ms(CAP_LAB) == 20000);
        CHECK(cap_to_real_ms(make_cap(0, 0xFF, 1, 0, 0, 0)) == 127500);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
