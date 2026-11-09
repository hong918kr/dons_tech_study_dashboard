/* STOR-M10 B+tree 동시성: latch crabbing & optimistic
 *
 * 메모리 안의 작은 B+tree (int 키, 노드당 최대 MAXK = 8 키, insert + lookup 만 — delete 는 없다).
 * 노드마다 pthread_rwlock_t 래치(latch) 하나, 트리에는 root 포인터를 지키는 root_latch 하나.
 *   Part A  pessimistic latch crabbing: 위에서 아래로 래치를 잡고, 자식이 "안전"(이번 삽입으로 split 안 함)
 *           하면 조상 래치를 모두 놓는다. lookup 은 R 래치로 한 칸씩 (자식 잡고 → 부모 놓기).
 *   Part B  optimistic: R 래치로 내려가고 리프만 W 래치. 리프가 꽉 차 있으면 다 놓고 pessimistic 으로
 *           재시작(restart, 횟수를 센다). 래치에는 교착 탐지기가 없다 → "제자리 R→W 업그레이드" 를
 *           두 스레드가 동시에 하면 영원히 멈춘다는 것을 강제 스케줄 + 시간 제한 대기로 보여 준다.
 *   Part C  SSD FTL 의 L2P(LBA → PPA) 인덱스: NVMe 큐 4개 = 스레드 4개가 같은 트리에 쓰기/덮어쓰기.
 *
 * 체크는 "올바르면 매 실행 항상 성립" 하는 것만 본다 (모든 키 정확히 한 번, B+tree 불변식, 결정적 키 집합,
 * 강제 스케줄의 재시작 횟수). 처리량은 demo_bench.c, 일부러 넣은 버그는 demo_planted.c.
 * 멀티스레드 스트레스는 단일 스레드 테스트가 모두 통과해야 실행한다 (시작 코드가 멈추지 않게).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

/* ======================= 주어짐 — B+tree 노드와 노드 단위 연산 ======================= */
#define MAXK 8                     /* 노드당 최대 키 수 (fanout = MAXK + 1 = 9) */
#define MINK (MAXK / 2)            /* root 가 아닌 노드의 최소 키 수 (split 이 9 → 4/5 로 나눈다) */
#define MAXH 32                    /* 트리 높이 상한 (경로 스택 크기) */

typedef struct node {
    pthread_rwlock_t latch;
    int level;                     /* 0 = 리프. 만든 뒤 절대 안 바뀐다 → 래치 없이 읽어도 된다 */
    int nkeys;
    int keys[MAXK];
    struct node *child[MAXK + 1];  /* 내부 노드: child[i] 의 키 k 는 keys[i-1] <= k < keys[i] */
    int val[MAXK];                 /* 리프: keys[i] 의 값 */
    struct node *next;             /* 리프: 오른쪽 형제 (sibling link) */
} node_t;

typedef struct {
    pthread_rwlock_t root_latch;   /* root 포인터를 지킨다 (root split 때 W) */
    node_t *root;
    atomic_long restarts;          /* optimistic → pessimistic 재시작 횟수 */
    atomic_long leaf_splits, inner_splits;
} bt_t;

/* 주어진 도우미는 시작 코드에서 아직 안 쓰일 수 있다 → unused 경고만 끈다 */
#define GIVEN __attribute__((unused))

static bool g_latch_off;           /* demo_bench 의 "전역 rwlock 하나" 판만 true (노드 래치 생략) */
static void (*g_hook)(node_t *n);  /* 강제 스케줄용: R 래치를 잡은 직후 불린다 (테스트에서만 설정) */

GIVEN static void lat_r(node_t *n)  { if (!g_latch_off) pthread_rwlock_rdlock(&n->latch); if (g_hook) g_hook(n); }
GIVEN static void lat_w(node_t *n)  { if (!g_latch_off) pthread_rwlock_wrlock(&n->latch); }
GIVEN static void unlat(node_t *n)  { if (!g_latch_off) pthread_rwlock_unlock(&n->latch); }
GIVEN static void root_r(bt_t *t)   { if (!g_latch_off) pthread_rwlock_rdlock(&t->root_latch); }
GIVEN static void root_w(bt_t *t)   { if (!g_latch_off) pthread_rwlock_wrlock(&t->root_latch); }
GIVEN static void root_unl(bt_t *t) { if (!g_latch_off) pthread_rwlock_unlock(&t->root_latch); }

static node_t *node_new(int level) {
    node_t *n = calloc(1, sizeof *n);
    if (!n) { perror("calloc"); exit(2); }
    pthread_rwlock_init(&n->latch, NULL);
    n->level = level;
    return n;
}
static void bt_init(bt_t *t) {
    pthread_rwlock_init(&t->root_latch, NULL);
    t->root = node_new(0);
    atomic_init(&t->restarts, 0); atomic_init(&t->leaf_splits, 0); atomic_init(&t->inner_splits, 0);
}
static void node_free(node_t *n) {
    if (n->level > 0) for (int i = 0; i <= n->nkeys; i++) node_free(n->child[i]);
    pthread_rwlock_destroy(&n->latch);
    free(n);
}
static void bt_destroy(bt_t *t) { node_free(t->root); pthread_rwlock_destroy(&t->root_latch); }

/* 리프: key 이상인 첫 칸 (lower bound). */
static int leaf_lb(const node_t *n, int key) {
    int i = 0;
    while (i < n->nkeys && n->keys[i] < key) i++;
    return i;
}
/* 내부 노드: key 가 들어 있을 자식 번호 = key 이하인 구분 키의 개수 (upper bound). */
static int inner_idx(const node_t *n, int key) {
    int i = 0;
    while (i < n->nkeys && n->keys[i] <= key) i++;
    return i;
}
GIVEN static void leaf_put_at(node_t *n, int i, int key, int val) {          /* n 은 꽉 차지 않음 */
    memmove(&n->keys[i + 1], &n->keys[i], (size_t)(n->nkeys - i) * sizeof n->keys[0]);
    memmove(&n->val[i + 1], &n->val[i], (size_t)(n->nkeys - i) * sizeof n->val[0]);
    n->keys[i] = key; n->val[i] = val; n->nkeys++;
}
/* 꽉 찬 리프 n 에 (key,val) 을 넣으며 둘로 나눈다. 새 오른쪽 노드를 돌려주고 *sep = 그 첫 키. */
GIVEN static node_t *leaf_split(bt_t *t, node_t *n, int key, int val, int *sep) {
    int k[MAXK + 1], v[MAXK + 1], i = leaf_lb(n, key), j = 0;
    for (int s = 0; s <= MAXK; s++) {
        if (s == i) { k[s] = key; v[s] = val; }
        else { k[s] = n->keys[j]; v[s] = n->val[j]; j++; }
    }
    node_t *r = node_new(0);
    int left = (MAXK + 1) / 2;                                       /* 9 → 4 | 5 */
    n->nkeys = left; r->nkeys = MAXK + 1 - left;
    memcpy(n->keys, k, (size_t)left * sizeof k[0]);         memcpy(n->val, v, (size_t)left * sizeof v[0]);
    memcpy(r->keys, k + left, (size_t)r->nkeys * sizeof k[0]); memcpy(r->val, v + left, (size_t)r->nkeys * sizeof v[0]);
    r->next = n->next; n->next = r;                                  /* 새 노드는 아직 아무도 못 본다 */
    *sep = r->keys[0];
    atomic_fetch_add_explicit(&t->leaf_splits, 1, memory_order_relaxed);
    return r;
}
GIVEN static void inner_put(node_t *n, int sep, node_t *right) {           /* n 은 꽉 차지 않음 */
    int i = inner_idx(n, sep);
    memmove(&n->keys[i + 1], &n->keys[i], (size_t)(n->nkeys - i) * sizeof n->keys[0]);
    memmove(&n->child[i + 2], &n->child[i + 1], (size_t)(n->nkeys - i) * sizeof n->child[0]);
    n->keys[i] = sep; n->child[i + 1] = right; n->nkeys++;
}
/* 꽉 찬 내부 노드 n 에 (sep,right) 를 넣으며 나눈다. 가운데 키는 위로 (*up). */
GIVEN static node_t *inner_split(bt_t *t, node_t *n, int sep, node_t *right, int *up) {
    int k[MAXK + 1]; node_t *c[MAXK + 2];
    int i = inner_idx(n, sep);
    for (int s = 0, j = 0; s <= MAXK; s++) k[s] = (s == i) ? sep : n->keys[j++];
    for (int s = 0, j = 0; s <= MAXK + 1; s++) c[s] = (s == i + 1) ? right : n->child[j++];
    int mid = (MAXK + 1) / 2;                                        /* 9 키 → 4 | 위로 1 | 4 */
    node_t *r = node_new(n->level);
    n->nkeys = mid; r->nkeys = MAXK - mid;
    memcpy(n->keys, k, (size_t)mid * sizeof k[0]);
    memcpy(n->child, c, (size_t)(mid + 1) * sizeof c[0]);
    memcpy(r->keys, k + mid + 1, (size_t)r->nkeys * sizeof k[0]);
    memcpy(r->child, c + mid + 1, (size_t)(r->nkeys + 1) * sizeof c[0]);
    *up = k[mid];
    atomic_fetch_add_explicit(&t->inner_splits, 1, memory_order_relaxed);
    return r;
}
GIVEN static void unlat_path(node_t **path, int *depth) {
    for (int d = 0; d < *depth; d++) unlat(path[d]);
    *depth = 0;
}

/* 불변식 검사 (조용할 때만 부른다 — 래치 없음). 0 = 정상, 아니면 어긴 규칙 번호. *count = 키 수. */
static node_t *g_chk_prev_leaf; static int g_chk_leaf_depth, g_chk_err; static long g_chk_count; static int g_chk_last;
static bool g_chk_have_last;
static void chk_node(node_t *n, int depth, bool is_root, long lo, long hi) {
    if (g_chk_err) return;
    if (n->nkeys > MAXK) { g_chk_err = 1; return; }                           /* 넘침 */
    if (!is_root && n->nkeys < MINK) { g_chk_err = 2; return; }               /* 채움 하한 */
    if (is_root && n->level > 0 && n->nkeys < 1) { g_chk_err = 2; return; }
    for (int i = 0; i < n->nkeys; i++) {
        if (i > 0 && n->keys[i - 1] >= n->keys[i]) { g_chk_err = 3; return; } /* 정렬, 중복 없음 */
        if (n->keys[i] < lo || n->keys[i] >= hi) { g_chk_err = 4; return; }   /* 구분 키 범위 */
    }
    if (n->level == 0) {
        if (g_chk_leaf_depth < 0) g_chk_leaf_depth = depth;
        if (depth != g_chk_leaf_depth) { g_chk_err = 5; return; }             /* 모든 리프 같은 깊이 */
        if (g_chk_prev_leaf && g_chk_prev_leaf->next != n) { g_chk_err = 6; return; } /* 형제 링크 */
        if (g_chk_have_last && n->nkeys > 0 && n->keys[0] <= g_chk_last) { g_chk_err = 3; return; }
        if (n->nkeys > 0) { g_chk_last = n->keys[n->nkeys - 1]; g_chk_have_last = true; }
        g_chk_prev_leaf = n;
        g_chk_count += n->nkeys;
        return;
    }
    for (int i = 0; i <= n->nkeys; i++) {
        node_t *c = n->child[i];
        if (!c || c->level != n->level - 1) { g_chk_err = 5; return; }
        chk_node(c, depth + 1, false, i == 0 ? lo : n->keys[i - 1], i == n->nkeys ? hi : n->keys[i]);
    }
}
static int bt_check(bt_t *t, long *count) {
    g_chk_prev_leaf = NULL; g_chk_leaf_depth = -1; g_chk_err = 0; g_chk_count = 0; g_chk_have_last = false;
    chk_node(t->root, 0, true, (long)INT32_MIN, (long)INT32_MAX + 1);
    if (!g_chk_err && g_chk_prev_leaf && g_chk_prev_leaf->next != NULL) g_chk_err = 6;
    if (count) *count = g_chk_count;
    return g_chk_err;
}
static int bt_height(bt_t *t) { return t->root->level + 1; }

/* ======================= Part A — pessimistic latch crabbing ======================= */

/* A1: 이 노드에 키 하나가 더 들어와도 split 하지 않나? (삽입 기준 "안전") */
static bool safe_for_insert(const node_t *n) {
    (void)n;
    return false; /* TODO A1 */
}

/* A2: lookup. root_latch(R) → root(R) → root_latch 해제 → 자식(R) 잡고 → 부모 해제 … 리프에서 찾기.
 *     찾으면 *val 에 값(val 이 NULL 이 아니면)을 쓰고 true. */
static bool bt_lookup(bt_t *t, int key, int *val) {
    (void)t; (void)key; (void)val;
    return false; /* TODO A2 */
}

/* A3: pessimistic insert. root_latch(W) 부터 W 래치로 내려가며 경로를 스택에 쌓고,
 *     방금 잡은 노드가 safe_for_insert 이면 그 위(조상 + root_latch)를 모두 놓는다.
 *     리프가 꽉 찼으면 leaf_split / inner_split 을 스택을 거슬러 올라가며 반영하고,
 *     root 까지 나뉘면 새 root 를 만든다 (그때는 root_latch 를 아직 쥐고 있어야 한다).
 *     이미 있는 키면 아무것도 바꾸지 않고 false. 새로 넣었으면 true. */
static bool bt_insert_pess(bt_t *t, int key, int val) {
    (void)t; (void)key; (void)val;
    return false; /* TODO A3 */
}

/* ======================= Part B — optimistic + 업그레이드 교착 ======================= */

/* B1: optimistic insert. root_latch(R) → R 래치로 내려가되 리프(level 0)만 W 래치.
 *     (부모가 level 1 이면 자식은 리프. root 자체가 리프면 root 를 W.)
 *     리프에 키가 이미 있으면 false. 리프가 안전하면 넣고 true.
 *     리프가 꽉 찼으면: 리프를 놓고(래치를 하나도 쥐지 않은 채) restarts 를 1 올리고 bt_insert_pess 로 재시작.
 *     절대 R 을 쥔 채 W 를 기다리지 않는다 (업그레이드 금지). */
static bool bt_insert_opt(bt_t *t, int key, int val) {
    (void)t; (void)key; (void)val;
    return false; /* TODO B1 */
}

/* 업그레이드 가능한 래치 모델 (mutex + cond). R 여럿 / W 하나. 주어짐: rlock, runlock, wlock, wunlock. */
typedef struct { pthread_mutex_t m; pthread_cond_t c; int readers; bool writer; } ulatch_t;
static void ul_init(ulatch_t *u) { pthread_mutex_init(&u->m, NULL); pthread_cond_init(&u->c, NULL); u->readers = 0; u->writer = false; }
static void ul_destroy(ulatch_t *u) { pthread_mutex_destroy(&u->m); pthread_cond_destroy(&u->c); }
static struct timespec deadline_ms(int ms) {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);         /* cond_timedwait 은 REALTIME 절대 시각 */
    ts.tv_sec += ms / 1000; ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    return ts;
}
static void ul_rlock(ulatch_t *u) {
    pthread_mutex_lock(&u->m);
    while (u->writer) pthread_cond_wait(&u->c, &u->m);
    u->readers++;
    pthread_mutex_unlock(&u->m);
}
static void ul_runlock(ulatch_t *u) {
    pthread_mutex_lock(&u->m); u->readers--; pthread_cond_broadcast(&u->c); pthread_mutex_unlock(&u->m);
}
static int ul_wlock_bounded(ulatch_t *u, int ms) {
    struct timespec dl = deadline_ms(ms);
    int rc = 0;
    pthread_mutex_lock(&u->m);
    while ((u->writer || u->readers > 0) && rc == 0) rc = pthread_cond_timedwait(&u->c, &u->m, &dl);
    if (!u->writer && u->readers == 0) { u->writer = true; rc = 0; }
    pthread_mutex_unlock(&u->m);
    return rc;
}
static void ul_wunlock(ulatch_t *u) {
    pthread_mutex_lock(&u->m); u->writer = false; pthread_cond_broadcast(&u->c); pthread_mutex_unlock(&u->m);
}

/* B2: 제자리 업그레이드. 호출자는 R 을 쥐고 있다. "R 을 쥔 사람이 나 하나(readers == 1)이고 writer 없음" 이
 *     될 때까지 기다렸다가 R 을 W 로 바꾼다(readers = 0, writer = true) → 0.
 *     ms 안에 안 되면 ETIMEDOUT 을 돌려준다 — 이때 R 은 그대로 쥔 채다 (호출자가 정리). */
static int ul_upgrade_bounded(ulatch_t *u, int ms) {
    (void)u; (void)ms;
    return -1; /* TODO B2 */
}

/* ======================= Part C — FTL L2P 인덱스 (LBA → PPA) ======================= */

/* C1: 호스트 쓰기 = 매핑 갱신. optimistic 으로 리프까지 가서
 *       - LBA 가 이미 있으면 값(PPA)만 덮어쓴다 → 0 (리프가 꽉 차 있어도 split 이 필요 없으니 재시작 없음)
 *       - 없고 리프가 안전하면 넣는다 → 1
 *       - 없고 리프가 꽉 찼으면 다 놓고 restarts++ 후 bt_insert_pess. pess 가 false 면(그 사이 다른 큐가
 *         같은 LBA 를 먼저 넣음) 처음부터 다시 (이번엔 덮어쓰기가 된다).                                 */
static int l2p_write(bt_t *t, int lba, int ppa) {
    (void)t; (void)lba; (void)ppa;
    return -1; /* TODO C1 */
}

/* ======================= 테스트 하네스 ======================= */
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static void shuffle(int *a, int n, uint32_t seed) {
    for (int i = n - 1; i > 0; i--) { int j = (int)(xs32(&seed) % (uint32_t)(i + 1)); int t = a[i]; a[i] = a[j]; a[j] = t; }
}

/* 2 명짜리 일회용 장벽 (macOS 에는 pthread_barrier_t 가 없다). 시간 제한이 있어 시작 코드가 멈추지 않는다. */
typedef struct { pthread_mutex_t m; pthread_cond_t c; int arrived, need; } bar_t;
static void bar_init(bar_t *b, int need) { pthread_mutex_init(&b->m, NULL); pthread_cond_init(&b->c, NULL); b->arrived = 0; b->need = need; }
static bool bar_wait(bar_t *b, int ms) {
    struct timespec dl = deadline_ms(ms);
    int rc = 0;
    pthread_mutex_lock(&b->m);
    if (++b->arrived >= b->need) pthread_cond_broadcast(&b->c);
    while (b->arrived < b->need && rc == 0) rc = pthread_cond_timedwait(&b->c, &b->m, &dl);
    bool ok = b->arrived >= b->need;
    pthread_mutex_unlock(&b->m);
    return ok;
}
static void bar_destroy(bar_t *b) { pthread_mutex_destroy(&b->m); pthread_cond_destroy(&b->c); }

/* ---- 멀티스레드 삽입 스트레스 ---- */
enum { NT = 4, PER = 20000 };
typedef struct { bt_t *t; int tid; int mode; const int *keys; int n; long ok; } ins_arg_t;
static void *ins_worker(void *p) {
    ins_arg_t *a = p;
    for (int i = 0; i < a->n; i++) {
        int k = a->keys[i];
        bool r = a->mode == 0 ? bt_insert_pess(a->t, k, k * 3 + 1) : bt_insert_opt(a->t, k, k * 3 + 1);
        a->ok += r;
    }
    return NULL;
}
/* disjoint=true: 스레드 t 는 k ≡ t (mod NT) 키만 (서로 다른 키가 같은 리프에서 부딪힌다).
 * disjoint=false: 모두 같은 키 집합 → 키마다 정확히 한 스레드만 true 여야 한다. */
static bool stress_insert(int mode, bool disjoint, long *total_ok, long *count, int *err) {
    static int keys[NT][PER];
    bt_t t; bt_init(&t);
    pthread_t th[NT]; ins_arg_t a[NT];
    for (int k = 0; k < NT; k++) {
        for (int i = 0; i < PER; i++) keys[k][i] = disjoint ? (i * NT + k) * 7 : i * 7;
        shuffle(keys[k], PER, 0x9E3779B9u ^ (uint32_t)(k * 7919 + mode));
        a[k] = (ins_arg_t){ &t, k, mode, keys[k], PER, 0 };
        pthread_create(&th[k], NULL, ins_worker, &a[k]);
    }
    *total_ok = 0;
    for (int k = 0; k < NT; k++) { pthread_join(th[k], NULL); *total_ok += a[k].ok; }
    *err = bt_check(&t, count);
    bool all = true;
    int v;
    long expect = disjoint ? (long)NT * PER : PER;
    for (long k = 0; k < expect; k++) if (!bt_lookup(&t, (int)k * 7, &v) || v != (int)k * 21 + 1) { all = false; break; }
    for (long k = 0; k < 2000 && all; k++) if (bt_lookup(&t, (int)k * 7 + 3, NULL)) all = false;   /* 없는 키 */
    bt_destroy(&t);
    return all;
}

/* ---- 읽는 중에 쓰기: 미리 넣은 키는 언제 찾아도 있어야 한다 ---- */
typedef struct { bt_t *t; int mode; int tid; long misses; long done; } mix_arg_t;
static atomic_int g_writers_left;
static void *mix_writer(void *p) {
    mix_arg_t *a = p;
    for (int i = 0; i < 15000; i++) {
        int k = (i * 2 + a->tid) * 2 + 1;                      /* 홀수 키만 새로 넣음 */
        if (a->mode == 0) bt_insert_pess(a->t, k, k); else bt_insert_opt(a->t, k, k);
    }
    atomic_fetch_sub(&g_writers_left, 1);
    return NULL;
}
static void *mix_reader(void *p) {
    mix_arg_t *a = p;
    uint32_t s = 12345u + (uint32_t)a->tid;
    while (atomic_load(&g_writers_left) > 0 || a->done < 20000) {
        int k = (int)(xs32(&s) % 30000u) * 2;                  /* 짝수 키 = 미리 넣은 것 */
        int v = -1;
        if (!bt_lookup(a->t, k, &v) || v != k) a->misses++;
        a->done++;
    }
    return NULL;
}
static bool stress_mixed(int mode, long *misses, int *err) {
    bt_t t; bt_init(&t);
    for (int k = 0; k < 30000; k++) bt_insert_pess(&t, k * 2, k * 2);
    atomic_store(&g_writers_left, 2);
    pthread_t th[4]; mix_arg_t a[4];
    for (int i = 0; i < 4; i++) {
        a[i] = (mix_arg_t){ &t, mode, i % 2, 0, 0 };
        pthread_create(&th[i], NULL, i < 2 ? mix_writer : mix_reader, &a[i]);
    }
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    *misses = a[2].misses + a[3].misses;
    long cnt;
    *err = bt_check(&t, &cnt);
    bool ok = cnt == 30000 + 2 * 15000;
    bt_destroy(&t);
    return ok;
}

/* ---- Part B 강제 스케줄 1: 두 optimistic 삽입이 같은 꽉 찬 리프로 ---- */
static bar_t g_bar_opt;
static node_t *g_opt_target;
static atomic_int g_opt_hits;
static void hook_opt(node_t *n) {
    if (n == g_opt_target && atomic_fetch_add(&g_opt_hits, 1) < 2) bar_wait(&g_bar_opt, 2000);
}
typedef struct { bt_t *t; int key; bool ok; } one_arg_t;
static void *opt_one(void *p) { one_arg_t *a = p; a->ok = bt_insert_opt(a->t, a->key, -a->key); return NULL; }

/* ---- Part B 강제 스케줄 2: 두 스레드가 같은 리프를 R 로 쥐고 둘 다 W 로 업그레이드 ---- */
static ulatch_t g_leaf;
static bar_t g_bar_up1, g_bar_up2;
static int g_leaf_keys[4], g_leaf_n;
typedef struct { int tid, key, up_rc, readers_seen, fix_rc; } up_arg_t;
static void *upgrade_worker(void *p) {
    up_arg_t *a = p;
    ul_rlock(&g_leaf);                                    /* optimistic 하게 R 로 리프를 읽었다 */
    bar_wait(&g_bar_up1, 2000);                           /* 둘 다 R 을 쥔 것을 확인한 뒤 */
    a->up_rc = ul_upgrade_bounded(&g_leaf, 300);          /* "써야 하네" → 제자리 업그레이드 */
    pthread_mutex_lock(&g_leaf.m); a->readers_seen = g_leaf.readers; pthread_mutex_unlock(&g_leaf.m);
    bar_wait(&g_bar_up2, 2000);                           /* 둘 다 결과를 기록할 때까지 R 유지 */
    if (a->up_rc == 0) {                                  /* (잘못 구현하면 여기로 온다) */
        g_leaf_keys[g_leaf_n++] = a->key;
        ul_wunlock(&g_leaf);
    } else {
        ul_runlock(&g_leaf);                              /* 고친 프로토콜: 다 놓고 */
        a->fix_rc = ul_wlock_bounded(&g_leaf, 2000);      /* 처음부터 W 로 다시 */
        if (a->fix_rc == 0) { g_leaf_keys[g_leaf_n++] = a->key; ul_wunlock(&g_leaf); }
    }
    return NULL;
}

/* ---- Part C: NVMe 큐 4 개 ---- */
enum { NQ = 4, NLBA = 16384, ROUNDS = 3 };
typedef struct { bt_t *t; int q; int round; long news; } q_arg_t;
static void *nvme_queue(void *p) {
    q_arg_t *a = p;
    int lbas[NLBA / NQ];
    for (int i = 0; i < NLBA / NQ; i++) lbas[i] = i * NQ + a->q;     /* 큐 q 는 LBA ≡ q (mod 4) 담당 */
    shuffle(lbas, NLBA / NQ, 777u + (uint32_t)(a->q * 31 + a->round));
    for (int i = 0; i < NLBA / NQ; i++) a->news += l2p_write(a->t, lbas[i], (a->round << 20) | lbas[i]);
    return NULL;
}

int main(void) {
    /* -------- Part A: 단일 스레드 단위 테스트 -------- */
    printf("-- Part A: 단일 스레드 --\n");
    node_t probe; memset(&probe, 0, sizeof probe);
    probe.nkeys = MAXK - 1; CHECK(safe_for_insert(&probe) == true);
    probe.nkeys = MAXK;     CHECK(safe_for_insert(&probe) == false);

    bt_t t; bt_init(&t);
    CHECK(bt_lookup(&t, 5, NULL) == false);
    CHECK(bt_insert_pess(&t, 5, 50) == true);
    int v = 0;
    CHECK(bt_lookup(&t, 5, &v) == true && v == 50);
    CHECK(bt_insert_pess(&t, 5, 99) == false);
    CHECK(bt_lookup(&t, 5, &v) == true && v == 50);        /* 중복 삽입은 값을 안 바꾼다 */
    for (int k = 10; k <= 80; k += 10) bt_insert_pess(&t, k, k);   /* 5,10..80 = 9 키 → root split */
    CHECK(bt_height(&t) == 2 && t.root->nkeys == 1);
    long cnt; CHECK(bt_check(&t, &cnt) == 0 && cnt == 9);
    bt_destroy(&t);

    enum { NSEQ = 5000 };
    static int perm[NSEQ];
    for (int i = 0; i < NSEQ; i++) perm[i] = i * 3;
    shuffle(perm, NSEQ, 2026u);
    bt_init(&t);
    int good = 0;
    for (int i = 0; i < NSEQ; i++) good += bt_insert_pess(&t, perm[i], perm[i] + 7);
    CHECK(good == NSEQ);
    int found = 0, absent = 0;
    for (int i = 0; i < NSEQ; i++) { found += bt_lookup(&t, i * 3, &v) && v == i * 3 + 7; absent += !bt_lookup(&t, i * 3 + 1, NULL); }
    CHECK(found == NSEQ && absent == NSEQ);
    CHECK(bt_check(&t, &cnt) == 0 && cnt == NSEQ);
    CHECK(bt_height(&t) >= 4);
    printf("    pess 5000 키: 높이 %d, leaf split %ld, inner split %ld\n", bt_height(&t),
           atomic_load(&t.leaf_splits), atomic_load(&t.inner_splits));
    bt_destroy(&t);

    /* -------- Part B: optimistic 단일 스레드 -------- */
    printf("-- Part B: optimistic 단일 스레드 --\n");
    bt_init(&t);
    good = 0;
    for (int i = 0; i < NSEQ; i++) good += bt_insert_opt(&t, perm[i], perm[i] + 7);
    found = 0;
    for (int i = 0; i < NSEQ; i++) found += bt_lookup(&t, i * 3, &v) && v == i * 3 + 7;
    CHECK(good == NSEQ && found == NSEQ);
    CHECK(bt_insert_opt(&t, perm[0], 1) == false);
    CHECK(bt_check(&t, &cnt) == 0 && cnt == NSEQ);
    /* 혼자일 때: 꽉 찬 리프에 넣을 때마다 정확히 한 번 재시작하고, 재시작한 pess 가 그 리프를 나눈다 */
    CHECK(atomic_load(&t.restarts) == atomic_load(&t.leaf_splits) && atomic_load(&t.restarts) > 0);
    printf("    opt 5000 키: restarts %ld = leaf split %ld (삽입의 %.1f%%)\n", atomic_load(&t.restarts),
           atomic_load(&t.leaf_splits), 100.0 * (double)atomic_load(&t.restarts) / NSEQ);
    bt_destroy(&t);

    /* -------- Part B: 업그레이드 단일 스레드 -------- */
    ul_init(&g_leaf);
    ul_rlock(&g_leaf);
    int rc = ul_upgrade_bounded(&g_leaf, 100);
    CHECK(rc == 0 && g_leaf.writer == true && g_leaf.readers == 0);   /* 혼자면 업그레이드 된다 */
    if (rc == 0) ul_wunlock(&g_leaf); else ul_runlock(&g_leaf);
    ul_rlock(&g_leaf);
    CHECK(ul_wlock_bounded(&g_leaf, 50) == ETIMEDOUT);       /* R 이 있으면 W 는 못 잡는다 (주어진 코드 확인) */
    ul_runlock(&g_leaf);
    ul_destroy(&g_leaf);

    bool units_ok = g_fail == 0;

    /* -------- Part B: 강제 스케줄 2 — 업그레이드 교착 (시간 제한 대기로 탐지) -------- */
    printf("-- Part B: 강제 스케줄 — 두 스레드가 R 을 쥔 채 W 로 업그레이드 --\n");
    ul_init(&g_leaf); bar_init(&g_bar_up1, 2); bar_init(&g_bar_up2, 2); g_leaf_n = 0;
    up_arg_t ua[2] = { { 0, 111, -1, -1, -1 }, { 1, 222, -1, -1, -1 } };
    pthread_t uth[2];
    for (int i = 0; i < 2; i++) pthread_create(&uth[i], NULL, upgrade_worker, &ua[i]);
    for (int i = 0; i < 2; i++) pthread_join(uth[i], NULL);
    bool both_timed_out = ua[0].up_rc == ETIMEDOUT && ua[1].up_rc == ETIMEDOUT;
    if (both_timed_out)
        printf("    LATCH DEADLOCK (upgrade): T0 holds R(leaf) waits W(leaf) [readers=%d] -> "
               "T1 holds R(leaf) waits W(leaf) [readers=%d] -> T0  (300 ms 동안 진행 0, 탐지기 없음 → 시간 제한으로 끊음)\n",
               ua[0].readers_seen, ua[1].readers_seen);
    else
        printf("    upgrade rc: T0=%d T1=%d (교착을 못 봤다)\n", ua[0].up_rc, ua[1].up_rc);
    CHECK(both_timed_out && ua[0].readers_seen == 2 && ua[1].readers_seen == 2);
    CHECK(ua[0].fix_rc == 0 && ua[1].fix_rc == 0 && g_leaf_n == 2);   /* 고친 프로토콜: 놓고 W 로 재시작 */
    CHECK(g_leaf.readers == 0 && g_leaf.writer == false);
    ul_destroy(&g_leaf); bar_destroy(&g_bar_up1); bar_destroy(&g_bar_up2);

    if (!units_ok) {
        printf("SKIP 12 stress checks (Part A/B/C 멀티스레드): 단일 스레드 체크가 실패했다\n");
        printf("%d/%d passed\n", g_pass, g_pass + g_fail);
        return 1;
    }

    /* -------- Part B: 강제 스케줄 1 — 두 optimistic 삽입이 같은 꽉 찬 리프로 -------- */
    printf("-- Part B: 강제 스케줄 — 두 optimistic 삽입, 같은 꽉 찬 리프 --\n");
    bt_init(&t);
    for (int k = 10; k <= 90; k += 10) bt_insert_pess(&t, k, k);    /* root(level 1) + 리프 2 개 */
    for (int k = 11; k <= 14; k++) bt_insert_pess(&t, k, k);         /* 왼쪽 리프 = 8 키 (꽉 참) */
    long splits0 = atomic_load(&t.leaf_splits);
    g_opt_target = t.root; atomic_store(&g_opt_hits, 0); bar_init(&g_bar_opt, 2);
    g_hook = hook_opt;                                              /* 둘 다 root 를 R 로 쥘 때까지 붙잡는다 */
    one_arg_t oa[2] = { { &t, 15, false }, { &t, 16, false } };
    pthread_t oth[2];
    for (int i = 0; i < 2; i++) pthread_create(&oth[i], NULL, opt_one, &oa[i]);
    for (int i = 0; i < 2; i++) pthread_join(oth[i], NULL);
    g_hook = NULL;
    printf("    restarts %ld, 새 leaf split %ld\n", atomic_load(&t.restarts), atomic_load(&t.leaf_splits) - splits0);
    CHECK(oa[0].ok && oa[1].ok && bt_lookup(&t, 15, &v) && v == -15 && bt_lookup(&t, 16, &v) && v == -16);
    CHECK(atomic_load(&t.restarts) == 2 && atomic_load(&t.leaf_splits) - splits0 == 1);
    CHECK(bt_check(&t, &cnt) == 0 && cnt == 15);
    bt_destroy(&t); bar_destroy(&g_bar_opt);

    /* -------- Part A/B: 멀티스레드 삽입 -------- */
    printf("-- Part A/B: 4 스레드 x 20000 삽입 --\n");
    const char *nm[2] = { "pess", "opt " };
    for (int mode = 0; mode < 2; mode++) {
        long ok, c; int e;
        bool all = stress_insert(mode, true, &ok, &c, &e);
        printf("    %s disjoint: ok %ld, keys %ld, check %d\n", nm[mode], ok, c, e);
        CHECK(all && ok == (long)NT * PER && c == (long)NT * PER && e == 0);
        all = stress_insert(mode, false, &ok, &c, &e);
        printf("    %s same keys: ok %ld (키당 한 스레드만 성공), keys %ld, check %d\n", nm[mode], ok, c, e);
        CHECK(all && ok == PER && c == PER && e == 0);
    }
    for (int mode = 0; mode < 2; mode++) {
        long miss; int e;
        bool ok = stress_mixed(mode, &miss, &e);
        printf("    %s 읽기 2 + 쓰기 2: 미리 넣은 키를 못 찾은 횟수 %ld, check %d\n", nm[mode], miss, e);
        CHECK(ok && miss == 0 && e == 0);
    }

    /* -------- Part C: FTL L2P -------- */
    printf("-- Part C: NVMe 큐 4 개 → L2P 인덱스 --\n");
    bt_init(&t);
    long news_total[ROUNDS], restarts_at[ROUNDS + 1];
    restarts_at[0] = 0;
    for (int r = 0; r < ROUNDS; r++) {
        pthread_t qt[NQ]; q_arg_t qa[NQ];
        for (int q = 0; q < NQ; q++) { qa[q] = (q_arg_t){ &t, q, r, 0 }; pthread_create(&qt[q], NULL, nvme_queue, &qa[q]); }
        news_total[r] = 0;
        for (int q = 0; q < NQ; q++) { pthread_join(qt[q], NULL); news_total[r] += qa[q].news; }
        restarts_at[r + 1] = atomic_load(&t.restarts);
        printf("    round %d: 새 매핑 %ld, 덮어쓰기 %ld, 이번 라운드 restarts %ld\n", r, news_total[r],
               (long)NLBA - news_total[r], restarts_at[r + 1] - restarts_at[r]);
    }
    CHECK(news_total[0] == NLBA && news_total[1] == 0 && news_total[2] == 0);
    CHECK(restarts_at[2] - restarts_at[1] == 0 && restarts_at[3] - restarts_at[2] == 0);  /* 덮어쓰기 = 재시작 0 */
    int right = 0;
    for (int l = 0; l < NLBA; l++) right += bt_lookup(&t, l, &v) && v == (((ROUNDS - 1) << 20) | l);
    CHECK(right == NLBA && bt_check(&t, &cnt) == 0 && cnt == NLBA);
    bt_destroy(&t);

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
