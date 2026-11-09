/* STOR-M10 demo_planted — 일부러 넣은 crabbing 버그 두 개
 *
 *   ./planted forced         : lookup 이 "자식을 잡기 전에 부모를 놓는다" — 강제 스케줄 (결정적).
 *                              부모를 놓은 틈에 다른 스레드가 그 리프를 split → 찾던 키가 오른쪽 새 노드로 이사 → miss.
 *                              같은 자리에서 올바른 crabbing 은 writer 를 막아 두므로 찾는다.
 *   ./planted lookup [widen] : 같은 버그를 무작위 스트레스로. 쓰기 2 개가 홀수 키를 오름차순으로 넣는 동안 읽기 2 개가
 *                              쓰는 곳 근처의 짝수 키(미리 넣은 것)를 찾는다. widen = 틈에 20 µs nanosleep.
 *                              (데이터 레이스가 아니다 — 모든 노드 접근은 그 노드의 래치 안. TSan 으로는 안 잡힌다)
 *   ./planted insert [widen] : pessimistic insert 가 자식의 nkeys 를 래치 없이 엿보고(peek), "안전해 보이면"
 *                              자식을 잡기 전에 조상 래치를 놓는다. 엿본 뒤 자식이 꽉 차면 split 을 올려 보낼 부모를
 *                              이미 놓은 상태 → root_latch 없이 root 를 바꾼다 → 키가 통째로 사라진다. TSan 은 레이스 보고.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_planted.c -o planted && ./planted forced
 *        cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_planted.c -o planted_tsan && ./planted_tsan insert
 */
#define main stor_m10_tests
#include "solution.c"
#undef main

static atomic_long g_bad_root_swaps;   /* root_latch 없이 root 를 바꾼 횟수 */
static int g_widen;                    /* 버그의 틈을 넓히는 20 µs 잠 (인자 widen) */
static void (*g_window)(void);         /* forced 모드: "부모를 놓은 틈"(버그) / "자식까지 쥔 때"(정답) 에 불린다 */
static void widen(void) { if (g_widen) { struct timespec z = { 0, 20000 }; nanosleep(&z, NULL); } }

/* 버그 1: 부모를 먼저 놓는다 */
static bool lookup_early_release(bt_t *t, int key, int *val) {
    root_r(t);
    node_t *n = t->root;
    lat_r(n);
    root_unl(t);
    while (n->level > 0) {
        node_t *c = n->child[inner_idx(n, key)];
        unlat(n);                    /* BUG: 자식을 잡기 전에 부모를 놓음 → 그 틈에 c 가 split 될 수 있다 */
        widen();
        if (g_window && c->level == 0) g_window();   /* level 은 바뀌지 않는 필드라 래치 없이 읽어도 된다 */
        lat_r(c);
        n = c;
    }
    int i = leaf_lb(n, key);
    bool found = i < n->nkeys && n->keys[i] == key;
    if (found && val) *val = n->val[i];
    unlat(n);
    return found;
}

/* 버그 2: 래치 없이 엿보고 조상을 먼저 놓는다 */
__attribute__((noinline)) static bool insert_peek_bug(bt_t *t, int key, int val) {
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
        if (c->nkeys < MAXK) {       /* BUG: c 의 래치 없이 nkeys 를 읽는다 (다른 writer 가 c 를 쓰는 중일 수 있다) */
            unlat_path(path, &depth);/* BUG: c 를 잡기 전에 부모를 놓는다 */
            if (root_held) { root_unl(t); root_held = false; }
            widen();
        }
        lat_w(c);
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
        if (right) {                 /* 엿본 값이 틀렸으면 여기서 root 래치 없이 root 를 바꾼다 */
            node_t *r = node_new(path[0]->level + 1);
            r->keys[0] = sep; r->child[0] = path[0]; r->child[1] = right; r->nkeys = 1;
            if (!root_held) atomic_fetch_add(&g_bad_root_swaps, 1);
            t->root = r;
        }
    }
    unlat_path(path, &depth);
    if (root_held) root_unl(t);
    return ok;
}

/* 비교용: 올바른 crabbing lookup + 같은 자리(자식을 잡은 뒤, 부모를 놓기 전)의 hook */
static bool lookup_crab_hooked(bt_t *t, int key, int *val) {
    root_r(t);
    node_t *n = t->root;
    lat_r(n);
    root_unl(t);
    while (n->level > 0) {
        node_t *c = n->child[inner_idx(n, key)];
        lat_r(c);
        if (g_window && c->level == 0) g_window();
        unlat(n);
        n = c;
    }
    int i = leaf_lb(n, key);
    bool found = i < n->nkeys && n->keys[i] == key;
    if (found && val) *val = n->val[i];
    unlat(n);
    return found;
}

/* forced 모드: hook 이 writer 를 깨워 "키 15 삽입(→ 리프 split)" 을 시키고 최대 200 ms 기다린다 */
static pthread_mutex_t g_fm = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_fc = PTHREAD_COND_INITIALIZER;
static int g_fstate;                   /* 0 대기, 1 writer 출발, 2 writer 끝 */
static bt_t *g_ft;
static bool g_writer_done_in_window;
static void *forced_writer(void *p) {
    (void)p;
    struct timespec dl = deadline_ms(3000); int rc = 0;
    pthread_mutex_lock(&g_fm);
    while (g_fstate < 1 && rc == 0) rc = pthread_cond_timedwait(&g_fc, &g_fm, &dl);
    pthread_mutex_unlock(&g_fm);
    bt_insert_pess(g_ft, 15, 15);      /* 꽉 찬 왼쪽 리프 → split: 14,15,20,30,40 이 새 오른쪽 노드로 */
    pthread_mutex_lock(&g_fm); g_fstate = 2; pthread_cond_broadcast(&g_fc); pthread_mutex_unlock(&g_fm);
    return NULL;
}
static void window_run_writer(void) {
    struct timespec dl = deadline_ms(200); int rc = 0;
    pthread_mutex_lock(&g_fm);
    g_fstate = 1; pthread_cond_broadcast(&g_fc);
    while (g_fstate < 2 && rc == 0) rc = pthread_cond_timedwait(&g_fc, &g_fm, &dl);
    g_writer_done_in_window = g_fstate == 2;
    pthread_mutex_unlock(&g_fm);
    g_window = NULL;                   /* 한 번만 */
}
static void forced_case(bool buggy) {
    bt_t t; bt_init(&t);
    for (int k = 10; k <= 90; k += 10) bt_insert_pess(&t, k, k);   /* root + [10..40] [50..90] */
    for (int k = 11; k <= 14; k++) bt_insert_pess(&t, k, k);        /* 왼쪽 리프 8 키 = 꽉 참 */
    g_ft = &t; g_fstate = 0;
    pthread_t w; pthread_create(&w, NULL, forced_writer, NULL);
    g_window = window_run_writer;
    int v = -1;
    bool f = buggy ? lookup_early_release(&t, 40, &v) : lookup_crab_hooked(&t, 40, &v);
    pthread_join(w, NULL);
    printf("%-22s lookup(40) = %s   (writer 가 틈 안에서 split 을 끝냈나: %s)  나중에 다시 lookup(40) = %s\n",
           buggy ? "early-release (BUG):" : "correct crabbing:", f ? "found" : "MISSING",
           g_writer_done_in_window ? "yes" : "no — 200 ms 동안 막힘", bt_lookup(&t, 40, &v) ? "found" : "MISSING");
    bt_destroy(&t);
}

static atomic_int g_left;
typedef struct { bt_t *t; int tid; long miss, done; } d_arg_t;
static atomic_int g_frontier;          /* 쓰기 스레드가 지금 넣고 있는 근처의 키 */
static void *w_odd(void *p) {
    d_arg_t *a = p;
    for (int i = 0; i < 100000; i++) {    /* 홀수 키(< 400000)를 오름차순으로 → split 이 "지금 쓰는 곳" 에 몰린다 */
        int k = (i * 2 + a->tid) * 2 + 1;
        bt_insert_pess(a->t, k, k);
        atomic_store_explicit(&g_frontier, k, memory_order_relaxed);
    }
    atomic_fetch_sub(&g_left, 1);
    return NULL;
}
static void *r_even(void *p) {
    d_arg_t *a = p;
    uint32_t s = 99u + (uint32_t)a->tid;
    while (atomic_load(&g_left) > 0) {
        int fr = atomic_load_explicit(&g_frontier, memory_order_relaxed) / 2;
        int k = (fr + (int)(xs32(&s) % 32u) - 8) * 2, v;   /* 쓰는 곳 근처의 짝수 키 (미리 넣은 것) */
        if (k < 0) k = 0;
        if (k > 399998) k = 399998;
        bool f = (a->tid >= 4) ? bt_lookup(a->t, k, &v) : lookup_early_release(a->t, k, &v);
        if (!f) a->miss++;
        a->done++;
    }
    return NULL;
}
static void *w_bug(void *p) {
    d_arg_t *a = p;
    int *ks = malloc(sizeof(int) * 50000);
    for (int i = 0; i < 50000; i++) ks[i] = i * 4 + a->tid;
    shuffle(ks, 50000, 5u + (uint32_t)a->tid);
    for (int i = 0; i < 50000; i++) a->done += insert_peek_bug(a->t, ks[i], ks[i]);
    free(ks);
    return NULL;
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "forced";
    g_widen = argc > 2 && strcmp(argv[2], "widen") == 0;
    if (strcmp(mode, "forced") == 0) { forced_case(true); forced_case(false); return 0; }
    bt_t t; bt_init(&t);
    if (strcmp(mode, "lookup") == 0) {
        for (int k = 0; k < 200000; k++) bt_insert_pess(&t, k * 2, k * 2);
        atomic_store(&g_left, 2);
        /* tid 0,1 = 쓰기, tid 2,3 = 버그 lookup, tid 4,5 = 올바른 lookup (같은 시간, 같은 트리) */
        pthread_t th[6]; d_arg_t a[6];
        for (int i = 0; i < 6; i++) { a[i] = (d_arg_t){ &t, i, 0, 0 }; pthread_create(&th[i], NULL, i < 2 ? w_odd : r_even, &a[i]); }
        for (int i = 0; i < 6; i++) pthread_join(th[i], NULL);
        printf("early-release lookup: %ld lookups, missed %ld (미리 넣은 키인데 없다고 답함)\n", a[2].done + a[3].done, a[2].miss + a[3].miss);
        printf("correct crabbing    : %ld lookups, missed %ld\n", a[4].done + a[5].done, a[4].miss + a[5].miss);
    } else {
        pthread_t th[4]; d_arg_t a[4];
        for (int i = 0; i < 4; i++) { a[i] = (d_arg_t){ &t, i, 0, 0 }; pthread_create(&th[i], NULL, w_bug, &a[i]); }
        long ok = 0;
        for (int i = 0; i < 4; i++) { pthread_join(th[i], NULL); ok += a[i].done; }
        long cnt; int e = bt_check(&t, &cnt);
        int lost = 0;
        for (int k = 0; k < 200000; k++) lost += !bt_lookup(&t, k, NULL);
        printf("peek-bug insert: returned true %ld / 200000, bt_check = %d, keys reachable %ld, lookup misses %d, "
               "root swaps without root_latch %ld\n", ok, e, cnt, lost, atomic_load(&g_bad_root_swaps));
    }
    return 0;   /* 버그 판이라 트리를 해제하지 않는다 (구조가 깨졌을 수 있다) */
}
