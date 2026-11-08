/* DS-B10 추가 연습 — 한 번에 검증
 * 본문 테이블을 조금 확장: 버킷 수가 변수(nbucket)이고, 노드에 해시 값을 캐시한다.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_BUCKET 32u
#define POOL_N     32u
#define KEY_MAX    16u

typedef struct Node {
    struct Node *next;
    uint32_t hash;          /* 연습 4: 해시 캐시 — strcmp 전에 먼저 비교, rehash 때 재계산 불필요 */
    uint32_t value;
    char key[KEY_MAX];
} Node;

typedef struct {
    Node *bucket[MAX_BUCKET];
    unsigned nbucket;       /* 2의 거듭제곱, MAX_BUCKET 이하 */
    Node pool[POOL_N];
    Node *free_list;
    unsigned count;
} HashTable;

static unsigned g_strcmp_calls;
static int counted_strcmp(const char *a, const char *b) { g_strcmp_calls++; return strcmp(a, b); }

static uint32_t fnv1a_str(const char *s) {
    uint32_t h = 2166136261u;
    for (const uint8_t *p = (const uint8_t *)s; *p; p++) { h ^= *p; h *= 16777619u; }
    return h;
}

static void ht_init(HashTable *t, unsigned nbucket) {
    memset(t, 0, sizeof *t);
    t->nbucket = nbucket;
    for (unsigned i = POOL_N; i-- > 0;) { t->pool[i].next = t->free_list; t->free_list = &t->pool[i]; }
}

static bool ht_put(HashTable *t, const char *key, uint32_t value) {
    size_t len = strlen(key);
    if (len >= KEY_MAX) return false;
    uint32_t h = fnv1a_str(key);
    Node **head = &t->bucket[h & (t->nbucket - 1u)];
    for (Node *n = *head; n; n = n->next)
        if (n->hash == h && counted_strcmp(n->key, key) == 0) { n->value = value; return true; }
    Node *n = t->free_list;
    if (!n) return false;
    t->free_list = n->next;
    memcpy(n->key, key, len + 1);
    n->hash = h; n->value = value;
    n->next = *head; *head = n;
    t->count++;
    return true;
}

/* 연습 4: use_hash=false 면 해시 비교 없이 strcmp 만 (캐시 없는 본문 방식) */
static Node *ht_find(HashTable *t, const char *key, bool use_hash) {
    uint32_t h = fnv1a_str(key);
    for (Node *n = t->bucket[h & (t->nbucket - 1u)]; n; n = n->next) {
        if (use_hash && n->hash != h) continue;          /* 32비트 비교 한 번으로 대부분 걸러낸다 */
        if (counted_strcmp(n->key, key) == 0) return n;
    }
    return NULL;
}

/* 연습 1: 순회 — 콜백 + 컨텍스트 포인터 (C 에서 클로저 대신 쓰는 관용구) */
typedef void (*VisitFn)(const char *key, uint32_t value, void *ctx);
static void ht_foreach(const HashTable *t, VisitFn fn, void *ctx) {
    for (unsigned b = 0; b < t->nbucket; b++)
        for (const Node *n = t->bucket[b]; n; n = n->next) fn(n->key, n->value, ctx);
}
static void sum_visit(const char *key, uint32_t value, void *ctx) {
    (void)key;
    uint32_t *acc = (uint32_t *)ctx;
    acc[0] += value;
    acc[1] += 1;
}

/* 연습 2: move-to-front — 찾은 노드를 체인 head 로 옮긴다 (자주 찾는 키가 빨라짐) */
static bool ht_get_mtf(HashTable *t, const char *key, uint32_t *out) {
    uint32_t h = fnv1a_str(key);
    Node **head = &t->bucket[h & (t->nbucket - 1u)];
    for (Node **pp = head; *pp; pp = &(*pp)->next) {
        Node *n = *pp;
        if (n->hash == h && counted_strcmp(n->key, key) == 0) {
            if (pp != head) { *pp = n->next; n->next = *head; *head = n; }  /* 떼고 → head 에 꽂기 */
            *out = n->value;
            return true;
        }
    }
    return false;
}

/* 연습 3: 버킷 수 늘리기 (rehash) — 노드는 새로 할당하지 않고 링크만 다시 건다 */
static bool ht_grow(HashTable *t, unsigned new_n) {
    if (new_n > MAX_BUCKET || new_n <= t->nbucket) return false;
    for (unsigned b = t->nbucket; b < new_n; b++) t->bucket[b] = NULL;
    unsigned old_n = t->nbucket;
    t->nbucket = new_n;
    for (unsigned b = 0; b < old_n; b++) {
        Node *n = t->bucket[b];
        t->bucket[b] = NULL;                       /* 먼저 체인을 통째로 떼어낸다 */
        while (n) {
            Node *next = n->next;
            Node **head = &t->bucket[n->hash & (new_n - 1u)];   /* 캐시된 해시 → 재계산 없음 */
            n->next = *head; *head = n;
            n = next;
        }
    }
    return true;
}

static unsigned chain_len(const HashTable *t, unsigned b) {
    unsigned c = 0;
    for (const Node *n = t->bucket[b]; n; n = n->next) c++;
    return c;
}
static bool ht_check(const HashTable *t) {
    unsigned total = 0;
    for (unsigned b = 0; b < t->nbucket; b++)
        for (const Node *n = t->bucket[b]; n; n = n->next) {
            if ((n->hash & (t->nbucket - 1u)) != b || n->hash != fnv1a_str(n->key)) return false;
            total++;
        }
    return total == t->count;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static HashTable T;

int main(void) {
    char key[KEY_MAX];
    uint32_t v = 0;

    /* 연습 1 */
    ht_init(&T, 8);
    for (unsigned i = 0; i < 20; i++) { snprintf(key, sizeof key, "sensor%u", i); ht_put(&T, key, i * 10u); }
    uint32_t acc[2] = {0, 0};
    ht_foreach(&T, sum_visit, acc);
    CHECK(acc[0] == 1900u && acc[1] == 20u);              /* 10*(0+...+19) = 1900 */

    /* 연습 2: 체인 tail 의 키를 찾으면 head 로 올라온다 */
    unsigned b = 0;
    while (chain_len(&T, b) < 3) b++;                     /* 길이 3 이상인 체인 하나 */
    const Node *tail = T.bucket[b];
    while (tail->next) tail = tail->next;
    char tail_key[KEY_MAX];
    memcpy(tail_key, tail->key, KEY_MAX);
    unsigned len_before = chain_len(&T, b);
    CHECK(ht_get_mtf(&T, tail_key, &v) && strcmp(T.bucket[b]->key, tail_key) == 0);
    CHECK(chain_len(&T, b) == len_before && ht_check(&T));
    g_strcmp_calls = 0;
    ht_get_mtf(&T, tail_key, &v);
    CHECK(g_strcmp_calls == 1);                           /* 이제 head 에서 바로 찾음 */

    /* 연습 3: 8 → 32 버킷 */
    unsigned max8 = 0, max32 = 0;
    for (unsigned i = 0; i < 8; i++) if (chain_len(&T, i) > max8) max8 = chain_len(&T, i);
    CHECK(ht_grow(&T, 32) && ht_check(&T) && T.count == 20);
    for (unsigned i = 0; i < 32; i++) if (chain_len(&T, i) > max32) max32 = chain_len(&T, i);
    printf("grow 8->32: max chain %u -> %u\n", max8, max32);
    CHECK(max32 <= max8);
    bool all = true;
    for (unsigned i = 0; i < 20; i++) {
        snprintf(key, sizeof key, "sensor%u", i);
        Node *n = ht_find(&T, key, true);
        if (!n || n->value != i * 10u) all = false;
    }
    CHECK(all);
    CHECK(!ht_grow(&T, 64));                              /* MAX_BUCKET 초과 거부 */

    /* 연습 4: 해시 캐시로 strcmp 호출 수 줄이기 (8 버킷으로 체인을 길게) */
    ht_init(&T, 8);
    for (unsigned i = 0; i < 32; i++) { snprintf(key, sizeof key, "cmd%u", i); ht_put(&T, key, i); }
    unsigned calls[2];
    for (int use = 0; use < 2; use++) {
        g_strcmp_calls = 0;
        for (unsigned i = 0; i < 64; i++) {               /* 32개는 있음, 32개는 없음 */
            snprintf(key, sizeof key, "cmd%u", i);
            ht_find(&T, key, use == 1);
        }
        calls[use] = g_strcmp_calls;
    }
    printf("64 lookups: strcmp calls without hash cache %u, with cache %u\n", calls[0], calls[1]);
    CHECK(calls[1] == 32);                                /* 해시가 같은(=진짜 그 키) 노드만 strcmp */
    CHECK(calls[0] > 3 * calls[1]);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
