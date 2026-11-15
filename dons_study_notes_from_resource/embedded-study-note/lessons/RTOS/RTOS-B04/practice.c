/* RTOS-B04 추가 연습 — 한 번에 검증
 *
 *   연습 1  rq_set_prio (vTaskPrioritySet): 옛 우선순위 리스트에서 빼고 → 바꾸고 → 새 리스트 꼬리에.
 *           순서를 거꾸로 하면(먼저 prio 를 바꾸면) 리스트가 망가지는 것도 확인
 *   연습 2  FreeRTOS 식 ready list: 실행 중인 태스크도 리스트에 남고, pxIndex 커서가 돈다
 *           (listINSERT_END / listGET_OWNER_OF_NEXT_ENTRY / uxListRemove 를 그대로 옮김)
 *   연습 3  256 우선순위: 2단 비트맵 (그룹 비트맵 8비트 + 32비트 워드 8개) — CLZ 두 번
 *   연습 4  CLZ 명령이 없는 Cortex-M0: de Bruijn 곱셈 + 32칸 표로 최상위 비트
 *   연습 5  "작은 숫자가 급하다" (Zephyr / µC/OS 방향): 최하위 비트 = x & -x 다음 CLZ
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NPRIO 32

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x1234ABCDu;
static uint32_t xorshift32(void)
{
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}

/* ---------------- 본문(solution.c) 의 Part A 를 그대로 ---------------- */
typedef struct tcb {
    const char *name; uint8_t prio; struct tcb *next, *prev; bool in_rq;
} tcb_t;
typedef struct { uint32_t bitmap; tcb_t *head[NPRIO], *tail[NPRIO]; unsigned count; } readyq_t;

static int my_clz32(uint32_t x)
{
    if (x == 0) return 32;
    int n = 0;
    if (x <= 0x0000FFFFu) { n += 16; x <<= 16; }
    if (x <= 0x00FFFFFFu) { n += 8;  x <<= 8;  }
    if (x <= 0x0FFFFFFFu) { n += 4;  x <<= 4;  }
    if (x <= 0x3FFFFFFFu) { n += 2;  x <<= 2;  }
    if (x <= 0x7FFFFFFFu) { n += 1; }
    return n;
}
static int bm_highest(uint32_t bm) { return bm ? 31 - my_clz32(bm) : -1; }

static bool rq_push_tail(readyq_t *q, tcb_t *t)
{
    if (t->in_rq || t->prio >= NPRIO) return false;
    unsigned p = t->prio;
    t->next = NULL; t->prev = q->tail[p];
    if (q->tail[p]) q->tail[p]->next = t; else q->head[p] = t;
    q->tail[p] = t; q->bitmap |= 1u << p; t->in_rq = true; q->count++;
    return true;
}
static bool rq_remove(readyq_t *q, tcb_t *t)
{
    if (!t->in_rq) return false;
    unsigned p = t->prio;
    if (t->prev) t->prev->next = t->next; else q->head[p] = t->next;
    if (t->next) t->next->prev = t->prev; else q->tail[p] = t->prev;
    t->next = t->prev = NULL; t->in_rq = false; q->count--;
    if (q->head[p] == NULL) q->bitmap &= ~(1u << p);
    return true;
}
static tcb_t *rq_pop_highest(readyq_t *q)
{
    int p = bm_highest(q->bitmap);
    if (p < 0) return NULL;
    tcb_t *t = q->head[p]; rq_remove(q, t); return t;
}
static int rq_check(const readyq_t *q)
{
    int bad = 0; unsigned total = 0;
    for (int p = 0; p < NPRIO; p++) {
        if ((bool)((q->bitmap >> p) & 1u) != (q->head[p] != NULL)) bad++;
        const tcb_t *prev = NULL; int guard = 0;
        for (const tcb_t *t = q->head[p]; t && guard < 100; t = t->next, guard++) {
            if (t->prev != prev || t->prio != p) bad++;
            prev = t; total++;
        }
        if (prev != q->tail[p]) bad++;
    }
    if (total != q->count) bad++;
    return bad;
}
static void mk(tcb_t *t, const char *name, uint8_t prio) { memset(t, 0, sizeof *t); t->name = name; t->prio = prio; }

/* ======================= 연습 1: rq_set_prio ======================= */
static bool rq_set_prio(readyq_t *q, tcb_t *t, uint8_t newp)
{
    if (newp >= NPRIO) return false;
    if (t->in_rq) {                       /* READY: 옛 리스트에서 빼고(옛 prio 로!) → 바꾸고 → 새 꼬리 */
        rq_remove(q, t);
        t->prio = newp;
        rq_push_tail(q, t);
    } else {
        t->prio = newp;                   /* BLOCKED/SUSPENDED: 숫자만 바꾼다 (FreeRTOS 도 같음) */
    }
    return true;
}
static void rq_set_prio_buggy(readyq_t *q, tcb_t *t, uint8_t newp)
{
    t->prio = newp;                       /* 버그: 먼저 바꾸면 rq_remove 가 새 prio 의 리스트를 건드린다 */
    rq_remove(q, t);
    rq_push_tail(q, t);
}

static void ex1(void)
{
    puts("== 연습 1: rq_set_prio ==");
    readyq_t q; memset(&q, 0, sizeof q);
    tcb_t a, b, c, d;
    mk(&a, "a", 5); mk(&b, "b", 5); mk(&c, "c", 9); mk(&d, "d", 2);
    rq_push_tail(&q, &a); rq_push_tail(&q, &b); rq_push_tail(&q, &c);
    CHECK(rq_set_prio(&q, &a, 9) && q.head[9] == &c && q.tail[9] == &a && q.head[5] == &b);  /* 새 꼬리 */
    CHECK(rq_set_prio(&q, &b, 1) && !(q.bitmap & (1u << 5)) && (q.bitmap & (1u << 1)));      /* p5 비었음 → 비트 끔 */
    CHECK(rq_set_prio(&q, &d, 30) && !d.in_rq && q.bitmap == ((1u << 9) | (1u << 1)));         /* 블록된 태스크 */
    CHECK(!rq_set_prio(&q, &c, 32) && rq_check(&q) == 0);
    char order[8] = {0}; int k = 0;
    for (tcb_t *t; (t = rq_pop_highest(&q)) != NULL && k < 7; ) order[k++] = t->name[0];
    printf("after set_prio: pop order %s\n", order);
    CHECK(strcmp(order, "cab") == 0);

    readyq_t q2; memset(&q2, 0, sizeof q2);
    tcb_t x, y;
    mk(&x, "x", 5); mk(&y, "y", 5);
    rq_push_tail(&q2, &x); rq_push_tail(&q2, &y);
    rq_set_prio_buggy(&q2, &x, 9);
    int bad = rq_check(&q2);
    printf("buggy set_prio: invariant violations = %d, head[5]=%s (x 는 이미 p9 인데)\n",
           bad, q2.head[5] ? q2.head[5]->name : "NULL");
    CHECK(bad > 0 && q2.head[5] == &x);   /* 옛 리스트에 x 가 남은 채 망가짐 */
}

/* ======================= 연습 2: FreeRTOS 식 리스트 (커서가 도는 원형 리스트) ======================= */
typedef struct fitem { struct fitem *next, *prev; void *owner; struct flist *container; } fitem_t;
typedef struct flist { unsigned n; fitem_t *index; fitem_t end; } flist_t;   /* end = 표지(sentinel) */

static void fl_init(flist_t *l) { l->end.next = l->end.prev = &l->end; l->end.owner = NULL; l->index = &l->end; l->n = 0; }
static void fl_insert_end(flist_t *l, fitem_t *it)       /* list.h listINSERT_END: 커서 바로 "앞" 에 */
{
    fitem_t *idx = l->index;
    it->next = idx; it->prev = idx->prev;
    idx->prev->next = it; idx->prev = it;
    it->container = l; l->n++;
}
static void *fl_next_owner(flist_t *l)                   /* list.h listGET_OWNER_OF_NEXT_ENTRY */
{
    l->index = l->index->next;
    if (l->index == &l->end) l->index = l->end.next;     /* 표지는 건너뛴다 */
    return l->index->owner;
}
static unsigned fl_remove(fitem_t *it)                   /* list.c uxListRemove */
{
    flist_t *l = it->container;
    it->next->prev = it->prev; it->prev->next = it->next;
    if (l->index == it) l->index = it->prev;             /* 커서가 지워진 칸을 가리키지 않게 */
    it->container = NULL;
    return --l->n;
}

typedef struct { uint32_t bm; flist_t l[NPRIO]; } frq_t;
typedef struct { char tag; uint8_t prio; fitem_t item; } ftask_t;

static void frq_init(frq_t *q) { q->bm = 0; for (int p = 0; p < NPRIO; p++) fl_init(&q->l[p]); }
static void frq_add(frq_t *q, ftask_t *t)    /* prvAddTaskToReadyList: 비트 켜고 INSERT_END */
{
    t->item.owner = t; q->bm |= 1u << t->prio; fl_insert_end(&q->l[t->prio], &t->item);
}
static void frq_remove(frq_t *q, ftask_t *t) /* uxListRemove == 0 이면 taskRESET_READY_PRIORITY */
{
    if (fl_remove(&t->item) == 0) q->bm &= ~(1u << t->prio);
}
static ftask_t *frq_select(frq_t *q)         /* taskSELECT_HIGHEST_PRIORITY_TASK (port optimised) */
{
    int p = bm_highest(q->bm);
    return p < 0 ? NULL : (ftask_t *)fl_next_owner(&q->l[p]);
}

static void ex2(void)
{
    puts("== 연습 2: FreeRTOS 식 ready list (실행 중에도 리스트에 남음, 커서 회전) ==");
    frq_t q; frq_init(&q);
    ftask_t A = {'A', 3, {0}}, B = {'B', 3, {0}}, C = {'C', 3, {0}}, D = {'D', 3, {0}};
    frq_add(&q, &A); frq_add(&q, &B); frq_add(&q, &C);
    char s[32] = {0}; int k = 0;
    for (int i = 0; i < 4; i++) s[k++] = frq_select(&q)->tag;          /* A B C A — 아무것도 빼지 않는다 */
    frq_add(&q, &D);                                                     /* 커서(A) 바로 앞 = 한 바퀴 맨 끝 */
    for (int i = 0; i < 4; i++) s[k++] = frq_select(&q)->tag;          /* B C D A */
    s[k++] = '|';
    frq_remove(&q, &A);   /* 커서가 A → A 의 앞(D) 으로 */
    for (int i = 0; i < 3; i++) s[k++] = frq_select(&q)->tag;          /* B C D */
    printf("select sequence: %s\n", s);
    CHECK(strcmp(s, "ABCABCDA|BCD") == 0);
    frq_remove(&q, &B); frq_remove(&q, &C);
    CHECK(q.bm == (1u << 3) && frq_select(&q) == &D);
    frq_remove(&q, &D);
    CHECK(q.bm == 0 && frq_select(&q) == NULL && q.l[3].index == &q.l[3].end);

    /* 선점당한 태스크의 차례: A 가 돌다가(커서 = A) H(p5) 가 READY → H 실행 → H 블록 → 다음은? */
    frq_t r; frq_init(&r);
    ftask_t a = {'A', 3, {0}}, b = {'B', 3, {0}}, h = {'H', 5, {0}};
    frq_add(&r, &a); frq_add(&r, &b);
    char t[8] = {0}; int m = 0;
    t[m++] = frq_select(&r)->tag;         /* A 실행 중 */
    frq_add(&r, &h);                      /* ISR 이 H 를 깨움 → 선점 */
    t[m++] = frq_select(&r)->tag;         /* H */
    frq_remove(&r, &h);                   /* H 블록 */
    t[m++] = frq_select(&r)->tag;         /* A 가 아니라 B — 커서가 A 다음으로 넘어간다 */
    printf("preempted A, then: %s\n", t);
    CHECK(strcmp(t, "AHB") == 0);
}

/* ======================= 연습 3: 256 우선순위, 2단 비트맵 ======================= */
typedef struct { uint32_t grp; uint32_t w[8]; } bm256_t;   /* grp 의 비트 g == (w[g] != 0) */
static void bm256_set(bm256_t *b, unsigned p)   { b->w[p >> 5] |= 1u << (p & 31u); b->grp |= 1u << (p >> 5); }
static void bm256_clear(bm256_t *b, unsigned p)
{
    b->w[p >> 5] &= ~(1u << (p & 31u));
    if (b->w[p >> 5] == 0) b->grp &= ~(1u << (p >> 5));
}
static int bm256_highest(const bm256_t *b)
{
    int g = bm_highest(b->grp);                  /* 1단: 어느 워드 */
    return g < 0 ? -1 : g * 32 + bm_highest(b->w[g]);   /* 2단: 그 워드 안 */
}

static void ex3(void)
{
    puts("== 연습 3: 256 우선순위 2단 비트맵 ==");
    bm256_t b; memset(&b, 0, sizeof b);
    bool ref[256] = {0};
    long mis = 0, inv = 0;
    for (int n = 0; n < 100000; n++) {
        unsigned p = xorshift32() & 255u;
        if (xorshift32() & 1u) { bm256_set(&b, p); ref[p] = true; }
        else                   { bm256_clear(&b, p); ref[p] = false; }
        int want = -1;
        for (int i = 255; i >= 0; i--) if (ref[i]) { want = i; break; }
        if (bm256_highest(&b) != want) mis++;
        for (int g = 0; g < 8; g++) if ((bool)((b.grp >> g) & 1u) != (b.w[g] != 0)) inv++;
    }
    printf("bm256 random 100000 ops: mismatches %ld, group-invariant violations %ld\n", mis, inv);
    CHECK(mis == 0 && inv == 0);
    memset(&b, 0, sizeof b); bm256_set(&b, 0); bm256_set(&b, 200); bm256_set(&b, 37);
    CHECK(bm256_highest(&b) == 200 && b.grp == ((1u << 6) | (1u << 1) | 1u));
    bm256_clear(&b, 200);
    CHECK(bm256_highest(&b) == 37);
}

/* ======================= 연습 4: CLZ 없는 Cortex-M0 — de Bruijn ======================= */
static int hb_debruijn(uint32_t v)                /* floor(log2 v), v != 0. Bit Twiddling Hacks */
{
    static const uint8_t tab[32] = {
        0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30,
        8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31 };
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;   /* 최상위 비트 아래를 전부 1 로 */
    return tab[(uint32_t)(v * 0x07C4ACDDu) >> 27];
}

static void ex4(void)
{
    puts("== 연습 4: de Bruijn 최상위 비트 (M0 용) ==");
    long mis = 0, n = 0;
    for (int i = 0; i < 32; i++, n++) if (hb_debruijn(1u << i) != i) mis++;
    for (int i = 0; i < 32; i++)
        for (int j = 0; j < i; j++, n++) if (hb_debruijn((1u << i) | (1u << j)) != i) mis++;
    for (int k = 0; k < 1000000; k++) {
        uint32_t v = xorshift32() >> (xorshift32() & 31u);
        if (!v) continue;
        n++;
        if (hb_debruijn(v) != 31 - __builtin_clz(v)) mis++;
    }
    printf("de Bruijn vs 31-__builtin_clz: %ld cases, mismatches %ld\n", n, mis);
    CHECK(mis == 0 && n > 900000);
    CHECK(hb_debruijn(0x54u) == 6 && hb_debruijn(1u) == 0 && hb_debruijn(0xFFFFFFFFu) == 31);
}

/* ======================= 연습 5: 최하위 비트 (작은 숫자 = 급함) ======================= */
static int bm_lowest(uint32_t bm)
{
    return bm ? 31 - my_clz32(bm & (0u - bm)) : -1;   /* x & -x = 최하위 1 비트만 남김 (unsigned 라 정의됨) */
}

static void ex5(void)
{
    puts("== 연습 5: 최하위 비트 (Zephyr / µC/OS 방향) ==");
    long mis = 0;
    for (int i = 0; i < 32; i++) if (bm_lowest(1u << i) != i) mis++;
    for (int k = 0; k < 1000000; k++) {
        uint32_t v = xorshift32() << (xorshift32() & 31u);
        if (!v) continue;
        if (bm_lowest(v) != __builtin_ctz(v)) mis++;
    }
    printf("bm_lowest vs __builtin_ctz: mismatches %ld\n", mis);
    CHECK(mis == 0);
    CHECK(bm_lowest(0) == -1 && bm_lowest(0x54u) == 2 && bm_lowest(0x80000000u) == 31);
}

int main(void)
{
    ex1(); ex2(); ex3(); ex4(); ex5();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
