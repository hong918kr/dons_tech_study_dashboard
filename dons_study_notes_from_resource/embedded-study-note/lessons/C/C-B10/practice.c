/* C-B10 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* 연습 1: static 배열로 메모이제이션 — 재귀 호출 전체가 같은 캐시를 공유 */
static unsigned g_fib_work;                 /* 실제로 계산한 항의 수 */
static uint64_t fib(unsigned n) {           /* n <= 93 (u64 범위) */
    static uint64_t memo[94];               /* 0 = 아직 모름 (fib(0)만 예외) */
    if (n < 2) return n;
    if (memo[n]) return memo[n];
    g_fib_work++;
    memo[n] = fib(n - 1) + fib(n - 2);
    return memo[n];
}

/* 연습 2: 함수 내 static = 싱글톤. 버튼이 둘이면 상태가 섞인다 */
#define DEB_N 3u                            /* 같은 값이 3번 연속이면 확정 */
static bool debounce1(bool raw) {           /* 버튼 1개용 (나쁜 예로 사용) */
    static bool stable, last;
    static unsigned cnt;
    if (raw == last) { if (cnt < DEB_N) cnt++; } else { last = raw; cnt = 1; }
    if (cnt >= DEB_N) stable = raw;
    return stable;
}
typedef struct { bool stable, last; unsigned cnt; } deb_t;   /* 인스턴스별 상태 */
static bool debounce(deb_t *d, bool raw) {
    if (raw == d->last) { if (d->cnt < DEB_N) d->cnt++; } else { d->last = raw; d->cnt = 1; }
    if (d->cnt >= DEB_N) d->stable = raw;
    return d->stable;
}

/* 연습 3: WARN_ONCE — 매크로가 펼쳐지는 자리마다 자기만의 static 플래그 */
static unsigned g_warn_emitted;
#define WARN_ONCE(cond) do {                         \
        static bool warned_;                         \
        if ((cond) && !warned_) { warned_ = true; g_warn_emitted++; } \
    } while (0)

/* 연습 4: malloc 없는 정적 풀 할당기 */
typedef struct { uint32_t period_ms; uint32_t remain_ms; } soft_timer;
#define POOL_N 4
static soft_timer s_pool[POOL_N];
static bool       s_used[POOL_N];
static soft_timer *timer_alloc(void) {
    for (int i = 0; i < POOL_N; i++)
        if (!s_used[i]) { s_used[i] = true; return &s_pool[i]; }
    return NULL;                            /* 풀 고갈 */
}
static void timer_free(soft_timer *t) {
    ptrdiff_t i = t - s_pool;
    if (i >= 0 && i < POOL_N) s_used[i] = false;
}

/* 연습 5: 블록 스코프 extern 은 이미 보이는 static 을 가리킨다 (C11 6.2.2p4) */
static int q = 5;                           /* 내부 연결 */
static int read_q(void) {
    extern int q;                           /* 새 외부 객체가 아니라 위의 static q (내부 연결) */
    return q;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 1 */
    CHECK(fib(50) == 12586269025ull);
    CHECK(g_fib_work == 49);                /* fib(2)..fib(50) 각 1번씩 */
    CHECK(fib(50) == 12586269025ull && g_fib_work == 49);   /* 캐시 적중: 추가 계산 0 */
    CHECK(fib(93) == 12200160415121876738ull && g_fib_work == 92);

    /* 2: 버튼 A 는 계속 눌림(1), 버튼 B 는 계속 떼짐(0). 번갈아 샘플링 */
    deb_t a = {0}, b = {0};
    bool sa = false, sb = true, s1a = false;
    for (int i = 0; i < 6; i++) {
        s1a = debounce1(true);              /* A */
        (void)debounce1(false);             /* B 가 같은 static 을 덮어씀 */
        sa = debounce(&a, true);
        sb = debounce(&b, false);
    }
    CHECK(s1a == false);                    /* 싱글톤: cnt 가 계속 1로 리셋 → A 확정 실패 */
    CHECK(sa == true && sb == false);       /* 인스턴스별 상태: 정상 */

    /* 3 */
    for (int i = 0; i < 5; i++) {
        WARN_ONCE(i >= 1);                  /* 자리 1: i==1 에서 한 번 */
        WARN_ONCE(true);                    /* 자리 2: i==0 에서 한 번 */
    }
    CHECK(g_warn_emitted == 2);

    /* 4 */
    soft_timer *t[POOL_N];
    for (int i = 0; i < POOL_N; i++) t[i] = timer_alloc();
    CHECK(t[0] && t[1] && t[2] && t[3] && t[0] != t[3]);
    CHECK(timer_alloc() == NULL);
    timer_free(t[2]);
    CHECK(timer_alloc() == t[2]);           /* 반납한 칸을 다시 받는다 */

    /* 5 */
    CHECK(read_q() == 5);
    q = 42;
    CHECK(read_q() == 42);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
