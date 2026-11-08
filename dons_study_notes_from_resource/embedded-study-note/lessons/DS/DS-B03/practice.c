/* DS-B03 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RB_N 8u

typedef struct {
    uint8_t  buf[RB_N];
    uint32_t head, tail, count;
} rb_t;

static inline uint32_t next_idx(uint32_t i) { return (i + 1u == RB_N) ? 0u : i + 1u; }

/* ---- 연습 1: 덮어쓰기(overwrite-oldest) 모드 — 가득 차면 가장 오래된 바이트를 버린다 ----
 * 로그/최근 N 샘플 버퍼용. 반환값: 오래된 바이트를 덮어썼으면 true. (count 방식 기반) */
static bool ow_push(rb_t *r, uint8_t b) {
    bool dropped = false;
    if (r->count == RB_N) {                  /* 가득 참: tail 을 한 칸 밀어 가장 오래된 것을 버린다 */
        r->tail = next_idx(r->tail);
        r->count--;
        dropped = true;
    }
    r->buf[r->head] = b;
    r->head = next_idx(r->head);
    r->count++;
    return dropped;
}
static bool cnt_pop(rb_t *r, uint8_t *out) {
    if (r->count == 0u) return false;
    *out = r->buf[r->tail];
    r->tail = next_idx(r->tail);
    r->count--;
    return true;
}

/* ---- 연습 2: 한 줄 꺼내기 (UART AT 명령 응답 파싱) ----
 * '\n' 이 버퍼 안에 있을 때만 그 줄을 꺼낸다 (끝의 '\r' 은 제거). 완전한 줄이 없으면 -1, 데이터는 그대로.
 * 반환: 줄 길이 (NUL 제외). line 은 cap 바이트 (NUL 포함). */
static int rb_getline(rb_t *r, char *line, uint32_t cap) {
    uint32_t i = r->tail, k = 0;
    for (; k < r->count; k++, i = next_idx(i))   /* 1) 꺼내지 않고 훑어서(peek) '\n' 찾기 */
        if (r->buf[i] == '\n') break;
    if (k == r->count) return -1;                /* 아직 줄 끝이 안 들어왔다 */
    uint32_t n = 0;
    uint8_t c = 0;
    for (uint32_t j = 0; j <= k; j++) {          /* 2) '\n' 까지 소비 */
        cnt_pop(r, &c);
        if (c != '\n' && n + 1u < cap) line[n++] = (char)c;
    }
    if (n > 0u && line[n - 1u] == '\r') n--;
    line[n] = '\0';
    return (int)n;
}

/* ---- 연습 3: 0..2N-1 인덱스 (미러 비트를 인덱스 안에 숨긴 형태) ---- */
typedef struct { uint8_t buf[RB_N]; uint32_t head, tail; /* 0..2N-1 */ } rb2_t;
static inline uint32_t adv2(uint32_t h) { return (h + 1u == 2u * RB_N) ? 0u : h + 1u; }
static inline uint32_t slot2(uint32_t h) { return (h < RB_N) ? h : h - RB_N; }   /* 미러 비트 = (h >= N) */
static uint32_t rb2_len(const rb2_t *r) { return (r->head >= r->tail) ? r->head - r->tail : 2u * RB_N - r->tail + r->head; }
static bool rb2_empty(const rb2_t *r) { return r->head == r->tail; }
static bool rb2_full(const rb2_t *r)  { return rb2_len(r) == RB_N; }
static bool rb2_push(rb2_t *r, uint8_t b) {
    if (rb2_full(r)) return false;
    r->buf[slot2(r->head)] = b;
    r->head = adv2(r->head);
    return true;
}
static bool rb2_pop(rb2_t *r, uint8_t *out) {
    if (rb2_empty(r)) return false;
    *out = r->buf[slot2(r->tail)];
    r->tail = adv2(r->tail);
    return true;
}

static uint32_t xorshift32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
#define FUZZ_OPS 50000u
static uint8_t g_q[FUZZ_OPS];
static bool fuzz_rb2(uint32_t seed) {
    rb2_t r;
    memset(&r, 0, sizeof r);
    uint32_t front = 0, back = 0;
    for (uint32_t i = 0; i < FUZZ_OPS; i++) {
        uint32_t x = xorshift32(&seed);
        if (x % 100u < (((i >> 6) & 1u) ? 30u : 70u)) {
            bool want = back - front < RB_N;
            if (rb2_push(&r, (uint8_t)(x >> 8)) != want) return false;
            if (want) g_q[back++] = (uint8_t)(x >> 8);
        } else {
            uint8_t got = 0;
            bool want = back > front;
            if (rb2_pop(&r, &got) != want) return false;
            if (want && got != g_q[front++]) return false;
        }
        if (rb2_len(&r) != back - front || rb2_full(&r) != (back - front == RB_N)) return false;
    }
    return true;
}

/* ---- 연습 4: count 방식의 경쟁 — main 의 count-- 사이에 ISR 이 끼어든다 ----
 * count-- 는 LDR / SUB / STR 3단계. LDR 과 STR 사이에 RX ISR 이 push(count++) 하면 그 ++ 가 사라진다. */
static void isr_push(rb_t *r, uint8_t b) { r->buf[r->head] = b; r->head = next_idx(r->head); r->count++; }
static void main_pop_with_isr_in_middle(rb_t *r, uint8_t *out) {
    *out = r->buf[r->tail];
    r->tail = next_idx(r->tail);
    uint32_t tmp = r->count;       /* LDR  r0, [count]        */
    isr_push(r, 'Z');              /* ← 여기서 UART RX 인터럽트 */
    r->count = tmp - 1u;           /* SUB, STR: ISR 의 ++ 를 덮어쓴다 */
}
/* head/tail 만 쓰는 방식이라면 len 은 두 인덱스에서 계산 → 각 변수는 한쪽만 쓰므로 lost update 없음 */
static uint32_t len_from_indices(const rb_t *r) { return (r->head >= r->tail) ? r->head - r->tail : RB_N - r->tail + r->head; }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 */
    rb_t r;
    memset(&r, 0, sizeof r);
    const char *s = "ABCDEFGHIJ";
    uint32_t drops = 0;
    for (int i = 0; s[i]; i++) drops += ow_push(&r, (uint8_t)s[i]);
    char out[16] = {0};
    uint32_t n = 0;
    uint8_t c = 0;
    while (n < 15u && cnt_pop(&r, &c)) out[n++] = (char)c;
    CHECK(drops == 2u);
    CHECK(strcmp(out, "CDEFGHIJ") == 0);

    /* 연습 2 */
    memset(&r, 0, sizeof r);
    char line[16];
    for (const char *p = "OK\r\nER"; *p; p++) (void)ow_push(&r, (uint8_t)*p);   /* ISR 가 받은 바이트 */
    CHECK(rb_getline(&r, line, sizeof line) == 2 && strcmp(line, "OK") == 0);
    CHECK(rb_getline(&r, line, sizeof line) == -1 && r.count == 2u);   /* "ER" 는 그대로 남는다 */
    ow_push(&r, 'R'); ow_push(&r, '\n');
    CHECK(rb_getline(&r, line, sizeof line) == 3 && strcmp(line, "ERR") == 0);
    CHECK(r.count == 0u);

    /* 연습 3 */
    rb2_t r2;
    memset(&r2, 0, sizeof r2);
    for (int i = 0; i < (int)RB_N; i++) rb2_push(&r2, (uint8_t)i);
    CHECK(slot2(r2.head) == slot2(r2.tail) && r2.head != r2.tail && rb2_full(&r2));  /* 같은 칸, 다른 미러 */
    CHECK(fuzz_rb2(12345u));

    /* 연습 4 */
    memset(&r, 0, sizeof r);
    isr_push(&r, 'A'); isr_push(&r, 'B');
    main_pop_with_isr_in_middle(&r, &c);
    CHECK(c == 'A');
    CHECK(len_from_indices(&r) == 2u);   /* 실제로는 B, Z 두 개 */
    CHECK(r.count == 1u);                /* count 는 1 — ISR 의 ++ 를 잃었다 (lost update) */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
