/*
 * C22_page_replacement.c
 * OSTEP Ch.22 — FIFO / LRU / Clock / OPT / Random 페이지 교체 정책 시뮬레이터
 *
 *  1) 책의 예제 트레이스(0,1,2,0,1,3,0,3,1,2,1)를 캐시 3칸으로 돌려 hit/miss 트레이스 출력
 *  2) Belady's anomaly 트레이스(1,2,3,4,1,2,5,1,2,3,4,5)를 캐시 3 vs 4로 비교
 *  3) 80-20 워크로드 / looping-sequential 워크로드에서 캐시 크기별 hit rate 표
 *
 * build: cc -Wall -Wextra -O0 code/C22_page_replacement.c -o .work/bin/C22_page_replacement
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define MAXC 128   /* 최대 캐시 크기(프레임 수) */

enum policy { FIFO, LRU, CLOCK, OPT, RAND };
static const char *pname[] = { "FIFO", "LRU", "CLOCK", "OPT", "RAND" };

/* 프레임 하나 = 물리 페이지 하나 */
struct frame {
    int page;          /* 들어 있는 가상 페이지 번호, -1 = 비어 있음 */
    uint64_t loaded;   /* FIFO용: 들어온 시각 */
    uint64_t used;     /* LRU용: 마지막 접근 시각 (HW 타임스탬프 흉내) */
    int ref;           /* CLOCK용: use(reference) bit */
};

struct cache {
    struct frame f[MAXC];
    int size;
    int hand;          /* CLOCK 바늘 */
};

static void cache_init(struct cache *c, int size)
{
    c->size = size;
    c->hand = 0;
    for (int i = 0; i < size; i++) {
        c->f[i].page = -1;
        c->f[i].loaded = c->f[i].used = 0;
        c->f[i].ref = 0;
    }
}

static int lookup(const struct cache *c, int page)
{
    for (int i = 0; i < c->size; i++)
        if (c->f[i].page == page)
            return i;
    return -1;
}

/* OPT: 미래에서 가장 늦게(또는 다시는) 쓰일 페이지를 고른다.
 * 10,000개 트레이스에서도 빠르게 돌도록 앞으로 스캔하되 지금까지 본 최댓값보다
 * 멀어지면 바로 멈춘다 (실제 OS는 미래를 모르니 시뮬레이션 전용). */
static int pick_opt(const struct cache *c, const int *trace, int n, int now)
{
    int victim = 0, best = -1;
    for (int i = 0; i < c->size; i++) {
        int next = n + 1;                 /* 다시 안 쓰이면 무한대 취급 */
        for (int t = now + 1; t < n; t++)
            if (trace[t] == c->f[i].page) { next = t; break; }
        if (next > best) { best = next; victim = i; }
        if (best == n + 1) break;         /* 다시 안 쓰이는 페이지 발견 = 최선 */
    }
    return victim;
}

/* 결정적 난수 (xorshift32) */
static uint32_t rng = 2463534242u;
static uint32_t xr(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}
static uint32_t vrng = 12345u;   /* RAND 정책 전용 난수 상태 */

/* victim 프레임 인덱스 선택 (캐시가 꽉 찼을 때만 호출) */
static int pick_victim(struct cache *c, enum policy p,
                       const int *trace, int n, int now)
{
    int v = 0;
    switch (p) {
    case FIFO:
        for (int i = 1; i < c->size; i++)
            if (c->f[i].loaded < c->f[v].loaded) v = i;
        return v;
    case LRU:
        for (int i = 1; i < c->size; i++)
            if (c->f[i].used < c->f[v].used) v = i;
        return v;
    case CLOCK:
        for (;;) {                         /* use bit 1이면 0으로 지우고 넘어감 */
            struct frame *fr = &c->f[c->hand];
            if (fr->ref == 0) {
                v = c->hand;
                c->hand = (c->hand + 1) % c->size;
                return v;
            }
            fr->ref = 0;
            c->hand = (c->hand + 1) % c->size;
        }
    case OPT:
        return pick_opt(c, trace, n, now);
    case RAND:
        vrng ^= vrng << 13; vrng ^= vrng >> 17; vrng ^= vrng << 5;
        return (int)(vrng % (uint32_t)c->size);
    }
    return 0;
}

/* 트레이스 전체를 돌리고 hit 수만 리턴 (집계용) */
static int run(enum policy p, int size, const int *trace, int n)
{
    struct cache c;
    int hits = 0;
    cache_init(&c, size);
    for (int t = 0; t < n; t++) {
        int idx = lookup(&c, trace[t]);
        if (idx >= 0) {
            hits++;
        } else {
            idx = lookup(&c, -1);          /* 빈 프레임이 있으면 먼저 사용 */
            if (idx < 0)
                idx = pick_victim(&c, p, trace, n, t);
            c.f[idx].page = trace[t];
            c.f[idx].loaded = (uint64_t)t + 1;
        }
        c.f[idx].used = (uint64_t)t + 1;   /* 매 접근마다 갱신 = 진짜 LRU의 비용 */
        c.f[idx].ref = 1;                  /* HW가 세팅하는 use bit 흉내 */
    }
    return hits;
}

/* 한 줄씩 상태를 찍어 주는 버전 (작은 트레이스용) */
static int run_trace(enum policy p, int size, const int *trace, int n)
{
    struct cache c;
    int hits = 0;
    cache_init(&c, size);
    printf("[%s, cache=%d]\n", pname[p], size);
    for (int t = 0; t < n; t++) {
        int pg = trace[t];
        int idx = lookup(&c, pg);
        int hit = idx >= 0, evicted = -1;
        if (hit) {
            hits++;
        } else {
            idx = lookup(&c, -1);
            if (idx < 0) {
                idx = pick_victim(&c, p, trace, n, t);
                evicted = c.f[idx].page;
            }
            c.f[idx].page = pg;
            c.f[idx].loaded = (uint64_t)t + 1;
        }
        c.f[idx].used = (uint64_t)t + 1;
        c.f[idx].ref = 1;

        printf("  %2d: access %d  %-4s ", t, pg, hit ? "HIT" : "MISS");
        if (evicted >= 0) printf("evict %d ", evicted); else printf("        ");
        printf(" frames:");
        for (int i = 0; i < c.size; i++) {
            if (c.f[i].page < 0) printf(" .");
            else if (p == CLOCK) printf(" %d%s", c.f[i].page, c.f[i].ref ? "*" : "");
            else printf(" %d", c.f[i].page);
        }
        if (p == CLOCK) printf("   (hand->%d)", c.hand);
        printf("\n");
    }
    printf("  => hits %d / %d  (hit rate %.1f%%)\n\n", hits, n, 100.0 * hits / n);
    return hits;
}


#define NREF 10000
static int wl[NREF];

int main(void)
{
    /* 1) 책의 예제 트레이스 */
    int book[] = { 0, 1, 2, 0, 1, 3, 0, 3, 1, 2, 1 };
    int nb = (int)(sizeof book / sizeof book[0]);
    printf("=== 1. Book trace 0,1,2,0,1,3,0,3,1,2,1 (cache=3) ===\n");
    run_trace(OPT, 3, book, nb);
    run_trace(FIFO, 3, book, nb);
    run_trace(LRU, 3, book, nb);
    run_trace(CLOCK, 3, book, nb);

    /* 2) Belady's anomaly */
    int bel[] = { 1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5 };
    int nbel = (int)(sizeof bel / sizeof bel[0]);
    printf("=== 2. Belady's anomaly trace 1,2,3,4,1,2,5,1,2,3,4,5 ===\n");
    for (int pol = FIFO; pol <= RAND; pol++) {
        printf("  %-5s", pname[pol]);
        for (int cs = 1; cs <= 5; cs++)
            printf("  C=%d:%2d hits", cs, run((enum policy)pol, cs, bel, nbel));
        printf("\n");
    }

    /* 3) 워크로드: 100개 고유 페이지, 10,000번 접근 */
    const char *wname[] = { "no-locality", "80-20", "looping-50" };
    for (int w = 0; w < 3; w++) {
        rng = 2463534242u;
        for (int i = 0; i < NREF; i++) {
            if (w == 0) {
                wl[i] = (int)(xr() % 100);
            } else if (w == 1) {               /* 80%는 hot 20페이지로 */
                if (xr() % 100 < 80) wl[i] = (int)(xr() % 20);
                else                 wl[i] = 20 + (int)(xr() % 80);
            } else {                           /* 0..49 반복 */
                wl[i] = i % 50;
            }
        }
        printf("\n=== 3.%d workload: %s (hit rate %%) ===\n", w + 1, wname[w]);
        printf("  cache   OPT   LRU  FIFO CLOCK  RAND\n");
        int sizes[] = { 1, 10, 20, 30, 40, 49, 50, 60, 80, 100 };
        for (int k = 0; k < 10; k++) {
            int cs = sizes[k];
            printf("  %5d", cs);
            int order[] = { OPT, LRU, FIFO, CLOCK, RAND };
            for (int j = 0; j < 5; j++)
                printf(" %5.1f", 100.0 * run((enum policy)order[j], cs, wl, NREF) / NREF);
            printf("\n");
        }
    }
    return 0;
}
