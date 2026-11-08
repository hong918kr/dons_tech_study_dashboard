/* DS-B01 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum { FV_OK = 0, FV_ERR_FULL, FV_ERR_EMPTY, FV_ERR_RANGE } fv_status;
typedef struct { uint32_t *data; size_t len; size_t cap; } fixed_vec;
#define FV_DEFINE(name, CAP)                                         \
    _Static_assert((CAP) > 0, "fixed_vec capacity must be > 0");     \
    static uint32_t name##_storage[(CAP)];                           \
    static fixed_vec name = { name##_storage, 0, (CAP) }

static fv_status fv_push(fixed_vec *v, uint32_t x) {
    if (v->len == v->cap) return FV_ERR_FULL;
    v->data[v->len++] = x;
    return FV_OK;
}
static fv_status fv_insert(fixed_vec *v, size_t i, uint32_t x) {
    if (i > v->len) return FV_ERR_RANGE;
    if (v->len == v->cap) return FV_ERR_FULL;
    memmove(&v->data[i + 1], &v->data[i], (v->len - i) * sizeof v->data[0]);
    v->data[i] = x;
    v->len++;
    return FV_OK;
}

/* 연습 1: swap-remove — 순서를 포기하고 O(1) 삭제 (마지막 원소를 구멍으로 옮긴다) */
static fv_status fv_swap_remove(fixed_vec *v, size_t i, uint32_t *out) {
    if (i >= v->len) return FV_ERR_RANGE;
    if (out) *out = v->data[i];
    v->data[i] = v->data[--v->len];     /* i 가 마지막이면 자기 자신에게 복사 — 무해 */
    return FV_OK;
}

/* 연습 2: remove_if — 조건에 맞는 원소를 모두 지우고 순서 유지, O(n) 한 번 훑기 (두 포인터 압축) */
static size_t fv_remove_if(fixed_vec *v, bool (*pred)(uint32_t)) {
    size_t w = 0;                                   /* w: 다음에 쓸 자리 */
    for (size_t r = 0; r < v->len; r++)             /* r: 지금 읽는 자리 */
        if (!pred(v->data[r])) v->data[w++] = v->data[r];
    size_t removed = v->len - w;
    v->len = w;
    return removed;
}
static bool is_even(uint32_t x) { return (x & 1u) == 0; }

/* 연습 3: 정렬 유지 삽입 — lower_bound(이진 탐색)로 자리 찾고 fv_insert */
static size_t lower_bound(const fixed_vec *v, uint32_t x) {
    size_t lo = 0, hi = v->len;                     /* 답은 [lo, hi] 안에 있다 */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;            /* (lo+hi)/2 의 오버플로 습관적으로 회피 */
        if (v->data[mid] < x) lo = mid + 1; else hi = mid;
    }
    return lo;
}
static fv_status fv_sorted_insert(fixed_vec *v, uint32_t x) {
    return fv_insert(v, lower_bound(v, x), x);
}

/* 연습 4: append_n — 전부 들어가거나 하나도 안 들어간다 (all-or-nothing) */
static fv_status fv_append_n(fixed_vec *v, const uint32_t *src, size_t n) {
    if (n > v->cap - v->len) return FV_ERR_FULL;    /* len + n > cap 은 덧셈 넘침 위험 → 뺄셈으로 */
    memcpy(&v->data[v->len], src, n * sizeof src[0]);
    v->len += n;
    return FV_OK;
}

static bool fv_equals(const fixed_vec *v, const uint32_t *exp, size_t n) {
    if (v->len != n) return false;
    for (size_t i = 0; i < n; i++)
        if (v->data[i] != exp[i]) return false;
    return true;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

FV_DEFINE(a, 8);
FV_DEFINE(b, 8);
FV_DEFINE(s, 6);
FV_DEFINE(c, 5);

int main(void) {
    uint32_t out = 0;

    /* 연습 1 */
    for (uint32_t k = 10; k <= 50; k += 10) (void)fv_push(&a, k);           /* [10,20,30,40,50] */
    CHECK(fv_swap_remove(&a, 1, &out) == FV_OK && out == 20 && fv_equals(&a, (const uint32_t[]){10, 50, 30, 40}, 4));
    CHECK(fv_swap_remove(&a, 3, &out) == FV_OK && out == 40 && fv_equals(&a, (const uint32_t[]){10, 50, 30}, 3));
    CHECK(fv_swap_remove(&a, 3, &out) == FV_ERR_RANGE);

    /* 연습 2 */
    for (uint32_t k = 1; k <= 8; k++) (void)fv_push(&b, k);                 /* [1..8] */
    CHECK(fv_remove_if(&b, is_even) == 4 && fv_equals(&b, (const uint32_t[]){1, 3, 5, 7}, 4));
    CHECK(fv_remove_if(&b, is_even) == 0 && b.len == 4);

    /* 연습 3 */
    const uint32_t in[] = { 50, 10, 40, 10, 30, 20 };
    for (size_t k = 0; k < 6; k++) (void)fv_sorted_insert(&s, in[k]);
    CHECK(fv_equals(&s, (const uint32_t[]){10, 10, 20, 30, 40, 50}, 6));
    CHECK(fv_sorted_insert(&s, 25) == FV_ERR_FULL);
    CHECK(lower_bound(&s, 35) == 4 && lower_bound(&s, 5) == 0 && lower_bound(&s, 99) == 6);

    /* 연습 4 */
    CHECK(fv_append_n(&c, (const uint32_t[]){1, 2, 3}, 3) == FV_OK);
    CHECK(fv_append_n(&c, (const uint32_t[]){4, 5, 6}, 3) == FV_ERR_FULL && fv_equals(&c, (const uint32_t[]){1, 2, 3}, 3));
    CHECK(fv_append_n(&c, (const uint32_t[]){4, 5}, 2) == FV_OK && c.len == 5);
    CHECK(fv_append_n(&c, in, 0) == FV_OK && fv_append_n(&c, (const uint32_t[]){9}, (size_t)-1) == FV_ERR_FULL);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
