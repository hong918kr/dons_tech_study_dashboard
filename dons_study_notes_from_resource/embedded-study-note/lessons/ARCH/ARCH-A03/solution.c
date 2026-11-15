/* ARCH-A03 가상 메모리 실측: 페이지 폴트와 TLB 미스 비용 (Apple M6, macOS 16 KB 페이지)
 *
 * 이 파일은 "측정 장치" 의 정확성만 체크한다. 시간 측정은 demo_fault.c / demo_tlb.c 에 있다
 * (둘 다 이 파일을 main 이름만 바꿔 포함한다 → 같은 함수로 잰다).
 *   Part A  페이지 수학 + 첫 접근(first touch): 페이지 올림(오버플로 없이), 페이지 바닥/번호,
 *           페이지마다 1 바이트 읽기/쓰기, mincore 로 "상주(resident) 페이지 수" 세기
 *   Part B  TLB 도달 범위(TLB reach) 측정용 포인터 체이스: Sattolo 로 "한 사이클" 순열,
 *           한 사이클 검사, 페이지 안 줄 위치(layout) 계산, 체인 만들기/걷기, 지연 그래프의 무릎(knee) 찾기
 *   Part C  펌웨어/OS 가 하는 일: 미리 폴트(prefault: 쓰기 터치, memset, madvise WILLNEED, mlock),
 *           시작 시간 예산 계산 (페이지 수 × 폴트 비용, TLB reach 안에 드나)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _DARWIN_C_SOURCE 1          /* mincore / madvise / mlock / MAP_ANON 을 -std=c11 에서 보이게 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <mach/mach.h>              /* vm_page_size */

#define LINE 128                    /* sysctl hw.cachelinesize = 128 */

/* ASan 은 mlock 을 가로채 아무것도 안 한다 (0 만 돌려줌) — 실제로 확인함. 그때는 mlock 상주 체크를 정보로만. */
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define UNDER_ASAN 1
#endif
#endif
#ifndef UNDER_ASAN
#define UNDER_ASAN 0
#endif

/* ======================= Part A — 페이지 수학 + 첫 접근 ======================= */

/* 주어짐: 런타임 페이지 크기. 하드코딩(4096)하지 않는다 — 이 맥은 16384. */
static size_t sys_page_size(void) { return (size_t)sysconf(_SC_PAGESIZE); }

/* 주어짐: 새 익명 매핑 (실패 시 NULL). 커널은 아직 물리 페이지를 하나도 주지 않았다. */
static uint8_t *map_anon(size_t bytes) {
    void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    return p == MAP_FAILED ? NULL : (uint8_t *)p;
}

/* TODO A1: bytes 를 담는 데 필요한 페이지 수 (올림). pg 는 2 의 거듭제곱.
 * (bytes + pg - 1) / pg 는 bytes 가 SIZE_MAX 근처에서 넘친다 — 넘치지 않게. */
static size_t pages_for(size_t bytes, size_t pg) {
    return bytes / pg + (bytes % pg != 0);
}

/* TODO A2: 주소 a 가 속한 페이지의 시작 주소 */
static uintptr_t page_floor(uintptr_t a, size_t pg) {
    return a & ~(uintptr_t)(pg - 1);
}

/* TODO A3: base(페이지 정렬) 부터 셌을 때 addr 가 몇 번째 페이지인가 */
static size_t page_index(const void *base, const void *addr, size_t pg) {
    return (size_t)((page_floor((uintptr_t)addr, pg) - page_floor((uintptr_t)base, pg)) / pg);
}

enum { T_READ, T_WRITE };

/* TODO A4: i = 0..n-1 에 대해 p[i*stride + off] 를 한 번씩 만진다.
 *   T_READ : 읽은 값을 모두 더해 돌려준다
 *   T_WRITE: (uint8_t)(i + 1) 을 쓰고 n 을 돌려준다
 * stride = 페이지 크기면 "페이지마다 1 바이트". volatile 이라 컴파일러가 접근을 지우지 못한다. */
static uint64_t touch_pages(volatile uint8_t *p, size_t n, size_t stride, size_t off, int mode) {
    uint64_t sum = 0;
    if (mode == T_WRITE) {
        for (size_t i = 0; i < n; i++) p[i * stride + off] = (uint8_t)(i + 1);
        return n;
    }
    for (size_t i = 0; i < n; i++) sum += p[i * stride + off];
    return sum;
}

/* TODO A5: [p, p + npages*pg) 중 물리 메모리에 올라와 있는(resident) 페이지 수.
 * mincore(addr, len, vec): 페이지마다 vec 1 바이트, MINCORE_INCORE 비트가 1 이면 상주. 실패하면 SIZE_MAX. */
static size_t resident_pages(const void *p, size_t npages, size_t pg) {
    char *vec = malloc(npages ? npages : 1);
    if (!vec) return SIZE_MAX;
    size_t r = SIZE_MAX;
    if (mincore(p, npages * pg, vec) == 0) {
        r = 0;
        for (size_t i = 0; i < npages; i++) r += (vec[i] & MINCORE_INCORE) != 0;
    }
    free(vec);
    return r;
}

/* ======================= Part B — TLB reach 용 포인터 체이스 ======================= */

/* 주어짐: xorshift64 (Marsaglia) — 고정 시드로 재현 가능 */
static uint64_t xs64(uint64_t *s) {
    uint64_t x = *s;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return *s = x;
}

/* TODO B1: Sattolo 알고리즘 — next[0..P-1] 을 "길이 P 짜리 사이클 하나" 인 무작위 순열로 만든다.
 * next[i] = i 에서 출발해 다음에 갈 원소. Fisher–Yates 와 딱 한 글자 다르다 (j 의 범위). P >= 1. */
static void sattolo(uint32_t *next, size_t P, uint64_t *seed) {
    for (size_t i = 0; i < P; i++) next[i] = (uint32_t)i;
    for (size_t i = P - 1; i > 0; i--) {
        size_t j = (size_t)(xs64(seed) % i);          /* 0..i-1 : 자기 자신(i)은 고르지 않는다 */
        uint32_t t = next[i]; next[i] = next[j]; next[j] = t;
    }
}

/* TODO B2: next 가 0..P-1 전부를 지나는 사이클 하나인가? (범위 밖 값, P == 0 → false) */
static bool is_single_cycle(const uint32_t *next, size_t P) {
    if (P == 0) return false;
    for (size_t i = 0; i < P; i++) if (next[i] >= P) return false;
    size_t cur = 0;
    for (size_t k = 1; k <= P; k++) {
        cur = next[cur];
        if (cur == 0) return k == P;                    /* 출발점에 돌아왔다: 정확히 P 걸음이어야 */
    }
    return false;                                       /* P 걸음 안에 0 으로 안 돌아옴 (0 이 다른 사이클 밖) */
}

enum { L_SPREAD, L_SAME, L_PACKED, L_QUAD, L_N };
static const char *const LNAME[L_N] = {"spread", "same", "packed", "quad"};

/* TODO B3: 체이스 원소 i 의 바이트 오프셋 (매핑 시작 기준).
 *   L_SPREAD: 페이지 i, 그 안의 줄 번호 = i mod (pg/line)   → 페이지마다 다른 캐시 세트
 *   L_SAME  : 페이지 i, 오프셋 0                           → 모든 원소가 같은 세트 (aliasing 대조군)
 *   L_PACKED: 페이지 무시, 줄 i                            → 같은 줄 수, 최소 페이지 (TLB 대조군)
 *   L_QUAD  : 한 페이지에 원소 4 개 — 페이지 i/4, 4 등분 중 (i mod 4) 번째 조각,
 *             그 조각 안의 줄 = (i/4) mod (pg/4/line)        → 원소 P 개 = 페이지 P/4 개 (무릎이 4 배 뒤로 가나?) */
static size_t chase_offset(size_t i, size_t pg, size_t line, int layout) {
    switch (layout) {
    case L_SPREAD: return i * pg + (i % (pg / line)) * line;
    case L_SAME:   return i * pg;
    case L_PACKED: return i * line;
    case L_QUAD:   return (i / 4) * pg + (i % 4) * (pg / 4) + ((i / 4) % (pg / 4 / line)) * line;
    default:       return 0;
    }
}

/* 주어짐: 체인이 필요로 하는 매핑 크기 (페이지 올림) */
static size_t chase_span(size_t P, size_t pg, size_t line, int layout) {
    size_t last = chase_offset(P - 1, pg, line, layout) + line;
    return pages_for(last, pg) * pg;
}

/* 주어짐: base 위에 체인을 만든다. 원소 i 자리에 "원소 next[i] 의 주소" 를 적는다. 시작 주소를 돌려준다. */
static void *chase_build(uint8_t *base, const uint32_t *next, size_t P, size_t pg, size_t line, int layout) {
    for (size_t i = 0; i < P; i++)
        *(void **)(void *)(base + chase_offset(i, pg, line, layout)) = base + chase_offset(next[i], pg, line, layout);
    return base + chase_offset(0, pg, line, layout);
}

/* 주어짐: steps 번 따라간다. 다음 주소가 이번 load 의 결과라 load 들이 겹치지 못한다 (지연 = 1 load 씩). */
static void *chase_walk(void *p, size_t steps) {
    for (size_t k = 0; k < steps; k++) p = *(void **)p;
    return p;
}

/* TODO B4: 지연 곡선 y[0..n-1] (P 가 커지는 순) 에서 무릎(knee) = "상승이 시작되는 점" 의 인덱스를 찾는다.
 *   base = y[0]. k = 1..n-1 에서 y[k] >= ratio * base 이면 상승:
 *     바로 앞 점(k-1)이 상승이 아니었으면 k 를 무릎으로 기록 (maxout 까지),
 *     base = y[k] 로 올린다 (상승 구간이 여러 점이면 첫 점 하나만 기록된다).
 *   상승이 아니면 base 는 그대로. 기록한 개수를 돌려준다. */
static size_t find_knees(const double *y, size_t n, double ratio, size_t *out, size_t maxout) {
    size_t cnt = 0;
    if (n == 0) return 0;
    double base = y[0];
    bool prev_rise = false;
    for (size_t k = 1; k < n; k++) {
        bool rise = y[k] >= ratio * base;
        if (rise) {
            if (!prev_rise && cnt < maxout) out[cnt++] = k;
            base = y[k];
        }
        prev_rise = rise;
    }
    return cnt;
}

/* ======================= Part C — prefault 와 예산 ======================= */

enum { PF_TOUCH, PF_MEMSET, PF_WILLNEED, PF_MLOCK, PF_N };
static const char *const PFNAME[PF_N] = {"touch-write", "memset", "madv-willneed", "mlock"};

/* TODO C1: [p, p+len) 을 미리 폴트시킨다. 성공 0, 실패/모르는 how → -1.
 *   PF_TOUCH   : 페이지마다 1 바이트에 0 을 쓴다 (페이지 단위 쓰기 → 폴트 1 번씩)
 *   PF_MEMSET  : 전체를 0 으로 memset
 *   PF_WILLNEED: madvise(MADV_WILLNEED) — "곧 쓸 거다" 힌트일 뿐
 *   PF_MLOCK   : mlock — 물리 메모리에 올리고 고정 (해제는 munlock/munmap) */
static int prefault(void *p, size_t len, size_t pg, int how) {
    volatile uint8_t *b = p;
    switch (how) {
    case PF_TOUCH:
        for (size_t off = 0; off < len; off += pg) b[off] = 0;
        return 0;
    case PF_MEMSET:   memset(p, 0, len); return 0;
    case PF_WILLNEED: return madvise(p, len, MADV_WILLNEED) == 0 ? 0 : -1;
    case PF_MLOCK:    return mlock(p, len) == 0 ? 0 : -1;
    default:          return -1;
    }
}

typedef struct {
    size_t pages;        /* 버퍼가 차지하는 페이지 수 */
    double fault_us;     /* 그 페이지를 처음 만질 때 드는 폴트 시간 합 (µs) */
    bool fits_reach;     /* 버퍼 전체가 TLB reach(바이트) 안에 드나 */
} vm_plan;

/* TODO C2: 버퍼 bytes, 페이지 pg, 페이지당 폴트 ns, 추정 TLB reach(바이트) → 계획 */
static vm_plan vm_budget(size_t bytes, size_t pg, double ns_per_fault, size_t reach_bytes) {
    vm_plan r;
    r.pages = pages_for(bytes, pg);
    r.fault_us = (double)r.pages * ns_per_fault / 1000.0;
    r.fits_reach = r.pages * pg <= reach_bytes;
    return r;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void test_part_a(void) {
    size_t pg = sys_page_size();
    CHECK(pg >= 4096 && (pg & (pg - 1)) == 0);
    CHECK(pg == (size_t)getpagesize() && pg == (size_t)vm_page_size);
    printf("info  page size: sysconf=%zu getpagesize=%d vm_page_size=%lu\n", pg, getpagesize(), (unsigned long)vm_page_size);

    CHECK(pages_for(0, 16384) == 0 && pages_for(1, 16384) == 1 && pages_for(16384, 16384) == 1);
    CHECK(pages_for(16385, 16384) == 2 && pages_for(64u << 20, 16384) == 4096 && pages_for(64u << 20, 4096) == 16384);
    CHECK(pages_for(SIZE_MAX, 16384) == SIZE_MAX / 16384 + 1);        /* (b+pg-1)/pg 였으면 0 근처로 넘침 */
    CHECK(page_floor(0x10003FFFu, 16384) == 0x10000000u && page_floor(0x10004000u, 16384) == 0x10004000u);
    CHECK(page_floor(0x10003FFFu, 4096) == 0x10003000u);

    size_t n = 64;
    uint8_t *m = map_anon(n * pg);
    CHECK(m != NULL);
    if (!m) return;
    CHECK(page_index(m, m, pg) == 0 && page_index(m, m + pg - 1, pg) == 0 && page_index(m, m + pg, pg) == 1);
    CHECK(page_index(m, m + 63 * pg + 5, pg) == 63);
    CHECK(resident_pages(m, n, pg) == 0);                              /* 새 익명 매핑: 아직 아무것도 없음 */
    CHECK(touch_pages(m, n / 2, 2 * pg, 1000, T_WRITE) == n / 2);      /* 짝수 페이지만 */
    CHECK(resident_pages(m, n, pg) == n / 2);
    CHECK(touch_pages(m, n, pg, 1000, T_WRITE) == n);
    CHECK(resident_pages(m, n, pg) == n);
    bool ok = true;
    for (size_t i = 0; i < n; i++) ok = ok && m[i * pg + 1000] == (uint8_t)(i + 1) && m[i * pg + 999] == 0;
    CHECK(ok);
    CHECK(touch_pages(m, n, pg, 1000, T_READ) == (uint64_t)n * (n + 1) / 2);
    munmap(m, n * pg);

    m = map_anon(n * pg);                                              /* 읽기 먼저: 0 이 읽혀야 한다 */
    CHECK(m != NULL && touch_pages(m, n, pg, 0, T_READ) == 0);
    if (m) {
        printf("info  after read-only first touch: resident %zu/%zu pages\n", resident_pages(m, n, pg), n);
        munmap(m, n * pg);
    }
}

static void test_part_b(void) {
    static uint32_t nx[65536];
    uint64_t seed = 0x9E3779B97F4A7C15ull;
    const size_t Ps[] = {1, 2, 3, 4, 16, 1000, 4096, 65536};
    bool all = true;
    for (size_t k = 0; k < sizeof Ps / sizeof Ps[0]; k++) {
        sattolo(nx, Ps[k], &seed);
        all = all && is_single_cycle(nx, Ps[k]);
    }
    CHECK(all);
    /* Sattolo 는 (P-1)! 개 사이클을 모두 (균등하게) 만든다: P = 4 → 6 가지, 600 번이면 전부 나온다 */
    unsigned seen = 0, cnt[256] = {0};
    for (int r = 0; r < 600; r++) {
        sattolo(nx, 4, &seed);
        unsigned key = nx[0] | nx[1] << 2 | nx[2] << 4 | nx[3] << 6;
        if (cnt[key]++ == 0) seen++;
    }
    CHECK(seen == 6);
    { uint32_t a[] = {1, 2, 0};       CHECK(is_single_cycle(a, 3)); }
    { uint32_t a[] = {0, 1, 2};       CHECK(!is_single_cycle(a, 3)); }      /* 고정점 셋 */
    { uint32_t a[] = {1, 0, 3, 2};    CHECK(!is_single_cycle(a, 4)); }      /* 2-사이클 둘 */
    { uint32_t a[] = {1, 2, 0, 3};    CHECK(!is_single_cycle(a, 4)); }      /* 0 이 3 을 못 감 */
    { uint32_t a[] = {1, 5, 0};       CHECK(!is_single_cycle(a, 3)); }      /* 범위 밖 */
    { uint32_t a[] = {0};             CHECK(is_single_cycle(a, 1) && !is_single_cycle(a, 0)); }

    size_t pg = 16384, L = LINE;
    CHECK(chase_offset(0, pg, L, L_SPREAD) == 0 && chase_offset(1, pg, L, L_SPREAD) == 16384 + 128);
    CHECK(chase_offset(127, pg, L, L_SPREAD) == 127 * 16384 + 127 * 128 && chase_offset(128, pg, L, L_SPREAD) == 128 * 16384);
    CHECK(chase_offset(5, pg, L, L_SAME) == 5 * 16384 && chase_offset(5, pg, L, L_PACKED) == 5 * 128);
    CHECK(chase_span(4096, pg, L, L_SPREAD) == 4096 * pg && chase_span(4096, pg, L, L_PACKED) == 32 * pg);
    CHECK(chase_offset(5, pg, L, L_QUAD) == 16384 + 4096 + 128 && chase_offset(3, pg, L, L_QUAD) == 3 * 4096);
    CHECK(chase_span(4096, pg, L, L_QUAD) == 1024 * pg);
    /* 페이지 안 줄 번호(= L1 세트를 정하는 비트)가 몇 가지인가: spread·packed·quad 128, same 1 */
    unsigned char used[L_N][128] = {{0}};
    for (size_t i = 0; i < 4096; i++)
        for (int lay = 0; lay < L_N; lay++) used[lay][(chase_offset(i, pg, L, lay) % pg) / L] = 1;
    unsigned d[L_N] = {0};
    for (int lay = 0; lay < L_N; lay++) for (int s = 0; s < 128; s++) d[lay] += used[lay][s];
    CHECK(d[L_SPREAD] == 128 && d[L_SAME] == 1 && d[L_PACKED] == 128 && d[L_QUAD] == 128);

    /* 실제 메모리 위에 체인을 만들고 걷는다: P 걸음에 시작점, 모든 페이지를 정확히 한 번 */
    size_t hp = sys_page_size(), P = 4096;
    for (int lay = 0; lay < L_N; lay++) {
        size_t span = chase_span(P, hp, L, lay);
        uint8_t *m = map_anon(span);
        CHECK(m != NULL);
        if (!m) { g_total += lay == L_SPREAD ? 2 : 1; printf("SKIP chain checks (%s): no mapping\n", LNAME[lay]); continue; }
        sattolo(nx, P, &seed);
        printf("info  chain %-6s P=%zu -> mapping %zu KiB (%zu pages)\n", LNAME[lay], P, span / 1024, span / hp);
        void *start = chase_build(m, nx, P, hp, L, lay);
        CHECK(chase_walk(start, P) == start && chase_walk(start, P / 2) != start);
        if (lay == L_SPREAD) {
            static unsigned char vis[4096];
            memset(vis, 0, sizeof vis);
            void *p = start;
            size_t distinct = 0;
            for (size_t k = 0; k < P; k++) {
                size_t pi = page_index(m, p, hp);
                if (pi < P && !vis[pi]) { vis[pi] = 1; distinct++; }
                p = *(void **)p;
            }
            CHECK(distinct == P);
        }
        munmap(m, span);
    }

    size_t out[8];
    { double y[] = {2, 2.1, 1.9, 2.0, 2.2};              CHECK(find_knees(y, 5, 1.5, out, 8) == 0); }
    { double y[] = {1, 1, 1, 3, 3, 3, 9, 9};             CHECK(find_knees(y, 8, 1.5, out, 8) == 2 && out[0] == 3 && out[1] == 6); }
    { double y[] = {1, 1, 2, 4, 4, 4.1};                 CHECK(find_knees(y, 6, 1.5, out, 8) == 1 && out[0] == 2); }
    { double y[] = {1, 1.4, 1.8, 2.3, 2.9};              CHECK(find_knees(y, 5, 1.5, out, 8) == 2 && out[0] == 2 && out[1] == 4); }  /* 서서히: base 기준 */
    { double y[] = {1, 2, 1, 2};                         CHECK(find_knees(y, 4, 1.5, out, 1) == 1 && out[0] == 1); }  /* maxout */
}

static void test_part_c(void) {
    size_t pg = sys_page_size(), n = 64;
    for (int how = 0; how < PF_N; how++) {
        uint8_t *m = map_anon(n * pg);
        CHECK(m != NULL);
        if (!m) continue;
        int rc = prefault(m, n * pg, pg, how);
        size_t res = resident_pages(m, n, pg);
        if (how == PF_WILLNEED || (how == PF_MLOCK && UNDER_ASAN)) {
            CHECK(rc == 0);                         /* WILLNEED 는 힌트: 상주 여부는 보장 안 됨 → demo 가 잰다 */
            printf("info  after %s: resident %zu/%zu pages\n", PFNAME[how], res, n);
        } else {
            CHECK(rc == 0 && res == n);
        }
        if (how == PF_MLOCK) munlock(m, n * pg);
        if (how == PF_MEMSET) CHECK(m[0] == 0 && m[n * pg - 1] == 0);
        munmap(m, n * pg);
    }
    CHECK(prefault(NULL, 0, pg, 99) == -1);

    vm_plan a = vm_budget(64u << 20, 16384, 1000.0, 3072u * 16384u);
    CHECK(a.pages == 4096 && a.fault_us > 4095.9 && a.fault_us < 4096.1 && !a.fits_reach);
    vm_plan b = vm_budget(64u << 20, 4096, 1000.0, 3072u * 4096u);
    CHECK(b.pages == 16384 && b.fault_us > 16383.9 && b.fault_us < 16384.1 && !b.fits_reach);
    vm_plan c = vm_budget(1u << 20, 16384, 800.0, 3072u * 16384u);
    CHECK(c.pages == 64 && c.fits_reach && c.fault_us > 51.1 && c.fault_us < 51.3);
    vm_plan d = vm_budget(1, 16384, 800.0, 16384);
    CHECK(d.pages == 1 && d.fits_reach);
}

int main(void) {
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
