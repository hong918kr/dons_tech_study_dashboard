/* ARCH-A12 SoC 설계 연습: 메모리 맵과 인터커넥트 — 체크 테스트
 *
 *   Part A  설계한 엣지 AI SoC 맵(40비트 PA)을 데이터로 둔다. 2단계 디코드(메인 인터커넥트 → APB 브리지 창)
 *           decode(addr, master, secure, write) → OKAY / SLVERR / DECERR + 이유, 마스터→슬레이브 접근 행렬 출력
 *   Part B  자동 검사기 map_check: 크기·2의 거듭제곱·정렬·PA 범위·속성·APB 창·라우팅·보안·겹침 (오류)
 *           + non-coherent 마스터·주소 폭 도달 범위 (경고). 오류를 하나씩 주입 → 정확히 그 진단 하나.
 *           무작위 맵 2000개를 "페이지 칠하기" 참조와 비교
 *   Part C  정렬된 맵 표 + 예약 구멍(gap) 보고, revision 2 변경 요청(DRAM 4 → 8 GB, NPU1 추가):
 *           순진한 수정이 깨뜨리는 것 → 고친 맵이 깨끗한지
 *
 * 이 표는 "주소 디코더 맵 + 방화벽(라우팅/TrustZone 필터) 설정" 을 한 표로 합친 교육용 모델이다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define KB(x) ((uint64_t)(x) << 10)
#define MB(x) ((uint64_t)(x) << 20)
#define GB(x) ((uint64_t)(x) << 30)
#define PAGE  KB(4)
#define MAXR  40
#define BIT(m) (1u << (m))

/* ======================= 마스터 (주어짐) ======================= */
enum { M_CPUB, M_CPUL, M_DMA, M_ISP, M_NPU0, M_DBG, M_NPU1, M_N };
typedef struct {
    const char *name;
    unsigned addr_bits;   /* 마스터 AXI 주소 폭 */
    bool secure_ok;       /* secure 트랜잭션을 낼 수 있나. false = AxPROT[1] 이 1(NS)로 고정된 마스터 */
    bool coherent;        /* CPU 캐시를 스누프하나 (완전 coherent 또는 IO-coherent) */
} master_t;

static const master_t MASTERS[M_N] = {
    { "CPU_BIG", 40, true,  true  },   /* 2×big 클러스터 — 코히런트 인터커넥트 */
    { "CPU_LIT", 40, true,  true  },   /* 4×little 클러스터 */
    { "DMA",     40, false, true  },   /* 시스템 DMA — IO-coherent 포트(ACE-Lite 같은) 가정 */
    { "ISP",     40, false, false },   /* CSI-2 수신 + ISP-lite 프레임 쓰기 마스터 — non-coherent */
    { "NPU0",    40, false, false },   /* NPU — non-coherent (대역폭이 커서 스누프하지 않음) */
    { "DEBUG",   40, true,  true  },   /* 디버그 AXI-AP — 잠금 해제 때만, IO-coherent 경로 가정 */
    { "NPU1",    32, false, false },   /* rev2 에 들어올 외부 IP — 32비트 주소 */
};

/* ======================= 영역 표 (주어짐) ======================= */
enum {
    A_DEV    = 1u << 0,  /* device 메모리 (아니면 normal) */
    A_SEC    = 1u << 1,  /* secure 트랜잭션만 (TrustZone 필터) */
    A_CACHE  = 1u << 2,  /* CPU 가 normal cacheable 로 매핑 */
    A_SWCMO  = 1u << 3,  /* non-coherent 마스터와 공유: 소프트웨어 clean/invalidate 계약 (ARCH-M12) */
    A_RO     = 1u << 4,  /* 읽기 전용 (ROM) — 쓰기는 슬레이브가 SLVERR */
    A_POW2   = 1u << 5,  /* 크기 2^k, base 는 크기의 배수 (상위 비트 비교 디코더 / 방화벽 영역 규칙) */
    A_BRIDGE = 1u << 6,  /* APB 브리지 창: 안쪽은 자식 영역으로 2단계 디코드 */
};
typedef struct {
    const char *name;
    uint64_t base, size;     /* [base, base+size), 64비트라 2^32 / 2^40 끝에서도 감기지 않는다 */
    const char *port;        /* 슬레이브 포트 (DDR 처럼 한 포트에 여러 영역 가능) */
    unsigned attr;
    unsigned masters;        /* 접근 허용 마스터 비트마스크 = 인터커넥트 라우팅 + 필터 */
    const char *parent;      /* NULL = 메인 인터커넥트, 아니면 APB 브리지 창 이름 */
} region_t;

typedef enum { DBG_LOCKED, DBG_NS, DBG_FULL } dbg_t;   /* 디버그 잠금: 잠김 / NS 만 (DBGEN) / 전부 (+SPIDEN) */
typedef struct { region_t r[MAXR]; int n; unsigned pa_bits; dbg_t dbg; } soc_t;

static uint64_t rend(const region_t *r) { return r->base + r->size; }
static bool is_pow2(uint64_t x) { return x != 0 && (x & (x - 1)) == 0; }
static int find_name(const soc_t *s, const char *name) {
    for (int i = 0; i < s->n; i++) if (strcmp(s->r[i].name, name) == 0) return i;
    return -1;
}
/* -1 = 최상위(메인 인터커넥트), -2 = 부모 이름이 표에 없음, 그 외 = 부모 인덱스 */
static int parent_of(const soc_t *s, int i) {
    if (!s->r[i].parent) return -1;
    int p = find_name(s, s->r[i].parent);
    return p >= 0 ? p : -2;
}

/* ======================= Part A — 2단계 디코드 ======================= */
typedef enum { RESP_OKAY, RESP_SLVERR, RESP_DECERR } resp_t;
typedef enum { WHY_OK, WHY_UNMAPPED, WHY_NOROUTE, WHY_DBGLOCK, WHY_TZ, WHY_HOLE, WHY_FILTER, WHY_RO } why_t;
typedef struct { resp_t resp; why_t why; int region; } dec_t;   /* region = 가장 안쪽까지 디코드된 영역, 없으면 -1 */

/* level 단계(-1 = 메인 인터커넥트, 그 외 = 그 브리지 창의 자식들)에서 addr 를 포함하는 영역, 없으면 -1.
 * 겹침이 있으면 표에서 먼저 나온 것 (겹침은 검사기가 따로 잡는다). */
static int find_region(const soc_t *s, uint64_t addr, int level) {
    (void)s; (void)addr; (void)level; (void)parent_of;
    return -1; /* TODO: level 단계(parent_of == level)에서 addr - base < size 인 첫 영역 */
}

/* 순서 고정:
 *   ① 디버그 마스터: 잠김, 또는 NS 만 풀렸는데 secure 요청 → DECERR DBGLOCK
 *   ② secure 를 못 내는 마스터는 secure=false 로 고정 (AxPROT[1] 배선)
 *   ③ addr ≥ 2^pa 또는 최상위 영역 없음 → DECERR UNMAPPED (기본 슬레이브)
 *   ④ 최상위 영역이 이 마스터를 허용 안 함 → DECERR NOROUTE (경로 없음 = 그 마스터의 기본 슬레이브)
 *   ⑤ 이제 슬레이브 쪽: secure 영역에 NS 접근 → SLVERR TZ
 *   ⑥ 브리지 창이면 자식 찾기: 없음 → SLVERR HOLE, 마스터 불허 → SLVERR FILTER, secure 자식에 NS → SLVERR TZ
 *   ⑦ RO 에 쓰기 → SLVERR RO,  그 외 OKAY */
static dec_t decode(const soc_t *s, uint64_t addr, int m, bool secure, bool write) {
    dec_t d = { RESP_DECERR, WHY_UNMAPPED, -1 };
    (void)s; (void)addr; (void)m; (void)secure; (void)write;
    return d; /* TODO: ①..⑦ 순서로 — 디버그 잠금, NS 고정, 최상위 디코드, 라우팅, TZ, 브리지 자식, RO */
}

/* ======================= Part B — 맵 검사기 ======================= */
typedef enum { D_SIZE, D_POW2, D_ALIGN, D_RANGE, D_ATTR, D_APB, D_ROUTE, D_SECM, D_OVERLAP,
               W_NONCOH, W_REACH, D_NCODE } dcode_t;
static const char *DNAME[D_NCODE] = { "SIZE", "POW2", "ALIGN", "RANGE", "ATTR", "APB", "ROUTE", "SECURE",
                                      "OVERLAP", "NONCOH", "REACH" };
typedef struct { dcode_t code; int a, b; uint64_t addr; } diag_t;   /* b = 두 번째 영역 또는 마스터, 없으면 -1 */

static void emit(diag_t *out, int max, int *n, dcode_t c, int a, int b, uint64_t addr) {
    if (*n < max) { out[*n].code = c; out[*n].a = a; out[*n].b = b; out[*n].addr = addr; }
    (*n)++;
}

/* 영역마다 표 순서로:
 *   기하 (첫 실패 하나만): size 0 또는 4 KB 배수 아님 → SIZE / POW2 영역인데 2^k 아님 → POW2 /
 *                          base 가 4 KB 정렬 아님 또는 POW2 영역인데 크기 정렬 아님 → ALIGN
 *   [base, base+size) 가 [0, 2^pa) 밖 → RANGE
 *   device 인데 cacheable(또는 SWCMO) → ATTR
 *   부모가 있으면: 부모 없음 / 브리지 아님 / 창 밖 → APB (b = 부모 또는 -1), 아니면 부모가 라우팅 안 하는 마스터마다 → ROUTE
 *   허용 마스터마다: secure 영역 + NS 전용 마스터 → SECURE / normal cacheable(SWCMO 아님) + non-coherent → NONCOH(경고) /
 *                    PA 안의 영역인데 끝 > 2^addr_bits → REACH(경고)
 * 그다음 같은 단계(같은 부모)의 모든 쌍 i<j 에서 겹치면 → OVERLAP (a=i, b=j, addr = 첫 겹침 바이트).
 * 반환 = 진단 수 (max 를 넘으면 앞 max 개만 저장). */
static int map_check(const soc_t *s, diag_t *out, int max) {
    (void)s; (void)out; (void)max; (void)emit; (void)is_pow2;
    return 0; /* TODO: 영역별 검사(기하·범위·속성·APB/ROUTE·마스터) → 같은 단계 쌍의 겹침 */
}

/* ======================= Part C — 정렬 출력과 예약 구멍 ======================= */
/* level 단계의 영역 인덱스를 base 오름차순으로 idx 에 채운다. 반환 = 개수 */
static int level_sorted(const soc_t *s, int level, int *idx) {
    (void)s; (void)level; (void)idx;
    return 0; /* TODO: parent_of == level 인 인덱스를 모아 base 오름차순 (같으면 표 순서) */
}

typedef struct { int level; uint64_t lo, hi; } gap_t;  /* [lo, hi) 비어 있음. level -1 = 최상위, 그 외 = 브리지 창 */

/* 예약 구멍: 먼저 최상위 [0, 2^pa) 안의 빈 구간 (주소 순), 그다음 브리지 창마다 (창의 base 순) 창 안의 빈 구간.
 * 겹침이 없는 맵을 가정. 반환 = 구멍 수 (max 를 넘으면 앞 max 개만 저장). */
static int map_gaps(const soc_t *s, gap_t *g, int max) {
    (void)s; (void)g; (void)max;
    return 0; /* TODO: 최상위 [0, 2^pa) 의 빈 구간 → 브리지 창마다 창 안의 빈 구간 */
}

/* 출력 순서 (주어짐): 최상위를 base 순으로, 브리지 창 바로 뒤에 그 자식들을 base 순으로 */
static int map_sorted(const soc_t *s, int *out) {
    int top[MAXR], nt = level_sorted(s, -1, top), n = 0;
    for (int t = 0; t < nt; t++) {
        out[n++] = top[t];
        if (s->r[top[t]].attr & A_BRIDGE) {
            int ch[MAXR], k = level_sorted(s, top[t], ch);
            for (int c = 0; c < k; c++) out[n++] = ch[c];
        }
    }
    return n;
}

/* ======================= 출력 도우미 (주어짐) ======================= */
static const char *fmt_pa(char *b, uint64_t a) {      /* 0x01_4000_0000 */
    snprintf(b, 20, "0x%02llx_%04llx_%04llx", (unsigned long long)(a >> 32),
             (unsigned long long)((a >> 16) & 0xFFFFu), (unsigned long long)(a & 0xFFFFu));
    return b;
}
static const char *fmt_size(char *b, uint64_t z) {
    if (z && z % GB(1) == 0) snprintf(b, 12, "%lluG", (unsigned long long)(z >> 30));
    else if (z && z % MB(1) == 0) snprintf(b, 12, "%lluM", (unsigned long long)(z >> 20));
    else if (z && z % KB(1) == 0) snprintf(b, 12, "%lluK", (unsigned long long)(z >> 10));
    else snprintf(b, 12, "%lluB", (unsigned long long)z);
    return b;
}
static void print_map(const soc_t *s) {
    int idx[MAXR], k = map_sorted(s, idx);
    char b1[24], b2[24], b3[16];
    printf("  %-15s %-15s %5s  %-14s %-7s %-12s %s\n", "base", "last", "size", "name", "port", "attr", "B L D I N G 1");
    for (int t = 0; t < k; t++) {
        const region_t *r = &s->r[idx[t]];
        char attr[16], ms[2 * M_N + 1], nm[24];
        snprintf(attr, sizeof attr, "%s %c %s%s%s", (r->attr & A_DEV) ? "DEV" : "NRM", (r->attr & A_SEC) ? 'S' : '-',
                 (r->attr & A_CACHE) ? ((r->attr & A_SWCMO) ? "C*" : "C ") : "- ",
                 (r->attr & A_RO) ? " RO" : "", (r->attr & A_BRIDGE) ? " BR" : "");
        for (int m = 0; m < M_N; m++) { ms[2 * m] = (r->masters & BIT(m)) ? 'x' : '.'; ms[2 * m + 1] = ' '; }
        ms[2 * M_N - 1] = '\0';
        snprintf(nm, sizeof nm, "%s%s", r->parent ? "  " : "", r->name);
        printf("  %-15s %-15s %5s  %-14s %-7s %-12s %s\n", fmt_pa(b1, r->base), fmt_pa(b2, rend(r) - 1),
               fmt_size(b3, r->size), nm, r->port, attr, ms);
    }
}
static void print_diags(const soc_t *s, const diag_t *d, int n) {
    char b[24];
    for (int i = 0; i < n; i++) {
        const char *sev = d[i].code >= W_NONCOH ? "WARN " : "ERROR";
        printf("  %s %-8s %-12s @%s", sev, DNAME[d[i].code], s->r[d[i].a].name, fmt_pa(b, d[i].addr));
        if (d[i].code == D_OVERLAP || d[i].code == D_APB)
            printf("  with %s", d[i].b >= 0 ? s->r[d[i].b].name : "(no such window)");
        else if (d[i].b >= 0) printf("  master %s", MASTERS[d[i].b].name);
        printf("\n");
    }
}

/* ======================= 설계 맵 v1 (주어짐 — §5 Part A 설계 문서와 같은 데이터) ======================= */
#define CPU   (BIT(M_CPUB) | BIT(M_CPUL))
#define DBGM  BIT(M_DBG)
static const region_t V1_REGIONS[] = {
/*  name           base                 size        port      attr                              masters                                         parent */
  { "BOOTROM",     0x0000000000ull,     KB(64),     "ROM",    A_SEC | A_RO | A_POW2,            CPU | DBGM,                                     NULL },
  { "SEC_SRAM",    0x0004000000ull,     KB(256),    "SRAM_S", A_SEC | A_CACHE | A_POW2,         CPU | DBGM,                                     NULL },
  { "SYS_SRAM",    0x0008000000ull,     MB(2),      "OCRAM",  A_CACHE | A_SWCMO | A_POW2,       CPU | BIT(M_DMA) | BIT(M_ISP) | BIT(M_NPU0) | DBGM, NULL },
  { "APB_NS",      0x0010000000ull,     MB(1),      "APB0",   A_DEV | A_BRIDGE | A_POW2,        CPU | BIT(M_DMA) | DBGM,                        NULL },
  { "APB_SEC",     0x0011000000ull,     MB(1),      "APB1",   A_DEV | A_SEC | A_BRIDGE | A_POW2, CPU | DBGM,                                    NULL },
  { "NPU0_CSR",    0x0020000000ull,     KB(64),     "CFG",    A_DEV | A_POW2,                   CPU | DBGM,                                     NULL },
  { "ISP_CSR",     0x0020010000ull,     KB(64),     "CFG",    A_DEV | A_POW2,                   CPU | DBGM,                                     NULL },
  { "DMA_CSR",     0x0020020000ull,     KB(64),     "CFG",    A_DEV | A_POW2,                   CPU | DBGM,                                     NULL },
  { "GIC",         0x0028000000ull,     MB(2),      "GIC",    A_DEV | A_POW2,                   CPU | DBGM,                                     NULL },
  { "CORESIGHT",   0x0030000000ull,     MB(16),     "DBGAPB", A_DEV | A_POW2,                   CPU | DBGM,                                     NULL },
  { "DRAM_MAIN",   0x0080000000ull,     GB(3),      "DDR",    A_CACHE,                          CPU | BIT(M_DMA) | DBGM,                        NULL },
  { "DRAM_SHARED", 0x0140000000ull,     MB(960),    "DDR",    A_CACHE | A_SWCMO,                CPU | BIT(M_DMA) | BIT(M_ISP) | BIT(M_NPU0) | DBGM, NULL },
  { "DRAM_SEC",    0x017C000000ull,     MB(64),     "DDR",    A_SEC | A_CACHE | A_POW2,         CPU | DBGM,                                     NULL },
  /* APB_NS 안 (주변장치 4 KB 슬롯) */
  { "UART0",       0x0010000000ull,     KB(4),      "APB0",   A_DEV,                            CPU | BIT(M_DMA) | DBGM,                        "APB_NS" },
  { "UART1",       0x0010001000ull,     KB(4),      "APB0",   A_DEV,                            CPU | BIT(M_DMA) | DBGM,                        "APB_NS" },
  { "SPI0",        0x0010002000ull,     KB(4),      "APB0",   A_DEV,                            CPU | BIT(M_DMA) | DBGM,                        "APB_NS" },
  { "SPI1",        0x0010003000ull,     KB(4),      "APB0",   A_DEV,                            CPU | BIT(M_DMA) | DBGM,                        "APB_NS" },
  { "I2C0",        0x0010004000ull,     KB(4),      "APB0",   A_DEV,                            CPU | BIT(M_DMA) | DBGM,                        "APB_NS" },
  { "I2C1",        0x0010005000ull,     KB(4),      "APB0",   A_DEV,                            CPU | BIT(M_DMA) | DBGM,                        "APB_NS" },
  { "GPIO",        0x0010006000ull,     KB(4),      "APB0",   A_DEV,                            CPU | DBGM,                                     "APB_NS" },
  { "TIMER0",      0x0010007000ull,     KB(4),      "APB0",   A_DEV,                            CPU | DBGM,                                     "APB_NS" },
  { "TIMER1",      0x0010008000ull,     KB(4),      "APB0",   A_DEV,                            CPU | DBGM,                                     "APB_NS" },
  { "WDT",         0x0010009000ull,     KB(4),      "APB0",   A_DEV,                            CPU | DBGM,                                     "APB_NS" },
  { "SYSCTRL",     0x001000A000ull,     KB(4),      "APB0",   A_DEV,                            CPU | DBGM,                                     "APB_NS" },
  /* APB_SEC 안 (secure 주변장치) */
  { "KEYSTORE",    0x0011000000ull,     KB(4),      "APB1",   A_DEV | A_SEC,                    CPU,                                            "APB_SEC" },
  { "OTP",         0x0011001000ull,     KB(4),      "APB1",   A_DEV | A_SEC,                    CPU | DBGM,                                     "APB_SEC" },
  { "TRNG",        0x0011002000ull,     KB(4),      "APB1",   A_DEV | A_SEC,                    CPU,                                            "APB_SEC" },
  { "TZC_CFG",     0x0011003000ull,     KB(4),      "APB1",   A_DEV | A_SEC,                    CPU,                                            "APB_SEC" },
};

static soc_t make_soc(const region_t *r, int n, unsigned pa_bits) {
    soc_t s;
    memset(&s, 0, sizeof s);
    memcpy(s.r, r, (size_t)n * sizeof *r);
    s.n = n; s.pa_bits = pa_bits; s.dbg = DBG_LOCKED;
    return s;
}
static soc_t make_v1(void) { return make_soc(V1_REGIONS, (int)(sizeof V1_REGIONS / sizeof V1_REGIONS[0]), 40); }
static void add_region(soc_t *s, region_t r) { if (s->n < MAXR) s->r[s->n++] = r; }
#define IX(s, nm) find_name(&(s), nm)

/* rev2 변경 요청: LPDDR 4 → 8 GB, NPU1 추가 (32비트 주소, non-coherent, CPU·ISP 와 버퍼 공유, 큰 가중치) */
static soc_t make_rev2_naive(void) {
    soc_t s = make_v1();
    s.r[IX(s, "DRAM_MAIN")].size += GB(4);                                  /* "DRAM 이 커졌으니 MAIN 을 늘리자" */
    add_region(&s, (region_t){ "NPU1_CSR", 0x0020010000ull, KB(64), "CFG", A_DEV | A_POW2, CPU | DBGM, NULL });
    s.r[IX(s, "DRAM_SHARED")].masters |= BIT(M_NPU1);                       /* NPU0 와 같은 버퍼 */
    s.r[IX(s, "SYS_SRAM")].masters |= BIT(M_NPU1);
    s.r[IX(s, "DRAM_MAIN")].masters |= BIT(M_NPU1);                         /* "가중치가 크니 MAIN 에도" */
    return s;
}
static soc_t make_rev2_fixed(void) {
    soc_t s = make_v1();
    region_t *sh = &s.r[IX(s, "DRAM_SHARED")], *mn = &s.r[IX(s, "DRAM_MAIN")], *sc = &s.r[IX(s, "DRAM_SEC")];
    sh->base = 0x0080000000ull; sh->size = GB(1);                           /* 공유 버퍼를 4 GB 아래 맨 앞으로 */
    mn->base = 0x00C0000000ull; mn->size = GB(8) - GB(1) - MB(64);
    sc->base = 0x027C000000ull;                                            /* secure carve-out 은 DRAM 맨 위 */
    sh->masters |= BIT(M_NPU1);
    s.r[IX(s, "SYS_SRAM")].masters |= BIT(M_NPU1);
    add_region(&s, (region_t){ "NPU1_CSR", 0x0020030000ull, KB(64), "CFG", A_DEV | A_POW2, CPU | DBGM, NULL });
    return s;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs_state = 0xA12C0DE5u;
static uint32_t xorshift32(void) {
    uint32_t x = xs_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return xs_state = x;
}

/* 정확히 진단 하나, 그 코드·영역·상대 */
static bool expect_one(const soc_t *s, dcode_t code, const char *a, int b) {
    diag_t d[16];
    int n = map_check(s, d, 16);
    bool ok = n == 1 && d[0].code == code && d[0].a == find_name(s, a) && d[0].b == b;
    if (ok) print_diags(s, d, 1);
    else { printf("  expect_one(%s, %s) got %d diag(s):\n", DNAME[code], a, n); print_diags(s, d, n < 16 ? n : 16); }
    return ok;
}
static bool dec_is(const soc_t *s, uint64_t addr, int m, bool sec, bool wr, resp_t resp, why_t why, const char *rname) {
    dec_t d = decode(s, addr, m, sec, wr);
    return d.resp == resp && d.why == why && d.region == (rname ? find_name(s, rname) : -1);
}

/* 독립 참조 디코드 (테스트 전용): 포함 체인을 64비트 비교로 모으고, 허용 = 모든 단계의 마스크 ∧ 보안 ∧ RO 규칙 */
static resp_t ref_decode(const soc_t *s, uint64_t addr, int m, bool sec, bool wr) {
    if (m == M_DBG && (s->dbg == DBG_LOCKED || (s->dbg == DBG_NS && sec))) return RESP_DECERR;
    bool S = sec && MASTERS[m].secure_ok;
    const region_t *top = NULL, *leaf = NULL;
    for (int i = 0; i < s->n; i++) {
        const region_t *r = &s->r[i];
        if (!(addr >= r->base && addr < r->base + r->size)) continue;
        if (!r->parent) top = r;
    }
    if (!top || addr >= ((uint64_t)1 << s->pa_bits)) return RESP_DECERR;
    if (!(top->masters & BIT(m))) return RESP_DECERR;
    if (top->attr & A_BRIDGE) {
        for (int i = 0; i < s->n; i++)
            if (s->r[i].parent && !strcmp(s->r[i].parent, top->name) && addr >= s->r[i].base && addr < rend(&s->r[i]))
                leaf = &s->r[i];
        if (!leaf) return RESP_SLVERR;
    } else leaf = top;
    unsigned sec_need = (top->attr | leaf->attr) & A_SEC;
    if (!(leaf->masters & BIT(m)) || (sec_need && !S) || (wr && (leaf->attr & A_RO))) return RESP_SLVERR;
    return RESP_OKAY;
}

/* 페이지 칠하기 참조: pa_bits = 24 (4096 페이지 × 4 KB)의 무작위 최상위 맵. 영역마다 페이지 비트맵을 칠해서
 * 겹침 쌍 = 비트맵 AND ≠ 0, 첫 겹침 주소 = AND 의 첫 비트, 디코드 주인 = 그 페이지를 칠한 첫 영역 — 구간 산술을 안 쓴다 */
#define FZ_PAGES 4096
#define FZ_WORDS (FZ_PAGES / 64)
static int fuzz_mismatches(int maps, int *overlap_maps) {
    static const char *NM[12] = { "r0","r1","r2","r3","r4","r5","r6","r7","r8","r9","r10","r11" };
    static uint64_t bm[12][FZ_WORDS];
    int bad = 0;
    *overlap_maps = 0;
    for (int t = 0; t < maps; t++) {
        soc_t s;
        memset(&s, 0, sizeof s);
        memset(bm, 0, sizeof bm);
        s.pa_bits = 24; s.dbg = DBG_LOCKED;
        s.n = 4 + (int)(xorshift32() % 9u);
        for (int i = 0; i < s.n; i++) {
            uint32_t b = xorshift32() % FZ_PAGES, len = 1 + xorshift32() % ((t & 1) ? 512u : 64u);
            if (b + len > FZ_PAGES) len = FZ_PAGES - b;
            s.r[i] = (region_t){ NM[i], (uint64_t)b * PAGE, (uint64_t)len * PAGE, "X", 0, BIT(M_CPUB), NULL };
            for (uint32_t p = b; p < b + len; p++) bm[i][p / 64] |= 1ull << (p % 64);
        }
        int want = 0, nov = 0;
        int64_t first[12][12];
        for (int i = 0; i < s.n; i++)
            for (int j = i + 1; j < s.n; j++) {
                first[i][j] = -1;
                for (int w = 0; w < FZ_WORDS && first[i][j] < 0; w++) {
                    uint64_t x = bm[i][w] & bm[j][w];
                    if (x) first[i][j] = (int64_t)(w * 64 + __builtin_ctzll(x)) * (int64_t)PAGE;
                }
                want += first[i][j] >= 0;
            }
        diag_t d[80];
        int nd = map_check(&s, d, 80);
        for (int k = 0; k < nd && k < 80; k++) {
            if (d[k].code != D_OVERLAP || d[k].a >= d[k].b) { bad++; continue; }
            if (first[d[k].a][d[k].b] != (int64_t)d[k].addr) bad++;
            nov++;
        }
        if (nov != want || nd > 80) bad++;
        if (want) (*overlap_maps)++;
        for (int k = 0; k < 64; k++) {                  /* 디코드 = 그 페이지를 칠한 첫 영역 (겹치면 표에서 먼저) */
            uint64_t a = xorshift32() % ((uint32_t)FZ_PAGES * (uint32_t)PAGE);
            uint64_t pg = a / PAGE;
            int o = -1;
            for (int i = 0; i < s.n && o < 0; i++) if (bm[i][pg / 64] >> (pg % 64) & 1u) o = i;
            dec_t dd = decode(&s, a, M_CPUB, false, false);
            if (dd.region != o || (o < 0 ? dd.resp != RESP_DECERR : dd.resp != RESP_OKAY)) bad++;
        }
    }
    return bad;
}

static const char *RESP3[3] = { "ok", "SE", "DE" };

int main(void) {
    soc_t v1 = make_v1();
    diag_t d[32];
    char b1[24], b2[24];

    /* ---------------- Part A: 디코드와 접근 행렬 ---------------- */
    printf("== Part A: decode (v1, 40-bit PA) ==\n");
    CHECK(find_region(&v1, 0x0000000000ull, -1) == IX(v1, "BOOTROM"));
    CHECK(find_region(&v1, 0x000000FFFFull, -1) == IX(v1, "BOOTROM"));
    CHECK(find_region(&v1, 0x0000010000ull, -1) == -1);
    CHECK(find_region(&v1, 0x0010000000ull, -1) == IX(v1, "APB_NS"));
    CHECK(find_region(&v1, 0x0010000000ull, IX(v1, "APB_NS")) == IX(v1, "UART0"));
    CHECK(find_region(&v1, 0x001000AFFFull, IX(v1, "APB_NS")) == IX(v1, "SYSCTRL"));
    CHECK(find_region(&v1, 0x001000B000ull, IX(v1, "APB_NS")) == -1);
    CHECK(find_region(&v1, 0x013FFFFFFFull, -1) == IX(v1, "DRAM_MAIN"));
    CHECK(find_region(&v1, 0x0140000000ull, -1) == IX(v1, "DRAM_SHARED"));
    CHECK(find_region(&v1, 0x017FFFFFFFull, -1) == IX(v1, "DRAM_SEC"));
    CHECK(find_region(&v1, 0x0180000000ull, -1) == -1);

    /* OKAY 경로 */
    CHECK(dec_is(&v1, 0x0000000000ull, M_CPUB, true,  false, RESP_OKAY,   WHY_OK,       "BOOTROM"));
    CHECK(dec_is(&v1, 0x0080000000ull, M_CPUL, false, true,  RESP_OKAY,   WHY_OK,       "DRAM_MAIN"));
    CHECK(dec_is(&v1, 0x0150000000ull, M_NPU0, false, true,  RESP_OKAY,   WHY_OK,       "DRAM_SHARED"));
    CHECK(dec_is(&v1, 0x0008100000ull, M_ISP,  false, true,  RESP_OKAY,   WHY_OK,       "SYS_SRAM"));
    CHECK(dec_is(&v1, 0x0010001004ull, M_DMA,  false, true,  RESP_OKAY,   WHY_OK,       "UART1"));
    CHECK(dec_is(&v1, 0x0011000000ull, M_CPUB, true,  false, RESP_OKAY,   WHY_OK,       "KEYSTORE"));
    CHECK(dec_is(&v1, 0x017C000000ull, M_CPUB, true,  true,  RESP_OKAY,   WHY_OK,       "DRAM_SEC"));
    /* DECERR: 기본 슬레이브 */
    CHECK(dec_is(&v1, 0x0000010000ull, M_CPUB, false, false, RESP_DECERR, WHY_UNMAPPED, NULL));
    CHECK(dec_is(&v1, 0x0180000000ull, M_CPUB, false, false, RESP_DECERR, WHY_UNMAPPED, NULL));
    CHECK(dec_is(&v1, 0x10000000000ull, M_CPUB, false, false, RESP_DECERR, WHY_UNMAPPED, NULL));   /* = 2^40 */
    CHECK(dec_is(&v1, 0x017C000000ull, M_NPU0, false, false, RESP_DECERR, WHY_NOROUTE,  "DRAM_SEC"));
    CHECK(dec_is(&v1, 0x0004000000ull, M_NPU0, false, false, RESP_DECERR, WHY_NOROUTE,  "SEC_SRAM"));
    CHECK(dec_is(&v1, 0x0080000000ull, M_ISP,  false, true,  RESP_DECERR, WHY_NOROUTE,  "DRAM_MAIN"));
    CHECK(dec_is(&v1, 0x0010000000ull, M_DBG,  false, false, RESP_DECERR, WHY_DBGLOCK,  NULL));     /* 잠김 */
    /* SLVERR: 슬레이브(또는 그 앞 필터/브리지)가 거부 */
    CHECK(dec_is(&v1, 0x017C000000ull, M_CPUB, false, false, RESP_SLVERR, WHY_TZ,       "DRAM_SEC"));
    CHECK(dec_is(&v1, 0x0011000000ull, M_CPUL, false, false, RESP_SLVERR, WHY_TZ,       "APB_SEC"));
    CHECK(dec_is(&v1, 0x001000B000ull, M_CPUB, false, false, RESP_SLVERR, WHY_HOLE,     "APB_NS"));
    CHECK(dec_is(&v1, 0x0010006000ull, M_DMA,  false, false, RESP_SLVERR, WHY_FILTER,   "GPIO"));
    CHECK(dec_is(&v1, 0x0000000100ull, M_CPUB, true,  true,  RESP_SLVERR, WHY_RO,       "BOOTROM"));
    /* NS 전용 마스터가 secure=true 를 내도 NS 로 고정 */
    CHECK(dec_is(&v1, 0x0150000000ull, M_NPU0, true,  false, RESP_OKAY,   WHY_OK,       "DRAM_SHARED"));
    /* 디버그 잠금 단계 */
    soc_t dbg = v1;
    dbg.dbg = DBG_NS;
    CHECK(dec_is(&dbg, 0x0010000000ull, M_DBG, false, false, RESP_OKAY,   WHY_OK,       "UART0"));
    CHECK(dec_is(&dbg, 0x0004000000ull, M_DBG, true,  false, RESP_DECERR, WHY_DBGLOCK,  NULL));
    CHECK(dec_is(&dbg, 0x0004000000ull, M_DBG, false, false, RESP_SLVERR, WHY_TZ,       "SEC_SRAM"));
    dbg.dbg = DBG_FULL;
    CHECK(dec_is(&dbg, 0x0004000000ull, M_DBG, true,  false, RESP_OKAY,   WHY_OK,       "SEC_SRAM"));
    CHECK(dec_is(&dbg, 0x0011001000ull, M_DBG, true,  false, RESP_OKAY,   WHY_OK,       "OTP"));
    CHECK(dec_is(&dbg, 0x0011000000ull, M_DBG, true,  false, RESP_SLVERR, WHY_FILTER,   "KEYSTORE"));   /* 키는 디버그로 절대 */

    /* 접근 행렬: 각 영역의 base 를 읽는다. CPU 는 NS/S 둘 다, 디버그는 FULL+secure 와 LOCKED */
    printf("  access matrix (read at base): ok / SE = SLVERR / DE = DECERR\n");
    printf("  %-12s %4s %4s %4s %4s %4s %4s %4s %4s %4s\n", "region", "B-NS", "B-S", "L-NS", "DMA", "ISP", "NPU0",
           "G-S", "G-lk", "NPU1");
    int idx[MAXR], k = map_sorted(&v1, idx);
    for (int t = 0; t < k; t++) {
        const region_t *r = &v1.r[idx[t]];
        if (r->attr & A_BRIDGE) continue;
        dec_t c[9] = { decode(&v1, r->base, M_CPUB, false, false), decode(&v1, r->base, M_CPUB, true, false),
                       decode(&v1, r->base, M_CPUL, false, false), decode(&v1, r->base, M_DMA, false, false),
                       decode(&v1, r->base, M_ISP, false, false),  decode(&v1, r->base, M_NPU0, false, false),
                       decode(&dbg, r->base, M_DBG, true, false),  decode(&v1, r->base, M_DBG, true, false),
                       decode(&v1, r->base, M_NPU1, false, false) };
        printf("  %-12s", r->name);
        for (int q = 0; q < 9; q++) printf(" %4s", RESP3[c[q].resp]);
        printf("\n");
    }

    /* 참조 디코드와 비교: 모든 영역 경계 ±1 × 마스터 7 × secure 2 × R/W 2 × 디버그 상태 3 */
    int dmis = 0, probes = 0;
    for (int ds = 0; ds < 3; ds++) {
        soc_t s = v1;
        s.dbg = (dbg_t)ds;
        for (int i = 0; i < s.n; i++) {
            uint64_t pts[4] = { s.r[i].base - 1, s.r[i].base, rend(&s.r[i]) - 1, rend(&s.r[i]) };
            for (int p = 0; p < 4; p++)
                for (int m = 0; m < M_N; m++)
                    for (int sec = 0; sec < 2; sec++)
                        for (int wr = 0; wr < 2; wr++) {
                            probes++;
                            if (decode(&s, pts[p], m, sec, wr).resp != ref_decode(&s, pts[p], m, sec, wr)) dmis++;
                        }
        }
    }
    printf("  decode vs reference: %d probes, %d mismatches\n", probes, dmis);
    CHECK(dmis == 0);

    /* ---------------- Part B: 검사기 — 오류 하나씩 주입 ---------------- */
    printf("== Part B: map_check fault injection ==\n");
    int nv1 = map_check(&v1, d, 32);
    CHECK(nv1 == 0);
    { soc_t s = v1; s.r[IX(s, "SPI1")].base = 0x0010002000ull;                 /* SPI0 과 같은 슬롯 */
      CHECK(expect_one(&s, D_OVERLAP, "SPI0", IX(s, "SPI1")));
      map_check(&s, d, 32); CHECK(d[0].addr == 0x0010002000ull); }
    { soc_t s = v1; s.r[IX(s, "DRAM_SHARED")].size += MB(64);                 /* DRAM_SEC 침범 */
      CHECK(expect_one(&s, D_OVERLAP, "DRAM_SHARED", IX(s, "DRAM_SEC")));
      map_check(&s, d, 32); CHECK(d[0].addr == 0x017C000000ull); }
    { soc_t s = v1; s.r[IX(s, "SYS_SRAM")].base = 0x0008100000ull;            /* 4 KB 정렬이지만 2 MB 정렬 아님 */
      CHECK(expect_one(&s, D_ALIGN, "SYS_SRAM", -1)); }
    { soc_t s = v1; s.r[IX(s, "SYSCTRL")].base = 0x001000A800ull;             /* 4 KB 미정렬 주변장치 */
      CHECK(expect_one(&s, D_ALIGN, "SYSCTRL", -1)); }
    { soc_t s = v1; s.r[IX(s, "GIC")].size = MB(3);                           /* POW2 영역인데 3 MB */
      CHECK(expect_one(&s, D_POW2, "GIC", -1)); }
    { soc_t s = v1; s.r[IX(s, "UART0")].size = KB(2);                         /* 4 KB 배수 아님 */
      CHECK(expect_one(&s, D_SIZE, "UART0", -1)); }
    { soc_t s = v1; s.r[IX(s, "DRAM_SEC")].base = 0x10000000000ull;           /* 2^40 에서 시작 */
      CHECK(expect_one(&s, D_RANGE, "DRAM_SEC", -1)); }
    { soc_t s = v1; s.r[IX(s, "DRAM_SEC")].base = 0xFFFC000000ull;            /* 정확히 2^40 에서 끝: OK */
      CHECK(map_check(&s, d, 32) == 0); }
    { soc_t s = v1; s.r[IX(s, "GPIO")].base = 0x0010100000ull;                /* APB_NS 창 밖 */
      CHECK(expect_one(&s, D_APB, "GPIO", IX(s, "APB_NS"))); }
    { soc_t s = v1; s.r[IX(s, "TIMER0")].parent = "APB2";                      /* 없는 창 */
      CHECK(expect_one(&s, D_APB, "TIMER0", -1)); }
    { soc_t s = v1; s.r[IX(s, "NPU0_CSR")].attr |= A_CACHE;                    /* device + cacheable */
      CHECK(expect_one(&s, D_ATTR, "NPU0_CSR", -1)); }
    { soc_t s = v1; s.r[IX(s, "UART0")].masters |= BIT(M_NPU0);                /* 창이 NPU0 을 라우팅 안 함 */
      CHECK(expect_one(&s, D_ROUTE, "UART0", M_NPU0)); }
    { soc_t s = v1; s.r[IX(s, "BOOTROM")].masters |= BIT(M_NPU0);              /* NPU → secure 영역 */
      CHECK(expect_one(&s, D_SECM, "BOOTROM", M_NPU0));
      /* 행렬이 틀려도 TZ 필터가 막는다: NPU0 은 secure=true 를 내도 NS 로 고정 */
      CHECK(dec_is(&s, 0x0000000000ull, M_NPU0, true, false, RESP_SLVERR, WHY_TZ, "BOOTROM")); }
    { soc_t s = v1; s.r[IX(s, "SEC_SRAM")].masters |= BIT(M_NPU0);             /* 한 실수, 두 문제 */
      int n = map_check(&s, d, 32);
      print_diags(&s, d, n);
      CHECK(n == 2 && d[0].code == D_SECM && d[1].code == W_NONCOH && d[1].b == M_NPU0); }
    { soc_t s = v1; s.r[IX(s, "DRAM_MAIN")].masters |= BIT(M_ISP);             /* non-coherent → cacheable */
      CHECK(expect_one(&s, W_NONCOH, "DRAM_MAIN", M_ISP)); }
    { soc_t s = v1; s.r[IX(s, "DRAM_SHARED")].masters |= BIT(M_NPU1);          /* 32비트 마스터 → 4 GB 위 */
      CHECK(expect_one(&s, W_REACH, "DRAM_SHARED", M_NPU1)); }
    { soc_t s = v1; s.pa_bits = 32;                                           /* PA 폭 결정의 근거 */
      int n = map_check(&s, d, 32), nr = 0;
      for (int q = 0; q < n; q++) nr += d[q].code == D_RANGE;
      printf("  v1 with 32-bit PA: %d diagnostics, %d RANGE (DRAM_MAIN/SHARED/SEC)\n", n, nr);
      CHECK(n == 3 && nr == 3); }

    int ovm, fz = fuzz_mismatches(2000, &ovm);
    printf("  random maps vs page-painting reference: 2000 maps (%d with overlaps), %d mismatches\n", ovm, fz);
    CHECK(fz == 0);
    CHECK(ovm > 500 && ovm < 2000);                     /* 겹침 있는 맵과 없는 맵이 둘 다 충분히 나왔다 */

    /* ---------------- Part C: 정렬 표, 예약 구멍, rev2 ---------------- */
    printf("== Part C: v1 map (sorted from the C table) ==\n");
    print_map(&v1);
    int n = map_sorted(&v1, idx);
    CHECK(n == v1.n);
    CHECK(n == v1.n && idx[0] == IX(v1, "BOOTROM") && idx[n - 1] == IX(v1, "DRAM_SEC"));
    CHECK(n == v1.n && idx[3] == IX(v1, "APB_NS") && idx[4] == IX(v1, "UART0") && idx[14] == IX(v1, "SYSCTRL"));
    CHECK(n == v1.n && idx[15] == IX(v1, "APB_SEC") && idx[16] == IX(v1, "KEYSTORE"));

    gap_t g[32];
    int ng = map_gaps(&v1, g, 32);
    printf("  reserved gaps: %d\n", ng);
    for (int q = 0; q < ng && q < 32; q++)
        printf("    %-8s %s .. %s\n", g[q].level < 0 ? "top" : v1.r[g[q].level].name, fmt_pa(b1, g[q].lo),
               fmt_pa(b2, g[q].hi - 1));
    CHECK(ng == 11);
    CHECK(ng >= 11 && g[0].level == -1 && g[0].lo == KB(64) && g[0].hi == 0x0004000000ull);
    CHECK(ng >= 11 && g[8].level == -1 && g[8].lo == 0x0180000000ull && g[8].hi == 0x10000000000ull);
    CHECK(ng >= 11 && g[9].level == IX(v1, "APB_NS") && g[9].lo == 0x001000B000ull && g[9].hi == 0x0010100000ull);
    CHECK(ng >= 11 && g[10].level == IX(v1, "APB_SEC") && g[10].lo == 0x0011004000ull);

    uint64_t ddr = 0;
    for (int q = 0; q < v1.n; q++) if (!strcmp(v1.r[q].port, "DDR")) ddr += v1.r[q].size;
    CHECK(ddr == GB(4));

    printf("== Part C: rev2 change request, naive edit ==\n");
    soc_t r2a = make_rev2_naive();
    int na = map_check(&r2a, d, 32);
    print_diags(&r2a, d, na);
    int cnt[D_NCODE] = { 0 };
    for (int q = 0; q < na && q < 32; q++) cnt[d[q].code]++;
    CHECK(na == 6);
    CHECK(cnt[D_OVERLAP] == 3 && cnt[W_REACH] == 2 && cnt[W_NONCOH] == 1);
    bool ov_isp = false, ov_sh = false, ov_sec = false, rc_main = false;
    for (int q = 0; q < na && q < 32; q++) {
        if (d[q].code == D_OVERLAP && d[q].a == IX(r2a, "ISP_CSR") && d[q].b == IX(r2a, "NPU1_CSR")) ov_isp = true;
        if (d[q].code == D_OVERLAP && d[q].a == IX(r2a, "DRAM_MAIN") && d[q].b == IX(r2a, "DRAM_SHARED")) ov_sh = true;
        if (d[q].code == D_OVERLAP && d[q].a == IX(r2a, "DRAM_MAIN") && d[q].b == IX(r2a, "DRAM_SEC")) ov_sec = true;
        if (d[q].code == W_REACH && d[q].a == IX(r2a, "DRAM_MAIN") && d[q].b == M_NPU1) rc_main = true;
    }
    CHECK(ov_isp && ov_sh && ov_sec && rc_main);

    printf("== Part C: rev2 fixed ==\n");
    soc_t r2 = make_rev2_fixed();
    int nb = map_check(&r2, d, 32);
    print_diags(&r2, d, nb);
    printf("  rev2 fixed: %d diagnostics\n", nb);
    CHECK(nb == 0);
    print_map(&r2);
    ddr = 0;
    for (int q = 0; q < r2.n; q++) if (!strcmp(r2.r[q].port, "DDR")) ddr += r2.r[q].size;
    CHECK(ddr == GB(8));
    CHECK(dec_is(&r2, 0x00BFFFFFFCull, M_NPU1, false, true, RESP_OKAY, WHY_OK, "DRAM_SHARED"));
    CHECK(dec_is(&r2, 0x00C0000000ull, M_NPU1, false, false, RESP_DECERR, WHY_NOROUTE, "DRAM_MAIN"));
    CHECK(dec_is(&r2, 0x0020030000ull, M_CPUB, false, true, RESP_OKAY, WHY_OK, "NPU1_CSR"));
    CHECK(dec_is(&r2, 0x027FFFFFFFull, M_CPUL, true, false, RESP_OKAY, WHY_OK, "DRAM_SEC"));
    ng = map_gaps(&r2, g, 32);
    CHECK(ng == 11 && g[8].lo == 0x0280000000ull);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
