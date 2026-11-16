/* PCIE-B12 추가 연습 — 한 번에 검증
 *
 * 연습 1: CMBLOC/CMBSZ 디코드 (BIR, 오프셋, 크기 = SZ × 4 KiB·16^SZU) + BAR 안에 들어가는지
 * 연습 2: AQA/ASQ/ACQ 값 점검 (예약 비트, 최소 2 엔트리, 페이지 정렬)
 * 연습 3: BAR 크기 → 도어벨을 둘 수 있는 큐 쌍 수 (Part A2 의 역함수)
 * 연습 4: Linux nvme_enable_ctrl() 이 쓰는 CC 값을 CAP 에서 재현 → QEMU 실측 0x00460061 과 비교
 * 연습 5: 준비 타임아웃 = CAP.TO 또는 CRTO.CRWMT (CAP.CRMS.CRWMS 일 때) × 500 ms, Linux 의 초 단위 반올림
 *
 * 오프셋·마스크는 Linux include/linux/nvme.h 기준 (NVME_CMB_BIR/OFST, NVME_CMBSZ_*, NVME_CRTO_*, NVME_CAP_CRMS_*).
 * 자립형 — solution.c 의 필요한 부분만 다시 담았다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define NVME_REG_DBS          0x1000u
#define NVME_CMBSZ_SQS        (1u << 0)
#define NVME_CMBSZ_CQS        (1u << 1)
#define NVME_CMBSZ_LISTS      (1u << 2)
#define NVME_CMBSZ_RDS        (1u << 3)
#define NVME_CMBSZ_WDS        (1u << 4)
#define NVME_CMBSZ_SZU_SHIFT  8
#define NVME_CMBSZ_SZU_MASK   0xFu
#define NVME_CMBSZ_SZ_SHIFT   12
#define NVME_CMBSZ_SZ_MASK    0xFFFFFu
#define NVME_CC_ENABLE        (1u << 0)
#define NVME_CC_CSS_SHIFT     4
#define NVME_CC_MPS_SHIFT     7
#define NVME_CC_AMS_SHIFT     11
#define NVME_CC_SHN_SHIFT     14
#define NVME_CC_IOSQES_SHIFT  16
#define NVME_CC_IOCQES_SHIFT  20
#define NVME_NVM_IOSQES       6
#define NVME_NVM_IOCQES       4
#define NVME_CAP_CSS_CSI      (1u << 6)
#define NVME_CAP_CRMS_CRWMS   (1ull << 59)

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---------- 연습 1: CMB ---------- */
typedef struct { uint8_t bir; uint64_t unit, ofst_bytes, size_bytes; uint8_t flags; } nvme_cmb_t;

/* false: SZU 예약값(>6) 또는 BIR 예약값(1 = 64-bit BAR0 의 위쪽 절반, 6·7) */
static bool nvme_cmb_decode(uint32_t cmbloc, uint32_t cmbsz, nvme_cmb_t *o) {
    uint32_t szu = (cmbsz >> NVME_CMBSZ_SZU_SHIFT) & NVME_CMBSZ_SZU_MASK;
    o->bir = (uint8_t)(cmbloc & 0x7u);
    if (szu > 6 || o->bir == 1 || o->bir > 5) return false;
    o->unit       = 4096ull << (4u * szu);                                  /* 4K, 64K, 1M, 16M, 256M, 4G, 64G */
    o->ofst_bytes = (uint64_t)((cmbloc >> 12) & 0xFFFFFu) * o->unit;
    o->size_bytes = (uint64_t)((cmbsz >> NVME_CMBSZ_SZ_SHIFT) & NVME_CMBSZ_SZ_MASK) * o->unit;
    o->flags      = (uint8_t)(cmbsz & 0x1Fu);
    return true;
}
static bool nvme_cmb_fits(const nvme_cmb_t *c, uint64_t bar_size) {
    return c->size_bytes != 0 && c->ofst_bytes <= bar_size && c->size_bytes <= bar_size - c->ofst_bytes;
}

/* ---------- 연습 2: AQA/ASQ/ACQ ---------- */
enum { ADM_OK = 0, ADM_RSVD = 1, ADM_SIZE = 2, ADM_ALIGN = 3 };
static int nvme_admin_regs_check(uint32_t aqa, uint64_t asq, uint64_t acq, uint32_t page_bytes) {
    if (aqa & 0xF000F000u) return ADM_RSVD;                                  /* 15:12, 31:28 예약 */
    if ((aqa & 0xFFFu) == 0 || ((aqa >> 16) & 0xFFFu) == 0) return ADM_SIZE; /* 0-based 0 = 1 엔트리 → 최소 2 */
    uint64_t m = (uint64_t)page_bytes - 1u;
    if ((asq & m) || (acq & m) || (asq & 0xFFFu) || (acq & 0xFFFu)) return ADM_ALIGN;
    return ADM_OK;
}

/* ---------- 연습 3: BAR 크기 → 큐 쌍 수 ---------- */
static uint64_t nvme_bar_min_size(uint32_t nr_io_queues, uint32_t dstrd) {   /* Part A2 와 같은 식 */
    return NVME_REG_DBS + (nr_io_queues + 1ull) * 2ull * (4ull << dstrd);
}
/* admin 포함 큐 쌍 수 (qid 0..n-1 의 SQ tail + CQ head 도어벨이 모두 BAR 안) */
static uint64_t nvme_max_qpairs_in_bar(uint64_t bar_size, uint32_t dstrd) {
    if (bar_size <= NVME_REG_DBS) return 0;
    return (bar_size - NVME_REG_DBS) / (2ull * (4ull << dstrd));
}

/* ---------- 연습 4: Linux 가 쓰는 CC ---------- */
/* drivers/nvme/host/core.c nvme_enable_ctrl(): CSS = CSI(6) if CAP.CSS bit6 else NVM(0), MPS = page_shift-12,
 * AMS=RR, SHN=none, IOSQES=6, IOCQES=4, CRIME 끔. EN 은 이 값을 쓴 뒤 따로 켠다. page_shift 가 범위 밖이면 -1. */
static int linux_cc_value(uint64_t cap, unsigned host_page_shift, uint32_t *cc) {
    unsigned mpsmin = (unsigned)((cap >> 48) & 0xFu), mpsmax = (unsigned)((cap >> 52) & 0xFu);
    if (host_page_shift < 12u + mpsmin || host_page_shift > 12u + mpsmax) return -1;
    uint32_t css = ((cap >> 37) & 0xFFu) & NVME_CAP_CSS_CSI ? 6u : 0u;
    *cc = css << NVME_CC_CSS_SHIFT | (host_page_shift - 12u) << NVME_CC_MPS_SHIFT | 0u << NVME_CC_AMS_SHIFT |
          0u << NVME_CC_SHN_SHIFT | (uint32_t)NVME_NVM_IOSQES << NVME_CC_IOSQES_SHIFT |
          (uint32_t)NVME_NVM_IOCQES << NVME_CC_IOCQES_SHIFT;
    return 0;
}

/* ---------- 연습 5: 준비 타임아웃 ---------- */
static uint32_t nvme_ready_timeout_ms(uint64_t cap, uint32_t crto, bool *crto_bad) {
    uint32_t to = (uint32_t)((cap >> 24) & 0xFFu);
    *crto_bad = false;
    if (cap & NVME_CAP_CRMS_CRWMS) {
        uint32_t crwmt = crto & 0xFFFFu;                                     /* NVME_CRTO_CRWMT, 500 ms 단위 */
        if (crwmt >= to) to = crwmt; else *crto_bad = true;                  /* Linux: 작으면 경고하고 CAP.TO 사용 */
    }
    return to * 500u;
}
static uint32_t linux_wait_s(uint32_t timeout_500ms_units) { return (timeout_500ms_units + 1u) / 2u; }

int main(void) {
    /* 연습 1 */
    {
        nvme_cmb_t c;
        CHECK(nvme_cmb_decode(0x00000002u, (64u << 12) | (2u << 8) | 0x1Fu, &c));   /* 가상: BAR2, 64 × 1 MiB */
        CHECK(c.bir == 2 && c.unit == (1u << 20) && c.ofst_bytes == 0 && c.size_bytes == (64ull << 20));
        CHECK(c.flags == (NVME_CMBSZ_SQS | NVME_CMBSZ_CQS | NVME_CMBSZ_LISTS | NVME_CMBSZ_RDS | NVME_CMBSZ_WDS));
        CHECK(nvme_cmb_fits(&c, 64ull << 20) && !nvme_cmb_fits(&c, 32ull << 20));
        CHECK(nvme_cmb_decode(0x00001002u, (64u << 12) | (2u << 8) | 0x3u, &c) && c.ofst_bytes == (1u << 20));
        CHECK(!nvme_cmb_fits(&c, 64ull << 20) && nvme_cmb_fits(&c, 128ull << 20));   /* 1 MiB 밀려 넘친다 */
        CHECK(nvme_cmb_decode(0, (1u << 12) | (6u << 8), &c) && c.size_bytes == (64ull << 30));   /* SZU 6 = 64 GiB */
        CHECK(!nvme_cmb_decode(0, (1u << 12) | (7u << 8), &c));                      /* SZU 예약 */
        CHECK(!nvme_cmb_decode(1, (1u << 12), &c) && !nvme_cmb_decode(6, (1u << 12), &c));
        CHECK(nvme_cmb_decode(0, 0, &c) && c.size_bytes == 0 && !nvme_cmb_fits(&c, 1u << 20));   /* CMB 없음 (QEMU 실측 0) */
    }
    /* 연습 2 */
    {
        CHECK(nvme_admin_regs_check(0x001F001Fu, 0x43F3C000ull, 0x43F3B000ull, 4096) == ADM_OK);   /* QEMU 실측 */
        CHECK(nvme_admin_regs_check(0x001F101Fu, 0x43F3C000ull, 0x43F3B000ull, 4096) == ADM_RSVD);
        CHECK(nvme_admin_regs_check(0x001F0000u, 0x43F3C000ull, 0x43F3B000ull, 4096) == ADM_SIZE);
        CHECK(nvme_admin_regs_check(0x0FFF0FFFu, 0x43F3C000ull, 0x43F3B000ull, 4096) == ADM_OK);   /* 4096 엔트리 */
        CHECK(nvme_admin_regs_check(0x001F001Fu, 0x43F3C800ull, 0x43F3B000ull, 4096) == ADM_ALIGN);
        CHECK(nvme_admin_regs_check(0x001F001Fu, 0x43F3C000ull, 0x43F3B000ull, 16384) == ADM_ALIGN); /* MPS 16K 면 C000 은 안 됨 */
        CHECK(nvme_admin_regs_check(0x001F001Fu, 0x43F40000ull, 0x43F3C000ull, 16384) == ADM_OK);
    }
    /* 연습 3 */
    {
        CHECK(nvme_max_qpairs_in_bar(0x4000, 0) == 1536);       /* QEMU BAR0 16 KiB 실측: qid 0..1535 */
        CHECK(nvme_max_qpairs_in_bar(0x4000, 2) == 384);
        CHECK(nvme_max_qpairs_in_bar(0x1000, 0) == 0 && nvme_max_qpairs_in_bar(0x1007, 0) == 0 && nvme_max_qpairs_in_bar(0x1008, 0) == 1);
        uint32_t seed = 0xB12Bu; bool ok = true;                 /* 역함수: min_size(n-2) ≤ bar < min_size(n-1) 꼴 */
        for (int i = 0; i < 100000 && ok; i++) {
            uint32_t d = xs32(&seed) % 16u;
            uint64_t bar = 0x1000u + (uint64_t)(xs32(&seed) % (1u << 24));
            uint64_t n = nvme_max_qpairs_in_bar(bar, d);
            ok = (n == 0 || nvme_bar_min_size((uint32_t)(n - 1u), d) <= bar) && nvme_bar_min_size((uint32_t)n, d) > bar;
        }
        CHECK(ok);
    }
    /* 연습 4 */
    {
        uint32_t cc = 0;
        CHECK(linux_cc_value(0x004008200F0107FFull, 12, &cc) == 0 && cc == 0x00460060u);   /* QEMU CAP (CSS bit6) */
        CHECK((cc | NVME_CC_ENABLE) == 0x00460061u);                                     /* QEMU 실측 CC 와 일치 */
        CHECK(linux_cc_value(0x00000030140103FFull, 12, &cc) == 0 && cc == 0x00460000u); /* 가상 SSD: NVM 만 */
        CHECK(linux_cc_value(0x00010030140103FFull | (1ull << 52), 12, &cc) == -1);      /* MPSMIN=1 (8K) > 호스트 4K */
        CHECK(linux_cc_value(0x004008200F0107FFull, 16, &cc) == 0 && ((cc >> NVME_CC_MPS_SHIFT) & 0xFu) == 4u);
        CHECK(linux_cc_value(0x004008200F0107FFull, 17, &cc) == -1);                    /* MPSMAX=4 (64K) 초과 */
    }
    /* 연습 5 */
    {
        bool bad;
        CHECK(nvme_ready_timeout_ms(0x004008200F0107FFull, 0, &bad) == 7500 && !bad);    /* QEMU: CRMS 없음 → CRTO 무시 */
        CHECK(linux_wait_s(15) == 8 && linux_wait_s(20) == 10 && linux_wait_s(1) == 1);
        uint64_t cap = 0x00000030140103FFull | NVME_CAP_CRMS_CRWMS;                       /* TO=20 (10 s) */
        CHECK(nvme_ready_timeout_ms(cap, (60u << 16) | 60u, &bad) == 30000 && !bad);      /* CRWMT 60 → 30 s */
        CHECK(nvme_ready_timeout_ms(cap, 4u, &bad) == 10000 && bad);                      /* CRWMT < TO: 이상값 */
        CHECK(nvme_ready_timeout_ms(0x00000030000103FFull, 0, &bad) == 0);                /* TO=0: 0 ms (가상) */
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
