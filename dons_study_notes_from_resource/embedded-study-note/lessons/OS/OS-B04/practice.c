/* OS-B04 추가 연습 — 한 번에 검증
 *   연습 1  스레드 인자: 루프 변수 주소(&i) 대신 스레드마다 자기 인자 칸 / intptr_t 로 값 넘기기
 *   연습 2  최종값 분포: 2 스레드 × 3 번, 4 스레드 × 1 번 — 모든 인터리빙 열거
 *   연습 3  비트필드는 "같은 메모리 위치" — 서로 다른 필드를 써도 lost update (C11 3.14)
 *   연습 4  스레드 결과 돌려받기: 힙에 담아 반환 → join 한 쪽이 free
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
        else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------------- 연습 1: 스레드마다 자기 인자 ---------------- */
#define NT1 8
typedef struct { int id; int seen; } slot_t;

static void *id_worker(void *arg) {          /* 인자 칸은 이 스레드 전용 → 레이스 없음 */
    slot_t *s = arg;
    s->seen = s->id;
    return NULL;
}
static void *id_worker_val(void *arg) {      /* 값 자체를 포인터 크기 정수로 실어 보냄 */
    return (void *)((intptr_t)arg * 10);
}
/* 성공하면 true: slots[i].seen == i 이고, 값 전달 방식의 반환값이 i*10. */
static bool spawn_ids(slot_t *slots, intptr_t *vals) {
    pthread_t t[NT1], u[NT1];
    for (int i = 0; i < NT1; i++) {
        slots[i] = (slot_t){ .id = i, .seen = -1 };
        if (pthread_create(&t[i], NULL, id_worker, &slots[i]) != 0) return false;
        if (pthread_create(&u[i], NULL, id_worker_val, (void *)(intptr_t)i) != 0) return false;
    }
    for (int i = 0; i < NT1; i++) {
        void *r = NULL;
        pthread_join(t[i], NULL);
        pthread_join(u[i], &r);
        vals[i] = (intptr_t)r;
    }
    return true;
}

/* ---------------- 연습 2/3: 단계 시뮬레이터 (solution.c Part B 와 같은 모델) ----------------
 * 스레드마다 3 단계: LOAD reg = mem → MOD reg = f(reg) → STORE mem = reg. 반복 k 번. */
#define MAXT 4
typedef enum { MOD_INC, MOD_FIELD_A, MOD_FIELD_B } modkind_t;
typedef struct { int pc; uint32_t reg; int left; modkind_t mod; int byte; } th_t;
typedef struct { uint32_t mem[2]; int nt; th_t th[MAXT]; } m_t;

static uint32_t apply(modkind_t k, uint32_t r) {
    switch (k) {
    case MOD_INC:     return r + 1u;
    case MOD_FIELD_A: return (r & ~0x0Fu) | 0x5u;          /* s.a = 5  (bits 3..0) */
    default:          return (r & ~0xF0u) | (0x9u << 4);   /* s.b = 9  (bits 7..4) */
    }
}
static bool step(m_t *m, int t) {
    th_t *c = &m->th[t];
    if (c->left == 0) return false;
    if (c->pc == 0) { c->reg = m->mem[c->byte]; c->pc = 1; }
    else if (c->pc == 1) { c->reg = apply(c->mod, c->reg); c->pc = 2; }
    else { m->mem[c->byte] = c->reg; c->pc = 0; c->left--; }
    return true;
}
/* 모든 인터리빙: 끝난 상태마다 visit(m, ctx) 호출, 개수 반환 */
typedef void (*visit_fn)(const m_t *, void *);
static uint64_t explore(m_t m, visit_fn visit, void *ctx) {
    uint64_t n = 0;
    bool any = false;
    for (int t = 0; t < m.nt; t++) {
        m_t next = m;
        if (step(&next, t)) { any = true; n += explore(next, visit, ctx); }
    }
    if (!any) { visit(&m, ctx); return 1; }
    return n;
}
static void hist_visit(const m_t *m, void *ctx) { ((uint64_t *)ctx)[m->mem[0]]++; }

/* 연습 2: nt 스레드 × k 번 counter++ 의 최종값 히스토그램 */
static uint64_t counter_hist(int nt, int k, uint64_t *hist) {
    m_t m;
    memset(&m, 0, sizeof m);
    m.nt = nt;
    for (int t = 0; t < nt; t++) m.th[t] = (th_t){ .left = k, .mod = MOD_INC };
    return explore(m, hist_visit, hist);
}

/* 연습 3: 스레드 A 는 a 필드, B 는 b 필드를 쓴다.
 * same_location = true  : struct { unsigned a:4, b:4; } — 두 필드가 한 바이트(같은 메모리 위치)
 *                 false : struct { uint8_t a, b; }      — 서로 다른 바이트(다른 메모리 위치)
 * 둘 중 하나라도 잃은 인터리빙 수를 *lost 에. 반환: 인터리빙 수. */
typedef struct { bool same; uint64_t lost; } bf_ctx_t;
static void bf_visit(const m_t *m, void *ctx) {
    bf_ctx_t *c = ctx;
    bool ok = c->same ? (m->mem[0] == 0x95u)
                      : ((m->mem[0] & 0x0Fu) == 0x5u && (m->mem[1] & 0xF0u) == 0x90u);
    if (!ok) c->lost++;
}
static uint64_t bitfield_race(bool same_location, uint64_t *lost) {
    m_t m;
    memset(&m, 0, sizeof m);
    m.nt = 2;
    m.th[0] = (th_t){ .left = 1, .mod = MOD_FIELD_A, .byte = 0 };
    m.th[1] = (th_t){ .left = 1, .mod = MOD_FIELD_B, .byte = same_location ? 0 : 1 };
    bf_ctx_t c = { .same = same_location, .lost = 0 };
    uint64_t n = explore(m, bf_visit, &c);
    *lost = c.lost;
    return n;
}

/* ---------------- 연습 4: 결과를 힙에 담아 반환 ---------------- */
static void *square_worker(void *arg) {
    uint64_t x = (uint64_t)(uintptr_t)arg;
    uint64_t *res = malloc(sizeof *res);     /* 지역 변수 주소를 반환하면 안 된다: 스택은 사라진다 */
    if (res) *res = x * x;
    return res;
}
static bool squares(int n, uint64_t *out) {
    pthread_t t[16];
    if (n > 16) return false;
    for (int i = 0; i < n; i++)
        if (pthread_create(&t[i], NULL, square_worker, (void *)(uintptr_t)(i + 1000)) != 0) return false;
    bool ok = true;
    for (int i = 0; i < n; i++) {
        void *r = NULL;
        pthread_join(t[i], &r);
        if (!r) { ok = false; continue; }
        out[i] = *(uint64_t *)r;
        free(r);                              /* 받은 쪽이 해제 */
    }
    return ok;
}

int main(void) {
    /* 연습 1 */
    slot_t slots[NT1];
    intptr_t vals[NT1];
    CHECK(spawn_ids(slots, vals));
    bool ids_ok = true;
    for (int i = 0; i < NT1; i++) ids_ok &= (slots[i].seen == i && vals[i] == (intptr_t)i * 10);
    CHECK(ids_ok);

    /* 연습 2 */
    uint64_t h[16] = { 0 };
    uint64_t n23 = counter_hist(2, 3, h);
    CHECK(n23 == 48620u);                                  /* 18!/(9!9!) */
    CHECK(h[0] == 0u && h[1] == 0u && h[2] > 0u && h[6] == 20u);   /* 최소 2, 정답 = 6!/(3!3!) */
    printf("info: 2x3 -> %llu interleavings, final 2:%llu 3:%llu 4:%llu 5:%llu 6:%llu\n",
           (unsigned long long)n23, (unsigned long long)h[2], (unsigned long long)h[3],
           (unsigned long long)h[4], (unsigned long long)h[5], (unsigned long long)h[6]);
    memset(h, 0, sizeof h);
    uint64_t n41 = counter_hist(4, 1, h);
    CHECK(n41 == 369600u && h[4] == 24u && h[0] == 0u);   /* 12!/(3!)^4, 정답 = 4! */
    printf("info: 4x1 -> %llu interleavings, final 1:%llu 2:%llu 3:%llu 4:%llu\n",
           (unsigned long long)n41, (unsigned long long)h[1], (unsigned long long)h[2],
           (unsigned long long)h[3], (unsigned long long)h[4]);

    /* 연습 3 */
    uint64_t lost_same = 0, lost_sep = 0;
    CHECK(bitfield_race(true, &lost_same) == 20u && lost_same == 18u);   /* 직렬 2 가지만 둘 다 남는다 */
    CHECK(bitfield_race(false, &lost_sep) == 20u && lost_sep == 0u);     /* 다른 메모리 위치: 레이스 없음 */

    /* 연습 4 */
    uint64_t sq[8];
    CHECK(squares(8, sq));
    bool sq_ok = true;
    for (int i = 0; i < 8; i++) sq_ok &= (sq[i] == (uint64_t)(i + 1000) * (uint64_t)(i + 1000));
    CHECK(sq_ok);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
