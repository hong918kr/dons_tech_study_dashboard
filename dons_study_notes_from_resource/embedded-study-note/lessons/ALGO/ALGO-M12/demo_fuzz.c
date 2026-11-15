/* ALGO-M12 demo_fuzz — NMEA FSM 대량 퍼즈 + "심은 버그를 무엇이 잡나"
 *
 * solution.c 의 nmea_feed 를 그대로 복사하고, -DBUG=n 으로 버그 하나를 심을 수 있다.
 *   BUG=0  정상
 *   BUG=1  TOOLONG 검사 off-by-one  (len >= max_body → len > max_body)
 *   BUG=2  '$' 를 IDLE 에서만 문장 시작으로 봄 (BODY 에서 '$' → BADCHAR)
 *   BUG=3  '$' 로 재시작할 때 ck 를 IDLE 에서 올 때만 초기화
 *   BUG=4  느슨한 16진 변환 (범위 확인 없이 c-'0' / (c|0x20)-'a'+10)
 *   BUG=5  소문자 16진 거절 (대문자만)
 * 검사 3가지: ① 속성 퍼즈 (틀린 체크섬 수락 / 복구 실패 / len > max) ② 참조 스캐너 차분 ③ ASan+UBSan
 *
 * build: cc -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -DBUG=0 demo_fuzz.c -o demo_fuzz && ./demo_fuzz
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef BUG
#define BUG 0
#endif

#define NMEA_MAX_BODY_STD 76
#define NMEA_BODY_CAP     128

typedef enum { NS_IDLE, NS_BODY, NS_CK1, NS_CK2, NS_CR, NS_LF } nmea_state_t;
typedef enum {
    NMEA_EV_NONE = 0, NMEA_EV_SENTENCE, NMEA_EV_ERR_CHECKSUM, NMEA_EV_ERR_TOOLONG, NMEA_EV_ERR_TRUNCATED,
    NMEA_EV_ERR_BADCHAR, NMEA_EV_ERR_NOCKSUM, NMEA_EV_ERR_BADHEX, NMEA_EV_ERR_BADEND, NMEA_EV__COUNT
} nmea_ev_t;
typedef struct {
    nmea_state_t state;
    uint8_t max_body, len, ck, ck_rx;
    char body[NMEA_BODY_CAP];
    uint32_t junk;
    uint32_t count[NMEA_EV__COUNT];
} nmea_parser_t;

static void nmea_init(nmea_parser_t *p, uint8_t max_body) {
    memset(p, 0, sizeof *p);
    p->max_body = max_body < NMEA_BODY_CAP ? max_body : (uint8_t)(NMEA_BODY_CAP - 1);
}
static int hex_ref(uint8_t c) {                      /* 테스트 쪽 엄격한 16진 */
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static int hex_val(uint8_t c) {
#if BUG == 4
    return c <= '9' ? (int)c - '0' : ((c | 0x20) - 'a' + 10) & 15;   /* 'G' → 0 으로 통과 */
#elif BUG == 5
    if (c >= 'a' && c <= 'f') return -1;
    return hex_ref(c);
#else
    return hex_ref(c);
#endif
}
static bool is_body_char(uint8_t c) { return c >= 0x20 && c <= 0x7E && c != '$' && c != '*' && c != '!'; }

static nmea_ev_t nmea_finish(nmea_parser_t *p) {
    p->body[p->len] = '\0';
    return p->ck == p->ck_rx ? NMEA_EV_SENTENCE : NMEA_EV_ERR_CHECKSUM;
}

static nmea_ev_t nmea_feed(nmea_parser_t *p, uint8_t c) {
    nmea_ev_t ev = NMEA_EV_NONE;
#if BUG == 2
    if (c == '$' && p->state == NS_IDLE) {
#else
    if (c == '$') {
#endif
        if (p->state != NS_IDLE) ev = NMEA_EV_ERR_TRUNCATED;
#if BUG == 3
        if (p->state == NS_IDLE) p->ck = 0;
#else
        p->ck = 0;
#endif
        p->state = NS_BODY;
        p->len = 0;
        if (ev != NMEA_EV_NONE) p->count[ev]++;
        return ev;
    }
    switch (p->state) {
    case NS_IDLE: p->junk++; break;
    case NS_BODY:
        if (c == '*') p->state = NS_CK1;
        else if (c == '\r' || c == '\n') ev = NMEA_EV_ERR_NOCKSUM;
        else if (!is_body_char(c)) ev = NMEA_EV_ERR_BADCHAR;
#if BUG == 1
        else if (p->len > p->max_body) ev = NMEA_EV_ERR_TOOLONG;
#else
        else if (p->len >= p->max_body) ev = NMEA_EV_ERR_TOOLONG;
#endif
        else { p->body[p->len++] = (char)c; p->ck ^= c; }
        break;
    case NS_CK1: { int h = hex_val(c); if (h < 0) ev = NMEA_EV_ERR_BADHEX; else { p->ck_rx = (uint8_t)(h << 4); p->state = NS_CK2; } break; }
    case NS_CK2: { int h = hex_val(c); if (h < 0) ev = NMEA_EV_ERR_BADHEX; else { p->ck_rx |= (uint8_t)h; p->state = NS_CR; } break; }
    case NS_CR:
        if (c == '\r') p->state = NS_LF;
        else if (c == '\n') ev = nmea_finish(p);
        else ev = NMEA_EV_ERR_BADEND;
        break;
    case NS_LF: ev = c == '\n' ? nmea_finish(p) : NMEA_EV_ERR_BADEND; break;
    }
    if (ev != NMEA_EV_NONE) { p->state = NS_IDLE; p->count[ev]++; }
    return ev;
}

/* ---- 테스트 쪽 ---- */
static uint32_t rng_state = 0xF022A12Cu;
static uint32_t xorshift32(void) { uint32_t x = rng_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng_state = x; }
static uint32_t rnd(uint32_t n) { return xorshift32() % n; }

static const char *const k_samples[] = {
    "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47",
    "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A",
    "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39",
    "$GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45*75",
    "$GPGGA,092750.000,5321.6802,N,00630.3372,W,1,8,1.03,61.7,M,55.2,M,,*76",
    "$GPGGA,092725.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*5B",
    "$GPGGA,,,,,,0,00,99.99,,,,,,*48",
    "$GPRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A*57",
    "$GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00*74",
    "$GPGLL,4916.45,N,12311.12,W,225444,A*31",
    "$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48",
};
#define N_SAMPLES ((int)(sizeof k_samples / sizeof k_samples[0]))

static bool verify_accept(const uint8_t *b, size_t end, const char *body) {
    size_t L = strlen(body);
    size_t tail = b[end] == '\n' ? (end >= 1 && b[end - 1] == '\r' ? 2 : 1) : 0;
    if (tail == 0 || end + 1 < L + 4 + tail) return false;
    size_t star = end + 1 - tail - 3;
    if (b[star] != '*' || b[star - L - 1] != '$' || memcmp(b + star - L, body, L) != 0) return false;
    int h1 = hex_ref(b[star + 1]), h2 = hex_ref(b[star + 2]);
    if (h1 < 0 || h2 < 0) return false;
    uint8_t x = 0;
    for (size_t t = 0; t < L; t++) x ^= (uint8_t)body[t];
    return x == (uint8_t)(h1 * 16 + h2);
}

/* 선언형 참조: 수락될 문장의 끝 위치 목록 (solution.c 의 ref_scan 과 같은 정의) */
static int ref_scan(const uint8_t *b, size_t n, int max_body, size_t *ends, int cap) {
    int k = 0;
    for (size_t i = 0; i < n; i++) {
        if (b[i] != '$') continue;
        size_t j = i + 1;
        int L = 0;
        while (j < n && is_body_char(b[j]) && L <= max_body) { j++; L++; }
        if (L > max_body || j + 3 >= n || b[j] != '*') continue;
        int h1 = hex_ref(b[j + 1]), h2 = hex_ref(b[j + 2]);
        if (h1 < 0 || h2 < 0) continue;
        size_t e;
        if (b[j + 3] == '\n') e = j + 3;
        else if (j + 4 < n && b[j + 3] == '\r' && b[j + 4] == '\n') e = j + 4;
        else continue;
        uint8_t x = 0;
        for (size_t t = i + 1; t < j; t++) x ^= b[t];
        if (x == (uint8_t)(h1 * 16 + h2) && k < cap) ends[k++] = e;
    }
    return k;
}

static size_t make_garbage(uint8_t *dst, size_t cap, int mode) {
    size_t n = rnd((uint32_t)cap);
    if (mode == 0) {
        for (size_t i = 0; i < n; i++) dst[i] = (uint8_t)rnd(256);
    } else if (mode == 1) {
        static const char soup[] = "$$**,,,\r\n\r\n0123456789ABCDEFabcdefGPGGA.NSEW!\x07\xff";
        for (size_t i = 0; i < n; i++) dst[i] = (uint8_t)soup[rnd((uint32_t)(sizeof soup - 1))];
    } else {                                          /* 예시 문장 변이 (가끔 소문자 hex / 긴 본문) */
        const char *s = k_samples[rnd((uint32_t)N_SAMPLES)];
        size_t L = strlen(s), k = 0;
        if (rnd(8) == 0) { dst[k++] = '$'; for (uint32_t i = 0, m = 70 + rnd(70); i < m && k < cap; i++) dst[k++] = 'A'; }
        for (size_t i = 0; i < L && k < cap; i++) dst[k++] = (uint8_t)s[i];
        if (rnd(4) == 0 && k >= 2) { dst[k - 1] |= 0x20; dst[k - 2] |= 0x20; }   /* 소문자 hex */
        if (k + 2 <= cap) { dst[k++] = '\r'; dst[k++] = '\n'; }
        n = k < n + 20 ? k : n + 20;
        for (int m = (int)rnd(3); m > 0 && n > 0; m--) {
            size_t at = rnd((uint32_t)n);
            switch (rnd(3)) {
            case 0: dst[at] ^= (uint8_t)(1u << rnd(8)); break;
            case 1: dst[at] = (uint8_t)rnd(256); break;
            default: memmove(dst + at, dst + at + 1, n - at - 1); n--; break;
            }
        }
    }
    return n;
}

static uint8_t g_buf[1024];
static size_t g_ends[256];

int main(void) {
    const long N = 1000000;                           /* 모드당 케이스 수 */
    long bad_accept = 0, no_recover = 0, overlen = 0, diff_mismatch = 0, accepted = 0, total_bytes = 0;
    clock_t t0 = clock();
    for (int mode = 0; mode < 3; mode++)
        for (long r = 0; r < N; r++) {
            uint8_t mb = (uint8_t)(r % 4 == 0 ? NMEA_BODY_CAP - 1 : NMEA_MAX_BODY_STD);
            size_t n = make_garbage(g_buf, 300, mode);
            const char *s = k_samples[rnd((uint32_t)N_SAMPLES)];
            size_t L = strlen(s);
            memcpy(g_buf + n, s, L); n += L;
            g_buf[n++] = '\r'; g_buf[n++] = '\n';
            nmea_parser_t P;
            nmea_init(&P, mb);
            nmea_ev_t last = NMEA_EV_NONE;
            int ng = 0;
            size_t got_end[256];
            for (size_t i = 0; i < n; i++) {
                last = nmea_feed(&P, g_buf[i]);
                if (P.len > P.max_body) overlen++;
                if (last == NMEA_EV_SENTENCE) {
                    accepted++;
                    if (!verify_accept(g_buf, i, P.body)) bad_accept++;
                    if (ng < 256) got_end[ng++] = i;
                }
            }
            size_t bl = (size_t)(strchr(s, '*') - s - 1);
            if (!(last == NMEA_EV_SENTENCE && strlen(P.body) == bl && memcmp(P.body, s + 1, bl) == 0)) no_recover++;
            int nr = ref_scan(g_buf, n, mb, g_ends, 256);
            bool same = nr == ng;
            for (int i = 0; same && i < ng; i++) same = got_end[i] == g_ends[i];
            diff_mismatch += !same;
            total_bytes += (long)n;
        }
    double sec = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("BUG=%d: %ld cases (3 modes x %ld), %ld bytes, %ld sentences accepted, %.2f s CPU\n",
           BUG, 3 * N, N, total_bytes, accepted, sec);
    printf("  property fuzz : bad accepts %ld, no recovery %ld, len>max %ld\n", bad_accept, no_recover, overlen);
    printf("  differential  : %ld cases differ from the reference scanner\n", diff_mismatch);
    return (bad_accept || no_recover || overlen || diff_mismatch) ? 1 : 0;
}
