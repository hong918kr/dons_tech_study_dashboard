/* DS-B01 고정 용량 벡터: malloc 없는 동적 배열
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 <파일>.c && ./a.out
 * ASan: cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined <파일>.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 공통: 타입과 상태 코드 ---------------- */
typedef enum { FV_OK = 0, FV_ERR_FULL, FV_ERR_EMPTY, FV_ERR_RANGE } fv_status;

typedef struct {
    uint32_t *data;   /* 정적 배열을 가리킨다 (malloc 아님)   */
    size_t    len;    /* 지금 들어 있는 원소 수 (0..cap)      */
    size_t    cap;    /* 컴파일 타임에 정해진 용량             */
} fixed_vec;

/* 저장소 배열 + 핸들을 한 번에 정의한다. 용량은 컴파일 타임 상수. */
#define FV_DEFINE(name, CAP)                                         \
    _Static_assert((CAP) > 0, "fixed_vec capacity must be > 0");     \
    static uint32_t name##_storage[(CAP)];                           \
    static fixed_vec name = { name##_storage, 0, (CAP) }

static size_t fv_len(const fixed_vec *v)  { return v->len; }
static bool   fv_full(const fixed_vec *v) { return v->len == v->cap; }
static void   fv_clear(fixed_vec *v)      { v->len = 0; }

/* ---------------- Part A: 끝에서 넣고 빼기 + 경계 검사 ---------------- */
static fv_status fv_push(fixed_vec *v, uint32_t x) {
    (void)v; (void)x;
    return FV_OK; /* TODO: 가득 차면 FV_ERR_FULL, 아니면 맨 뒤(data[len])에 넣고 len++ */
}
static fv_status fv_pop(fixed_vec *v, uint32_t *out) {
    (void)v; (void)out;
    return FV_ERR_EMPTY; /* TODO: 비었으면 FV_ERR_EMPTY, 아니면 맨 뒤 값을 꺼낸다 */
}
static fv_status fv_get(const fixed_vec *v, size_t i, uint32_t *out) {
    (void)v; (void)i; (void)out;
    return FV_ERR_RANGE; /* TODO: 유효 인덱스는 0..len-1 (cap 이 아니다!) */
}
static fv_status fv_set(fixed_vec *v, size_t i, uint32_t x) {
    (void)v; (void)i; (void)x;
    return FV_ERR_RANGE; /* TODO */
}

/* ---------------- Part B: 중간 삽입/삭제 (원소 이동) ---------------- */
/* i 는 0..len (len 이면 맨 뒤). 검사 순서: RANGE → FULL */
static fv_status fv_insert(fixed_vec *v, size_t i, uint32_t x) {
    (void)v; (void)i; (void)x;
    return FV_ERR_RANGE; /* TODO: 검사 → data[i..len) 를 한 칸 뒤로 → data[i] = x → len++ */
}
/* i 는 0..len-1. out 이 NULL 이면 값은 버린다 */
static fv_status fv_erase(fixed_vec *v, size_t i, uint32_t *out) {
    (void)v; (void)i; (void)out;
    return FV_ERR_RANGE; /* TODO: 검사 → 값 저장 → data[i+1..len) 를 한 칸 앞으로 → len-- */
}

/* ---------------- Part C: 불변식 + 가득 참 정책 ---------------- */
/* 모든 연산 전후에 참이어야 하는 조건 */
static bool fv_invariant(const fixed_vec *v) {
    (void)v;
    return false; /* TODO: v, data 가 NULL 아님 + cap > 0 + len <= cap */
}
/* "최근 N개 로그" 정책: 가득 차면 가장 오래된(맨 앞) 것을 버리고 넣는다. 버렸으면 true */
static bool fv_push_drop_oldest(fixed_vec *v, uint32_t x) {
    (void)v; (void)x; (void)fv_full;
    return false; /* TODO: 가득 차면 맨 앞을 지우고(true) 맨 뒤에 넣는다 */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

FV_DEFINE(v4, 4);       /* 경계 테스트용: 용량 4 */
FV_DEFINE(log3, 3);     /* drop-oldest 테스트용 */
#define STRESS_CAP 16
FV_DEFINE(vs, STRESS_CAP);

/* 내용 비교 도우미: v 가 정확히 exp[0..n) 인가 */
static bool fv_equals(const fixed_vec *v, const uint32_t *exp, size_t n) {
    if (v->len != n) return false;
    for (size_t i = 0; i < n; i++)
        if (v->data[i] != exp[i]) return false;
    return true;
}

/* 캐너리(canary) 로 둘러싼 저장소: 범위 밖 쓰기가 옆 필드를 덮으면 탐지 */
#define CANARY 0xDEADBEEFu
static struct { uint32_t pre; uint32_t buf[4]; uint32_t post; } guarded = { CANARY, {0}, CANARY };
static fixed_vec gv = { guarded.buf, 0, 4 };

/* 스트레스 테스트용 참조 모델: 임시 배열에 다시 쌓는 단순한(느리지만 명백한) 구현 */
static uint32_t mdl[STRESS_CAP];
static size_t   mlen;
static unsigned long g_moves;   /* insert/erase 가 옮긴 원소 수 합계 */
static fv_status model_insert(size_t i, uint32_t x) {
    if (i > mlen) return FV_ERR_RANGE;
    if (mlen == STRESS_CAP) return FV_ERR_FULL;
    uint32_t tmp[STRESS_CAP]; size_t k = 0;
    for (size_t j = 0; j < i; j++) tmp[k++] = mdl[j];
    tmp[k++] = x;
    for (size_t j = i; j < mlen; j++) tmp[k++] = mdl[j];
    g_moves += mlen - i;
    memcpy(mdl, tmp, k * sizeof tmp[0]); mlen = k;
    return FV_OK;
}
static fv_status model_erase(size_t i) {
    if (i >= mlen) return FV_ERR_RANGE;
    uint32_t tmp[STRESS_CAP]; size_t k = 0;
    for (size_t j = 0; j < mlen; j++) if (j != i) tmp[k++] = mdl[j];
    g_moves += mlen - i - 1;
    memcpy(mdl, tmp, k * sizeof tmp[0]); mlen = k;
    return FV_OK;
}
static uint32_t rng_state = 0x12345678u;      /* 고정 시드 → 재현 가능 */
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

int main(void) {
    uint32_t out = 0;

    /* Part A: push/pop/get/set 과 경계 */
    CHECK(fv_len(&v4) == 0 && v4.cap == 4);
    CHECK(fv_pop(&v4, &out) == FV_ERR_EMPTY);                 /* 빈 벡터 pop */
    CHECK(fv_push(&v4, 10) == FV_OK && fv_push(&v4, 20) == FV_OK);
    CHECK(fv_push(&v4, 30) == FV_OK && fv_push(&v4, 40) == FV_OK);
    CHECK(fv_push(&v4, 50) == FV_ERR_FULL && fv_len(&v4) == 4); /* 가득 참: 상태 그대로 */
    CHECK(fv_get(&v4, 3, &out) == FV_OK && out == 40);
    CHECK(fv_get(&v4, 4, &out) == FV_ERR_RANGE);               /* i == len 은 밖 */
    CHECK(fv_get(&v4, (size_t)-1, &out) == FV_ERR_RANGE);      /* 음수 인덱스 */
    CHECK(fv_set(&v4, 0, 11) == FV_OK && v4.data[0] == 11);
    CHECK(fv_pop(&v4, &out) == FV_OK && out == 40 && fv_len(&v4) == 3);

    /* Part B: insert/erase 와 원소 이동 — 현재 [11,20,30] */
    CHECK(fv_insert(&v4, 0, 5) == FV_OK && fv_equals(&v4, (const uint32_t[]){5, 11, 20, 30}, 4));
    CHECK(fv_insert(&v4, 2, 99) == FV_ERR_FULL && fv_equals(&v4, (const uint32_t[]){5, 11, 20, 30}, 4));
    CHECK(fv_insert(&v4, 9, 99) == FV_ERR_RANGE);              /* RANGE 가 FULL 보다 먼저 */
    CHECK(fv_erase(&v4, 1, &out) == FV_OK && out == 11 && fv_equals(&v4, (const uint32_t[]){5, 20, 30}, 3));
    CHECK(fv_erase(&v4, 3, &out) == FV_ERR_RANGE);             /* i == len 은 삭제 불가 */
    CHECK(fv_insert(&v4, 3, 7) == FV_OK && fv_equals(&v4, (const uint32_t[]){5, 20, 30, 7}, 4)); /* i == len → 맨 뒤 */
    CHECK(fv_erase(&v4, 3, NULL) == FV_OK && fv_erase(&v4, 0, NULL) == FV_OK
          && fv_equals(&v4, (const uint32_t[]){20, 30}, 2));
    fv_clear(&v4);
    CHECK(fv_erase(&v4, 0, &out) == FV_ERR_RANGE && fv_insert(&v4, 0, 1) == FV_OK && fv_len(&v4) == 1);

    /* Part C-1: 불변식과 drop-oldest 정책 */
    CHECK(fv_invariant(&v4));
    fixed_vec broken = { v4.data, 5, 4 };                     /* len > cap 은 깨진 상태 */
    CHECK(!fv_invariant(&broken));
    for (uint32_t k = 1; k <= 3; k++) (void)fv_push_drop_oldest(&log3, k);
    CHECK(fv_push_drop_oldest(&log3, 4) == true && fv_equals(&log3, (const uint32_t[]){2, 3, 4}, 3));
    CHECK(fv_invariant(&log3));

    /* Part C-2: 캐너리 — 가득 찬 상태에서 넘치는 시도를 퍼부어도 옆 메모리는 그대로 */
    for (int k = 0; k < 10; k++) { (void)fv_push(&gv, 0xAAAAAAAAu); (void)fv_insert(&gv, 0, 0xBBBBBBBBu); }
    (void)fv_set(&gv, 4, 0xCCCCCCCCu); (void)fv_insert(&gv, 5, 0xCCCCCCCCu);
    CHECK(guarded.pre == CANARY && guarded.post == CANARY && fv_len(&gv) == 4);

    /* Part C-3: 무작위 연산 20000 번을 참조 모델과 비교 */
    enum { OP_PUSH, OP_POP, OP_INSERT, OP_ERASE, OP_SET, OP_N };
    static const char *op_name[OP_N] = { "push", "pop", "insert", "erase", "set" };
    unsigned long stat[OP_N][4] = {{0}};
    unsigned long mismatch = 0, inv_fail = 0, moved_ops = 0;
    for (int step = 0; step < 20000; step++) {
        uint32_t r = xorshift32(), x = xorshift32();
        int op = (int)(r % 100u);
        op = op < 30 ? OP_PUSH : op < 45 ? OP_POP : op < 70 ? OP_INSERT : op < 90 ? OP_ERASE : OP_SET;
        size_t i = (size_t)((r >> 8) % (mlen + 2u));           /* 0..len+1 : 가끔 범위 밖 */
        fv_status got = FV_OK, want = FV_OK;
        switch (op) {
        case OP_PUSH:   got = fv_push(&vs, x);   want = model_insert(mlen, x); if (want == FV_ERR_RANGE) want = FV_ERR_FULL; break;
        case OP_POP:    got = fv_pop(&vs, &out); want = mlen ? model_erase(mlen - 1) : FV_ERR_EMPTY; break;
        case OP_INSERT: got = fv_insert(&vs, i, x); want = model_insert(i, x); moved_ops += (want == FV_OK); break;
        case OP_ERASE:  got = fv_erase(&vs, i, NULL); want = model_erase(i); moved_ops += (want == FV_OK); break;
        default:        got = fv_set(&vs, i, x); want = (i < mlen) ? FV_OK : FV_ERR_RANGE; if (want == FV_OK) mdl[i] = x; break;
        }
        stat[op][want]++;
        if (got != want || !fv_equals(&vs, mdl, mlen)) mismatch++;
        if (!fv_invariant(&vs)) inv_fail++;
    }
    printf("stress: 20000 ops, cap=%d, seed=0x12345678\n", STRESS_CAP);
    printf("  %-7s %6s %6s %6s %6s\n", "op", "OK", "FULL", "EMPTY", "RANGE");
    for (int k = 0; k < OP_N; k++)
        printf("  %-7s %6lu %6lu %6lu %6lu\n", op_name[k], stat[k][0], stat[k][1], stat[k][2], stat[k][3]);
    printf("  avg moved per insert/erase call = %.2f\n", moved_ops ? (double)g_moves / (double)moved_ops : 0.0);
    CHECK(mismatch == 0);
    CHECK(inv_fail == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
