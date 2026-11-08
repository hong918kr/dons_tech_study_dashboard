/* C-B14 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* 연습 1: 짝수 판별.  원본(버그): return x & 1 == 0;  → x & (1 == 0) → 항상 0 */
static bool is_even(uint32_t x) { return (x & 1u) == 0; }

/* 연습 2: lo <= x <= hi.  원본(버그): return lo <= x <= hi;  → (lo <= x) <= hi → 0/1 <= hi */
static bool in_window(int lo, int x, int hi) { return lo <= x && x <= hi; }

/* 연습 3: 끝 표시(END)까지 읽어 개수를 센다.
 * 원본(버그): while (c = next_char(st) != END) out[n++] = c;  → c 는 항상 0/1 */
#define END (-1)
typedef struct { const int *data; size_t pos; } stream_t;
static int next_char(stream_t *st) { return st->data[st->pos++]; }
static size_t read_all(stream_t *st, int *out) {
    size_t n = 0;
    int c;
    while ((c = next_char(st)) != END) out[n++] = c;   /* 대입을 괄호로 먼저 */
    return n;
}

/* 연습 4: *p++ 관용구 — 서로 다른 객체를 하나씩만 수정하므로 정의된 동작 */
static void copy_bytes(uint8_t *d, const uint8_t *s, size_t n) {
    while (n--) *d++ = *s++;
}
static void histogram(unsigned hist[4], const uint8_t *p, size_t n) {
    while (n--) hist[*p++ & 3u]++;     /* p 1번 수정, hist[...] 1번 수정 → OK */
}

/* 연습 5: 인자 평가 순서. 원본: return pack_ids(next_id(), next_id());  → 미지정 */
static uint8_t g_id;
static uint8_t next_id(void) { return ++g_id; }
static uint16_t pack_ids(uint8_t first, uint8_t second) { return (uint16_t)((first << 8) | second); }
static uint16_t two_ids(void) {
    uint8_t first = next_id();          /* 문장 경계 = 시퀀스 포인트 */
    uint8_t second = next_id();
    return pack_ids(first, second);
}

/* 연습 6: ?: 는 오른쪽 결합 — a ? b : c ? d : e == a ? b : (c ? d : e) */
static char grade(int s) { return s >= 90 ? 'A' : s >= 80 ? 'B' : s >= 70 ? 'C' : 'F'; }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    CHECK(is_even(4) == true);
    CHECK(is_even(7) == false);

    CHECK(in_window(10, 15, 20) == true);
    CHECK(in_window(10, 25, 20) == false);
    CHECK(in_window(10, 5, 20) == false);

    const int text[] = {'O', 'K', '!', END};
    stream_t st = { text, 0 };
    int got[8];
    CHECK(read_all(&st, got) == 3);
    CHECK(got[0] == 'O' && got[1] == 'K' && got[2] == '!');

    const uint8_t src[] = {1, 2, 3, 5};
    uint8_t dst[4] = {0};
    copy_bytes(dst, src, 4);
    CHECK(dst[0] == 1 && dst[3] == 5);
    unsigned hist[4] = {0};
    histogram(hist, src, 4);            /* 1&3=1, 2&3=2, 3&3=3, 5&3=1 */
    CHECK(hist[0] == 0 && hist[1] == 2 && hist[2] == 1 && hist[3] == 1);

    g_id = 0;
    CHECK(two_ids() == 0x0102);
    CHECK(two_ids() == 0x0304);

    CHECK(grade(95) == 'A');
    CHECK(grade(85) == 'B');
    CHECK(grade(72) == 'C');
    CHECK(grade(10) == 'F');

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
