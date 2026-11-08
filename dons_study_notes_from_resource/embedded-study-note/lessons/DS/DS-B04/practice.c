/* DS-B04 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool is_pow2(uint32_t x) { return x != 0u && (x & (x - 1u)) == 0u; }

/* ---- 연습 1: 2의 거듭제곱이 아니면 자유 증가 카운터가 왜 깨지나 ----
 * 카운터 c 가 UINT32_MAX → 0 으로 넘어갈 때 칸 번호도 "다음 칸" 으로 가야 한다.
 * c % n 이 끊김 없이 이어지는지 확인: 2^32 가 n 의 배수일 때만 성립 → n 이 2의 거듭제곱. */
static bool wrap_is_contiguous(uint32_t n) {
    uint32_t c = UINT32_MAX;
    return (uint32_t)(c + 1u) % n == (c % n + 1u) % n;
}

/* ---- 연습 2: uint16_t 카운터 — 정수 승격 함정 (C-B02) ---- */
#define R16_SIZE 8u
typedef struct { uint8_t buf[R16_SIZE]; uint16_t head, tail; } prb16_t;
static int      len16_wrong(const prb16_t *r) { return r->head - r->tail; }              /* int 로 승격된 뺄셈 */
static uint16_t len16_right(const prb16_t *r) { return (uint16_t)(r->head - r->tail); }  /* 16비트로 다시 자른다 */
static bool push16(prb16_t *r, uint8_t b) {
    if (len16_right(r) == R16_SIZE) return false;
    r->buf[r->head & (R16_SIZE - 1u)] = b;
    r->head++;                                  /* uint16_t: 0xFFFF 다음 0 */
    return true;
}

/* ---- 연습 3: 카운터 폭 W 비트면 크기는 최대 2^(W-1) ----
 * uint8_t 카운터로 size 개를 채운 뒤 len 을 계산해 본다. size == 256 이면 head == tail → '비었다' 로 보인다. */
static uint32_t u8_len_after_fill(uint32_t size) {
    uint8_t head = 0xF0u, tail = 0xF0u;         /* 아무 데서나 시작해도 결과는 같다 */
    for (uint32_t k = 0; k < size; k++) head = (uint8_t)(head + 1u);
    return (uint8_t)(head - tail);
}

/* ---- 연습 4: peek / skip 로 프레임 파싱 (sync 0xA5, len, payload) ---- */
#define RB_SIZE 16u
#define RB_MASK (RB_SIZE - 1u)
typedef struct { uint8_t buf[RB_SIZE]; uint32_t head, tail; } prb_t;
static uint32_t prb_len(const prb_t *r) { return r->head - r->tail; }
static uint32_t prb_write(prb_t *r, const uint8_t *src, uint32_t n) {
    uint32_t space = RB_SIZE - prb_len(r);
    if (n > space) n = space;
    for (uint32_t k = 0; k < n; k++) r->buf[(r->head + k) & RB_MASK] = src[k];
    r->head += n;
    return n;
}
/* tail 에서 i 번째 바이트를 꺼내지 않고 본다 */
static bool prb_peek(const prb_t *r, uint32_t i, uint8_t *out) {
    if (i >= prb_len(r)) return false;
    *out = r->buf[(r->tail + i) & RB_MASK];      /* tail + i 도 그냥 더하고 마스크 — 경계 걱정 없음 */
    return true;
}
static void prb_skip(prb_t *r, uint32_t n) { uint32_t l = prb_len(r); r->tail += (n < l) ? n : l; }

/* 완전한 프레임이 있으면 payload 를 복사하고 길이 반환, 아직 덜 왔으면 -1 */
static int take_frame(prb_t *r, uint8_t *payload) {
    uint8_t b = 0, len = 0;
    while (prb_peek(r, 0, &b) && b != 0xA5u) prb_skip(r, 1);   /* sync 전 쓰레기 버리기 */
    if (!prb_peek(r, 1, &len)) return -1;
    if (prb_len(r) < 2u + len) return -1;                      /* payload 가 덜 왔다: 소비하지 않는다 */
    for (uint32_t k = 0; k < len; k++) prb_peek(r, 2u + k, &payload[k]);
    prb_skip(r, 2u + len);
    return len;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 */
    CHECK(UINT32_MAX % 5u == 0u && (uint32_t)(UINT32_MAX + 1u) % 5u == 0u);   /* 같은 칸을 두 번 쓴다! */
    CHECK(!wrap_is_contiguous(5u) && !wrap_is_contiguous(3u) && !wrap_is_contiguous(6u));
    bool match = true;
    for (uint32_t n = 1; n <= 64u; n++) match = match && (wrap_is_contiguous(n) == is_pow2(n));
    CHECK(match);

    /* 연습 2 */
    prb16_t q;
    memset(&q, 0, sizeof q);
    q.head = q.tail = 0xFFFCu;
    for (uint8_t v = 0; v < R16_SIZE; v++) push16(&q, v);
    CHECK(q.head == 4u);
    CHECK(len16_wrong(&q) == 4 - 0xFFFC);        /* -65528: int 로 계산돼 음수 */
    CHECK(len16_right(&q) == 8u);
    CHECK(!push16(&q, 99u));                    /* len16_right 덕분에 full 판정도 맞다 */

    /* 연습 3 */
    CHECK(u8_len_after_fill(128u) == 128u);     /* 2^(8-1): OK */
    CHECK(u8_len_after_fill(256u) == 0u);       /* 2^8: 가득 찼는데 len 0 → full/empty 구별 불가 */

    /* 연습 4 — 카운터를 UINT32_MAX 바로 앞에서 시작 */
    prb_t r;
    memset(&r, 0, sizeof r);
    r.head = r.tail = UINT32_MAX - 2u;
    const uint8_t rx1[] = {0x00, 0x13, 0xA5, 3, 'a', 'b', 'c', 0xA5, 2, 'x'};
    uint8_t pl[8] = {0};
    CHECK(prb_write(&r, rx1, sizeof rx1) == sizeof rx1);
    CHECK(take_frame(&r, pl) == 3 && memcmp(pl, "abc", 3) == 0);
    CHECK(take_frame(&r, pl) == -1 && prb_len(&r) == 3u);       /* A5 02 'x' — 'y' 가 아직 안 왔다 */
    CHECK(prb_write(&r, (const uint8_t *)"y", 1u) == 1u);
    CHECK(take_frame(&r, pl) == 2 && memcmp(pl, "xy", 2) == 0);
    CHECK(prb_len(&r) == 0u && r.tail == r.head && r.head < UINT32_MAX - 2u);   /* 2^32 를 넘어갔다 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
