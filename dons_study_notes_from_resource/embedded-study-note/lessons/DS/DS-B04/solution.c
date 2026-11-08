/* DS-B04 2의 거듭제곱 링 버퍼: 마스크 인덱스와 자유 증가 카운터
 * head/tail 은 "지금까지 넣은/뺀 총 바이트 수" 를 uint32_t 로 그냥 계속 더한다 (2^32 에서 저절로 0 으로).
 * 칸 번호는 counter & (SIZE-1), 길이는 head - tail. Linux kfifo 와 같은 설계.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RB_BITS 3u
#define RB_SIZE (1u << RB_BITS)            /* 8 칸 */
#define RB_MASK (RB_SIZE - 1u)             /* 0b111 */
_Static_assert((RB_SIZE & (RB_SIZE - 1u)) == 0u, "RB_SIZE must be a power of two");

typedef struct {
    uint8_t  buf[RB_SIZE];
    uint32_t head;   /* 지금까지 넣은 총 바이트 수 (mod 2^32). 생산자(ISR/DMA)만 증가 */
    uint32_t tail;   /* 지금까지 뺀 총 바이트 수 (mod 2^32). 소비자(main)만 증가 */
} prb_t;

/* 테스트용: 카운터를 아무 값(예: UINT32_MAX 근처)에서 시작할 수 있게 한다 */
static void prb_init(prb_t *r, uint32_t start) {
    memset(r->buf, 0, sizeof r->buf);
    r->head = r->tail = start;
}

/* ---------------- Part A: 마스크 인덱스 ---------------- */
static bool is_pow2(uint32_t x) { return x != 0u && (x & (x - 1u)) == 0u; }
static inline uint32_t prb_idx(uint32_t counter) { return counter & RB_MASK; }   /* == counter % RB_SIZE */

/* ---------------- Part B: 자유 증가 카운터 ---------------- */
static inline uint32_t prb_len(const prb_t *r) { return r->head - r->tail; }    /* unsigned 뺄셈 = mod 2^32 */
static inline uint32_t prb_free(const prb_t *r) { return RB_SIZE - prb_len(r); }
static inline bool prb_empty(const prb_t *r) { return r->head == r->tail; }
static inline bool prb_full(const prb_t *r)  { return prb_len(r) == RB_SIZE; }

static bool prb_push(prb_t *r, uint8_t b) {
    if (prb_full(r)) return false;
    r->buf[prb_idx(r->head)] = b;
    r->head++;                              /* UINT32_MAX 다음은 0 — unsigned 라 정의된 동작 */
    return true;
}
static bool prb_pop(prb_t *r, uint8_t *out) {
    if (prb_empty(r)) return false;
    *out = r->buf[prb_idx(r->tail)];
    r->tail++;
    return true;
}

/* ---------------- Part C: 일괄 쓰기/읽기 (최대 memcpy 2번) ---------------- */
/* 들어갈 만큼만 쓴다. 반환: 실제로 쓴 바이트 수 */
static uint32_t prb_write(prb_t *r, const uint8_t *src, uint32_t n) {
    uint32_t space = prb_free(r);
    if (n > space) n = space;
    uint32_t off = prb_idx(r->head);
    uint32_t first = RB_SIZE - off;         /* 배열 끝까지 연속된 칸 수 */
    if (first > n) first = n;
    memcpy(&r->buf[off], src, first);
    memcpy(&r->buf[0], src + first, n - first);
    r->head += n;
    return n;
}
/* 있는 만큼만 읽는다. 반환: 실제로 읽은 바이트 수 */
static uint32_t prb_read(prb_t *r, uint8_t *dst, uint32_t n) {
    uint32_t avail = prb_len(r);
    if (n > avail) n = avail;
    uint32_t off = prb_idx(r->tail);
    uint32_t first = RB_SIZE - off;
    if (first > n) first = n;
    memcpy(dst, &r->buf[off], first);
    memcpy(dst + first, &r->buf[0], n - first);
    r->tail += n;
    return n;
}

/* ---------------- 참조 모델 + 경계 랜덤 테스트 (하네스) ---------------- */
#define FUZZ_OPS  30000u
#define MODEL_MAX (FUZZ_OPS * 12u)          /* 연산당 최대 12 바이트 → 모델은 감길 일이 없다 */
static uint8_t  g_q[MODEL_MAX];
static uint32_t g_front, g_back;            /* 모델: 감기지 않는 1차원 배열 큐 */

typedef struct { uint32_t bytes_in, bytes_out, head_wraps, tail_wraps; } fuzz_stats_t;

static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

/* 카운터를 start 에서 시작해 무작위 push/pop/write/read 를 모델과 대조한다.
 * 매 연산 뒤: 길이, 그리고 head == start + 넣은 총량, tail == start + 뺀 총량 (mod 2^32) 인지 확인. */
static bool fuzz_wrap(uint32_t start, uint32_t seed, fuzz_stats_t *st) {
    prb_t r;
    prb_init(&r, start);
    g_front = g_back = 0u;
    memset(st, 0, sizeof *st);
    uint8_t tmp[16];
    for (uint32_t i = 0; i < FUZZ_OPS; i++) {
        uint32_t x = xorshift32(&seed);
        bool produce = x % 100u < (((i >> 5) & 1u) ? 30u : 70u);   /* 32 연산마다 채우기/비우기 교대 */
        bool bulk = (x >> 7) & 1u;
        uint32_t n = 1u + (x >> 8) % 12u;
        uint32_t mlen = g_back - g_front;
        uint32_t ph = r.head, pt = r.tail;
        if (produce && !bulk) {
            uint8_t b = (uint8_t)(x >> 20);
            bool want = mlen < RB_SIZE;
            if (prb_push(&r, b) != want) { printf("  start 0x%08X op %u: push 불일치\n", start, i); return false; }
            if (want) g_q[g_back++] = b;
        } else if (produce) {
            for (uint32_t k = 0; k < n; k++) tmp[k] = (uint8_t)xorshift32(&seed);
            uint32_t want = (n < RB_SIZE - mlen) ? n : RB_SIZE - mlen;
            if (prb_write(&r, tmp, n) != want) { printf("  start 0x%08X op %u: write 불일치\n", start, i); return false; }
            memcpy(&g_q[g_back], tmp, want);
            g_back += want;
        } else if (!bulk) {
            uint8_t got = 0;
            bool want = mlen > 0u;
            if (prb_pop(&r, &got) != want || (want && got != g_q[g_front])) {
                printf("  start 0x%08X op %u: pop 불일치\n", start, i);
                return false;
            }
            if (want) g_front++;
        } else {
            uint32_t want = (n < mlen) ? n : mlen;
            if (prb_read(&r, tmp, n) != want || memcmp(tmp, &g_q[g_front], want) != 0) {
                printf("  start 0x%08X op %u: read 불일치\n", start, i);
                return false;
            }
            g_front += want;
        }
        if (prb_len(&r) != g_back - g_front || r.head != start + g_back || r.tail != start + g_front) {
            printf("  start 0x%08X op %u: len %u, 모델 %u\n", start, i, prb_len(&r), g_back - g_front);
            return false;
        }
        if (r.head < ph) st->head_wraps++;  /* 숫자가 작아졌다 = 2^32 를 넘었다 */
        if (r.tail < pt) st->tail_wraps++;
    }
    st->bytes_in = g_back;
    st->bytes_out = g_front;
    return true;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* Part A — 마스크 인덱스 */
    CHECK(is_pow2(1u) && is_pow2(8u) && is_pow2(0x80000000u));
    CHECK(!is_pow2(0u) && !is_pow2(6u) && !is_pow2(0xFFFFFFFFu));
    CHECK(prb_idx(0u) == 0u && prb_idx(9u) == 1u && prb_idx(UINT32_MAX) == 7u);
    bool same = true, contiguous = true;
    uint32_t c = UINT32_MAX - 20u;
    for (int k = 0; k < 40; k++, c++) {     /* 2^32 경계를 가로지르며 확인 */
        same = same && prb_idx(c) == c % RB_SIZE;
        contiguous = contiguous && prb_idx(c + 1u) == ((prb_idx(c) + 1u) & RB_MASK);
    }
    CHECK(same);
    CHECK(contiguous);                      /* UINT32_MAX(7번 칸) 다음 0 은 0번 칸 — 끊김 없음 */

    /* Part B — 0 에서 시작: 기본 동작 */
    prb_t r;
    uint8_t b = 0;
    prb_init(&r, 0u);
    CHECK(prb_empty(&r) && prb_len(&r) == 0u && prb_free(&r) == RB_SIZE);
    CHECK(prb_push(&r, 'A') && prb_push(&r, 'B') && prb_len(&r) == 2u);
    CHECK(prb_pop(&r, &b) && b == 'A' && prb_len(&r) == 1u);

    /* Part B — UINT32_MAX 경계: head 가 먼저 넘어가고, 나중에 tail 이 넘어간다 */
    prb_init(&r, UINT32_MAX - 3u);          /* head = tail = 0xFFFFFFFC → 칸 4 */
    bool all = true;
    for (uint8_t v = 0; v < RB_SIZE; v++) all = all && prb_push(&r, v);
    CHECK(all);
    CHECK(r.head == 4u && r.tail == 0xFFFFFFFCu);   /* head 는 2^32 를 넘어 4 */
    CHECK(r.head < r.tail);                         /* 숫자로는 head 가 더 작다! */
    CHECK(prb_len(&r) == RB_SIZE && prb_full(&r));  /* 4 - 0xFFFFFFFC == 8 (mod 2^32) */
    CHECK(!prb_push(&r, 99u));
    CHECK(r.buf[4] == 0u && r.buf[7] == 3u && r.buf[0] == 4u && r.buf[3] == 7u);
    for (int k = 0; k < 3; k++) prb_pop(&r, &b);
    CHECK(b == 2u && r.tail == UINT32_MAX && prb_len(&r) == 5u);
    CHECK(prb_pop(&r, &b) && b == 3u && r.tail == 0u && prb_len(&r) == 4u);   /* 이제 tail 도 넘어감 */
    while (prb_pop(&r, &b)) { }
    CHECK(b == 7u && prb_empty(&r) && r.head == 4u);

    /* Part C — 일괄 쓰기/읽기: 배열 끝에서 둘로 나뉘는 경우 */
    prb_init(&r, UINT32_MAX - 1u);          /* 칸 6 에서 시작 → 6,7 | 0,1,2 로 나뉜다 */
    uint8_t in[10] = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19}, outb[10] = {0};
    CHECK(prb_write(&r, in, 5u) == 5u && r.buf[6] == 10u && r.buf[7] == 11u && r.buf[0] == 12u);
    CHECK(prb_write(&r, in + 5, 5u) == 3u && prb_full(&r));   /* 남은 3칸만 들어간다 */
    CHECK(prb_read(&r, outb, 10u) == 8u && memcmp(outb, in, 8u) == 0 && prb_empty(&r));
    CHECK(prb_read(&r, outb, 4u) == 0u);

    /* Part C — 경계 랜덤 테스트: 여러 시작점에서 모델과 대조 */
    const uint32_t starts[4] = { 0u, UINT32_MAX - 3u, UINT32_MAX - 100u, UINT32_MAX - 5000u };
    printf("%-12s %9s %9s %10s %10s %12s\n", "start", "bytes_in", "bytes_out", "head_wraps", "tail_wraps", "final_head");
    for (int k = 0; k < 4; k++) {
        fuzz_stats_t st = {0};
        bool ok = fuzz_wrap(starts[k], 0xB04Bu + (uint32_t)k, &st);
        CHECK(ok);
        CHECK(k == 0 ? st.head_wraps == 0u : (st.head_wraps == 1u && st.tail_wraps == 1u));
        printf("0x%08X   %9u %9u %10u %10u   0x%08X\n", starts[k], st.bytes_in, st.bytes_out,
               st.head_wraps, st.tail_wraps, starts[k] + st.bytes_in);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
