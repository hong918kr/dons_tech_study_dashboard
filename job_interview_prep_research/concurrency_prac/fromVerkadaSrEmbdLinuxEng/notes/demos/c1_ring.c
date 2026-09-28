/* c1_ring.c — C1 노트의 코드 조각 검증
 * cc -std=c11 -O2 -Wall -Wextra -o c1 c1_ring.c && ./c1
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ---------- v1: count 필드 ---------- */
#define CAP 4u
typedef struct {
    int      buf[CAP];
    unsigned head;      /* 다음에 쓸 칸 */
    unsigned tail;      /* 가장 오래된 칸 */
    unsigned count;
} ring_t;

static bool ring_push(ring_t *r, int v)
{
    if (r->count == CAP) return false;          /* full */
    r->buf[r->head] = v;
    r->head = (r->head + 1u) % CAP;
    r->count++;
    return true;
}

static bool ring_pop(ring_t *r, int *out)
{
    if (r->count == 0) return false;            /* empty */
    *out = r->buf[r->tail];
    r->tail = (r->tail + 1u) % CAP;
    r->count--;
    return true;
}

/* ---------- v1b: 한 칸 비우기 (count 없음) ---------- */
typedef struct { int buf[CAP]; unsigned head, tail; } ring2_t;
static bool ring2_empty(const ring2_t *r) { return r->head == r->tail; }
static bool ring2_full (const ring2_t *r) { return (r->head + 1u) % CAP == r->tail; }
static bool ring2_push(ring2_t *r, int v)
{
    if (ring2_full(r)) return false;
    r->buf[r->head] = v;
    r->head = (r->head + 1u) % CAP;
    return true;
}

/* ---------- v2: 자유 증가 인덱스 + 마스크 ---------- */
#define FCAP  8u
#define FMASK (FCAP - 1u)
typedef struct {
    int      buf[FCAP];
    uint32_t head;      /* 감싸지 않는다. 계속 증가 */
    uint32_t tail;
} fring_t;

static uint32_t fring_count(const fring_t *r) { return r->head - r->tail; }
static bool     fring_empty(const fring_t *r) { return r->head == r->tail; }
static bool     fring_full (const fring_t *r) { return r->head - r->tail == FCAP; }

/* 막는 링: 꽉 차면 거절 */
static bool fring_push_bounded(fring_t *r, int v)   /* 막는 링 */
{
    if (fring_full(r)) return false;            /* 새것을 거절한다 */
    r->buf[r->head & FMASK] = v; r->head++;
    return true;
}

/* 덮어쓰는 링: 꽉 차면 가장 오래된 것을 버린다 */
static void fring_push_overwrite(fring_t *r, int v) /* 덮어쓰는 링 */
{
    if (fring_full(r)) r->tail++;               /* drop-oldest: 오래된 것을 버린다 */
    r->buf[r->head & FMASK] = v; r->head++;
}

static int fring_at(const fring_t *r, uint32_t off) { return r->buf[(r->tail + off) & FMASK]; }

/* ---------- 시간 기반 eviction (창 경계 하나를 남긴다) ---------- */
#define HCAP  16u
#define HMASK (HCAP - 1u)
typedef struct { uint64_t ts; int v; } sample_t;
typedef struct { sample_t buf[HCAP]; uint32_t head, tail; } hist_t;

static sample_t *hist_at(hist_t *h, uint32_t off) { return &h->buf[(h->tail + off) & HMASK]; }

static void hist_append(hist_t *h, uint64_t now, uint64_t window, uint64_t ts, int v)
{
    uint32_t n = h->head - h->tail;
    /* 0번을 버리는 조건은 "1번도 창 밖"일 때뿐 — 경계 직전 샘플 하나는 남긴다 */
    while (n >= 2 && hist_at(h, 1)->ts <= now - window) { h->tail++; n--; }
    if (n == HCAP) { h->tail++; n--; }
    h->buf[h->head & HMASK] = (sample_t){ ts, v };
    h->head++;
}

int main(void)
{
    puts("== v1: count 필드 ==");
    ring_t r = {{0}, 0, 0, 0};
    for (int i = 1; i <= 6; i++)
        printf("push %d -> %s (count=%u)\n", i, ring_push(&r, i) ? "ok" : "FULL", r.count);
    int x;
    while (ring_pop(&r, &x)) printf("pop  %d (count=%u)\n", x, r.count);
    printf("pop  -> %s\n", ring_pop(&r, &x) ? "ok" : "EMPTY");

    puts("\n== v1b: 한 칸 비우기 (CAP=4 이므로 3개만 들어간다) ==");
    ring2_t r2 = {{0}, 0, 0};
    for (int i = 1; i <= 4; i++)
        printf("push %d -> %s\n", i, ring2_push(&r2, i) ? "ok" : "FULL");
    printf("empty=%d full=%d\n", ring2_empty(&r2), ring2_full(&r2));

    puts("\n== v2: 자유 증가 인덱스, 막는 링 ==");
    fring_t f = {{0}, 0, 0};
    printf("초기 empty=%d full=%d\n", fring_empty(&f), fring_full(&f));
    for (int i = 1; i <= 10; i++)
        printf("push %2d -> %-4s count=%u\n", i,
               fring_push_bounded(&f, i) ? "ok" : "FULL", fring_count(&f));

    puts("\n== v2: 자유 증가 인덱스, 덮어쓰는 링 ==");
    fring_t g = {{0}, 0, 0};
    for (int i = 1; i <= 10; i++) fring_push_overwrite(&g, i);
    printf("count=%u  내용(오래된 것부터):", fring_count(&g));
    for (uint32_t i = 0; i < fring_count(&g); i++) printf(" %d", fring_at(&g, i));
    printf("\nhead=%u tail=%u  (둘 다 8을 넘었지만 차이는 %u)\n", g.head, g.tail, g.head - g.tail);

    puts("\n== 인덱스 오버플로: head 가 uint32 끝을 넘어간다 ==");
    fring_t h = {{0}, 0xFFFFFFFEu, 0xFFFFFFFEu};
    for (int i = 1; i <= 5; i++) {
        fring_push_overwrite(&h, i);
        printf("push %d -> head=%10u tail=%10u count=%u\n", i, h.head, h.tail, fring_count(&h));
    }
    printf("내용:");
    for (uint32_t i = 0; i < fring_count(&h); i++) printf(" %d", fring_at(&h, i));
    puts("");

    puts("\n== 시간 기반 eviction: 창 = 100, now = 250 ==");
    hist_t hh = {{{0, 0}}, 0, 0};
    uint64_t ts[] = {40, 90, 140, 190, 240};
    int vals[]    = {10, 20,  30,  40,  50};
    for (int i = 0; i < 5; i++) hist_append(&hh, 250, 100, ts[i], vals[i]);
    printf("남은 샘플 (창은 [150, 250]):");
    for (uint32_t i = 0; i < hh.head - hh.tail; i++)
        printf(" (t=%llu,v=%d)", (unsigned long long)hist_at(&hh, i)->ts, hist_at(&hh, i)->v);
    puts("\n  -> t=140 은 창 밖이지만 살아 있다. t=150 의 값을 물으면 답이 이것이다.");
    return 0;
}
