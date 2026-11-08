/* SIV-B05 추가 연습 — 한 번에 검증
 *
 * 연습 1  walking-1 로 비트 매핑 복원 (write 경로 bit/byte-lane swap) + 대칭 swap 은 readback 으로 안 보인다
 * 연습 2  Barr 스타일 data bus test: 첫 실패 패턴을 돌려준다
 * 연습 3  패턴 세트별 커버리지: 32비트의 모든 쌍 short(496쌍 x OR/AND) 와 stuck-at 중 몇 개를 놓치나
 * 연습 4  "쓴 값의 반대가 읽힘" 증상 구분: write 경로 반전(INVERT_W) vs 끊긴 선(OPEN)
 *
 * 본문 solution.c 와 독립된 작은 mock: off 0x0 = SCRATCH(RW), 0x4 = ID(RO), 0x8 = DECOY(RW).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ID_VALUE 0xD3A00105u

typedef struct {
    uint32_t sa0, sa1, open, inv_w;   /* inv_w: write 경로에서만 반전된 비트 (RTL 통합 실수) */
    int      br_kind, br_a, br_b;     /* 0 = 없음, 1 = OR, 2 = AND                           */
    bool     perm_on;
    int      perm_w[32], perm_r[32];  /* CPU 비트 i -> 레지스터 비트 perm_w[i] / 레지스터 i -> CPU perm_r[i] */
    uint32_t scratch, decoy, hold;
} pbus_t;

static uint32_t lines(const pbus_t *s, uint32_t v) {
    if (s->br_kind) {
        uint32_t a = (v >> s->br_a) & 1u, b = (v >> s->br_b) & 1u, r = s->br_kind == 1 ? (a | b) : (a & b);
        v = (v & ~((1u << s->br_a) | (1u << s->br_b))) | (r << s->br_a) | (r << s->br_b);
    }
    return (v & ~s->sa0) | s->sa1;
}
static uint32_t permute(const int *perm, uint32_t v) {
    uint32_t o = 0;
    for (int i = 0; i < 32; i++) if (v & (1u << i)) o |= 1u << perm[i];
    return o;
}
static void pw(pbus_t *s, uint32_t off, uint32_t v) {
    uint32_t w = lines(s, v);
    s->hold = w;
    if (s->perm_on) w = permute(s->perm_w, w);
    w ^= s->inv_w;
    if (off == 0x0) s->scratch = (s->scratch & s->open) | (w & ~s->open);
    if (off == 0x8) s->decoy   = (s->decoy & s->open) | (w & ~s->open);
}
static uint32_t pr(pbus_t *s, uint32_t off) {
    uint32_t v = off == 0x0 ? s->scratch : off == 0x4 ? ID_VALUE : off == 0x8 ? s->decoy : 0xDEADBEEFu;
    if (s->perm_on) v = permute(s->perm_r, v);
    v = lines(s, v);
    v = (v & ~s->open) | (s->hold & s->open);
    s->hold = v;
    return v;
}
static void pbus_init(pbus_t *s) {
    memset(s, 0, sizeof *s);
    for (int i = 0; i < 32; i++) s->perm_w[i] = s->perm_r[i] = i;
}
static int popc(uint32_t x) { int c = 0; while (x) { x &= x - 1u; c++; } return c; }
static int lowbit(uint32_t x) { int i = 0; if (!x) return -1; while (!(x & 1u)) { x >>= 1; i++; } return i; }

/* ---- 연습 1: walking-1 로 "CPU 비트 i 가 어디로 갔나" 복원. 반환 = 제자리가 아닌 비트 수 ---- */
static int recover_map(pbus_t *s, int map[32]) {
    int moved = 0;
    for (int i = 0; i < 32; i++) {
        pw(s, 0x0, 1u << i);
        uint32_t r = pr(s, 0x0);
        map[i] = (popc(r) == 1) ? lowbit(r) : -1;   /* 0개/2개 이상이면 stuck/short — 매핑 아님 */
        if (map[i] != i) moved++;
    }
    return moved;
}

/* ---- 연습 2: Barr 스타일 data bus test (한 주소, walking 1). 0 = PASS, 아니면 첫 실패 패턴 ---- */
static uint32_t data_bus_test(pbus_t *s) {
    for (uint32_t p = 1u; p != 0; p <<= 1) {
        pw(s, 0x0, p);
        if (pr(s, 0x0) != p) return p;
    }
    return 0;
}

/* ---- 연습 3: 패턴 세트 커버리지 ---- */
static bool set_fails(pbus_t *s, const uint32_t *p, int n) {
    for (int i = 0; i < n; i++) { pw(s, 0x0, p[i]); if (pr(s, 0x0) != p[i]) return true; }
    return false;
}
static int pair_short_misses(const uint32_t *p, int n) {     /* 496 쌍 x {OR, AND} = 992 결함 중 놓친 수 */
    int miss = 0;
    for (int kind = 1; kind <= 2; kind++)
        for (int a = 0; a < 32; a++)
            for (int b = a + 1; b < 32; b++) {
                pbus_t s; pbus_init(&s); s.br_kind = kind; s.br_a = a; s.br_b = b;
                if (!set_fails(&s, p, n)) miss++;
            }
    return miss;
}
static int stuck_misses(const uint32_t *p, int n) {           /* 32 x {SA0, SA1} = 64 결함 중 놓친 수 */
    int miss = 0;
    for (int i = 0; i < 32; i++) {
        pbus_t s; pbus_init(&s); s.sa0 = 1u << i; if (!set_fails(&s, p, n)) miss++;
        pbus_init(&s); s.sa1 = 1u << i; if (!set_fails(&s, p, n)) miss++;
    }
    return miss;
}
/* counting sequence: 패턴 j 의 비트 i = (i >> j) & 1. with_compl 이면 보수도 추가. 반환 = 패턴 수 */
static int counting_patterns(uint32_t *out, bool with_compl) {
    int n = 0;
    for (int j = 0; j < 5; j++) {
        uint32_t p = 0;
        for (int i = 0; i < 32; i++) if ((i >> j) & 1) p |= 1u << i;
        out[n++] = p;
        if (with_compl) out[n++] = ~p;
    }
    return n;
}

/* ---- 연습 4: 쓴 값의 반대가 읽히는 비트 — 원인 구분 ---- */
static int walk1_nfail(pbus_t *s, bool shake) {
    int nfail = 0;
    for (int i = 0; i < 32; i++) {
        uint32_t p = 1u << i;
        pw(s, 0x0, p);
        if (shake) pw(s, 0x8, ~p);
        if (pr(s, 0x0) != p) nfail++;
    }
    return nfail;
}
static const char *complement_cause(pbus_t *s) {
    if (walk1_nfail(s, false)) return "INVERT_W";   /* 흔들지 않아도 틀림 = flop 에 반대 값이 저장됨 */
    if (walk1_nfail(s, true))  return "OPEN";       /* 흔들어야 틀림   = 버스에 남은 값을 읽고 있었음 */
    return "PASS";
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    pbus_t s;
    int map[32];

    /* 연습 1 */
    pbus_init(&s);
    CHECK(recover_map(&s, map) == 0);
    pbus_init(&s); s.perm_on = true;                     /* write 경로 byte-lane 역순 (RTL 연결 실수) */
    for (int i = 0; i < 32; i++) s.perm_w[i] = (3 - i / 8) * 8 + i % 8;
    CHECK(recover_map(&s, map) == 32 && map[0] == 24 && map[31] == 7 && map[12] == 20);
    pbus_init(&s); s.perm_on = true; s.perm_w[7] = 8; s.perm_w[8] = 7;   /* write 경로 D7<->D8 */
    CHECK(recover_map(&s, map) == 2 && map[7] == 8 && map[8] == 7);
    pbus_init(&s); s.perm_on = true;                     /* 양방향 대칭 swap: readback 으로는 안 보인다 */
    s.perm_w[7] = s.perm_r[7] = 8; s.perm_w[8] = s.perm_r[8] = 7;
    CHECK(recover_map(&s, map) == 0);
    CHECK(pr(&s, 0x4) == 0xD3A00085u);                   /* 값이 알려진 RO ID 레지스터가 잡는다 */
    pbus_init(&s); s.sa0 = 1u << 5;
    recover_map(&s, map);
    CHECK(map[5] == -1 && map[4] == 4);                  /* stuck 은 매핑이 아니라 -1 */

    /* 연습 2 */
    pbus_init(&s);                CHECK(data_bus_test(&s) == 0);
    pbus_init(&s); s.sa0 = 1u << 7; CHECK(data_bus_test(&s) == 0x80u);
    pbus_init(&s); s.sa1 = 1u << 3; CHECK(data_bus_test(&s) == 0x1u);   /* 첫 패턴부터 bit3 이 1 */
    pbus_init(&s); s.br_kind = 1; s.br_a = 7; s.br_b = 8; CHECK(data_bus_test(&s) == 0x80u);
    pbus_init(&s); s.br_kind = 2; s.br_a = 7; s.br_b = 8; CHECK(data_bus_test(&s) == 0x80u);

    /* 연습 3 */
    static const uint32_t zo[2] = { 0x00000000u, 0xFFFFFFFFu }, ck[2] = { 0x55555555u, 0xAAAAAAAAu };
    uint32_t w1[32], cnt[10], cntc[10];
    for (int i = 0; i < 32; i++) w1[i] = 1u << i;
    int ncnt = counting_patterns(cnt, false), ncntc = counting_patterns(cntc, true);
    CHECK(ncnt == 5 && ncntc == 10 && cnt[0] == 0xAAAAAAAAu && cnt[4] == 0xFFFF0000u);
    int m_zo = pair_short_misses(zo, 2), m_ck = pair_short_misses(ck, 2);
    int m_cnt = pair_short_misses(cnt, ncnt), m_cntc = pair_short_misses(cntc, ncntc), m_w1 = pair_short_misses(w1, 32);
    int s_zo = stuck_misses(zo, 2), s_ck = stuck_misses(ck, 2);
    int s_cnt = stuck_misses(cnt, ncnt), s_cntc = stuck_misses(cntc, ncntc), s_w1 = stuck_misses(w1, 32);
    printf("pattern set      #pat  short-miss/992  stuck-miss/64\n");
    printf("0/~0             %4d  %14d  %13d\n", 2, m_zo, s_zo);
    printf("55/AA            %4d  %14d  %13d\n", 2, m_ck, s_ck);
    printf("counting         %4d  %14d  %13d\n", ncnt, m_cnt, s_cnt);
    printf("counting+compl   %4d  %14d  %13d\n", ncntc, m_cntc, s_cntc);
    printf("walk1            %4d  %14d  %13d\n", 32, m_w1, s_w1);
    CHECK(m_zo == 992 && s_zo == 0);
    CHECK(m_ck == 480 && s_ck == 0);                     /* 같은 홀짝 쌍 240 x 2 */
    CHECK(m_cnt == 0 && s_cnt == 2);                     /* bit0 은 늘 0 (SA0 놓침), bit31 은 늘 1 (SA1 놓침) */
    CHECK(m_cntc == 0 && s_cntc == 0);
    CHECK(m_w1 == 0 && s_w1 == 0);

    /* 연습 4 */
    pbus_init(&s);                  CHECK(strcmp(complement_cause(&s), "PASS") == 0);
    pbus_init(&s); s.inv_w = 1u << 4; CHECK(strcmp(complement_cause(&s), "INVERT_W") == 0);
    pbus_init(&s); s.open = 1u << 4;  CHECK(strcmp(complement_cause(&s), "OPEN") == 0);
    pbus_init(&s); s.inv_w = 1u << 4;
    CHECK(walk1_nfail(&s, false) == 32);                 /* 반전 비트는 모든 단계에서 틀린다 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
