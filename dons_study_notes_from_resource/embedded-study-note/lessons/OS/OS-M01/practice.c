/* OS-M01 추가 연습 — 한 번에 검증
 *   연습 1  needs_flush: leaf 엔트리 변화(old → new)마다 "무효화 없음 / 무효화 / break-before-make" 분류
 *           (권한 = user·kernel × R/W/X 6 가지 능력. 하나라도 잃으면 무효화. U 를 "더하는" 것도 kernel X 를 잃을 수 있다)
 *   연습 2  tables_needed: 4 KB 페이지 집합이 필요로 하는 테이블 페이지 수 (회수가 맞는지 확인하는 공식)
 *   연습 3  linux_flush_choice: Linux x86 (ceiling 33) vs arm64 (MAX_DVM_OPS = 512, range TLBI 없음) 의 결정 재현
 *   연습 4  bbm_check: 이벤트 로그(쓰기 · 무효화 · 반납)에서 BBM 위반과 "무효화 전 반납" 찾기
 *   연습 5  ipi_count: SMP 에서 x86 식 IPI 슈트다운 vs arm64 브로드캐스트 TLBI, 호출마다 vs 한 번에 모아서
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

#define D_V (1ull << 0)
#define D_T (1ull << 1)
#define D_R (1ull << 2)
#define D_W (1ull << 3)
#define D_X (1ull << 4)
#define D_U (1ull << 5)
#define D_OA 0x0000FFFFFFFFF000ull
#define PG(n) ((uint64_t)(n) << 12)

/* ---------- 연습 1 ---------- */
typedef enum { FL_NONE, FL_FLUSH, FL_BBM } flush_need_t;
static const char *const FL_NAME[] = {"none", "flush", "BBM"};
static unsigned abilities(uint64_t d) {               /* bit k = (user?3:0)+kind 접근이 허용되나 (ARCH-A01 규칙) */
    if (!(d & D_V)) return 0;
    unsigned m = 0;
    for (int user = 0; user < 2; user++)
        for (int kind = 0; kind < 3; kind++) {
            bool ok;
            if (user && !(d & D_U)) ok = false;
            else if (!user && kind == 2 && (d & D_U)) ok = false;           /* kernel 은 U 페이지 실행 금지 */
            else ok = kind == 0 ? (d & D_R) != 0 : kind == 1 ? (d & D_W) != 0 : (d & D_X) != 0;
            if (ok) m |= 1u << (user * 3 + kind);
        }
    return m;
}
static flush_need_t needs_flush(uint64_t old, uint64_t nw) {
    if (!(old & D_V)) return FL_NONE;                                    /* 무효 엔트리는 캐시되지 않는다 */
    if (!(nw & D_V)) return FL_FLUSH;
    if ((old ^ nw) & D_OA) return FL_BBM;                                 /* 먼저 무효로 → 무효화 → 새 값 */
    return (abilities(old) & ~abilities(nw)) ? FL_FLUSH : FL_NONE;        /* 잃은 능력이 있으면 */
}

/* ---------- 연습 2 ---------- */
static int distinct(const uint64_t *va, int n, unsigned shift) {
    int c = 0;
    for (int i = 0; i < n; i++) {
        bool seen = false;
        for (int j = 0; j < i && !seen; j++) seen = (va[j] >> shift) == (va[i] >> shift);
        c += !seen;
    }
    return c;
}
static int tables_needed(const uint64_t *va, int n) {   /* 루트 + L1 + L2 + L3 */
    return 1 + distinct(va, n, 39) + distinct(va, n, 30) + distinct(va, n, 21);
}

/* ---------- 연습 3 ---------- */
typedef enum { ARCH_X86, ARCH_ARM64 } arch_t;
/* 반환: 페이지 단위 무효화 명령 수, 또는 -1 = 전체 플러시 (user: 그 mm 전체 / kernel: 전부) */
static long linux_flush_choice(arch_t a, unsigned long npages) {
    if (a == ARCH_X86) return npages > 33 ? -1 : (long)npages;          /* tlb_single_page_flush_ceiling = 33 */
    return npages >= 512 ? -1 : (long)npages;                            /* MAX_DVM_OPS = PTRS_PER_PTE (4 KB) */
}

/* ---------- 연습 4 ---------- */
typedef enum { EV_WRITE, EV_TLBI, EV_FREE } ev_kind_t;
typedef struct { ev_kind_t k; int slot; uint64_t val; } ev_t;            /* WRITE: slot ← val / TLBI: slot (-1 = 전부) / FREE: val = 프레임 PA */
enum { V_BBM = 1, V_EARLY_FREE = 2 };
static int bbm_check(const ev_t *ev, int n) {
    uint64_t cur[8] = {0};                       /* 슬롯 8 개 */
    uint64_t pend_pa[16]; int pend_slot[16], np = 0;
    int viol = 0;
    for (int i = 0; i < n; i++) {
        const ev_t *e = &ev[i];
        if (e->k == EV_WRITE) {
            uint64_t old = cur[e->slot];
            if ((old & D_V) && (e->val & D_V) && ((old ^ e->val) & D_OA)) viol |= V_BBM;
            if ((old & D_V) && (!(e->val & D_V) || ((old ^ e->val) & D_OA)) && np < 16) {
                pend_pa[np] = old & D_OA; pend_slot[np] = e->slot; np++;
            }
            cur[e->slot] = e->val;
        } else if (e->k == EV_TLBI) {
            for (int j = 0; j < np; )
                if (e->slot < 0 || pend_slot[j] == e->slot) { pend_pa[j] = pend_pa[np - 1]; pend_slot[j] = pend_slot[np - 1]; np--; }
                else j++;
        } else {
            for (int j = 0; j < np; j++) if (pend_pa[j] == e->val) viol |= V_EARLY_FREE;
        }
    }
    return viol;
}

/* ---------- 연습 5 ---------- */
static int popcount32(uint32_t x) { int c = 0; while (x) { x &= x - 1; c++; } return c; }
/* mm_cpumask = 이 mm 의 TLB 엔트리를 가질 수 있는 CPU 들. self = 무효화를 시작한 CPU. ncalls = 무효화 라운드 수. */
static int ipi_count(arch_t a, uint32_t mm_cpumask, int self, int ncalls) {
    if (a == ARCH_ARM64) return 0;                                        /* TLBI ...IS 가 하드웨어로 브로드캐스트 */
    return popcount32(mm_cpumask & ~(1u << self)) * ncalls;               /* 다른 CPU 마다 IPI 한 번씩, 라운드마다 */
}

int main(void) {
    puts("-- 연습 1: needs_flush (abilities lost? OA changed?)");
    const uint64_t P = PG(0x40000), Q = PG(0x40001);
    struct { uint64_t o, n; flush_need_t want; const char *what; } t1[] = {
        {0, D_V | D_T | D_R | D_U | P, FL_NONE, "invalid -> valid (new mapping)"},
        {D_V | D_T | D_R | D_U | P, D_V | D_T | D_R | D_W | D_U | P, FL_NONE, "R -> RW (increase: spurious fault fixes it)"},
        {D_V | D_T | D_R | D_W | D_U | P, D_V | D_T | D_R | D_U | P, FL_FLUSH, "RW -> R (decrease)"},
        {D_V | D_T | D_R | D_X | D_U | P, D_V | D_T | D_R | D_W | D_U | P, FL_FLUSH, "RX -> RW (X lost even though W gained)"},
        {D_V | D_T | D_R | D_X | P, D_V | D_T | D_R | D_X | D_U | P, FL_FLUSH, "kernel RX -> +U (kernel loses X: SMEP/PXN rule)"},
        {D_V | D_T | D_R | P, D_V | D_T | D_R | D_U | P, FL_NONE, "kernel R -> +U (only gains)"},
        {D_V | D_T | D_U | P, D_V | D_T | D_R | D_U | P, FL_NONE, "PROT_NONE -> R"},
        {D_V | D_T | D_R | D_U | P, 0, FL_FLUSH, "valid -> invalid (unmap)"},
        {D_V | D_T | D_R | D_U | P, D_V | D_T | D_R | D_U | Q, FL_BBM, "OA change (migration)"},
    };
    for (unsigned i = 0; i < sizeof t1 / sizeof t1[0]; i++) {
        flush_need_t got = needs_flush(t1[i].o, t1[i].n);
        printf("  %-48s -> %s\n", t1[i].what, FL_NAME[got]);
        CHECK(got == t1[i].want);
    }

    puts("-- 연습 2: tables_needed (root + L1 + L2 + L3)");
    const uint64_t s1[] = {0x400000, 0x401000}, s2[] = {0x400000, 0x600000}, s3[] = {0x400000, 0x40000000},
                   s4[] = {0x400000, 1ull << 39}, s5[] = {0x400000, 0x600000, 0x40000000, 1ull << 39, (1ull << 39) + 0x1000};
    CHECK(tables_needed(NULL, 0) == 1 && tables_needed(s1, 1) == 4 && tables_needed(s1, 2) == 4);
    CHECK(tables_needed(s2, 2) == 5 && tables_needed(s3, 2) == 6 && tables_needed(s4, 2) == 7);
    CHECK(tables_needed(s5, 5) == 10);
    printf("  5 pages spread over 2 L0 slots -> %d table pages = %d KB of page tables for 20 KB of data\n",
           tables_needed(s5, 5), tables_needed(s5, 5) * 4);

    puts("-- 연습 3: Linux flush decision (x86 ceiling 33 vs arm64 MAX_DVM_OPS 512)");
    const unsigned long np[] = {1, 33, 34, 511, 512};
    for (unsigned i = 0; i < 5; i++)
        printf("  npages=%-4lu x86=%-4ld arm64=%ld\n", np[i], linux_flush_choice(ARCH_X86, np[i]), linux_flush_choice(ARCH_ARM64, np[i]));
    CHECK(linux_flush_choice(ARCH_X86, 33) == 33 && linux_flush_choice(ARCH_X86, 34) == -1);
    CHECK(linux_flush_choice(ARCH_ARM64, 34) == 34 && linux_flush_choice(ARCH_ARM64, 511) == 511 && linux_flush_choice(ARCH_ARM64, 512) == -1);

    puts("-- 연습 4: event-log checker (BBM, free before invalidate)");
    const uint64_t OLD = PG(0x40010), NEW = PG(0x40020), A = D_V | D_T | D_R | D_W;
    const ev_t good[] = {{EV_WRITE, 0, A | OLD}, {EV_WRITE, 0, 0}, {EV_TLBI, 0, 0}, {EV_WRITE, 0, A | NEW}, {EV_FREE, 0, OLD}};
    const ev_t swap[] = {{EV_WRITE, 0, A | OLD}, {EV_WRITE, 0, A | NEW}, {EV_TLBI, 0, 0}, {EV_FREE, 0, OLD}};
    const ev_t early[] = {{EV_WRITE, 0, A | OLD}, {EV_WRITE, 0, 0}, {EV_FREE, 0, OLD}, {EV_TLBI, 0, 0}};
    const ev_t wrong_slot[] = {{EV_WRITE, 1, A | OLD}, {EV_WRITE, 1, 0}, {EV_TLBI, 2, 0}, {EV_FREE, 0, OLD}};
    const ev_t tlbi_first[] = {{EV_WRITE, 3, A | OLD}, {EV_TLBI, 3, 0}, {EV_WRITE, 3, 0}, {EV_FREE, 0, OLD}};
    const ev_t all[] = {{EV_WRITE, 4, A | OLD}, {EV_WRITE, 4, 0}, {EV_TLBI, -1, 0}, {EV_FREE, 0, OLD}};
    CHECK(bbm_check(good, 5) == 0);
    CHECK(bbm_check(swap, 4) == V_BBM);
    CHECK(bbm_check(early, 4) == V_EARLY_FREE);
    CHECK(bbm_check(wrong_slot, 4) == V_EARLY_FREE && bbm_check(tlbi_first, 4) == V_EARLY_FREE && bbm_check(all, 4) == 0);

    puts("-- 연습 5: shootdown IPIs (4 CPUs, mm ran on CPU 0,1,3; CPU 0 unmaps 16 pages)");
    const uint32_t mask = 0x0B;
    int per_call = ipi_count(ARCH_X86, mask, 0, 16), batched = ipi_count(ARCH_X86, mask, 0, 1);
    printf("  x86: 16 munmap(1 page) -> %d IPIs, one munmap(16 pages) -> %d IPIs; arm64 -> %d IPIs\n",
           per_call, batched, ipi_count(ARCH_ARM64, mask, 0, 16));
    CHECK(per_call == 32 && batched == 2 && ipi_count(ARCH_ARM64, mask, 0, 16) == 0);
    CHECK(ipi_count(ARCH_X86, 0x01, 0, 16) == 0);           /* 단일 스레드 프로세스: 로컬 무효화만 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
