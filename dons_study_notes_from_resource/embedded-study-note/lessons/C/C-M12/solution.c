/* C-M12 정의되지 않은 동작(UB) 카탈로그 — "UB 가 일어날 수 없는" 안전한 버전 만들기
 *
 * demo_ub_catalog.c 는 UB 를 일부러 일으켜 -O0 / -O2 결과가 갈리는 것을 보여준다.
 * 이 파일은 그 반대다: 같은 일을 하되 어떤 입력에서도 UB 가 없는 코드를 쓴다.
 *   Part A  검사하는 산술        (카탈로그 ① signed overflow, ⑤ 시프트, ⑨ INT_MIN/-1, /0)
 *   Part B  검사하는 메모리 접근  (② NULL, ④ 범위 밖, ⑦ dangling)
 *   Part C  명령 패킷 인터프리터  (전부 + ⑥ unsequenced, ⑧ aliasing 을 피한 디코딩)
 * 규칙: 계산·접근 "전에" 검사한다. TODO 안에서는 더 넓은 타입(int64_t)과 __builtin_*_overflow 금지.
 *       (테스트의 기준값 계산은 int64_t 를 쓴다 — 32비트 입력이면 넘칠 수 없다.)
 * strict 빌드와 -fsanitize=address,undefined 에서 모두 깨끗해야 한다.
 */
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- Part A: 검사하는 산술 ---------------- */
/* 공통 계약: 결과가 int32_t 에 들어가면 *out 에 쓰고 true.
 *            아니면(또는 UB 가 될 입력이면) *out 을 건드리지 않고 false. */
static bool add_i32(int32_t a, int32_t b, int32_t *out) {
    if ((b > 0 && a > INT32_MAX - b) || (b < 0 && a < INT32_MIN - b)) return false;
    *out = a + b;
    return true;
}
static bool sub_i32(int32_t a, int32_t b, int32_t *out) {
    if ((b < 0 && a > INT32_MAX + b) || (b > 0 && a < INT32_MIN + b)) return false;
    *out = a - b;
    return true;
}
static bool mul_i32(int32_t a, int32_t b, int32_t *out) {
    if (a > 0) {
        if (b > 0) { if (a > INT32_MAX / b) return false; }      /* +·+ */
        else       { if (b < INT32_MIN / a) return false; }      /* +·(0 이하) */
    } else {
        if (b > 0) { if (a < INT32_MIN / b) return false; }      /* (0 이하)·+ */
        else       { if (a != 0 && b < INT32_MAX / a) return false; } /* (0 이하)·(0 이하) */
    }
    *out = a * b;
    return true;
}
/* C 의 나눗셈은 0 쪽으로 버림(truncation): -7/2 == -3 (C11 6.5.5p6) */
static bool div_i32(int32_t a, int32_t b, int32_t *out) {
    if (b == 0 || (a == INT32_MIN && b == -1)) return false;
    *out = a / b;
    return true;
}
/* x << n 을 "x * 2^n" 으로. x < 0, n >= 32, 결과가 int32_t 범위 밖이면 false (C11 6.5.7p3·p4) */
static bool shl_i32(int32_t x, unsigned n, int32_t *out) {
    if (x < 0 || n >= 32) return false;
    if (x > (INT32_MAX >> n)) return false;
    *out = x << n;
    return true;
}

/* ---------------- Part B: 검사하는 메모리 접근 ---------------- */
/* [off, off+len) 이 [0, cap) 안에 있나? off + len 이 size_t 에서 한 바퀴 돌아도 속지 않아야 한다. */
static bool range_ok(size_t off, size_t len, size_t cap) {
    return off <= cap && len <= cap - off;
}
/* 신뢰할 수 없는 signed 인덱스 → 원소 주소. tab == NULL, idx < 0, idx >= n 이면 NULL. */
static int32_t *slot_at(int32_t *tab, size_t n, int32_t idx) {
    if (tab == NULL || idx < 0) return NULL;
    if ((size_t)idx >= n) return NULL;
    return &tab[idx];
}
/* t[0..n) 에서 v 를 찾아 *pos 에 위치. 없으면 false (*pos 그대로). */
static bool table_find(const int32_t *t, size_t n, int32_t v, size_t *pos) {
    if (t == NULL) return false;
    for (size_t i = 0; i < n; i++) {
        if (t[i] == v) { *pos = i; return true; }
    }
    return false;
}
/* "0xDEADBEEF" 형식(10글자 + NUL)으로 호출자 버퍼에 쓴다 — 지역 배열 주소를 돌려주는 ⑦ 의 수정.
 * cap < 11 이면 아무것도 쓰지 않고 NULL. 성공하면 buf 를 돌려준다. */
static char *fmt_hex32(uint32_t v, char *buf, size_t cap) {
    static const char hex[] = "0123456789ABCDEF";
    if (buf == NULL || cap < 11) return NULL;
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) buf[2 + i] = hex[(v >> (28 - 4 * i)) & 0xFu];
    buf[10] = '\0';
    return buf;
}

/* ---------------- Part C: 명령 패킷 인터프리터 ---------------- */
/* 패킷 = 정확히 6바이트: [op][reg][val: big-endian 32비트, 2의 보수]
 * op: 1 SET  2 ADD  3 MUL  4 SHL(val = 시프트 양)  5 DIV
 * 검사 순서: 길이 → 레지스터 번호 → op → 연산. 에러면 regs 는 하나도 바뀌지 않는다. */
typedef enum {
    ST_OK = 0, ST_ERR_LEN = -1, ST_ERR_REG = -2, ST_ERR_OP = -3, ST_ERR_OVF = -4, ST_ERR_DIV0 = -5
} status_t;
enum { OP_SET = 1, OP_ADD = 2, OP_MUL = 3, OP_SHL = 4, OP_DIV = 5 };
#define NREGS 8
#define PKT_LEN 6

/* 4바이트 big-endian → int32_t. 포인터 캐스트(⑧)·*p++ 두 번(⑥)·p[0] << 24 의 int 넘침(C-B02) 없이,
 * 그리고 uint32_t → int32_t 의 범위 밖 변환(구현 정의, 6.3.1.3p3)도 없이. */
static int32_t i32_from_be(const uint8_t *p) {
    uint32_t u = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
    if (u <= (uint32_t)INT32_MAX) return (int32_t)u;
    return (int32_t)(u - 0x80000000u) + INT32_MIN;
}
static status_t exec_packet(int32_t regs[NREGS], const uint8_t *pkt, size_t n) {
    if (pkt == NULL || n != PKT_LEN) return ST_ERR_LEN;
    uint8_t op = pkt[0], reg = pkt[1];
    if (reg >= NREGS) return ST_ERR_REG;
    int32_t val = i32_from_be(&pkt[2]);
    int32_t r;                                     /* 모든 성공 경로에서 쓰인 뒤에만 읽힌다 */
    bool ok;
    switch (op) {
    case OP_SET: r = val; ok = true;                        break;
    case OP_ADD: ok = add_i32(regs[reg], val, &r);          break;
    case OP_MUL: ok = mul_i32(regs[reg], val, &r);          break;
    case OP_SHL: ok = val >= 0 && shl_i32(regs[reg], (unsigned)val, &r); break;
    case OP_DIV:
        if (val == 0) return ST_ERR_DIV0;
        ok = div_i32(regs[reg], val, &r);
        break;
    default: return ST_ERR_OP;
    }
    if (!ok) return ST_ERR_OVF;
    regs[reg] = r;                                 /* 검사가 다 끝난 뒤 한 번에 커밋 */
    return ST_OK;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x12345678u;               /* xorshift32 — 고정 시드, 매번 같은 수열 */
static uint32_t rnd(void) {
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
    return g_rng;
}
static int32_t rnd_i32(void) {                     /* 경계 근처 값이 자주 나오게 섞는다 */
    uint32_t u = rnd();
    switch (rnd() % 4u) {
    case 0:  return (int32_t)(u % 0x10000u) - 0x8000;                 /* 작은 값 */
    case 1:  return (int32_t)(u % 0x20000u) - 0x10000;                /* 곱셈 경계(±46341) 근처 */
    case 2:  return (u & 1u) ? INT32_MAX - (int32_t)(u % 64u) : INT32_MIN + (int32_t)(u % 64u);
    default: return (int32_t)(u >> 1) - (int32_t)(rnd() >> 1);              /* 거의 전 범위 */
    }
}
/* 기준값: int64_t 로 정확히 계산한 뒤 범위 검사 */
static bool fits(int64_t r) { return r >= INT32_MIN && r <= INT32_MAX; }
static int check_op(int which, int32_t a, int32_t b) {   /* 불일치면 1 */
    int32_t got = 0x5A5A5A5A;
    bool ok;
    int64_t want;
    bool want_ok;
    switch (which) {
    case 0:  ok = add_i32(a, b, &got); want = (int64_t)a + b; want_ok = fits(want); break;
    case 1:  ok = sub_i32(a, b, &got); want = (int64_t)a - b; want_ok = fits(want); break;
    case 2:  ok = mul_i32(a, b, &got); want = (int64_t)a * b; want_ok = fits(want); break;
    default:
        ok = div_i32(a, b, &got);
        want_ok = b != 0 && fits(b ? (int64_t)a / b : 0);
        want = want_ok ? (int64_t)a / b : 0;
        break;
    }
    if (ok != want_ok) return 1;
    if (ok && got != (int32_t)want) return 1;
    if (!ok && got != 0x5A5A5A5A) return 1;        /* 실패하면 *out 을 건드리지 않는다 */
    return 0;
}

int main(void) {
    int32_t r = 0;

    /* ---- Part A: 경계값 쌍 전수 (16 × 16) ---- */
    static const int32_t edge[] = { INT32_MIN, INT32_MIN + 1, -65536, -46341, -46340, -2, -1, 0,
                                    1, 2, 46340, 46341, 65535, 65536, INT32_MAX - 1, INT32_MAX };
    const size_t ne = sizeof edge / sizeof edge[0];
    for (int op = 0; op < 4; op++) {
        int bad = 0;
        for (size_t i = 0; i < ne; i++)
            for (size_t j = 0; j < ne; j++) bad += check_op(op, edge[i], edge[j]);
        CHECK(bad == 0);                           /* op 0 add, 1 sub, 2 mul, 3 div */
    }
    /* ---- Part A: 퍼즈 20000 쌍 × 4 연산 ---- */
    for (int op = 0; op < 4; op++) {
        int bad = 0;
        for (int k = 0; k < 20000; k++) { int32_t a = rnd_i32(), b = rnd_i32(); bad += check_op(op, a, b); }
        CHECK(bad == 0);
    }
    /* ---- Part A: 대표 사례 ---- */
    CHECK(!add_i32(INT32_MAX, 1, &r));
    CHECK(add_i32(INT32_MAX, INT32_MIN, &r) && r == -1);
    CHECK(!sub_i32(0, INT32_MIN, &r));             /* -INT32_MIN 은 표현 불가 */
    CHECK(sub_i32(-1, INT32_MIN, &r) && r == INT32_MAX);
    CHECK(!mul_i32(-1, INT32_MIN, &r) && !mul_i32(INT32_MIN, -1, &r));
    CHECK(mul_i32(INT32_MIN, 1, &r) && r == INT32_MIN);
    CHECK(mul_i32(46340, 46340, &r) && r == 2147395600);
    CHECK(!mul_i32(46341, 46341, &r));
    CHECK(!div_i32(INT32_MIN, -1, &r) && !div_i32(7, 0, &r));
    CHECK(div_i32(-7, 2, &r) && r == -3);
    /* shl: 기준값과 대조 (x 는 -4..2^31-1 일부, n 은 0..40) */
    {
        int bad = 0;
        static const int32_t xs[] = { -4, -1, 0, 1, 3, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x3FFFFFFF, 0x40000000, INT32_MAX };
        for (size_t i = 0; i < sizeof xs / sizeof xs[0]; i++)
            for (unsigned n = 0; n <= 40; n++) {
                int32_t got = 0x5A5A5A5A;
                bool ok = shl_i32(xs[i], n, &got);
                bool want_ok = xs[i] >= 0 && n < 32 && (int64_t)xs[i] * ((int64_t)1 << n) <= INT32_MAX;
                if (ok != want_ok) bad++;
                else if (ok && got != (int32_t)((int64_t)xs[i] * ((int64_t)1 << n))) bad++;
                else if (!ok && got != 0x5A5A5A5A) bad++;
            }
        CHECK(bad == 0);
    }
    CHECK(shl_i32(1, 30, &r) && r == 0x40000000);
    CHECK(!shl_i32(1, 31, &r));                    /* 1 << 31 은 int 에서 UB */
    CHECK(shl_i32(0, 31, &r) && r == 0);
    CHECK(!shl_i32(0, 32, &r) && !shl_i32(-1, 1, &r));
    CHECK(shl_i32(0x7FFF, 16, &r) && r == 0x7FFF0000 && !shl_i32(0x8000, 16, &r));

    /* ---- Part B ---- */
    CHECK(range_ok(0, 0, 0) && range_ok(0, 16, 16) && range_ok(16, 0, 16) && range_ok(15, 1, 16));
    CHECK(!range_ok(17, 0, 16) && !range_ok(8, 9, 16) && !range_ok(16, 1, 16));
    CHECK(!range_ok(SIZE_MAX, 2, 16));             /* off + len 이 1 로 돌아간다 */
    CHECK(!range_ok(2, SIZE_MAX, 16));             /* len 이 거대 */
    int32_t tab[4] = { 10, 20, 30, 40 };
    CHECK(slot_at(tab, 4, 0) == &tab[0] && slot_at(tab, 4, 3) == &tab[3]);
    CHECK(slot_at(tab, 4, 4) == NULL && slot_at(tab, 4, -1) == NULL && slot_at(tab, 4, INT32_MIN) == NULL);
    CHECK(slot_at(NULL, 4, 0) == NULL && slot_at(tab, 0, 0) == NULL);
    size_t pos = 99;
    CHECK(table_find(tab, 4, 40, &pos) && pos == 3);
    pos = 99;
    CHECK(!table_find(tab, 4, 50, &pos) && pos == 99);   /* 범위 밖을 읽지 않고 "없음" */
    CHECK(!table_find(tab, 0, 10, &pos) && !table_find(NULL, 0, 10, &pos));
    char buf[16];
    memset(buf, '#', sizeof buf);
    CHECK(fmt_hex32(0xDEADBEEFu, buf, sizeof buf) == buf && strcmp(buf, "0xDEADBEEF") == 0);
    CHECK(fmt_hex32(0, buf, 11) == buf && strcmp(buf, "0x00000000") == 0);
    memset(buf, '#', sizeof buf);
    CHECK(fmt_hex32(1, buf, 10) == NULL && buf[0] == '#' && buf[9] == '#');   /* 자리 부족 → 손대지 않음 */

    /* ---- Part C: 디코딩 ---- */
    static const uint8_t be[][4] = { {0,0,0,0}, {0x7F,0xFF,0xFF,0xFF}, {0x80,0,0,0}, {0xFF,0xFF,0xFF,0xFF},
                                     {0xFF,0xFF,0xFF,0xFE}, {0x12,0x34,0x56,0x78}, {0xED,0xCB,0xA9,0x88} };
    CHECK(i32_from_be(be[0]) == 0 && i32_from_be(be[1]) == INT32_MAX && i32_from_be(be[2]) == INT32_MIN);
    CHECK(i32_from_be(be[3]) == -1 && i32_from_be(be[4]) == -2);
    CHECK(i32_from_be(be[5]) == 0x12345678 && i32_from_be(be[6]) == -0x12345678);

    /* ---- Part C: 스크립트 — 각 패킷의 기대 상태와 그 뒤 레지스터 값 ---- */
    struct { uint8_t pkt[PKT_LEN]; status_t st; int reg; int32_t after; } script[] = {
        { {OP_SET, 0, 0x00,0x00,0x00,0x64}, ST_OK,       0, 100 },
        { {OP_ADD, 0, 0x7F,0xFF,0xFF,0xFF}, ST_ERR_OVF,  0, 100 },        /* 100 + INT32_MAX */
        { {OP_MUL, 0, 0xFF,0xFF,0xFF,0xFD}, ST_OK,       0, -300 },       /* × -3 */
        { {OP_SET, 1, 0x00,0x00,0x00,0x01}, ST_OK,       1, 1 },
        { {OP_SHL, 1, 0x00,0x00,0x00,0x1E}, ST_OK,       1, 0x40000000 }, /* 1 << 30 */
        { {OP_SHL, 1, 0x00,0x00,0x00,0x01}, ST_ERR_OVF,  1, 0x40000000 }, /* 2^31 은 범위 밖 */
        { {OP_SHL, 1, 0xFF,0xFF,0xFF,0xFF}, ST_ERR_OVF,  1, 0x40000000 }, /* 음수 시프트 양 */
        { {OP_SHL, 1, 0x00,0x00,0x00,0x20}, ST_ERR_OVF,  1, 0x40000000 }, /* 32 */
        { {OP_DIV, 0, 0x00,0x00,0x00,0x00}, ST_ERR_DIV0, 0, -300 },
        { {OP_SET, 2, 0x80,0x00,0x00,0x00}, ST_OK,       2, INT32_MIN },
        { {OP_DIV, 2, 0xFF,0xFF,0xFF,0xFF}, ST_ERR_OVF,  2, INT32_MIN },  /* INT32_MIN / -1 */
        { {OP_MUL, 2, 0x00,0x00,0x00,0x01}, ST_OK,       2, INT32_MIN },
        { {OP_ADD, 2, 0xFF,0xFF,0xFF,0xFF}, ST_ERR_OVF,  2, INT32_MIN },  /* INT32_MIN + -1 */
        { {OP_DIV, 0, 0x00,0x00,0x00,0x07}, ST_OK,       0, -42 },        /* -300 / 7 = -42 (버림) */
        { {OP_SET, 7, 0x00,0x00,0x00,0x05}, ST_OK,       7, 5 },          /* 마지막 레지스터 */
        { {OP_SET, 8, 0x00,0x00,0x00,0x05}, ST_ERR_REG,  7, 5 },          /* 범위 밖 레지스터 */
        { {OP_SET, 0xFF, 0,0,0,5},          ST_ERR_REG,  7, 5 },
        { {0x00, 0, 0,0,0,0},               ST_ERR_OP,   0, -42 },
        { {0x06, 0, 0,0,0,0},               ST_ERR_OP,   0, -42 },
        { {0x06, 9, 0,0,0,0},               ST_ERR_REG,  0, -42 },        /* 둘 다 틀리면 레지스터 에러가 먼저 */
    };
    int32_t regs[NREGS] = { 0 };
    int bad_st = 0, bad_val = 0, bad_untouched = 0;
    for (size_t i = 0; i < sizeof script / sizeof script[0]; i++) {
        int32_t before[NREGS];
        memcpy(before, regs, sizeof regs);
        status_t st = exec_packet(regs, script[i].pkt, PKT_LEN);
        if (st != script[i].st) { bad_st++; printf("  script[%zu]: status %d, want %d\n", i, st, script[i].st); }
        if (regs[script[i].reg] != script[i].after) bad_val++;
        if (st != ST_OK && memcmp(before, regs, sizeof regs) != 0) bad_untouched++;
    }
    CHECK(bad_st == 0);
    CHECK(bad_val == 0);
    CHECK(bad_untouched == 0);                     /* 에러 패킷은 상태를 바꾸지 않는다 */
    static const uint8_t ok_pkt[8] = { OP_SET, 3, 0, 0, 0, 9, 0xAA, 0xBB };
    CHECK(exec_packet(regs, ok_pkt, 5) == ST_ERR_LEN && exec_packet(regs, ok_pkt, 7) == ST_ERR_LEN);
    CHECK(exec_packet(regs, NULL, PKT_LEN) == ST_ERR_LEN && regs[3] == 0);
    CHECK(exec_packet(regs, ok_pkt, PKT_LEN) == ST_OK && regs[3] == 9);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
