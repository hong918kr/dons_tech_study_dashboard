/* OS-B10 주소 변환 시뮬레이터: 단일 레벨 페이지 테이블
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 *   python3 -I paging-linear-translate.py -s 3 -c | ./solution --ostep -    # OSTEP 출력 대조 모드
 *
 * Part A: 파라미터 검사, VPN/offset 분해, PA 조립
 * Part B: PTE(valid/R/W/X/accessed/dirty) 와 translate() — 폴트 종류 판정, A/D 갱신
 * Part C: OSTEP paging-linear-translate.py 출력 대조 + 펌웨어 메모리 맵(W^X, 가드 페이지)
 */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- PTE 형식: RISC-V Sv32 leaf PTE 와 같은 비트 위치 (U/G/RSW 는 안 씀) ---- */
#define PTE_V (1u << 0)    /* valid (present)            */
#define PTE_R (1u << 1)    /* readable                   */
#define PTE_W (1u << 2)    /* writable                   */
#define PTE_X (1u << 3)    /* executable                 */
#define PTE_A (1u << 6)    /* accessed: 성공한 접근이 있었다 */
#define PTE_D (1u << 7)    /* dirty: 성공한 쓰기가 있었다   */
#define PTE_FLAGS_MASK 0x3FFu
#define PTE_PFN_SHIFT 10
#define PTE_PFN_MAX ((1u << 22) - 1)  /* [31:10] = 22비트 PFN */

typedef enum { ACC_READ, ACC_WRITE, ACC_EXEC } acc_t;
typedef enum { XL_OK, XL_RANGE, XL_INVALID, XL_PROT, XL_BADPFN, XL_NKIND } xl_t;
static const char *const xl_name[XL_NKIND] = {"OK", "RANGE", "INVALID", "PROT", "BADPFN"};

typedef struct {
    uint64_t asize, pgsize, psize;   /* 바이트, 모두 2의 거듭제곱 */
    unsigned off_bits, vpn_bits;
    uint64_t npages, nframes;
    uint32_t *pt;                    /* npages 개의 PTE (호출자 소유 배열) */
    unsigned long stat[XL_NKIND];    /* 결과 종류별 횟수 */
} mmu_t;

/* ================= Part A: 파라미터와 주소 분해 ================= */

/* x 가 2^k 이면 k, 아니면(0 포함) -1 */
static int log2_exact(uint64_t x) {
    (void)x;
    return -1; /* TODO: 2^k 판정 + k 계산 */
}

/* 성공 0, 잘못된 파라미터 -1. 성공하면 pt[0..npages) 를 모두 0(invalid) 으로 */
static int mmu_init(mmu_t *m, uint64_t asize, uint64_t pgsize, uint64_t psize,
                    uint32_t *pt, size_t cap) {
    (void)m; (void)asize; (void)pgsize; (void)psize; (void)pt; (void)cap;
    /* TODO: 세 크기 모두 2의 거듭제곱? 페이지 ≤ 주소 공간·물리 메모리? npages ≤ cap?
     *       nframes-1 ≤ PTE_PFN_MAX? → 필드 채우고 pt 를 0 으로 */
    return -1;
}

static uint64_t va_vpn(const mmu_t *m, uint64_t va) {
    (void)m; (void)va;
    return 0; /* TODO */
}
static uint64_t va_off(const mmu_t *m, uint64_t va) {
    (void)m; (void)va;
    return 0; /* TODO */
}
static uint64_t make_pa(const mmu_t *m, uint64_t pfn, uint64_t off) {
    (void)m; (void)pfn; (void)off;
    return 0; /* TODO */
}

/* ================= Part B: PTE 와 translate ================= */

static uint32_t pte_make(uint64_t pfn, uint32_t flags) {
    (void)pfn; (void)flags;
    return 0; /* TODO: PFN 은 [31:10], 플래그는 [9:0] */
}
static uint64_t pte_pfn(uint32_t pte) {
    (void)pte;
    return 0; /* TODO */
}
static bool perm_ok(uint32_t pte, acc_t acc) {
    (void)pte; (void)acc;
    return false; /* TODO: 접근 종류별로 R/W/X 비트 하나 */
}

/* 검사 순서: RANGE → INVALID → PROT → BADPFN. 성공하면 *pa 를 쓰고 A(쓰기면 D 도)를 켠다.
 * 폴트면 *pa 와 PTE 를 건드리지 않는다. 모든 결과를 m->stat 에 센다. */
static xl_t mmu_translate(mmu_t *m, uint64_t va, acc_t acc, uint64_t *pa) {
    (void)m; (void)va; (void)acc; (void)pa;
    /* TODO: 순서대로 검사하고, 성공이면 A/D 를 켜고 *pa 를 쓴다. m->stat[결과]++ 도 */
    return XL_INVALID;
}

/* ================= Part C: OSTEP 대조 + 펌웨어 메모리 맵 ================= */

/* OSTEP PTE: bit31 = valid, 나머지 = PFN. 보호 비트가 없으므로 RWX 로 옮긴다 */
static uint32_t pte_from_ostep(uint32_t raw) {
    (void)raw;
    return 0; /* TODO: bit31 이 valid, 나머지가 PFN */
}

typedef struct { uint64_t va, pa, len; uint32_t prot; const char *name; } region_t;

/* 전부 성공하면 0, 하나라도 문제면 -1 이고 테이블은 그대로 (all-or-nothing).
 * 거절: len 0 / 페이지 정렬 아님 / VA·PA 범위 밖 / prot 에 R,W,X 외 비트 / W^X 위반(W+X) /
 *       W 인데 R 없음 / 이미 valid 인 페이지 또는 같은 호출의 다른 region 과 겹침 */
static int mmu_map_regions(mmu_t *m, const region_t *r, size_t n) {
    (void)m; (void)r; (void)n;
    /* TODO: 1차 패스 = 전부 검사(하나라도 틀리면 -1, 테이블 안 건드림), 2차 패스 = 페이지마다 PTE 쓰기 */
    return -1;
}

/* ================= OSTEP 출력 파서 (하네스, TODO 아님) ================= */

#define OS_MAX_PTE (1u << 16)
#define OS_MAX_VA  4096
typedef struct {
    uint64_t asize, pgsize, psize;
    size_t npte, nva;
    bool has_answers;
    uint32_t pte[OS_MAX_PTE];
    uint64_t va[OS_MAX_VA], pa[OS_MAX_VA];
    bool valid[OS_MAX_VA];
} ostep_run_t;

/* OSTEP convert(): "16k", "1m", "32", "1g" (대소문자 무관) */
static uint64_t ostep_size(const char *s) {
    char *end = NULL;
    uint64_t v = strtoull(s, &end, 10);
    if (*end == 'k' || *end == 'K') v <<= 10;
    else if (*end == 'm' || *end == 'M') v <<= 20;
    else if (*end == 'g' || *end == 'G') v <<= 30;
    return v;
}

/* 성공 0, 형식 오류 -1 */
static int ostep_parse(const char *text, ostep_run_t *o) {
    memset(o, 0, sizeof *o);
    o->has_answers = true;
    enum { HDR, PT, TRACE, DONE } st = HDR;
    char line[512];
    const char *p = text;
    while (*p && st != DONE) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len); line[len] = '\0';
        p = nl ? nl + 1 : p + strlen(p);
        bool blank = strspn(line, " \t\r") == strlen(line);
        if (st == HDR) {
            if (!strncmp(line, "ARG address space size ", 23)) o->asize = ostep_size(line + 23);
            else if (!strncmp(line, "ARG phys mem size ", 18)) o->psize = ostep_size(line + 18);
            else if (!strncmp(line, "ARG page size ", 14)) o->pgsize = ostep_size(line + 14);
            else if (!strncmp(line, "Page Table", 10)) st = PT;
        } else if (st == PT) {
            if (blank) { if (o->npte) st = TRACE; continue; }
            const char *h = strstr(line, "0x");
            if (!h || o->npte >= OS_MAX_PTE) return -1;
            o->pte[o->npte++] = (uint32_t)strtoul(h, NULL, 16);
        } else {
            if (!strncmp(line, "Virtual Address Trace", 21)) continue;
            if (blank) { if (o->nva) st = DONE; continue; }
            const char *v = strstr(line, "VA 0x");
            const char *arrow = strstr(line, "-->");
            if (!v || !arrow || o->nva >= OS_MAX_VA) return -1;
            size_t i = o->nva++;
            o->va[i] = strtoull(v + 3, NULL, 16);
            if (strstr(arrow, "Invalid")) o->valid[i] = false;
            else if (strstr(arrow, "PA or invalid")) o->has_answers = false;
            else { o->valid[i] = true; o->pa[i] = strtoull(arrow + 3, NULL, 16); }
        }
    }
    return (o->asize && o->pgsize && o->psize && o->npte) ? 0 : -1;
}

/* 내 translate(ACC_READ) 와 OSTEP 답을 비교. 불일치 개수, 설정 실패면 -1 */
static long ostep_crosscheck(const ostep_run_t *o, bool verbose) {
    static uint32_t pt[OS_MAX_PTE];
    mmu_t m;
    memset(&m, 0, sizeof m);
    if (!o->has_answers) return -1;
    if (mmu_init(&m, o->asize, o->pgsize, o->psize, pt, OS_MAX_PTE) != 0) return -1;
    if (m.npages != o->npte) return -1;
    for (size_t i = 0; i < o->npte; i++) pt[i] = pte_from_ostep(o->pte[i]);
    long bad = 0;
    for (size_t i = 0; i < o->nva; i++) {
        uint64_t pa = UINT64_MAX;
        xl_t x = mmu_translate(&m, o->va[i], ACC_READ, &pa);
        bool ok = o->valid[i] ? (x == XL_OK && pa == o->pa[i]) : (x == XL_INVALID);
        if (!ok) bad++;
        if (verbose) {
            char mine[32], theirs[32];
            if (x == XL_OK) snprintf(mine, sizeof mine, "0x%08" PRIx64, pa);
            else snprintf(mine, sizeof mine, "%s", xl_name[x]);
            if (o->valid[i]) snprintf(theirs, sizeof theirs, "0x%08" PRIx64, o->pa[i]);
            else snprintf(theirs, sizeof theirs, "Invalid");
            printf("  VA 0x%08" PRIx64 "  VPN %-5" PRIu64 " mine %-10s ostep %-10s %s\n",
                   o->va[i], o->va[i] / o->pgsize, mine, theirs, ok ? "match" : "MISMATCH");
        }
    }
    return bad;
}

/* ================= 테스트 하네스 ================= */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) {
    uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x;
}

/* 비트 연산 없이 /, % 로만 짠 기준 구현 (같은 검사 순서) */
static xl_t ref_translate(uint64_t asize, uint64_t pgsize, uint64_t nframes, uint32_t *pt,
                          uint64_t va, acc_t acc, uint64_t *pa) {
    if (va >= asize) return XL_RANGE;
    uint32_t e = pt[va / pgsize];
    if (e % 2 == 0) return XL_INVALID;
    uint32_t need = acc == ACC_READ ? 2 : acc == ACC_WRITE ? 4 : 8;
    if ((e / need) % 2 == 0) return XL_PROT;
    uint64_t pfn = e / 1024;
    if (pfn >= nframes) return XL_BADPFN;
    if ((e / 64) % 2 == 0) e += 64;
    if (acc == ACC_WRITE && (e / 128) % 2 == 0) e += 128;
    pt[va / pgsize] = e;
    *pa = pfn * pgsize + va % pgsize;
    return XL_OK;
}

static const char OSTEP_FX1[] =   /* python3 -I paging-linear-translate.py -s 0 -c (도움말 문단 생략) */
    "ARG seed 0\n"
    "ARG address space size 16k\n"
    "ARG phys mem size 64k\n"
    "ARG page size 4k\n"
    "ARG verbose False\n"
    "ARG addresses -1\n"
    "\n"
    "\n"
    "Page Table (from entry 0 down to the max size)\n"
    "  0x8000000c\n"
    "  0x00000000\n"
    "  0x00000000\n"
    "  0x80000006\n"
    "\n"
    "Virtual Address Trace\n"
    "  VA 0x00003229 (decimal:    12841) --> 00006229 (decimal    25129) [VPN 3]\n"
    "  VA 0x00001369 (decimal:     4969) -->  Invalid (VPN 1 not valid)\n"
    "  VA 0x00001e80 (decimal:     7808) -->  Invalid (VPN 1 not valid)\n"
    "  VA 0x00002556 (decimal:     9558) -->  Invalid (VPN 2 not valid)\n"
    "  VA 0x00003a1e (decimal:    14878) --> 00006a1e (decimal    27166) [VPN 3]\n";

static const char OSTEP_FX2[] =   /* ... -P 1k -a 16k -p 32k -v -u 75 -s 2 -c */
    "ARG seed 2\n"
    "ARG address space size 16k\n"
    "ARG phys mem size 32k\n"
    "ARG page size 1k\n"
    "ARG verbose True\n"
    "ARG addresses -1\n"
    "\n"
    "\n"
    "Page Table (from entry 0 down to the max size)\n"
    "  [       0]  0x8000001e\n"
    "  [       1]  0x00000000\n"
    "  [       2]  0x00000000\n"
    "  [       3]  0x80000017\n"
    "  [       4]  0x80000009\n"
    "  [       5]  0x80000013\n"
    "  [       6]  0x80000005\n"
    "  [       7]  0x8000000c\n"
    "  [       8]  0x8000001f\n"
    "  [       9]  0x80000011\n"
    "  [      10]  0x80000008\n"
    "  [      11]  0x00000000\n"
    "  [      12]  0x00000000\n"
    "  [      13]  0x8000000a\n"
    "  [      14]  0x8000001c\n"
    "  [      15]  0x00000000\n"
    "\n"
    "Virtual Address Trace\n"
    "  VA 0x00000186 (decimal:      390) --> 00007986 (decimal    31110) [VPN 0]\n"
    "  VA 0x000014cf (decimal:     5327) --> 00004ccf (decimal    19663) [VPN 5]\n"
    "  VA 0x000008bf (decimal:     2239) -->  Invalid (VPN 2 not valid)\n"
    "  VA 0x000020a7 (decimal:     8359) --> 00007ca7 (decimal    31911) [VPN 8]\n"
    "  VA 0x00003fea (decimal:    16362) -->  Invalid (VPN 15 not valid)\n";

static ostep_run_t g_os;   /* 큰 구조체라 static */

static void test_part_a(void) {
    CHECK(log2_exact(1) == 0);
    CHECK(log2_exact(4096) == 12);
    CHECK(log2_exact(1ull << 40) == 40);
    CHECK(log2_exact(0) == -1);
    CHECK(log2_exact(6144) == -1);

    uint32_t pt[16];
    mmu_t m = {0};
    CHECK(mmu_init(&m, 16384, 4096, 65536, pt, 16) == 0);
    CHECK(m.off_bits == 12 && m.vpn_bits == 2);
    CHECK(m.npages == 4 && m.nframes == 16);
    CHECK(va_vpn(&m, 0x3229) == 3 && va_off(&m, 0x3229) == 0x229);
    CHECK(make_pa(&m, 6, 0x229) == 0x6229);

    /* OSTEP 18장 예: 64B 주소 공간, 16B 페이지, VA 21 → VPN 1 offset 5, PFN 7 → PA 117 */
    mmu_t t = {0};
    CHECK(mmu_init(&t, 64, 16, 128, pt, 16) == 0 && va_vpn(&t, 21) == 1 && va_off(&t, 21) == 5
          && make_pa(&t, 7, 5) == 117);

    /* 32비트 주소 공간 + 1 MB 페이지 (ARM 섹션 크기): 4096 항목 */
    static uint32_t big[4096];
    mmu_t s = {0};
    CHECK(mmu_init(&s, 1ull << 32, 1u << 20, 1ull << 32, big, 4096) == 0
          && va_vpn(&s, 0xFFFFFFFFu) == 4095 && va_off(&s, 0xFFFFFFFFu) == 0xFFFFF);

    mmu_t r = {0};
    CHECK(mmu_init(&r, 12288, 4096, 65536, pt, 16) == -1);          /* 12k: 2의 거듭제곱 아님 */
    CHECK(mmu_init(&r, 4096, 8192, 65536, pt, 16) == -1);           /* 페이지 > 주소 공간 */
    CHECK(mmu_init(&r, 1ull << 32, 4096, 1ull << 32, big, 4096) == -1);  /* 2^20 항목 > cap */
    CHECK(mmu_init(&r, 256, 16, 1ull << 27, pt, 16) == -1             /* 2^23 프레임: 22비트 PFN 초과 */
          && mmu_init(&r, 256, 16, 1ull << 26, pt, 16) == 0);         /* 2^22 프레임: 딱 맞음 */
}

static void test_part_b(void) {
    CHECK(pte_pfn(pte_make(PTE_PFN_MAX, PTE_V | PTE_R)) == PTE_PFN_MAX);
    CHECK(pte_make(5, PTE_V | PTE_R | PTE_W) == ((5u << 10) | 7u));
    CHECK(perm_ok(PTE_V | PTE_R, ACC_READ) && !perm_ok(PTE_V | PTE_R, ACC_WRITE)
          && !perm_ok(PTE_V | PTE_R, ACC_EXEC));
    CHECK(perm_ok(PTE_V | PTE_X, ACC_EXEC) && !perm_ok(PTE_V | PTE_X, ACC_READ));

    uint32_t pt[4];
    mmu_t m = {0};
    if (mmu_init(&m, 16384, 4096, 65536, pt, 4) != 0) memset(&m, 0, sizeof m);
    pt[0] = pte_make(12, PTE_V | PTE_R | PTE_W);
    pt[1] = pte_make(3, PTE_V | PTE_R);
    pt[2] = 0;
    pt[3] = pte_make(20, PTE_V | PTE_R | PTE_W | PTE_X);   /* PFN 20 ≥ 16 프레임 */
    uint64_t pa = 0;
    CHECK(mmu_translate(&m, 0x0123, ACC_READ, &pa) == XL_OK && pa == 0xC123);
    CHECK((pt[0] & PTE_A) && !(pt[0] & PTE_D));
    CHECK(mmu_translate(&m, 0x0456, ACC_WRITE, &pa) == XL_OK && pa == 0xC456 && (pt[0] & PTE_D));
    uint32_t before = pt[1];
    pa = 0xDEAD;
    CHECK(mmu_translate(&m, 0x1000, ACC_WRITE, &pa) == XL_PROT && pt[1] == before);
    CHECK(pa == 0xDEAD);                                   /* 폴트면 *pa 를 안 건드린다 */
    CHECK(mmu_translate(&m, 0x2FFF, ACC_READ, &pa) == XL_INVALID);
    CHECK(mmu_translate(&m, 0x3000, ACC_READ, &pa) == XL_BADPFN && !(pt[3] & PTE_A));
    CHECK(mmu_translate(&m, 0x4000, ACC_READ, &pa) == XL_RANGE);   /* VA == asize */
    CHECK(mmu_translate(&m, 0x1000, ACC_EXEC, &pa) == XL_PROT);
    CHECK(m.stat[XL_OK] == 2 && m.stat[XL_PROT] == 2 && m.stat[XL_INVALID] == 1
          && m.stat[XL_BADPFN] == 1 && m.stat[XL_RANGE] == 1);

    /* 무작위 설정·테이블·접근: /, % 기준 구현과 결과·PA·PTE 가 모두 같아야 한다 */
    static uint32_t pa_tab[256], pb_tab[256];
    uint32_t seed = 0xB10u;
    long mism = 0, n = 0;
    unsigned long seen[XL_NKIND] = {0};
    for (int cfg = 0; cfg < 300; cfg++) {
        unsigned pb = 4 + xs32(&seed) % 11;            /* 16 B .. 16 KB 페이지 */
        unsigned vb = xs32(&seed) % 9;                 /* 1 .. 256 페이지 */
        unsigned fb = vb + 1 + xs32(&seed) % 3;
        uint64_t pg = 1ull << pb, as = pg << vb, ps = pg << fb, nf = 1ull << fb;
        mmu_t q = {0};
        if (mmu_init(&q, as, pg, ps, pa_tab, 256) != 0) { mism++; continue; }
        for (uint64_t i = 0; i < (as >> pb); i++) {
            uint32_t v = xs32(&seed);
            uint32_t flags = (v & 1) ? (PTE_V | (v & (PTE_R | PTE_W | PTE_X | PTE_A | PTE_D))) : 0;
            uint64_t pfn = (v >> 16) % (nf + nf / 4 + 1);  /* 가끔 프레임 밖 */
            pa_tab[i] = pb_tab[i] = pte_make(pfn, flags);
        }
        for (int k = 0; k < 50; k++) {
            uint64_t va = xs32(&seed) % (as + as / 8);     /* 가끔 주소 공간 밖 */
            acc_t acc = (acc_t)(xs32(&seed) % 3);
            uint64_t p1 = 1, p2 = 2;
            xl_t r1 = mmu_translate(&q, va, acc, &p1);
            xl_t r2 = ref_translate(as, pg, nf, pb_tab, va, acc, &p2);
            n++; seen[r2]++;
            if (r1 != r2 || (r2 == XL_OK && p1 != p2)) mism++;
        }
        if (memcmp(pa_tab, pb_tab, (size_t)(as >> pb) * sizeof pa_tab[0]) != 0) mism++;
    }
    printf("random: %ld translations, %ld mismatches (OK=%lu RANGE=%lu INVALID=%lu PROT=%lu BADPFN=%lu)\n",
           n, mism, seen[XL_OK], seen[XL_RANGE], seen[XL_INVALID], seen[XL_PROT], seen[XL_BADPFN]);
    CHECK(n == 15000 && mism == 0);
    CHECK(seen[XL_OK] && seen[XL_RANGE] && seen[XL_INVALID] && seen[XL_PROT] && seen[XL_BADPFN]);
}

static void test_part_c(void) {
    CHECK(pte_from_ostep(0x8000000cu) == pte_make(12, PTE_V | PTE_R | PTE_W | PTE_X));
    CHECK(pte_from_ostep(0) == 0 && pte_from_ostep(0x0000000cu) == 0);

    CHECK(ostep_parse(OSTEP_FX1, &g_os) == 0 && g_os.asize == 16384 && g_os.pgsize == 4096
          && g_os.psize == 65536 && g_os.npte == 4 && g_os.nva == 5 && g_os.has_answers);
    printf("OSTEP fixture 1 (-s 0 -c):\n");
    CHECK(ostep_crosscheck(&g_os, true) == 0);
    CHECK(ostep_parse(OSTEP_FX2, &g_os) == 0 && g_os.npte == 16 && g_os.nva == 5);
    printf("OSTEP fixture 2 (-P 1k -a 16k -p 32k -v -u 75 -s 2 -c):\n");
    CHECK(ostep_crosscheck(&g_os, true) == 0);

    /* 펌웨어 메모리 맵: 64 KB VA, 4 KB 페이지, 512 KB 물리 */
    uint32_t pt[16];
    mmu_t m = {0};
    if (mmu_init(&m, 0x10000, 0x1000, 0x80000, pt, 16) != 0) {
        memset(&m, 0, sizeof m);
        memset(pt, 0, sizeof pt);
    }
    const region_t fw[] = {
        {0x1000, 0x20000, 0x3000, PTE_R | PTE_X, ".text"},
        {0x4000, 0x23000, 0x1000, PTE_R,         ".rodata"},
        {0x5000, 0x40000, 0x3000, PTE_R | PTE_W, ".data+.bss"},
        {0xE000, 0x48000, 0x2000, PTE_R | PTE_W, "stack"},   /* 0xD000 = 가드 페이지 */
    };
    uint64_t pa = 0;
    CHECK(mmu_map_regions(&m, fw, 4) == 0);
    CHECK(mmu_translate(&m, 0x0010, ACC_READ, &pa) == XL_INVALID);         /* NULL 역참조 */
    CHECK(mmu_translate(&m, 0x1234, ACC_EXEC, &pa) == XL_OK && pa == 0x20234);
    CHECK(mmu_translate(&m, 0x1234, ACC_WRITE, &pa) == XL_PROT);           /* 코드 덮어쓰기 */
    CHECK(mmu_translate(&m, 0x5010, ACC_EXEC, &pa) == XL_PROT);            /* 데이터 실행 (XN) */
    CHECK(mmu_translate(&m, 0x4100, ACC_WRITE, &pa) == XL_PROT);           /* const 쓰기 */
    CHECK(mmu_translate(&m, 0xFFFC, ACC_WRITE, &pa) == XL_OK && pa == 0x49FFC);
    CHECK(mmu_translate(&m, 0xDFF8, ACC_WRITE, &pa) == XL_INVALID);        /* 스택 오버플로 */
    unsigned valid = 0, wx = 0, dirty = 0;
    for (int i = 0; i < 16; i++) {
        valid += (pt[i] & PTE_V) != 0;
        wx += (pt[i] & PTE_W) && (pt[i] & PTE_X);
        dirty += (pt[i] & PTE_D) != 0;
    }
    CHECK(valid == 9 && wx == 0 && dirty == 1);

    uint32_t snap[16];
    memcpy(snap, pt, sizeof snap);
    const region_t overlap[] = {{0x8000, 0x50000, 0x1000, PTE_R, "ok"},
                                {0x2000, 0x60000, 0x1000, PTE_R, "overlaps .text"}};
    CHECK(mmu_map_regions(&m, overlap, 2) == -1 && memcmp(snap, pt, sizeof snap) == 0);
    const region_t self[] = {{0x8000, 0x50000, 0x2000, PTE_R, "a"},
                             {0x9000, 0x60000, 0x1000, PTE_R, "b overlaps a"}};
    CHECK(mmu_map_regions(&m, self, 2) == -1 && memcmp(snap, pt, sizeof snap) == 0);
    const region_t mis[] = {{0x8800, 0x50000, 0x1000, PTE_R, "misaligned"}};
    const region_t wxr[] = {{0x8000, 0x50000, 0x1000, PTE_R | PTE_W | PTE_X, "W+X"}};
    const region_t far[] = {{0xF000, 0x50000, 0x2000, PTE_R, "past VA end"}};
    CHECK(mmu_map_regions(&m, mis, 1) == -1);
    CHECK(mmu_map_regions(&m, wxr, 1) == -1);
    CHECK(mmu_map_regions(&m, far, 1) == -1 && memcmp(snap, pt, sizeof snap) == 0);
}

static int ostep_mode(const char *path) {
    FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "r");
    if (!f) { perror(path); return 2; }
    size_t cap = 1u << 20, len = 0;
    char *buf = malloc(cap + 1);
    if (!buf) return 2;
    for (;;) {
        size_t k = fread(buf + len, 1, cap - len, f);
        len += k;
        if (k == 0) break;
        if (len == cap) {
            char *nb = realloc(buf, cap * 2 + 1);
            if (!nb) { free(buf); return 2; }
            buf = nb; cap *= 2;
        }
    }
    buf[len] = '\0';
    if (f != stdin) fclose(f);
    int rc = 2;
    if (ostep_parse(buf, &g_os) != 0) {
        printf("parse error (no ARG/Page Table section?)\n");
    } else if (!g_os.has_answers) {
        printf("no answers in input: run the OSTEP simulator with -c\n");
    } else {
        printf("asize=%" PRIu64 " pgsize=%" PRIu64 " psize=%" PRIu64 " ptes=%zu vas=%zu\n",
               g_os.asize, g_os.pgsize, g_os.psize, g_os.npte, g_os.nva);
        long bad = ostep_crosscheck(&g_os, true);
        if (bad < 0) printf("setup failed (mmu_init rejected the parameters?)\n");
        else printf("ostep cross-check: %zu/%zu match\n", g_os.nva - (size_t)bad, g_os.nva);
        rc = bad == 0 ? 0 : 1;
    }
    free(buf);
    return rc;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--ostep") == 0) return ostep_mode(argv[2]);
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
