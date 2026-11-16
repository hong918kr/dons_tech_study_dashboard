/* PCIE-B12 NVMe 컨트롤러 레지스터: CAP/VS/CC/CSTS/AQA/ASQ/ACQ
 *
 * NVMe 컨트롤러의 BAR0 (MLBAR/MUBAR, 64-bit 메모리 BAR) 앞 4 KiB 는 "컨트롤러 레지스터", 0x1000 부터는 도어벨.
 * 오프셋·비트 위치는 Linux include/linux/nvme.h (7.0 헤더, NVME_REG_* / NVME_CAP_* / NVME_CC_* / NVME_CSTS_*) 기준.
 * 헤더에 없는 비트(CAP.CQR/AMS/BPS/PMRS, AQA 필드, 0x58..0x64, 0xE14..)는 NVMe Base Spec 2.x 기억값 — md 의 "스펙 확인 권장" 표 참고.
 *
 * Part A: A1 레지스터 맵 구조체 (+ _Static_assert 로 오프셋 고정), A2 도어벨 주소, A3 CAP/VS/CC/CSTS/AQA 디코더
 * Part B: B1 64-bit 레지스터를 32-bit 두 번으로 읽기 (lo→hi, hi-lo-hi 재시도), B2 불가능한 CAP 거르기,
 *         B3 (CC, CSTS) → 컨트롤러 상태 분류 (all-1s = 장치 없음 먼저)
 * Part C: C1 BAR0 덤프(바이트 배열) → 프로브 리포트 문자열 + 상태 판정 (실제 QEMU 캡처 2개, 가상 SSD 덤프 7개)
 *
 * 레지스터 이미지는 little-endian 바이트 배열 (PCIe/NVMe 레지스터는 LE). 읽기는 바이트 조립, 포인터 캐스트 안 함.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- BAR0 오프셋: Linux include/linux/nvme.h enum 과 같은 이름·값 ---- */
#define NVME_REG_CAP     0x0000u  /* Controller Capabilities (64) */
#define NVME_REG_VS      0x0008u  /* Version */
#define NVME_REG_INTMS   0x000Cu  /* Interrupt Mask Set */
#define NVME_REG_INTMC   0x0010u  /* Interrupt Mask Clear */
#define NVME_REG_CC      0x0014u  /* Controller Configuration */
#define NVME_REG_CSTS    0x001Cu  /* Controller Status */
#define NVME_REG_NSSR    0x0020u  /* NVM Subsystem Reset */
#define NVME_REG_AQA     0x0024u  /* Admin Queue Attributes */
#define NVME_REG_ASQ     0x0028u  /* Admin SQ Base Address (64) */
#define NVME_REG_ACQ     0x0030u  /* Admin CQ Base Address (64) */
#define NVME_REG_CMBLOC  0x0038u
#define NVME_REG_CMBSZ   0x003Cu
#define NVME_REG_BPINFO  0x0040u
#define NVME_REG_BPRSEL  0x0044u
#define NVME_REG_BPMBL   0x0048u  /* (64) */
#define NVME_REG_CMBMSC  0x0050u  /* (64) */
#define NVME_REG_CRTO    0x0068u
#define NVME_REG_PMRCAP  0x0E00u
#define NVME_REG_PMRCTL  0x0E04u
#define NVME_REG_PMRSTS  0x0E08u
#define NVME_REG_PMREBS  0x0E0Cu
#define NVME_REG_PMRSWTP 0x0E10u
#define NVME_REG_DBS     0x1000u  /* SQ 0 Tail Doorbell */

/* CC / CSTS / CAP 비트 (nvme.h) */
#define NVME_CC_ENABLE        (1u << 0)
#define NVME_CC_CSS_SHIFT     4
#define NVME_CC_MPS_SHIFT     7
#define NVME_CC_AMS_SHIFT     11
#define NVME_CC_SHN_SHIFT     14
#define NVME_CC_IOSQES_SHIFT  16
#define NVME_CC_IOCQES_SHIFT  20
#define NVME_CC_CRIME         (1u << 24)
#define NVME_CSTS_RDY         (1u << 0)
#define NVME_CSTS_CFS         (1u << 1)
#define NVME_CSTS_SHST_SHIFT  2
#define NVME_CSTS_SHST_MASK   (3u << 2)
#define NVME_CSTS_NSSRO       (1u << 4)
#define NVME_CSTS_PP          (1u << 5)
#define NVME_CAP_CSS_NVM      (1u << 0)
#define NVME_CAP_CSS_CSI      (1u << 6)
#define NVME_CAP_CRMS_CRWMS   (1ull << 59)
#define NVME_CAP_CRMS_CRIMS   (1ull << 60)

/* ---------------- Part A1: 레지스터 맵 구조체 ---------------- */

/* 실제 드라이버라면 volatile nvme_bar_regs *r = ioremap(BAR0) 로 겹쳐 쓰지만, 64-bit 필드를 r->cap 으로 읽으면
 * 컴파일러가 64-bit 로드 한 번을 낸다 — Linux 는 lo_hi_readq() 로 32-bit 두 번 (Part B1). */
typedef struct {
    uint64_t cap;                      /* 0x00 */
    uint32_t vs;                       /* 0x08 */
    uint32_t intms;                    /* 0x0C */
    uint32_t intmc;                    /* 0x10 */
    uint32_t cc;                       /* 0x14 */
                                       /* TODO A1: 0x18 — CC(0x14) 와 CSTS(0x1C) 사이에 무엇이 있나? */
    uint32_t csts;                     /* 0x1C */
    uint32_t nssr;                     /* 0x20 */
    uint32_t aqa;                      /* 0x24 */
    uint64_t asq;                      /* 0x28 */
    uint64_t acq;                      /* 0x30 */
    uint32_t cmbloc;                   /* 0x38 */
    uint32_t cmbsz;                    /* 0x3C */
    uint32_t bpinfo;                   /* 0x40 */
    uint32_t bprsel;                   /* 0x44 */
    uint64_t bpmbl;                    /* 0x48 */
    uint64_t cmbmsc;                   /* 0x50 */
    uint32_t cmbsts;                   /* 0x58 */
    uint32_t cmbebs;                   /* 0x5C */
    uint32_t cmbswtp;                  /* 0x60 */
    uint32_t nssd;                     /* 0x64 */
    uint32_t crto;                     /* 0x68 */
    uint8_t  rsvd6c[1];                /* TODO A1: 0x6C..0xDFF 예약 — 크기를 계산하라 */
    uint32_t pmrcap;                   /* 0xE00 */
    uint32_t pmrctl;                   /* 0xE04 */
    uint32_t pmrsts;                   /* 0xE08 */
    uint32_t pmrebs;                   /* 0xE0C */
    uint32_t pmrswtp;                  /* 0xE10 */
    uint32_t pmrmscl;                  /* 0xE14 */
    uint32_t pmrmscu;                  /* 0xE18 */
    uint8_t  rsvde1c[0x1000 - 0xE1C];  /* 0xE1C..0xFFF (명령 집합별/벤더 영역 포함) */
} nvme_bar_regs;                       /* 정확히 4 KiB. 도어벨(0x1000~)은 간격이 CAP.DSTRD 로 바뀌므로 구조체 밖 */

/* TODO A1: 예약 필드 두 곳을 맞추고 1 로 바꿔 컴파일 타임 검사를 켠다 */
#define NVME_LAYOUT_CHECKED 0
#if NVME_LAYOUT_CHECKED
_Static_assert(offsetof(nvme_bar_regs, cap)    == NVME_REG_CAP,    "CAP");
_Static_assert(offsetof(nvme_bar_regs, vs)     == NVME_REG_VS,     "VS");
_Static_assert(offsetof(nvme_bar_regs, intms)  == NVME_REG_INTMS,  "INTMS");
_Static_assert(offsetof(nvme_bar_regs, intmc)  == NVME_REG_INTMC,  "INTMC");
_Static_assert(offsetof(nvme_bar_regs, cc)     == NVME_REG_CC,     "CC");
_Static_assert(offsetof(nvme_bar_regs, csts)   == NVME_REG_CSTS,   "CSTS");
_Static_assert(offsetof(nvme_bar_regs, nssr)   == NVME_REG_NSSR,   "NSSR");
_Static_assert(offsetof(nvme_bar_regs, aqa)    == NVME_REG_AQA,    "AQA");
_Static_assert(offsetof(nvme_bar_regs, asq)    == NVME_REG_ASQ,    "ASQ");
_Static_assert(offsetof(nvme_bar_regs, acq)    == NVME_REG_ACQ,    "ACQ");
_Static_assert(offsetof(nvme_bar_regs, cmbloc) == NVME_REG_CMBLOC, "CMBLOC");
_Static_assert(offsetof(nvme_bar_regs, cmbsz)  == NVME_REG_CMBSZ,  "CMBSZ");
_Static_assert(offsetof(nvme_bar_regs, bpinfo) == NVME_REG_BPINFO, "BPINFO");
_Static_assert(offsetof(nvme_bar_regs, bpmbl)  == NVME_REG_BPMBL,  "BPMBL");
_Static_assert(offsetof(nvme_bar_regs, cmbmsc) == NVME_REG_CMBMSC, "CMBMSC");
_Static_assert(offsetof(nvme_bar_regs, crto)   == NVME_REG_CRTO,   "CRTO");
_Static_assert(offsetof(nvme_bar_regs, pmrcap) == NVME_REG_PMRCAP, "PMRCAP");
_Static_assert(offsetof(nvme_bar_regs, pmrswtp)== NVME_REG_PMRSWTP,"PMRSWTP");
_Static_assert(sizeof(nvme_bar_regs)           == NVME_REG_DBS,    "doorbells start at 0x1000");
#endif

/* ---------------- Part A2: 도어벨 ---------------- */

/* 도어벨 간격(바이트) = 4 << CAP.DSTRD. SQ y tail = 0x1000 + (2y) * 간격, CQ y head = 0x1000 + (2y+1) * 간격.
 * qid 는 0..65535, dstrd 는 0..15 → 결과가 32-bit 를 넘을 수 있어 uint64_t. */
static uint64_t nvme_sq_tail_db(uint32_t qid, uint32_t dstrd) {
    (void)qid; (void)dstrd;
    return 0; /* TODO A2 */
}
static uint64_t nvme_cq_head_db(uint32_t qid, uint32_t dstrd) {
    (void)qid; (void)dstrd;
    return 0; /* TODO A2 */
}
/* admin + nr_io 개 큐 쌍의 도어벨까지 담는 BAR0 최소 크기 (Linux db_bar_size 와 같은 식) */
static uint64_t nvme_bar_min_size(uint32_t nr_io_queues, uint32_t dstrd) {
    (void)nr_io_queues; (void)dstrd;
    return 0; /* TODO A2 */
}

/* ---------------- Part A3: 디코더 ---------------- */

typedef struct {
    uint16_t mqes;          /* 15:0  0-based */
    uint32_t max_entries;   /* mqes + 1 */
    bool     cqr;           /* 16    Contiguous Queues Required */
    uint8_t  ams;           /* 18:17 bit0 = WRR+Urgent, bit1 = vendor specific */
    uint8_t  to;            /* 31:24 500 ms 단위 */
    uint32_t to_ms;
    uint8_t  dstrd;         /* 35:32 */
    uint32_t db_stride;     /* 4 << dstrd 바이트 */
    bool     nssrs;         /* 36 */
    uint8_t  css;           /* 44:37 bit0 NVM, bit6 I/O Command Set(s), bit7 admin only */
    bool     bps;           /* 45 */
    uint8_t  mpsmin, mpsmax;            /* 51:48, 55:52 */
    uint32_t mps_min_bytes, mps_max_bytes; /* 2^(12+n) */
    bool     pmrs;          /* 56 */
    bool     cmbs;          /* 57 */
    uint8_t  crms;          /* 60:59 bit0 CRWMS, bit1 CRIMS */
} nvme_cap_t;

static void nvme_cap_decode(uint64_t cap, nvme_cap_t *o) {
    memset(o, 0, sizeof *o);
    (void)cap; /* TODO A3: 표의 비트 위치대로 필드를 꺼내고 max_entries, to_ms, db_stride, mps_*_bytes 계산 */
}

typedef struct { uint16_t major; uint8_t minor, tertiary; } nvme_vs_t;

static void nvme_vs_decode(uint32_t vs, nvme_vs_t *o) {
    (void)vs;
    o->major = 0; o->minor = 0; o->tertiary = 0; /* TODO A3 */
}

typedef struct {
    bool     en;
    uint8_t  css, mps, ams, shn, iosqes, iocqes;
    bool     crime;
    uint32_t page_bytes;    /* 2^(12+mps) */
    uint32_t sqe_bytes, cqe_bytes; /* 2^iosqes, 2^iocqes */
} nvme_cc_t;

static void nvme_cc_decode(uint32_t cc, nvme_cc_t *o) {
    memset(o, 0, sizeof *o);
    (void)cc; /* TODO A3: EN, CSS, MPS, AMS, SHN, IOSQES, IOCQES, CRIME + 바이트 단위 값 */
}

typedef struct { bool rdy, cfs, nssro, pp; uint8_t shst; } nvme_csts_t;

static void nvme_csts_decode(uint32_t csts, nvme_csts_t *o) {
    (void)csts;
    memset(o, 0, sizeof *o); /* TODO A3 */
}

/* AQA: ASQS 11:0, ACQS 27:16 — 둘 다 0-based. 반환은 1-based 엔트리 수 */
typedef struct { uint16_t asq_entries, acq_entries; } nvme_aqa_t;

static void nvme_aqa_decode(uint32_t aqa, nvme_aqa_t *o) {
    (void)aqa;
    o->asq_entries = 0; o->acq_entries = 0; /* TODO A3 */
}

/* ---------------- 레지스터 이미지 (주어짐) ---------------- */

#define BAR_IMG_LEN 0x1010u   /* 레지스터 4 KiB + 도어벨 4개 */

static uint32_t img_rd32(const uint8_t *img, size_t len, uint32_t off) {
    if ((size_t)off + 4u > len) return 0xFFFFFFFFu;       /* 범위 밖 = 버스가 돌려주는 all-1s 처럼 */
    return (uint32_t)img[off] | (uint32_t)img[off + 1] << 8 | (uint32_t)img[off + 2] << 16 | (uint32_t)img[off + 3] << 24;
}
static uint64_t img_rd64(const uint8_t *img, size_t len, uint32_t off) {
    return (uint64_t)img_rd32(img, len, off) | (uint64_t)img_rd32(img, len, off + 4u) << 32;
}
static void img_wr32(uint8_t *img, uint32_t off, uint32_t v) {
    img[off] = (uint8_t)v; img[off + 1] = (uint8_t)(v >> 8); img[off + 2] = (uint8_t)(v >> 16); img[off + 3] = (uint8_t)(v >> 24);
}

/* ---------------- Part B1: 64-bit 레지스터를 32-bit 두 번으로 ---------------- */

/* MMIO 모의 장치: 32-bit 읽기만 받는다. 접근 로그를 남기고, gone 이면 모든 읽기가 0xFFFFFFFF.
 * CNT_OFF 의 64-bit 카운터는 "가상 레지스터" — 읽을 때마다 1 증가 (NVMe 에는 없다. 찢어진 읽기 시연용). */
#define CNT_OFF 0x0F00u
typedef struct {
    uint8_t  img[BAR_IMG_LEN];
    uint64_t counter;
    bool     gone;
    uint32_t nacc, bad;
    uint32_t log[16];
} mmio_mock;

static uint32_t mmio_rd32(mmio_mock *m, uint32_t off) {
    if (m->nacc < 16) m->log[m->nacc] = off;
    m->nacc++;
    if (m->gone) return 0xFFFFFFFFu;
    if ((off & 3u) != 0 || (size_t)off + 4u > BAR_IMG_LEN) { m->bad++; return 0xFFFFFFFFu; }
    if (off == CNT_OFF || off == CNT_OFF + 4u) {
        uint32_t v = (off == CNT_OFF) ? (uint32_t)m->counter : (uint32_t)(m->counter >> 32);
        m->counter++;                                     /* 접근 하나마다 시간이 흐른다 */
        return v;
    }
    return img_rd32(m->img, BAR_IMG_LEN, off);
}

/* Linux lo_hi_readq 와 같은 순서: 하위 32-bit 먼저, 그다음 상위. off 는 8 의 배수만 (아니면 false). */
static bool nvme_rd64_lohi(mmio_mock *m, uint32_t off, uint64_t *out) {
    (void)m; (void)off;
    *out = 0;
    return false; /* TODO B1: 8 배수 검사 → 하위 32-bit → 상위 32-bit */
}

/* 값이 변하는 64-bit 레지스터: hi → lo → hi 를 읽어 hi 가 같을 때까지 (최대 max_try 번). 실패하면 false. */
static bool rd64_hilohi(mmio_mock *m, uint32_t off, unsigned max_try, uint64_t *out) {
    (void)m; (void)off; (void)max_try;
    *out = 0;
    return false; /* TODO B1: hi → lo → hi, 두 hi 가 같으면 성공. 최대 max_try 번 */
}

/* ---------------- Part B2/B3: 이상값 거르기와 상태 분류 ---------------- */

enum { CAPCHK_OK = 0, CAPCHK_ALLONES = 1, CAPCHK_MQES0 = 2, CAPCHK_MPS = 3, CAPCHK_CSS0 = 4 };

/* 스펙상 나올 수 없는 CAP 를 거른다. 순서: all-1s → MQES=0 → MPSMIN>MPSMAX → CSS=0 */
static int nvme_cap_check(uint64_t cap) {
    (void)cap;
    return CAPCHK_OK; /* TODO B2 */
}

typedef enum {
    NVS_DISABLED, NVS_ENABLING, NVS_READY, NVS_DISABLING, NVS_FATAL,
    NVS_SHUTDOWN_BUSY, NVS_SHUTDOWN_DONE, NVS_SHST_RSVD, NVS_GONE, NVS_BAD_CAP, NVS_TRUNC
} nv_state;

static const char *nvs_name(nv_state s) {
    static const char *n[] = { "DISABLED", "ENABLING", "READY", "DISABLING", "FATAL",
                               "SHUTDOWN_BUSY", "SHUTDOWN_DONE", "SHST_RSVD", "GONE", "BAD_CAP", "TRUNC" };
    return (unsigned)s < sizeof n / sizeof n[0] ? n[s] : "?";
}

/* 우선순위: GONE(CSTS 또는 CAP 가 all-1s) → BAD_CAP → FATAL(CFS) → SHST(10 완료 / 01 진행 / 11 예약)
 *           → (EN,RDY) 2x2: 11 READY, 10 ENABLING, 01 DISABLING, 00 DISABLED */
static nv_state nvme_classify(uint64_t cap, uint32_t cc, uint32_t csts) {
    (void)cap; (void)cc; (void)csts;
    return NVS_DISABLED; /* TODO B3 */
}

/* ---------------- Part C1: 프로브 리포트 ---------------- */

/* out 에 이어 쓰기. 넘치면 잘라서 항상 NUL 종료 (pos 는 cap-1 에서 멈춘다) */
static void appendf(char *out, size_t cap, size_t *pos, const char *fmt, ...) {
    if (cap == 0 || *pos >= cap - 1) return;
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(out + *pos, cap - *pos, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    *pos = ((size_t)n >= cap - *pos) ? cap - 1 : *pos + (size_t)n;
}

/* bar: BAR0 이미지 (len ≥ 0x40 이어야 CAP..CMBSZ 를 본다). 리포트를 out 에 쓰고 상태를 돌려준다.
 * len < 0x40 이면 NVS_TRUNC 와 한 줄 오류. 리포트 줄: VS, CAP, CC, CSTS, AQA, ASQ/ACQ, DB, STATE(+진단 문장) */
static nv_state nvme_report(const uint8_t *bar, size_t len, char *out, size_t cap) {
    size_t pos = 0;
    if (cap) out[0] = '\0';
    (void)bar; (void)len;
    appendf(out, cap, &pos, "STATE %s: TODO C1\n", nvs_name(NVS_TRUNC));
    return NVS_TRUNC; /* TODO C1: 길이 검사 → 레지스터 읽기 → 디코드 → 분류 → 줄마다 appendf → 상태별 진단 문장 */
}

/* ================= 테스트 하네스 ================= */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

typedef struct { uint32_t off, val; } regval;

/* 실제 캡처 (QEMU 10.2.1 emulated nvme, BusyBox devmem, capture_qemu_nvme_bar0.txt) — 실리콘 아님 */
static const regval QEMU_NODRV[] = { {0x00, 0x0F0107FF}, {0x04, 0x00400820}, {0x08, 0x00010400} };   /* 나머지 0 */
static const regval QEMU_DRV[]   = { {0x00, 0x0F0107FF}, {0x04, 0x00400820}, {0x08, 0x00010400},
                                     {0x14, 0x00460061}, {0x1C, 0x00000001}, {0x24, 0x001F001F},
                                     {0x28, 0x43F3C000}, {0x30, 0x43F3B000} };
/* 가상 덤프: "흔한 소비자 Gen4 SSD 같은" 값 (특정 제품 아님) — MQES 1023, TO 10 s, NSSRS, MPS 4K 만 */
static const regval SSD_OK[]     = { {0x00, 0x140103FF}, {0x04, 0x00000030}, {0x08, 0x00010400},
                                     {0x14, 0x00460001}, {0x1C, 0x00000001}, {0x24, 0x001F001F},
                                     {0x28, 0x02A40000}, {0x2C, 0x00000001}, {0x30, 0x02A41000}, {0x34, 0x00000001} };

static uint8_t g_img[BAR_IMG_LEN];
static void build(const regval *rv, size_t n) {
    memset(g_img, 0, sizeof g_img);
    for (size_t i = 0; i < n; i++) img_wr32(g_img, rv[i].off, rv[i].val);
}
#define BUILD(arr) build(arr, sizeof arr / sizeof arr[0])

static char g_rep[2048];
static bool has(const char *needle) { return strstr(g_rep, needle) != NULL; }

int main(void) {
    /* ---- A1: 레이아웃 ---- */
    CHECK(offsetof(nvme_bar_regs, cc)     == NVME_REG_CC);
    CHECK(offsetof(nvme_bar_regs, csts)   == NVME_REG_CSTS);
    CHECK(offsetof(nvme_bar_regs, nssr)   == NVME_REG_NSSR && offsetof(nvme_bar_regs, aqa) == NVME_REG_AQA);
    CHECK(offsetof(nvme_bar_regs, asq)    == NVME_REG_ASQ && offsetof(nvme_bar_regs, acq) == NVME_REG_ACQ);
    CHECK(offsetof(nvme_bar_regs, cmbsz)  == NVME_REG_CMBSZ && offsetof(nvme_bar_regs, crto) == NVME_REG_CRTO);
    CHECK(offsetof(nvme_bar_regs, pmrcap) == NVME_REG_PMRCAP);
    CHECK(sizeof(nvme_bar_regs) == 0x1000u);
    CHECK(NVME_LAYOUT_CHECKED == 1);
    {   /* 구조체 겹쳐 읽기 == 바이트 조립 읽기 (LE 호스트에서만 의미 있음 — Apple Silicon/x86/Cortex-A 기본 LE) */
        static nvme_bar_regs r;
        BUILD(QEMU_DRV);
        memcpy(&r, g_img, sizeof r);
        const uint16_t one = 1;
        bool le = *(const uint8_t *)&one == 1;
        CHECK(!le || (r.cap == img_rd64(g_img, BAR_IMG_LEN, NVME_REG_CAP) && r.cc == 0x00460061u &&
                      r.csts == 1u && r.aqa == 0x001F001Fu && r.asq == 0x43F3C000ull && r.acq == 0x43F3B000ull));
    }

    /* ---- A2: 도어벨 ---- */
    CHECK(nvme_sq_tail_db(0, 0) == 0x1000 && nvme_cq_head_db(0, 0) == 0x1004);
    CHECK(nvme_sq_tail_db(1, 0) == 0x1008 && nvme_cq_head_db(1, 0) == 0x100C);
    CHECK(nvme_sq_tail_db(3, 2) == 0x1000 + 6 * 16 && nvme_cq_head_db(3, 2) == 0x1000 + 7 * 16);   /* 0x1060, 0x1070 */
    CHECK(nvme_cq_head_db(65535, 15) == 0x1000ull + 131071ull * 131072ull);                     /* 32-bit 넘침 */
    {
        bool ok = true;                                                  /* 전수: 간격·교대·겹침 없음 */
        for (uint32_t d = 0; d <= 15 && ok; d++)
            for (uint32_t q = 0; q < 4096 && ok; q++) {
                uint64_t s = nvme_sq_tail_db(q, d), c = nvme_cq_head_db(q, d), st = 4ull << d;
                ok = (c - s == st) && (q == 0 || s - nvme_cq_head_db(q - 1, d) == st) && (s % 4 == 0)
                     && nvme_bar_min_size(q, d) == c + st;               /* q 개 IO 큐: 마지막 CQ 도어벨 끝 */
            }
        CHECK(ok);
    }
    CHECK(nvme_bar_min_size(1, 0) == 0x1010 && nvme_bar_min_size(1535, 0) == 0x4000);   /* QEMU BAR0 16 KiB */

    /* ---- A3: 디코더 (QEMU 실측 CAP) ---- */
    {
        nvme_cap_t c; nvme_cap_decode(0x004008200F0107FFull, &c);
        CHECK(c.mqes == 0x7FF && c.max_entries == 2048 && c.cqr && c.ams == 0);
        CHECK(c.to == 15 && c.to_ms == 7500 && c.dstrd == 0 && c.db_stride == 4 && !c.nssrs);
        CHECK(c.css == 0x41 && !c.bps && c.mpsmin == 0 && c.mpsmax == 4);
        CHECK(c.mps_min_bytes == 4096 && c.mps_max_bytes == 65536 && !c.cmbs && !c.pmrs && c.crms == 0);
        nvme_cap_decode(0x00000030140103FFull, &c);                        /* 가상 SSD */
        CHECK(c.max_entries == 1024 && c.to_ms == 10000 && c.nssrs && c.css == 0x01 && c.mps_max_bytes == 4096);
        /* 필드 하나씩 켜 보기: 경계 비트가 이웃으로 새지 않는지 */
        nvme_cap_decode(1ull << 16, &c);  CHECK(c.cqr && c.mqes == 0 && c.ams == 0);
        nvme_cap_decode(3ull << 17, &c);  CHECK(c.ams == 3 && !c.cqr && c.to == 0);
        nvme_cap_decode(0xFull << 32, &c); CHECK(c.dstrd == 15 && c.db_stride == 131072 && !c.nssrs && c.to == 0);
        nvme_cap_decode(1ull << 45, &c);  CHECK(c.bps && c.css == 0 && c.mpsmin == 0);
        nvme_cap_decode(0xFull << 52, &c); CHECK(c.mpsmax == 15 && c.mps_max_bytes == (1u << 27) && !c.pmrs && c.mpsmin == 0);
        nvme_cap_decode((1ull << 56) | (1ull << 57) | NVME_CAP_CRMS_CRWMS | NVME_CAP_CRMS_CRIMS, &c);
        CHECK(c.pmrs && c.cmbs && c.crms == 3 && c.mpsmax == 0);
        uint32_t seed = 0x12B12u; bool ok = true;                          /* 랜덤 CAP: 디코드 → 재조립 == 원본 (예약 비트 제외) */
        for (int i = 0; i < 20000 && ok; i++) {
            uint64_t v = (uint64_t)xs32(&seed) << 32 | xs32(&seed);
            nvme_cap_decode(v, &c);
            uint64_t re = c.mqes | (uint64_t)c.cqr << 16 | (uint64_t)c.ams << 17 | (uint64_t)c.to << 24 |
                          (uint64_t)c.dstrd << 32 | (uint64_t)c.nssrs << 36 | (uint64_t)c.css << 37 | (uint64_t)c.bps << 45 |
                          (uint64_t)c.mpsmin << 48 | (uint64_t)c.mpsmax << 52 | (uint64_t)c.pmrs << 56 |
                          (uint64_t)c.cmbs << 57 | (uint64_t)c.crms << 59;
            const uint64_t known = 0xFFFFull | 0x7ull << 16 | 0xFFull << 24 | 0x3FFFull << 32 |
                                   0xFFull << 48 | 0x3ull << 56 | 0x3ull << 59;   /* 19..23, 46,47(CPS), 58, 61..63 제외 */
            ok = (re == (v & known)) && c.max_entries == c.mqes + 1u;
        }
        CHECK(ok);
    }
    {
        nvme_vs_t v;
        nvme_vs_decode(0x00010400, &v); CHECK(v.major == 1 && v.minor == 4 && v.tertiary == 0);
        nvme_vs_decode(0x00020001, &v); CHECK(v.major == 2 && v.minor == 0 && v.tertiary == 1);
        nvme_vs_decode(0x00010201, &v); CHECK(v.major == 1 && v.minor == 2 && v.tertiary == 1);
    }
    {
        nvme_cc_t k; nvme_cc_decode(0x00460061, &k);                       /* QEMU 실측: Linux 가 쓴 값 */
        CHECK(k.en && k.css == 6 && k.mps == 0 && k.page_bytes == 4096 && k.ams == 0 && k.shn == 0);
        CHECK(k.iosqes == 6 && k.sqe_bytes == 64 && k.iocqes == 4 && k.cqe_bytes == 16 && !k.crime);
        nvme_cc_decode(0x01464000u | (2u << 7) | (1u << 11), &k);          /* SHN=01, MPS=2, AMS=1, CRIME */
        CHECK(!k.en && k.shn == 1 && k.mps == 2 && k.page_bytes == 16384 && k.ams == 1 && k.crime && k.css == 0);
        nvme_csts_t s;
        nvme_csts_decode(0x00000001, &s); CHECK(s.rdy && !s.cfs && s.shst == 0 && !s.nssro && !s.pp);
        nvme_csts_decode(0x00000009, &s); CHECK(s.rdy && s.shst == 2 && !s.cfs);
        nvme_csts_decode(0x00000032, &s); CHECK(!s.rdy && s.cfs && s.nssro && s.pp && s.shst == 0);
        nvme_aqa_t a;
        nvme_aqa_decode(0x001F001F, &a); CHECK(a.asq_entries == 32 && a.acq_entries == 32);
        nvme_aqa_decode(0x0FFF0001, &a); CHECK(a.asq_entries == 2 && a.acq_entries == 4096);
        nvme_aqa_decode(0xF000F000, &a); CHECK(a.asq_entries == 1 && a.acq_entries == 1);   /* 예약 비트 무시 */
    }

    /* ---- B1: 64-bit 읽기 ---- */
    {
        static mmio_mock m;
        memset(&m, 0, sizeof m);
        build(QEMU_DRV, sizeof QEMU_DRV / sizeof QEMU_DRV[0]); memcpy(m.img, g_img, BAR_IMG_LEN);
        uint64_t v = 0;
        CHECK(nvme_rd64_lohi(&m, NVME_REG_CAP, &v) && v == 0x004008200F0107FFull);
        CHECK(m.nacc == 2 && m.log[0] == 0x00 && m.log[1] == 0x04 && m.bad == 0);     /* 하위 먼저, 32-bit 두 번 */
        CHECK(nvme_rd64_lohi(&m, NVME_REG_ASQ, &v) && v == 0x43F3C000ull);
        m.nacc = 0;
        CHECK(!nvme_rd64_lohi(&m, 0x14, &v) && m.nacc == 0);                           /* 8 배수 아님 → 접근 0 */
        CHECK(mmio_rd32(&m, NVME_REG_VS) == 0x00010400u && mmio_rd32(&m, 0x16) == 0xFFFFFFFFu && m.bad == 1); /* 모의 장치 점검 */
        m.counter = 0x00000001FFFFFFFFull;                                              /* 찢어진 읽기 */
        CHECK(nvme_rd64_lohi(&m, CNT_OFF, &v) && v == 0x00000002FFFFFFFFull);          /* 실제값보다 4 Gi 큼 */
        m.counter = 0x00000001FFFFFFFFull; uint64_t before = m.counter;
        CHECK(rd64_hilohi(&m, CNT_OFF, 4, &v) && v >= before && v <= m.counter);
        m.counter = 0x00000001FFFFFFFFull; m.nacc = 0;
        CHECK(rd64_hilohi(&m, CNT_OFF, 4, &v) && m.nacc == 6);                         /* 한 번 재시도 */
        CHECK(!rd64_hilohi(&m, CNT_OFF, 0, &v));
        m.gone = true;
        CHECK(nvme_rd64_lohi(&m, NVME_REG_CAP, &v) && v == ~0ull);                     /* 장치 사라짐: 값은 all-1s */
    }

    /* ---- B2/B3: 이상 CAP, 상태 분류 ---- */
    {
        CHECK(nvme_cap_check(0x004008200F0107FFull) == CAPCHK_OK && nvme_cap_check(0x00000030140103FFull) == CAPCHK_OK);
        CHECK(nvme_cap_check(~0ull) == CAPCHK_ALLONES);
        CHECK(nvme_cap_check(0x0000002014010000ull) == CAPCHK_MQES0);
        CHECK(nvme_cap_check(0x00010020140103FFull) == CAPCHK_MPS);                    /* MPSMIN=1 > MPSMAX=0 */
        CHECK(nvme_cap_check(0x00000000140103FFull) == CAPCHK_CSS0);
        const uint64_t C = 0x00000030140103FFull;
        CHECK(nvme_classify(C, 0x00460001, 0x1) == NVS_READY);
        CHECK(nvme_classify(C, 0x00460001, 0x0) == NVS_ENABLING);
        CHECK(nvme_classify(C, 0x00460000, 0x1) == NVS_DISABLING);
        CHECK(nvme_classify(C, 0x00000000, 0x0) == NVS_DISABLED);
        CHECK(nvme_classify(C, 0x00460001, 0x3) == NVS_FATAL);
        CHECK(nvme_classify(C, 0x00464001, 0x9) == NVS_SHUTDOWN_DONE);
        CHECK(nvme_classify(C, 0x00464001, 0x5) == NVS_SHUTDOWN_BUSY);
        CHECK(nvme_classify(C, 0x00464001, 0xD) == NVS_SHST_RSVD);
        CHECK(nvme_classify(C, 0x00460001, 0xFFFFFFFFu) == NVS_GONE);                  /* CFS 비트도 1 이지만 GONE */
        CHECK(nvme_classify(~0ull, 0, 0) == NVS_GONE);
        CHECK(nvme_classify(0x0000002014010000ull, 0x00460001, 0x1) == NVS_BAD_CAP);
        bool ok = true;                                                    /* 전수: CSTS 하위 6비트 × EN */
        for (uint32_t cs = 0; cs < 64 && ok; cs++)
            for (uint32_t en = 0; en < 2 && ok; en++) {
                nv_state s = nvme_classify(C, 0x00460000u | en, cs);
                uint32_t shst = (cs >> 2) & 3u, rdy = cs & 1u;
                if (cs & 2u) ok = s == NVS_FATAL;
                else if (shst) ok = s == (shst == 2 ? NVS_SHUTDOWN_DONE : shst == 1 ? NVS_SHUTDOWN_BUSY : NVS_SHST_RSVD);
                else ok = s == (en ? (rdy ? NVS_READY : NVS_ENABLING) : (rdy ? NVS_DISABLING : NVS_DISABLED));
            }
        CHECK(ok);
    }

    /* ---- C1: 프로브 리포트 ---- */
    {
        BUILD(QEMU_NODRV);
        nv_state st = nvme_report(g_img, BAR_IMG_LEN, g_rep, sizeof g_rep);
        printf("--- QEMU nvme, 드라이버 없음 (실측) ---\n%s", g_rep);
        CHECK(st == NVS_DISABLED && has("VS   1.4.0") && has("MQES=2047 (큐당 최대 2048 엔트리)") && has("TO=15 (7500 ms)"));
        CHECK(has("CSS=0x41 NVM IOCS") && has("MPS 4096..65536 B") && has("SQ1 tail 0x1008, CQ1 head 0x100c"));

        BUILD(QEMU_DRV);
        st = nvme_report(g_img, BAR_IMG_LEN, g_rep, sizeof g_rep);
        printf("--- QEMU nvme, nvme.ko probe 후 (실측) ---\n%s", g_rep);
        CHECK(st == NVS_READY && has("CC   0x00460061 EN=1 CSS=6 MPS=0 (4096 B)") && has("IOSQES=6 (64 B) IOCQES=4 (16 B)"));
        CHECK(has("AQA  0x001f001f ASQ 32 엔트리, ACQ 32 엔트리") && has("ASQ  0x0000000043f3c000 ACQ 0x0000000043f3b000"));

        BUILD(SSD_OK);
        st = nvme_report(g_img, BAR_IMG_LEN, g_rep, sizeof g_rep);
        printf("--- 가상 소비자 SSD (동작 중) ---\n%s", g_rep);
        CHECK(st == NVS_READY && has("MQES=1023 (큐당 최대 1024 엔트리)") && has("NSSRS=1") && has("MPS 4096..4096 B"));
        CHECK(has("ASQ  0x0000000102a40000"));

        img_wr32(g_img, NVME_REG_CSTS, 0x3);
        CHECK(nvme_report(g_img, BAR_IMG_LEN, g_rep, sizeof g_rep) == NVS_FATAL && has("CFS=1") && has("STATE FATAL"));
        img_wr32(g_img, NVME_REG_CSTS, 0x0);
        CHECK(nvme_report(g_img, BAR_IMG_LEN, g_rep, sizeof g_rep) == NVS_ENABLING && has("10000 ms"));
        img_wr32(g_img, NVME_REG_CC, 0x00464001); img_wr32(g_img, NVME_REG_CSTS, 0x9);
        CHECK(nvme_report(g_img, BAR_IMG_LEN, g_rep, sizeof g_rep) == NVS_SHUTDOWN_DONE && has("SHN=1") && has("SHST=2"));
        BUILD(SSD_OK); img_wr32(g_img, NVME_REG_CAP, 0x14010000);         /* MQES=0 */
        CHECK(nvme_report(g_img, BAR_IMG_LEN, g_rep, sizeof g_rep) == NVS_BAD_CAP && has("check=2"));

        memset(g_img, 0xFF, sizeof g_img);                                  /* 링크 다운 뒤 읽은 BAR */
        st = nvme_report(g_img, BAR_IMG_LEN, g_rep, sizeof g_rep);
        printf("--- 가상: 장치가 버스에서 사라짐 ---\n%s", g_rep);
        CHECK(st == NVS_GONE && !has("MQES") && has("all-1s"));

        CHECK(nvme_report(g_img, 0x3C, g_rep, sizeof g_rep) == NVS_TRUNC && has("0x40"));
        CHECK(nvme_report(NULL, 0, g_rep, sizeof g_rep) == NVS_TRUNC);
        BUILD(QEMU_DRV);
        char tiny[24]; memset(tiny, 'x', sizeof tiny);
        CHECK(nvme_report(g_img, BAR_IMG_LEN, tiny, sizeof tiny) == NVS_READY && tiny[sizeof tiny - 1] == '\0'
              && strncmp(tiny, "VS   1.4.0\nCAP  0x", 18) == 0);              /* 잘려도 NUL 종료, 넘치지 않음 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
