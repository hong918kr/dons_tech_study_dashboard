/* DS-B10 해시 테이블: 분리 체이닝
 * 문자열 키 → uint32_t 값. 버킷 배열 + 정적 노드 풀(free list). malloc 없음.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NBUCKET 8u          /* 2의 거듭제곱 → h & (NBUCKET-1). 일부러 작게 잡아 충돌을 자주 낸다 */
#define POOL_N  32u         /* 노드 최대 개수 = 테이블 용량 */
#define KEY_MAX 16u         /* NUL 포함. 키는 노드 안에 복사해 둔다 (호출자 버퍼에 의존 X) */

typedef struct Node {
    struct Node *next;      /* 같은 버킷 체인의 다음 노드, 또는 free list 의 다음 노드 */
    uint32_t value;
    char key[KEY_MAX];
} Node;

typedef struct {
    Node *bucket[NBUCKET];  /* 각 버킷 = 단일 연결 리스트의 head */
    Node pool[POOL_N];      /* 모든 노드는 여기서만 나온다 */
    Node *free_list;
    unsigned count;
} HashTable;

typedef enum { HT_INSERTED, HT_UPDATED, HT_ERR_FULL, HT_ERR_KEY } HtResult;

static uint32_t fnv1a_str(const char *s) {               /* DS-B09 의 FNV-1a */
    uint32_t h = 2166136261u;
    for (const uint8_t *p = (const uint8_t *)s; *p; p++) { h ^= *p; h *= 16777619u; }
    return h;
}
static unsigned bucket_of(const char *key) { return fnv1a_str(key) & (NBUCKET - 1u); }

/* ---------------- Part A: 정적 노드 풀 ---------------- */
static void ht_init(HashTable *t) {
    memset(t->bucket, 0, sizeof t->bucket);
    t->count = 0;
    t->free_list = NULL;
    /* TODO: pool[] 의 모든 노드를 free_list 에 연결한다 */
}

static Node *pool_alloc(HashTable *t) {
    (void)t;
    return NULL; /* TODO: free_list 의 head 를 떼어 돌려준다 (비었으면 NULL) */
}

static void pool_free(HashTable *t, Node *n) {
    (void)t; (void)n; /* TODO: n 을 free_list 의 head 에 꽂는다 */
}

/* ---------------- Part B: find / put ---------------- */
static bool key_ok(const char *key) { return key && strlen(key) < KEY_MAX; }

static bool ht_get(const HashTable *t, const char *key, uint32_t *out) {
    (void)t; (void)key; (void)out; (void)key_ok;
    return false; /* TODO: 버킷 체인을 따라가며 strcmp */
}

static HtResult ht_put(HashTable *t, const char *key, uint32_t value) {
    (void)t; (void)key; (void)value; (void)pool_alloc;
    return HT_ERR_FULL; /* TODO: ① 키 검사 ② 있으면 갱신 ③ 없으면 노드 할당 → 키 복사 → head 삽입 */
}

/* ---------------- Part C: delete (pointer-to-pointer) ---------------- */
static bool ht_del(HashTable *t, const char *key) {
    (void)t; (void)key; (void)pool_free;
    return false; /* TODO: Node **pp 로 체인을 걸으며 찾으면 *pp = n->next; pool_free; count-- */
}

/* ---------------- 검사 도구 (하네스) ---------------- */
static unsigned free_count(const HashTable *t) {
    unsigned c = 0;
    for (const Node *n = t->free_list; n && c <= POOL_N; n = n->next) c++;
    return c;
}
static unsigned chain_len(const HashTable *t, unsigned b) {
    unsigned c = 0;
    for (const Node *n = t->bucket[b]; n && c <= POOL_N; n = n->next) c++;
    return c;
}
/* 불변식: ① 노드는 자기 해시의 버킷에만 ② 키 중복 없음 ③ count = 체인 합 ④ 체인 + free = POOL_N */
static bool ht_check(const HashTable *t) {
    unsigned total = 0;
    for (unsigned b = 0; b < NBUCKET; b++) {
        unsigned len = chain_len(t, b);
        if (len > POOL_N) return false;                 /* 사이클 */
        total += len;
        for (const Node *n = t->bucket[b]; n; n = n->next) {
            if (bucket_of(n->key) != b) return false;
            for (const Node *m = n->next; m; m = m->next)
                if (strcmp(n->key, m->key) == 0) return false;
        }
    }
    return total == t->count && total + free_count(t) == POOL_N;
}

/* 재현 가능한 PRNG (xorshift32) */
static uint32_t g_rng = 0x1234567u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static HashTable T;

int main(void) {
    uint32_t v = 0;
    char key[KEY_MAX];

    /* Part A: 초기화와 풀 */
    ht_init(&T);
    CHECK(T.count == 0 && free_count(&T) == POOL_N);
    CHECK(ht_check(&T));

    /* Part B: 기본 put / get / update */
    CHECK(ht_put(&T, "imu0_x", 100) == HT_INSERTED);
    CHECK(ht_put(&T, "baro1_raw", 200) == HT_INSERTED);
    CHECK(ht_get(&T, "imu0_x", &v) && v == 100);
    CHECK(ht_put(&T, "imu0_x", 111) == HT_UPDATED);          /* 같은 키 → 갱신, 노드 추가 없음 */
    CHECK(ht_get(&T, "imu0_x", &v) && v == 111 && T.count == 2);
    CHECK(!ht_get(&T, "imu0_y", &v));
    CHECK(ht_put(&T, "this_key_is_too_long", 1) == HT_ERR_KEY);  /* 16바이트 이상 거부 */
    CHECK(ht_check(&T));

    /* Part C-1: 같은 버킷 충돌 — 버킷이 같은 키 4개를 찾아 체인 head/중간/tail 삭제 */
    ht_init(&T);
    const char *same[4]; static char same_buf[4][KEY_MAX];
    unsigned found = 0, target = bucket_of("s0");
    for (unsigned i = 0; i < 1000 && found < 4; i++) {
        snprintf(key, sizeof key, "s%u", i);
        if (bucket_of(key) == target) { memcpy(same_buf[found], key, sizeof key); same[found] = same_buf[found]; found++; }
    }
    CHECK(found == 4);
    for (unsigned i = 0; i < 4; i++) ht_put(&T, same[i], 10 + i);   /* head 삽입 → [3]→[2]→[1]→[0] */
    CHECK(chain_len(&T, target) == 4 && T.count == 4);
    CHECK(ht_del(&T, same[2]));                                  /* 중간 */
    CHECK(ht_del(&T, same[3]));                                  /* head */
    CHECK(ht_del(&T, same[0]));                                  /* tail */
    CHECK(ht_get(&T, same[1], &v) && v == 11);                   /* 남은 하나는 그대로 */
    CHECK(!ht_get(&T, same[0], &v) && !ht_get(&T, same[2], &v) && !ht_get(&T, same[3], &v));
    CHECK(chain_len(&T, target) == 1 && ht_check(&T));

    /* Part C-2: 삭제 후 재삽입 */
    CHECK(ht_del(&T, same[1]) && T.count == 0 && free_count(&T) == POOL_N);
    CHECK(!ht_del(&T, same[1]));                                 /* 두 번 삭제 → false */
    CHECK(ht_put(&T, same[1], 77) == HT_INSERTED);               /* UPDATED 가 아니라 INSERTED */
    CHECK(ht_get(&T, same[1], &v) && v == 77 && ht_check(&T));

    /* Part C-3: 풀 고갈 */
    ht_init(&T);
    for (unsigned i = 0; i < POOL_N; i++) { snprintf(key, sizeof key, "cmd%u", i); ht_put(&T, key, i); }
    CHECK(T.count == POOL_N && free_count(&T) == 0);
    CHECK(ht_put(&T, "one_more", 1) == HT_ERR_FULL);
    CHECK(ht_put(&T, "cmd7", 700) == HT_UPDATED);               /* 꽉 차도 갱신은 된다 */
    CHECK(ht_del(&T, "cmd0") && ht_put(&T, "one_more", 1) == HT_INSERTED);
    CHECK(ht_check(&T));

    /* Part C-4: 참조 모델 대조 랜덤 테스트 (키 48종 > 용량 32 → FULL 도 나온다) */
    enum { NKEY = 48, OPS = 20000 };
    static bool ref_has[NKEY]; static uint32_t ref_val[NKEY];
    unsigned ref_count = 0, mismatch = 0, bad_inv = 0, n_full = 0, max_chain = 0;
    ht_init(&T);
    for (unsigned op = 0; op < OPS; op++) {
        uint32_t r = xorshift32();
        unsigned k = (r >> 8) % NKEY, kind = r % 10;
        uint32_t val = r >> 4;
        snprintf(key, sizeof key, "k%02u", k);
        if (kind < 5) {                                          /* put 50% */
            HtResult want = ref_has[k] ? HT_UPDATED : (ref_count == POOL_N ? HT_ERR_FULL : HT_INSERTED);
            HtResult got = ht_put(&T, key, val);
            if (got != want) mismatch++;
            if (want == HT_INSERTED) { ref_has[k] = true; ref_count++; }
            if (want != HT_ERR_FULL) ref_val[k] = val; else n_full++;
        } else if (kind < 8) {                                   /* get 30% */
            bool got = ht_get(&T, key, &v);
            if (got != ref_has[k] || (got && v != ref_val[k])) mismatch++;
        } else {                                                 /* delete 20% */
            if (ht_del(&T, key) != ref_has[k]) mismatch++;
            if (ref_has[k]) { ref_has[k] = false; ref_count--; }
        }
        if (T.count != ref_count) mismatch++;
        if (op % 100 == 0 && !ht_check(&T)) bad_inv++;
        for (unsigned b = 0; b < NBUCKET; b++) if (chain_len(&T, b) > max_chain) max_chain = chain_len(&T, b);
    }
    printf("random: %u ops, mismatch=%u, invariant_fail=%u, FULL=%u, final count=%u, max chain=%u\n",
           (unsigned)OPS, mismatch, bad_inv, n_full, T.count, max_chain);
    CHECK(mismatch == 0);
    CHECK(bad_inv == 0 && ht_check(&T));
    CHECK(n_full > 0);                                           /* 고갈 경로도 실제로 밟았나 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
