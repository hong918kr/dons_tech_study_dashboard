/* BUILD: -pthread */
/* OS-M06 추가 연습 — 한 번에 검증 (thread-caching allocator 주변 5개)
 *   연습 1  요청 크기 → tcache bin, 스레드 하나가 tcache 에 쥘 수 있는 최대 바이트(hoarding bound)
 *   연습 2  묶음 remote free: 미리 엮은 사슬을 CAS 한 번으로 push (스레드 4 + 동시 drain)
 *   연습 3  trylock 으로 "안 바쁜 arena" 고르기 (glibc reused_arena 의 아이디어 축소판)
 *   연습 4  sloppy 카운터(OSTEP 29장)로 live 바이트 세기: 오차 상한 = 스레드 수 x S
 *   연습 5  ABA: "하나씩 pop" 하는 CAS 스택은 강제 스케줄에서 깨지고, "통째로 exchange" 는 안 깨진다
 * solution.c 를 include 하지 않는다 (작은 조각을 이 파일 안에 다시 둔다).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---------- 연습 1: bin 번호와 hoarding 상한 ---------- */
enum { TC_BINS = 63, TC_COUNT = 7 };
static size_t adjust(size_t n) { return n <= 16 ? 32 : (n + 16 + 15) & ~(size_t)15; }
static int tc_idx_req(size_t n) {                 /* 요청 n 바이트 → bin 번호, tcache 대상이 아니면 -1 */
    size_t a = adjust(n);
    return a <= 1024 ? (int)(a / 16) - 2 : -1;
}
static long hoard_bytes(int count) {              /* 모든 bin 이 count 개씩 찼을 때 스레드 하나가 쥔 바이트 */
    long s = 0;
    for (int i = 0; i < TC_BINS; i++) s += (long)count * (32 + 16 * i);
    return s;
}

/* ---------- 연습 2: 사슬을 한 번에 push ---------- */
typedef struct node { struct node *next; int id; } node_t;
static _Atomic(node_t *) g_stack;
static void push_chain(node_t *first, node_t *last) {        /* first→…→last 가 이미 엮여 있음 */
    node_t *old = atomic_load_explicit(&g_stack, memory_order_relaxed);
    do { last->next = old; }
    while (!atomic_compare_exchange_weak_explicit(&g_stack, &old, first, memory_order_release, memory_order_relaxed));
}
static node_t *drain(void) { return atomic_exchange_explicit(&g_stack, NULL, memory_order_acquire); }
enum { P_THREADS = 4, P_PER = 20000, P_CHAIN = 16 };
static node_t g_nodes[P_THREADS * P_PER];
static atomic_int g_seen[P_THREADS * P_PER];
static atomic_long g_pushes, g_drained;
static atomic_bool g_stop;
static void count_list(node_t *p) {
    for (; p; p = p->next) { atomic_fetch_add(&g_seen[p->id], 1); atomic_fetch_add(&g_drained, 1); }
}
static void *pusher(void *arg) {
    int t = *(int *)arg;
    for (int i = 0; i < P_PER; i += P_CHAIN) {
        node_t *first = &g_nodes[t * P_PER + i];
        for (int j = 0; j < P_CHAIN - 1; j++) first[j].next = &first[j + 1];   /* 내 지역에서 미리 엮기 */
        push_chain(first, &first[P_CHAIN - 1]);
        atomic_fetch_add(&g_pushes, 1);
    }
    return NULL;
}
static void *drainer(void *arg) {
    (void)arg;
    while (!atomic_load(&g_stop)) count_list(drain());
    return NULL;
}

/* ---------- 연습 3: trylock 으로 arena 고르기 ---------- */
static int pick_arena(pthread_mutex_t *m, int n, int start) {  /* 잠근 arena 번호를 돌려준다 (호출자가 unlock) */
    for (int k = 0; k < n; k++) {
        int i = (start + k) % n;
        if (pthread_mutex_trylock(&m[i]) == 0) return i;
    }
    pthread_mutex_lock(&m[start]);                              /* 전부 바쁘면 자기 것을 기다린다 */
    return start;
}

/* ---------- 연습 4: sloppy live-bytes 카운터 ---------- */
enum { S_THREADS = 4, S_OPS = 50000, S_THRESH = 4096 };
static pthread_mutex_t g_cnt_lock = PTHREAD_MUTEX_INITIALIZER;
static long g_global;                                           /* 대략값 (g_cnt_lock 아래) */
static long g_exact[S_THREADS], g_local_left[S_THREADS];
static void *sloppy_body(void *arg) {
    int t = *(int *)arg;
    uint32_t s = 77u + (uint32_t)t;
    long local = 0, exact = 0;
    for (int i = 0; i < S_OPS; i++) {
        long d = (long)(xs32(&s) % 512) - (exact > 100000 ? 300 : 200);   /* +/- 섞인 할당·해제 바이트 */
        local += d; exact += d;
        if (labs(local) >= S_THRESH) {                          /* 임계값을 넘을 때만 전역 락 */
            pthread_mutex_lock(&g_cnt_lock); g_global += local; pthread_mutex_unlock(&g_cnt_lock);
            local = 0;
        }
    }
    g_exact[t] = exact; g_local_left[t] = local;                /* 남은 지역값은 아직 전역에 안 감 */
    return NULL;
}

/* ---------- 연습 5: ABA 강제 스케줄 (스레드 없이 단계별로) ---------- */
typedef struct snode { struct snode *next; char name; } snode_t;
static bool cas_ptr(snode_t **top, snode_t *expect, snode_t *desired) {  /* 값만 비교하는 CAS 의 의미 */
    if (*top != expect) return false;
    *top = desired; return true;
}
static void list_str(const snode_t *p, char *out) { int n = 0; for (; p && n < 8; p = p->next) out[n++] = p->name; out[n] = '\0'; }

int main(void) {
    printf("-- 연습 1: request -> bin, hoarding bound\n");
    CHECK(tc_idx_req(1) == 0 && tc_idx_req(16) == 0 && tc_idx_req(17) == 1 && tc_idx_req(40) == 2);
    CHECK(tc_idx_req(1008) == 62 && tc_idx_req(1009) == -1);
    long h7 = hoard_bytes(TC_COUNT), h3 = hoard_bytes(3);
    printf("  per-thread worst case: count 7 -> %ld B, count 3 -> %ld B, 8 threads x count 7 -> %ld B\n", h7, h3, 8 * h7);
    CHECK(h7 == 232848 && h3 == 99792);

    printf("-- 연습 2: batched remote free (one CAS per chain of %d)\n", P_CHAIN);
    for (int i = 0; i < P_THREADS * P_PER; i++) { g_nodes[i].id = i; g_nodes[i].next = NULL; }
    pthread_t pt[P_THREADS], dt;
    int ids[P_THREADS];
    pthread_create(&dt, NULL, drainer, NULL);
    for (int t = 0; t < P_THREADS; t++) { ids[t] = t; pthread_create(&pt[t], NULL, pusher, &ids[t]); }
    for (int t = 0; t < P_THREADS; t++) pthread_join(pt[t], NULL);
    atomic_store(&g_stop, true);
    pthread_join(dt, NULL);
    count_list(drain());
    int once = 0;
    for (int i = 0; i < P_THREADS * P_PER; i++) once += atomic_load(&g_seen[i]) == 1;
    printf("  CAS pushes=%ld nodes drained=%ld seen-exactly-once=%d\n", (long)atomic_load(&g_pushes), (long)atomic_load(&g_drained), once);
    CHECK(atomic_load(&g_pushes) == P_THREADS * P_PER / P_CHAIN);
    CHECK(atomic_load(&g_drained) == P_THREADS * P_PER && once == P_THREADS * P_PER);

    printf("-- 연습 3: pick a non-busy arena with trylock\n");
    pthread_mutex_t ar[4];
    for (int i = 0; i < 4; i++) pthread_mutex_init(&ar[i], NULL);
    int a0 = pick_arena(ar, 4, 0);
    CHECK(a0 == 0);                                     /* 아무도 안 쥠 → 자기 것 */
    int a1 = pick_arena(ar, 4, 0);
    CHECK(a1 == 1);                                     /* 0 은 (내가) 쥐고 있음 → trylock EBUSY → 다음 */
    pthread_mutex_lock(&ar[2]);
    int a3 = pick_arena(ar, 4, 1);
    CHECK(a3 == 3);                                     /* 1, 2 바쁨 → 3 */
    CHECK(pthread_mutex_trylock(&ar[0]) == EBUSY);
    for (int i = 0; i < 4; i++) { pthread_mutex_unlock(&ar[i]); pthread_mutex_destroy(&ar[i]); }

    printf("-- 연습 4: sloppy live-bytes counter, S = %d\n", S_THRESH);
    pthread_t st[S_THREADS]; int sid[S_THREADS];
    for (int t = 0; t < S_THREADS; t++) { sid[t] = t; pthread_create(&st[t], NULL, sloppy_body, &sid[t]); }
    for (int t = 0; t < S_THREADS; t++) pthread_join(st[t], NULL);
    long exact = 0, left = 0;
    for (int t = 0; t < S_THREADS; t++) { exact += g_exact[t]; left += g_local_left[t]; }
    long err = labs(exact - g_global);
    printf("  exact=%ld approx=%ld |error|=%ld bound=%d\n", exact, g_global, err, S_THREADS * S_THRESH);
    CHECK(err < (long)S_THREADS * S_THRESH && g_global + left == exact);

    printf("-- 연습 5: ABA with pop-one CAS vs exchange-all\n");
    snode_t C = { NULL, 'C' }, B = { &C, 'B' }, A = { &B, 'A' };
    snode_t *top = &A; char s[16];
    snode_t *t1_old = top, *t1_next = t1_old->next;     /* T1: top=A, next=B 를 읽고 멈춤 */
    snode_t *t2a = top; top = t2a->next;                /* T2: pop A */
    snode_t *t2b = top; top = t2b->next;                /* T2: pop B (T2 가 B 를 쓰는 중) */
    t2a->next = top; top = t2a;                         /* T2: push A (같은 주소 재사용) → top=A, A->C */
    bool ok = cas_ptr(&top, t1_old, t1_next);           /* T1: CAS(top, A -> B) 성공! */
    list_str(top, s);
    printf("  pop-one: T1 CAS %s, stack now \"%s\" while T2 still owns B\n", ok ? "succeeded" : "failed", s);
    CHECK(ok && top == &B && t2b == &B && strcmp(s, "BC") == 0);   /* B 가 스택에도, T2 손에도 = 두 번 나감 */
    snode_t Z = { NULL, 'Z' }, Y = { &Z, 'Y' }, X = { &Y, 'X' };
    _Atomic(snode_t *) head = &X;
    snode_t *got = atomic_exchange(&head, NULL);        /* 주인: 통째로 가져감 (다음 포인터를 미리 읽지 않음) */
    snode_t W = { NULL, 'W' };
    snode_t *o = atomic_load(&head);                    /* 다른 스레드가 그사이 push 해도 */
    do { W.next = o; } while (!atomic_compare_exchange_weak(&head, &o, &W));
    list_str(got, s);
    char s2[16]; list_str(atomic_load(&head), s2);
    printf("  exchange-all: owner got \"%s\", stack now \"%s\"\n", s, s2);
    CHECK(strcmp(s, "XYZ") == 0 && strcmp(s2, "W") == 0);

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
