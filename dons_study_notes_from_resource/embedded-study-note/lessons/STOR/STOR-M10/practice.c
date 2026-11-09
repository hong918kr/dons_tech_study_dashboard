/* STOR-M10 추가 연습 — 한 번에 검증
 *
 * 본문 정답 코드(B+tree + crabbing + optimistic)를 그대로 복사해 두고, 그 위에 연습 4 개를 얹었다.
 *   연습 1  범위 스캔: 형제 링크(next)를 따라 왼쪽 → 오른쪽으로 R 래치 crabbing (다음 리프 잡고 → 지금 리프 놓기)
 *   연습 2  U(update) 래치: R 과는 함께, U·W 와는 함께 못 가는 "쓸지도 모르는 읽기" 모드 → 업그레이드 교착이 없다
 *   연습 3  래치 순서 검사기: 스레드별 쥔 래치 목록으로 "위 → 아래, 같은 층은 왼쪽 → 오른쪽" 규칙 위반을 센다
 *           (mini lockdep — OS-M11 의 아이디어를 래치에 적용)
 *   연습 4  오름차순 삽입의 재시작률: 4|5 split 이면 restarts = (N - 5) / 4, 리프는 거의 다 4 키(반만 참)
 *
 * 연습 3 때문에 복사본의 lat_r / lat_w / unlat / root_* 는 순서 검사 호출이 하나씩 더 붙어 있다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
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

/* ---- 연습 3: 래치 순서 검사기 (래치를 잡기 "전에" 검사 — lockdep 처럼 실제로 멈추기 전에 잡아낸다) ----
 * 규칙: 새로 잡는 래치의 층(level)은 지금 쥔 모든 래치보다 낮아야 한다 (root_latch = 가장 높은 층).
 *       예외: 같은 층의 리프끼리는 "쥔 리프의 next" 만 잡을 수 있다 (스캔은 왼쪽 → 오른쪽).          */
#define ROOT_LATCH_LEVEL 1000000
typedef struct { const void *obj; int level; } held_t;
static _Thread_local held_t tl_held[MAXH + 4];
static _Thread_local int tl_nheld;
static atomic_long g_order_viol;
static void ord_acq(const void *obj, int level) {
    for (int i = 0; i < tl_nheld; i++) {
        const held_t *h = &tl_held[i];
        bool ok = level < h->level ||
                  (level == 0 && h->level == 0 && h->obj != (const void *)0 &&
                   ((const struct node *)h->obj)->next == (const struct node *)obj);
        if (!ok) atomic_fetch_add_explicit(&g_order_viol, 1, memory_order_relaxed);
    }
    if (tl_nheld < MAXH + 4) tl_held[tl_nheld++] = (held_t){ obj, level };
}
static void ord_rel(const void *obj) {
    for (int i = 0; i < tl_nheld; i++)
        if (tl_held[i].obj == obj) { tl_held[i] = tl_held[--tl_nheld]; return; }
}

static bool g_latch_off;           /* demo_bench 의 "전역 rwlock 하나" 판만 true (노드 래치 생략) */
static void (*g_hook)(node_t *n);  /* 강제 스케줄용: R 래치를 잡은 직후 불린다 (테스트에서만 설정) */

GIVEN static void lat_r(node_t *n)  { ord_acq(n, n->level); if (!g_latch_off) pthread_rwlock_rdlock(&n->latch); if (g_hook) g_hook(n); }
GIVEN static void lat_w(node_t *n)  { ord_acq(n, n->level); if (!g_latch_off) pthread_rwlock_wrlock(&n->latch); }
GIVEN static void unlat(node_t *n)  { if (!g_latch_off) pthread_rwlock_unlock(&n->latch); ord_rel(n); }
GIVEN static void root_r(bt_t *t)   { ord_acq(&t->root_latch, ROOT_LATCH_LEVEL); if (!g_latch_off) pthread_rwlock_rdlock(&t->root_latch); }
GIVEN static void root_w(bt_t *t)   { ord_acq(&t->root_latch, ROOT_LATCH_LEVEL); if (!g_latch_off) pthread_rwlock_wrlock(&t->root_latch); }
GIVEN static void root_unl(bt_t *t) { if (!g_latch_off) pthread_rwlock_unlock(&t->root_latch); ord_rel(&t->root_latch); }

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
    return n->nkeys < MAXK;
}

/* A2: lookup. root_latch(R) → root(R) → root_latch 해제 → 자식(R) 잡고 → 부모 해제 … 리프에서 찾기.
 *     찾으면 *val 에 값(val 이 NULL 이 아니면)을 쓰고 true. */
static bool bt_lookup(bt_t *t, int key, int *val) {
    root_r(t);
    node_t *n = t->root;
    lat_r(n);
    root_unl(t);
    while (n->level > 0) {
        node_t *c = n->child[inner_idx(n, key)];
        lat_r(c);                    /* 자식을 먼저 잡고 */
        unlat(n);                    /* 그다음 부모를 놓는다 (crab) */
        n = c;
    }
    int i = leaf_lb(n, key);
    bool found = i < n->nkeys && n->keys[i] == key;
    if (found && val) *val = n->val[i];
    unlat(n);
    return found;
}

/* A3: pessimistic insert. root_latch(W) 부터 W 래치로 내려가며 경로를 스택에 쌓고,
 *     방금 잡은 노드가 safe_for_insert 이면 그 위(조상 + root_latch)를 모두 놓는다.
 *     리프가 꽉 찼으면 leaf_split / inner_split 을 스택을 거슬러 올라가며 반영하고,
 *     root 까지 나뉘면 새 root 를 만든다 (그때는 root_latch 를 아직 쥐고 있어야 한다).
 *     이미 있는 키면 아무것도 바꾸지 않고 false. 새로 넣었으면 true. */
static bool bt_insert_pess(bt_t *t, int key, int val) {
    node_t *path[MAXH];
    int depth = 0;
    bool root_held = true;
    root_w(t);
    node_t *n = t->root;
    lat_w(n);
    path[depth++] = n;
    if (safe_for_insert(n)) { root_unl(t); root_held = false; }
    while (n->level > 0) {
        node_t *c = n->child[inner_idx(n, key)];
        lat_w(c);
        if (safe_for_insert(c)) {    /* c 아래의 split 은 c 에서 멈춘다 → 위는 필요 없다 */
            unlat_path(path, &depth);
            if (root_held) { root_unl(t); root_held = false; }
        }
        path[depth++] = c;
        n = c;
    }
    int i = leaf_lb(n, key);
    bool ok = !(i < n->nkeys && n->keys[i] == key);
    if (ok && n->nkeys < MAXK) {
        leaf_put_at(n, i, key, val);
    } else if (ok) {
        int sep;
        node_t *right = leaf_split(t, n, key, val, &sep);
        for (int d = depth - 2; d >= 0 && right; d--) {
            node_t *p = path[d];
            if (p->nkeys < MAXK) { inner_put(p, sep, right); right = NULL; }
            else right = inner_split(t, p, sep, right, &sep);
        }
        if (right) {                 /* 경로 맨 위 = root 가 나뉘었다 → root_held 가 반드시 true */
            node_t *r = node_new(path[0]->level + 1);
            r->keys[0] = sep; r->child[0] = path[0]; r->child[1] = right; r->nkeys = 1;
            t->root = r;
        }
    }
    unlat_path(path, &depth);
    if (root_held) root_unl(t);
    return ok;
}

/* ======================= Part B — optimistic + 업그레이드 교착 ======================= */

/* B1: optimistic insert. root_latch(R) → R 래치로 내려가되 리프(level 0)만 W 래치.
 *     (부모가 level 1 이면 자식은 리프. root 자체가 리프면 root 를 W.)
 *     리프에 키가 이미 있으면 false. 리프가 안전하면 넣고 true.
 *     리프가 꽉 찼으면: 리프를 놓고(래치를 하나도 쥐지 않은 채) restarts 를 1 올리고 bt_insert_pess 로 재시작.
 *     절대 R 을 쥔 채 W 를 기다리지 않는다 (업그레이드 금지). */
static bool bt_insert_opt(bt_t *t, int key, int val) {
    root_r(t);
    node_t *n = t->root;
    if (n->level == 0) lat_w(n); else lat_r(n);
    root_unl(t);
    while (n->level > 0) {
        node_t *c = n->child[inner_idx(n, key)];
        if (n->level == 1) lat_w(c); else lat_r(c);
        unlat(n);
        n = c;
    }
    int i = leaf_lb(n, key);
    if (i < n->nkeys && n->keys[i] == key) { unlat(n); return false; }
    if (n->nkeys < MAXK) { leaf_put_at(n, i, key, val); unlat(n); return true; }
    unlat(n);                        /* 다 놓고 */
    atomic_fetch_add_explicit(&t->restarts, 1, memory_order_relaxed);
    return bt_insert_pess(t, key, val);   /* 처음부터 W 로 */
}


/* ======================= 연습 1 — 범위 스캔 (형제 링크 crabbing) ======================= */
/* [lo, hi] 의 키를 오름차순으로 out 에 (최대 max 개) 담고, 범위 안 키의 총수를 돌려준다.
 * 내려갈 때는 lookup 과 같고, 리프 층에서는 "다음 리프 R → 지금 리프 해제" 로 오른쪽으로만 간다. */
static int bt_scan(bt_t *t, int lo, int hi, int *out, int max) {
    root_r(t);
    node_t *n = t->root;
    lat_r(n);
    root_unl(t);
    while (n->level > 0) {
        node_t *c = n->child[inner_idx(n, lo)];
        lat_r(c);
        unlat(n);
        n = c;
    }
    int cnt = 0;
    for (;;) {
        for (int i = leaf_lb(n, lo); i < n->nkeys; i++) {
            if (n->keys[i] > hi) { unlat(n); return cnt; }
            if (cnt < max) out[cnt] = n->keys[i];
            cnt++;
        }
        node_t *nx = n->next;
        if (!nx) { unlat(n); return cnt; }
        lat_r(nx);                    /* 오른쪽 형제를 먼저 잡고 */
        unlat(n);                     /* 그다음 지금 리프를 놓는다 */
        n = nx;
    }
}

/* ======================= 연습 2 — U(update) 래치 ======================= */
/* 호환표:      R    U    W
 *          R   O    O    X
 *          U   O    X    X
 *          W   X    X    X        U 는 한 번에 한 명 → 업그레이드를 기다리는 사람이 둘일 수 없다. */
typedef struct { pthread_mutex_t m; pthread_cond_t c; int readers; bool upd, writer; } uxlatch_t;
static void ux_init(uxlatch_t *u) { pthread_mutex_init(&u->m, NULL); pthread_cond_init(&u->c, NULL); u->readers = 0; u->upd = u->writer = false; }
static void ux_destroy(uxlatch_t *u) { pthread_mutex_destroy(&u->m); pthread_cond_destroy(&u->c); }
static struct timespec ux_deadline(int ms) {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000; ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    return ts;
}
static int ux_rlock_bounded(uxlatch_t *u, int ms) {
    struct timespec dl = ux_deadline(ms); int rc = 0;
    pthread_mutex_lock(&u->m);
    while (u->writer && rc == 0) rc = pthread_cond_timedwait(&u->c, &u->m, &dl);
    if (!u->writer) { u->readers++; rc = 0; }
    pthread_mutex_unlock(&u->m);
    return rc;
}
static void ux_runlock(uxlatch_t *u) { pthread_mutex_lock(&u->m); u->readers--; pthread_cond_broadcast(&u->c); pthread_mutex_unlock(&u->m); }
static int ux_ulock_bounded(uxlatch_t *u, int ms) {
    struct timespec dl = ux_deadline(ms); int rc = 0;
    pthread_mutex_lock(&u->m);
    while ((u->writer || u->upd) && rc == 0) rc = pthread_cond_timedwait(&u->c, &u->m, &dl);
    if (!u->writer && !u->upd) { u->upd = true; rc = 0; }
    pthread_mutex_unlock(&u->m);
    return rc;
}
/* U → W: R 이 다 빠질 때까지만 기다린다. 다른 U 는 있을 수 없으니 순환이 안 생긴다. */
static int ux_upgrade_bounded(uxlatch_t *u, int ms) {
    struct timespec dl = ux_deadline(ms); int rc = 0;
    pthread_mutex_lock(&u->m);
    while (u->readers > 0 && rc == 0) rc = pthread_cond_timedwait(&u->c, &u->m, &dl);
    if (u->readers == 0) { u->upd = false; u->writer = true; rc = 0; }
    pthread_mutex_unlock(&u->m);
    return rc;
}
static void ux_uunlock(uxlatch_t *u) { pthread_mutex_lock(&u->m); u->upd = false; pthread_cond_broadcast(&u->c); pthread_mutex_unlock(&u->m); }
static void ux_wunlock(uxlatch_t *u) { pthread_mutex_lock(&u->m); u->writer = false; pthread_cond_broadcast(&u->c); pthread_mutex_unlock(&u->m); }

/* ======================= 하네스 ======================= */
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static void shuffle(int *a, int n, uint32_t seed) {
    for (int i = n - 1; i > 0; i--) { int j = (int)(xs32(&seed) % (uint32_t)(i + 1)); int t = a[i]; a[i] = a[j]; a[j] = t; }
}

/* 연습 1: 쓰는 중에 스캔 */
static atomic_int g_wleft;
typedef struct { bt_t *t; int tid; long bad, scans; } sc_arg_t;
static void *scan_writer(void *p) {
    sc_arg_t *a = p;
    int ks[20000];
    for (int i = 0; i < 20000; i++) ks[i] = (i * 2 + a->tid) * 2 + 1;     /* 홀수 키 */
    shuffle(ks, 20000, 99u + (uint32_t)a->tid);
    for (int i = 0; i < 20000; i++) { if (a->tid == 0) bt_insert_pess(a->t, ks[i], ks[i]); else bt_insert_opt(a->t, ks[i], ks[i]); }
    atomic_fetch_sub(&g_wleft, 1);
    return NULL;
}
static void *scan_reader(void *p) {
    sc_arg_t *a = p;
    static _Thread_local int out[4096];
    uint32_t s = 7u + (uint32_t)a->tid;
    while (atomic_load(&g_wleft) > 0 || a->scans < 300) {
        int lo = (int)(xs32(&s) % 70000u), hi = lo + 400;
        int n = bt_scan(a->t, lo, hi, out, 4096);
        int even = 0;
        for (int i = 0; i < n && i < 4096; i++) {
            if (i > 0 && out[i - 1] >= out[i]) a->bad++;                 /* 정렬, 중복 없음 */
            if (out[i] < lo || out[i] > hi) a->bad++;
            if (out[i] % 2 == 0) even++;
        }
        int e_lo = (lo + 1) / 2 * 2, e_hi = hi / 2 * 2, expect = 0;      /* 미리 넣은 짝수 키 [0, 60000) */
        if (e_hi > 59998) e_hi = 59998;
        if (e_lo <= e_hi) expect = (e_hi - e_lo) / 2 + 1;
        if (even != expect) a->bad++;
        a->scans++;
    }
    return NULL;
}

/* 연습 2: 쓸지도 모르는 두 스레드 */
static uxlatch_t g_ux;
static pthread_mutex_t g_seq_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_seq_c = PTHREAD_COND_INITIALIZER;
static int g_stage, g_log[4], g_nlog;
typedef struct { int tid, u_rc, up_rc; } ux_arg_t;
static void wait_stage(int s) {
    struct timespec dl = ux_deadline(2000); int rc = 0;
    pthread_mutex_lock(&g_seq_m);
    while (g_stage < s && rc == 0) rc = pthread_cond_timedwait(&g_seq_c, &g_seq_m, &dl);
    pthread_mutex_unlock(&g_seq_m);
}
static void set_stage(int s) { pthread_mutex_lock(&g_seq_m); g_stage = s; pthread_cond_broadcast(&g_seq_c); pthread_mutex_unlock(&g_seq_m); }
static void *ux_worker(void *p) {
    ux_arg_t *a = p;
    if (a->tid == 1) wait_stage(1);                   /* T1 은 T0 가 U 를 쥔 뒤에 출발 */
    a->u_rc = ux_ulock_bounded(&g_ux, 2000);
    if (a->u_rc != 0) return NULL;
    if (a->tid == 0) set_stage(1);
    a->up_rc = ux_upgrade_bounded(&g_ux, 2000);       /* main 의 R 이 빠질 때까지 기다림 */
    if (a->up_rc == 0) { g_log[g_nlog++] = a->tid; ux_wunlock(&g_ux); }
    else ux_uunlock(&g_ux);
    return NULL;
}

int main(void) {
    int v; long cnt;

    /* -------- 연습 1 -------- */
    printf("-- 연습 1: 범위 스캔 --\n");
    bt_t t; bt_init(&t);
    for (int k = 0; k < 30000; k++) bt_insert_pess(&t, k * 2, k * 2);
    int out[512];
    int n = bt_scan(&t, 100, 140, out, 512);
    CHECK(n == 21 && out[0] == 100 && out[20] == 140);
    n = bt_scan(&t, 59990, 1 << 30, out, 512);
    CHECK(n == 5 && out[4] == 59998);
    CHECK(bt_scan(&t, 101, 101, out, 512) == 0);
    atomic_store(&g_wleft, 2);
    pthread_t th[4]; sc_arg_t sa[4];
    for (int i = 0; i < 4; i++) { sa[i] = (sc_arg_t){ &t, i % 2, 0, 0 }; pthread_create(&th[i], NULL, i < 2 ? scan_writer : scan_reader, &sa[i]); }
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    printf("    스캔 %ld 회, 이상한 결과 %ld\n", sa[2].scans + sa[3].scans, sa[2].bad + sa[3].bad);
    CHECK(sa[2].bad == 0 && sa[3].bad == 0 && sa[2].scans >= 300 && sa[3].scans >= 300);
    CHECK(bt_check(&t, &cnt) == 0 && cnt == 30000 + 40000);
    n = bt_scan(&t, 0, 1 << 30, NULL, 0);
    CHECK(n == 70000);
    bt_destroy(&t);

    /* -------- 연습 2 -------- */
    printf("-- 연습 2: U 래치 --\n");
    ux_init(&g_ux);
    CHECK(ux_rlock_bounded(&g_ux, 10) == 0);                       /* main 이 R 을 쥔다 */
    ux_arg_t ua[2] = { { 0, -1, -1 }, { 1, -1, -1 } };
    pthread_t ut[2];
    for (int i = 0; i < 2; i++) pthread_create(&ut[i], NULL, ux_worker, &ua[i]);
    wait_stage(1);                                                  /* T0 가 U 를 쥐었다 (R 과 공존) */
    struct timespec nap = { 0, 50 * 1000000L }; nanosleep(&nap, NULL);
    pthread_mutex_lock(&g_ux.m);
    bool coexist = g_ux.upd && g_ux.readers == 1 && !g_ux.writer;   /* R + U 공존, T0 는 업그레이드 대기 */
    pthread_mutex_unlock(&g_ux.m);
    CHECK(coexist);
    ux_runlock(&g_ux);                                              /* R 이 빠지면 T0 업그레이드 */
    for (int i = 0; i < 2; i++) pthread_join(ut[i], NULL);
    printf("    T0 u=%d up=%d, T1 u=%d up=%d, 쓴 순서 %d,%d\n", ua[0].u_rc, ua[0].up_rc, ua[1].u_rc, ua[1].up_rc, g_log[0], g_log[1]);
    CHECK(ua[0].u_rc == 0 && ua[0].up_rc == 0 && ua[1].u_rc == 0 && ua[1].up_rc == 0);
    CHECK(g_nlog == 2 && g_log[0] == 0 && g_log[1] == 1);           /* 두 번째 U 는 첫 번째가 끝날 때까지 기다렸다 */
    ux_destroy(&g_ux);

    /* -------- 연습 3 -------- */
    printf("-- 연습 3: 래치 순서 검사기 --\n");
    CHECK(atomic_load(&g_order_viol) == 0);                         /* 연습 1 의 4 스레드 스트레스 동안에도 0 */
    bt_init(&t);
    for (int k = 0; k < 3000; k++) bt_insert_opt(&t, (k * 37) % 3001, k);
    for (int k = 0; k < 3000; k++) bt_lookup(&t, k, &v);
    bt_scan(&t, 0, 1 << 30, NULL, 0);
    CHECK(atomic_load(&g_order_viol) == 0 && tl_nheld == 0);        /* 올바른 프로토콜: 위반 0, 남은 래치 0 */
    /* 일부러 틀린 순서: 리프를 쥔 채 root 를 다시 잡기 (아래 → 위). 혼자라 멈추진 않지만 규칙 위반 */
    root_r(&t); node_t *r = t.root; lat_r(r); root_unl(&t);
    node_t *leaf = r; while (leaf->level > 0) leaf = leaf->child[0];
    unlat(r);
    lat_r(leaf);
    lat_r(r);                                                       /* 위반 1 */
    unlat(r); unlat(leaf);
    CHECK(atomic_load(&g_order_viol) == 1 && tl_nheld == 0);
    /* 오른쪽 → 왼쪽 형제도 위반 */
    node_t *l0 = leaf, *l1 = leaf->next;
    lat_r(l1); lat_r(l0); unlat(l0); unlat(l1);
    CHECK(atomic_load(&g_order_viol) == 2);
    bt_destroy(&t);

    /* -------- 연습 4 -------- */
    printf("-- 연습 4: 오름차순 삽입의 재시작률 --\n");
    const int N = 1000;
    bt_init(&t);
    for (int k = 0; k < N; k++) bt_insert_opt(&t, k, k);
    long rs = atomic_load(&t.restarts);
    node_t *lf = t.root; while (lf->level > 0) lf = lf->child[0];
    int leaves = 0, four = 0;
    for (node_t *x = lf; x; x = x->next) { leaves++; if (x->next && x->nkeys == 4) four++; }
    CHECK(bt_height(&t) == 4);                 /* 리프 249 → level1 ~50 → level2 ~10 → root(level 3) */
    printf("    오름차순 %d 키: restarts %ld (%.1f%%), 리프 %d 개 중 마지막 빼고 4 키인 것 %d\n",
           N, rs, 100.0 * (double)rs / N, leaves, four);
    CHECK(rs == (N - 5) / 4 && rs == atomic_load(&t.leaf_splits));
    CHECK(four == leaves - 1);
    bt_destroy(&t);
    bt_init(&t);
    static int perm[1000];
    for (int i = 0; i < N; i++) perm[i] = i;
    shuffle(perm, N, 2026u);
    for (int i = 0; i < N; i++) bt_insert_opt(&t, perm[i], perm[i]);
    printf("    무작위 %d 키: restarts %ld (%.1f%%)\n", N, atomic_load(&t.restarts), 100.0 * (double)atomic_load(&t.restarts) / N);
    CHECK(atomic_load(&t.restarts) < rs);
    bt_destroy(&t);

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
