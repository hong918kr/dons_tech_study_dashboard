/* ALGO-M12 추가 연습 — 한 번에 검증
 *   연습 1  nmea_parse_rmc()   RMC: 상태 A/V, 속도 knot → mm/s, 침로, 날짜 ddmmyy → Unix ms (days_from_civil)
 *   연습 2  gsv_feed()         여러 문장으로 나뉜 GSV 묶음 조립 (순서 / 누락 / 위성 수 검사)
 *   연습 3  nmea_build()       "$body*HH\r\n" 생성기 + FSM 왕복 성질
 *   연습 4  XOR 체크섬의 사각지대 전수 측정 (1글자 / 2글자 치환, 인접 자리 바꿈)
 * 본문 함수(nmea_feed, nmea_split, parse_*)는 solution.c 와 같은 것을 짧게 다시 둔다 (파일 자립).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 본문에서 가져온 것 (요약본) ---------------- */
#define NMEA_MAX_BODY_STD 76
#define NMEA_BODY_CAP     128
#define NMEA_MAX_FIELDS   24
typedef enum { NS_IDLE, NS_BODY, NS_CK1, NS_CK2, NS_CR, NS_LF } nmea_state_t;
typedef enum { EV_NONE, EV_SENTENCE, EV_ERR } nmea_ev_t;   /* 여기선 에러 종류를 하나로 묶는다 */
typedef struct { nmea_state_t st; uint8_t max_body, len, ck, ck_rx; char body[NMEA_BODY_CAP]; } nmea_parser_t;

static int hex_val(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static bool is_body_char(uint8_t c) { return c >= 0x20 && c <= 0x7E && c != '$' && c != '*' && c != '!'; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static void nmea_init(nmea_parser_t *p, uint8_t mb) { memset(p, 0, sizeof *p); p->max_body = mb; }
static nmea_ev_t nmea_feed(nmea_parser_t *p, uint8_t c) {
    if (c == '$') { nmea_ev_t e = p->st != NS_IDLE ? EV_ERR : EV_NONE; p->st = NS_BODY; p->len = 0; p->ck = 0; return e; }
    int h;
    switch (p->st) {
    case NS_IDLE: return EV_NONE;
    case NS_BODY:
        if (c == '*') { p->st = NS_CK1; return EV_NONE; }
        if (!is_body_char(c) || p->len >= p->max_body) break;
        p->body[p->len++] = (char)c; p->ck ^= c;
        return EV_NONE;
    case NS_CK1: if ((h = hex_val(c)) < 0) break; p->ck_rx = (uint8_t)(h << 4); p->st = NS_CK2; return EV_NONE;
    case NS_CK2: if ((h = hex_val(c)) < 0) break; p->ck_rx |= (uint8_t)h; p->st = NS_CR; return EV_NONE;
    case NS_CR:
        if (c == '\r') { p->st = NS_LF; return EV_NONE; }
        if (c != '\n') break;
        /* fall through */
    case NS_LF:
        if (c != '\n') break;
        p->st = NS_IDLE; p->body[p->len] = '\0';
        return p->ck == p->ck_rx ? EV_SENTENCE : EV_ERR;
    }
    p->st = NS_IDLE;
    return EV_ERR;
}
static bool nmea_split(char *body, char *f[], int max, int *nf) {
    int n = 0;
    for (char *p = body;;) {
        if (n == max) { *nf = 0; return false; }
        f[n++] = p;
        while (*p != ',' && *p != '\0') p++;
        if (*p == '\0') break;
        *p++ = '\0';
    }
    *nf = n;
    return true;
}
static bool parse_fixed(const char *s, int frac, bool allow_neg, int32_t *out) {
    bool neg = false;
    if (allow_neg && *s == '-') { neg = true; s++; }
    int64_t v = 0;
    int nd = 0, got = 0;
    while (is_digit(*s)) { if (++nd > 7) return false; v = v * 10 + (*s++ - '0'); }
    if (nd == 0) return false;
    if (*s == '.') { s++; if (!is_digit(*s)) return false; for (; is_digit(*s); s++) if (got < frac) { v = v * 10 + (*s - '0'); got++; } }
    if (*s != '\0') return false;
    for (; got < frac; got++) v *= 10;
    *out = (int32_t)(neg ? -v : v);
    return true;
}
static bool parse_hms_ms(const char *s, uint32_t *out) {
    for (int i = 0; i < 6; i++) if (!is_digit(s[i])) return false;
    uint32_t hh = (uint32_t)((s[0] - '0') * 10 + s[1] - '0'), mm = (uint32_t)((s[2] - '0') * 10 + s[3] - '0'),
             ss = (uint32_t)((s[4] - '0') * 10 + s[5] - '0'), ms = 0;
    if (hh > 23 || mm > 59 || ss > 60) return false;
    const char *p = s + 6;
    if (*p == '.') { p++; if (!is_digit(*p)) return false; for (uint32_t sc = 100; is_digit(*p); p++, sc /= 10) ms += (uint32_t)(*p - '0') * sc; }
    if (*p != '\0') return false;
    *out = ((hh * 60 + mm) * 60 + ss) * 1000 + ms;
    return true;
}
static bool parse_latlon_e7(const char *v, const char *hemi, int dd, int32_t *out) {
    const uint32_t maxd = dd == 2 ? 90u : 180u;
    const char pc = dd == 2 ? 'N' : 'E', nc = dd == 2 ? 'S' : 'W';
    uint32_t deg = 0, mi = 0;
    int i = 0;
    for (; i < dd; i++) { if (!is_digit(v[i])) return false; deg = deg * 10 + (uint32_t)(v[i] - '0'); }
    for (; i < dd + 2; i++) { if (!is_digit(v[i])) return false; mi = mi * 10 + (uint32_t)(v[i] - '0'); }
    const char *p = v + i;
    uint64_t fr = 0, sc = 1;
    if (*p == '.') { p++; if (!is_digit(*p)) return false; for (; is_digit(*p); p++) { if (sc == 100000000u) return false; fr = fr * 10 + (uint64_t)(*p - '0'); sc *= 10; } }
    if (*p != '\0' || mi > 59 || deg > maxd || (deg == maxd && (mi | fr) != 0)) return false;
    int sign = (hemi[0] == pc && !hemi[1]) ? 1 : (hemi[0] == nc && !hemi[1]) ? -1 : 0;
    if (!sign) return false;
    uint64_t M = (uint64_t)mi * sc + fr, D = 60u * sc;
    uint32_t mag = deg * 10000000u + (uint32_t)((M * 10000000u + D / 2) / D);
    *out = sign > 0 ? (int32_t)mag : -(int32_t)mag;
    return true;
}

/* ---------------- 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t rng_state = 0x0A12C0DEu;
static uint32_t xorshift32(void) { uint32_t x = rng_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng_state = x; }
static uint32_t rnd(uint32_t n) { return xorshift32() % n; }

/* 문자열 사본을 분리 (테스트 편의) */
static int split_copy(const char *body, char *buf, size_t cap, char *f[]) {
    snprintf(buf, cap, "%s", body);
    int nf = 0;
    return nmea_split(buf, f, NMEA_MAX_FIELDS, &nf) ? nf : 0;
}

/* ================= 연습 1: RMC + 날짜 → Unix 시각 ================= */
static bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int days_in_month(int y, int m) {
    static const uint8_t d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return m == 2 && is_leap(y) ? 29 : d[m - 1];
}
/* 1970-01-01 부터의 일수 (proleptic Gregorian, y ≥ 0). 4·100·400년 주기를 닫힌 식으로 */
static int32_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int era = y / 400;
    int yoe = y - era * 400;                                   /* [0, 399]  */
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  /* [0, 365], 3월 시작 */
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           /* [0, 146096] */
    return era * 146097 + doe - 719468;
}

typedef struct {
    uint32_t time_ms;
    bool     active;                 /* 'A' = 유효, 'V' = 무효(void) */
    bool     has_pos;
    int32_t  lat_e7, lon_e7;
    uint32_t speed_mm_s;
    uint16_t course_x100;
    uint16_t year; uint8_t month, day;
    int64_t  unix_ms;
} rmc_t;
typedef enum { RMC_OK, RMC_ERR_NOTRMC, RMC_ERR_FIELDS, RMC_ERR_TIME, RMC_ERR_STATUS, RMC_ERR_POS,
               RMC_ERR_SPEED, RMC_ERR_COURSE, RMC_ERR_DATE } rmc_err_t;

/* 필드: 0 주소, 1 시각, 2 상태, 3-4 위도, 5-6 경도, 7 속도(knot), 8 침로(도), 9 날짜 ddmmyy, 10-11 자편차,
 *       [12 모드 표시 (NMEA 2.3+)], [13 항법 상태 (4.1+)] → n ∈ {12, 13, 14} */
static rmc_err_t nmea_parse_rmc(char *const f[], int n, rmc_t *out) {
    rmc_t r;
    memset(&r, 0, sizeof r);
    memset(out, 0, sizeof *out);
    if (n < 1 || strlen(f[0]) != 5 || strcmp(f[0] + 2, "RMC") != 0) return RMC_ERR_NOTRMC;
    if (n < 12 || n > 14) return RMC_ERR_FIELDS;
    if (!parse_hms_ms(f[1], &r.time_ms)) return RMC_ERR_TIME;
    if (strcmp(f[2], "A") == 0) r.active = true;
    else if (strcmp(f[2], "V") != 0) return RMC_ERR_STATUS;
    bool no_pos = !f[3][0] && !f[4][0] && !f[5][0] && !f[6][0];
    if (!(no_pos && !r.active)) {
        if (!parse_latlon_e7(f[3], f[4], 2, &r.lat_e7) || !parse_latlon_e7(f[5], f[6], 3, &r.lon_e7)) return RMC_ERR_POS;
        r.has_pos = true;
    }
    int32_t kn = 0, crs = 0;
    if (f[7][0] && (!parse_fixed(f[7], 3, false, &kn))) return RMC_ERR_SPEED;
    r.speed_mm_s = (uint32_t)(((uint64_t)kn * 1852u + 1800u) / 3600u);   /* 1 knot = 1852 m / 3600 s, 반올림 */
    if (f[8][0] && (!parse_fixed(f[8], 2, false, &crs) || crs >= 36000)) return RMC_ERR_COURSE;
    r.course_x100 = (uint16_t)crs;
    const char *d = f[9];
    for (int i = 0; i < 6; i++) if (!is_digit(d[i])) return RMC_ERR_DATE;
    if (d[6] != '\0') return RMC_ERR_DATE;
    int dd = (d[0] - '0') * 10 + d[1] - '0', mm = (d[2] - '0') * 10 + d[3] - '0', yy = (d[4] - '0') * 10 + d[5] - '0';
    int year = yy >= 80 ? 1900 + yy : 2000 + yy;                         /* 두 자리 연도 피벗 (관례) */
    if (mm < 1 || mm > 12 || dd < 1 || dd > days_in_month(year, mm)) return RMC_ERR_DATE;
    r.year = (uint16_t)year; r.month = (uint8_t)mm; r.day = (uint8_t)dd;
    r.unix_ms = (int64_t)days_from_civil(year, mm, dd) * 86400000 + r.time_ms;
    *out = r;
    return RMC_OK;
}

/* ================= 연습 2: GSV 묶음 조립 ================= */
#define GSV_MAX_SATS 64
typedef struct { uint8_t prn; int8_t elev; uint16_t az; int8_t snr; } gsv_sat_t;   /* snr -1 = 추적 안 됨 */
typedef struct {
    int total, next, in_view, n;               /* next = 기다리는 문장 번호 (0 = 묶음 밖) */
    gsv_sat_t sat[GSV_MAX_SATS];
    uint32_t complete, errors;
} gsv_asm_t;
typedef enum { GSV_PENDING, GSV_COMPLETE, GSV_ERROR } gsv_ev_t;

static bool small_uint(const char *s, int maxd, int *out) {
    int v = 0, k = 0;
    for (; is_digit(s[k]); k++) { if (k == maxd) return false; v = v * 10 + (s[k] - '0'); }
    if (k == 0 || s[k] != '\0') return false;
    *out = v;
    return true;
}
static gsv_ev_t gsv_fail(gsv_asm_t *a) { a->next = 0; a->n = 0; a->errors++; return GSV_ERROR; }

/* 문장 번호 1 은 언제나 새 묶음을 시작 (이전 미완성 묶음은 버리고 에러 1회) */
static gsv_ev_t gsv_feed(gsv_asm_t *a, char *const f[], int n) {
    int total, num, inv;
    if (n < 4 || (n - 4) % 4 != 0 || !small_uint(f[1], 1, &total) || !small_uint(f[2], 1, &num) ||
        !small_uint(f[3], 2, &inv) || total < 1 || num < 1 || num > total)
        return gsv_fail(a);
    if (num == 1) {
        bool dropped = a->next != 0;
        a->total = total; a->in_view = inv; a->n = 0; a->next = 1;
        if (dropped) a->errors++;
    }
    if (a->next != num || a->total != total || a->in_view != inv) return gsv_fail(a);
    for (int g = 4; g + 3 < n; g += 4) {
        if (f[g][0] == '\0') continue;                         /* 빈 칸 묶음 (마지막 문장의 남는 자리) */
        int prn, el, az, snr = -1;
        if (!small_uint(f[g], 3, &prn) || !small_uint(f[g + 1], 2, &el) || !small_uint(f[g + 2], 3, &az) ||
            (f[g + 3][0] && !small_uint(f[g + 3], 2, &snr)) || el > 90 || az > 359 || a->n == GSV_MAX_SATS)
            return gsv_fail(a);
        a->sat[a->n++] = (gsv_sat_t){ (uint8_t)prn, (int8_t)el, (uint16_t)az, (int8_t)snr };
    }
    if (num < total) { a->next++; return GSV_PENDING; }
    a->next = 0;
    if (a->n != a->in_view) { a->errors++; return GSV_ERROR; }
    a->complete++;
    return GSV_COMPLETE;
}

/* ================= 연습 3: 생성기 ================= */
/* "$body*HH\r\n" + NUL 을 dst 에 쓴다. 본문 글자 위반 / 76자 초과 / cap 부족이면 0. */
static size_t nmea_build(char *dst, size_t cap, const char *body) {
    static const char H[] = "0123456789ABCDEF";
    size_t L = 0;
    uint8_t x = 0;
    for (; body[L] != '\0'; L++) {
        if (L == NMEA_MAX_BODY_STD || !is_body_char((uint8_t)body[L])) return 0;
        x ^= (uint8_t)body[L];
    }
    size_t need = 1 + L + 3 + 2;                               /* 글자 수 (NUL 제외) */
    if (cap < need + 1) return 0;
    dst[0] = '$';
    memcpy(dst + 1, body, L);
    dst[1 + L] = '*'; dst[2 + L] = H[x >> 4]; dst[3 + L] = H[x & 15];
    dst[4 + L] = '\r'; dst[5 + L] = '\n'; dst[6 + L] = '\0';
    return need;
}

int main(void) {
    char buf[160], *f[NMEA_MAX_FIELDS];

    /* ---- 연습 1 ---- */
    {
        rmc_t r;
        int n = split_copy("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W", buf, sizeof buf, f);
        CHECK(n == 12 && nmea_parse_rmc(f, n, &r) == RMC_OK && r.active && r.lat_e7 == 481173000 &&
              r.lon_e7 == 115166667 && r.speed_mm_s == 11524 && r.course_x100 == 8440 && r.year == 1994 &&
              r.month == 3 && r.day == 23 && r.unix_ms == 764426119000LL);
        n = split_copy("GPRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A", buf, sizeof buf, f);
        CHECK(n == 13 && nmea_parse_rmc(f, n, &r) == RMC_OK && r.speed_mm_s == 2 && r.course_x100 == 7752 &&
              r.unix_ms == 1039422959000LL);
        n = split_copy("GNRMC,000000.00,V,,,,,,,290224,,,N,V", buf, sizeof buf, f);       /* 4.1 형식, 무효 */
        CHECK(n == 14 && nmea_parse_rmc(f, n, &r) == RMC_OK && !r.active && !r.has_pos && r.day == 29 && r.month == 2);
        n = split_copy("GNRMC,000000.00,V,,,,,,,290223,,,N,V", buf, sizeof buf, f);       /* 2023 은 평년 */
        CHECK(nmea_parse_rmc(f, n, &r) == RMC_ERR_DATE);
        n = split_copy("GPRMC,123519,X,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W", buf, sizeof buf, f);
        CHECK(nmea_parse_rmc(f, n, &r) == RMC_ERR_STATUS);
        n = split_copy("GPRMC,123519,A,,,,,022.4,084.4,230394,003.1,W", buf, sizeof buf, f);
        CHECK(nmea_parse_rmc(f, n, &r) == RMC_ERR_POS);                                  /* 'A' 인데 위치 없음 */
        n = split_copy("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,360.0,230394,003.1,W", buf, sizeof buf, f);
        CHECK(nmea_parse_rmc(f, n, &r) == RMC_ERR_COURSE);
        n = split_copy("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,310494,003.1,W", buf, sizeof buf, f);
        CHECK(nmea_parse_rmc(f, n, &r) == RMC_ERR_DATE);                                 /* 4월 31일 없음 */
        /* days_from_civil 을 1970-01-01 부터 하루씩 세는 기준과 1970..2099 전 날짜 대조 */
        long wrong = 0, days = 0;
        for (int y = 1970; y <= 2099; y++)
            for (int m = 1; m <= 12; m++)
                for (int d = 1; d <= days_in_month(y, m); d++) { wrong += days_from_civil(y, m, d) != days; days++; }
        printf("days_from_civil: %ld dates checked, %ld wrong\n", days, wrong);
        CHECK(days == 47482 && wrong == 0);
        CHECK(days_from_civil(2000, 3, 1) == 11017 && days_from_civil(1969, 12, 31) == -1);
    }

    /* ---- 연습 2 ---- */
    {
        static const char *const grp[] = {
            "GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00",
            "GPGSV,3,2,11,14,25,170,00,16,57,208,39,18,67,296,40,19,40,246,00",
            "GPGSV,3,3,11,22,42,067,42,24,14,311,43,27,05,244,00,,,,",
        };
        gsv_asm_t a;
        memset(&a, 0, sizeof a);
        gsv_ev_t e[3];
        for (int i = 0; i < 3; i++) { int n = split_copy(grp[i], buf, sizeof buf, f); e[i] = gsv_feed(&a, f, n); }
        CHECK(e[0] == GSV_PENDING && e[1] == GSV_PENDING && e[2] == GSV_COMPLETE && a.n == 11 && a.complete == 1);
        CHECK(a.sat[5].prn == 16 && a.sat[5].elev == 57 && a.sat[5].az == 208 && a.sat[5].snr == 39 && a.sat[10].prn == 27);
        /* 2번 문장 누락 → 3번에서 에러, 다음 1번부터 정상 */
        memset(&a, 0, sizeof a);
        int n = split_copy(grp[0], buf, sizeof buf, f); gsv_ev_t x0 = gsv_feed(&a, f, n);
        n = split_copy(grp[2], buf, sizeof buf, f); gsv_ev_t x1 = gsv_feed(&a, f, n);
        CHECK(x0 == GSV_PENDING && x1 == GSV_ERROR && a.errors == 1);
        for (int i = 0; i < 3; i++) { n = split_copy(grp[i], buf, sizeof buf, f); e[i] = gsv_feed(&a, f, n); }
        CHECK(e[2] == GSV_COMPLETE && a.complete == 1);
        /* 묶음 도중 새 1번 → 이전 것 버림(에러 1) 후 새 묶음 진행 */
        memset(&a, 0, sizeof a);
        n = split_copy(grp[0], buf, sizeof buf, f); gsv_feed(&a, f, n);
        n = split_copy(grp[0], buf, sizeof buf, f); gsv_ev_t y = gsv_feed(&a, f, n);
        CHECK(y == GSV_PENDING && a.errors == 1 && a.n == 4);
        /* SNR 빈칸 = 추적 안 됨, 위성 수 불일치 → 에러 */
        memset(&a, 0, sizeof a);
        n = split_copy("GPGSV,1,1,02,05,10,100,,07,20,200,35", buf, sizeof buf, f);
        CHECK(gsv_feed(&a, f, n) == GSV_COMPLETE && a.sat[0].snr == -1 && a.sat[1].snr == 35);
        n = split_copy("GPGSV,1,1,03,05,10,100,,07,20,200,35", buf, sizeof buf, f);
        CHECK(gsv_feed(&a, f, n) == GSV_ERROR);
        n = split_copy("GPGSV,1,1,02,05,10,100", buf, sizeof buf, f);                    /* 4개 묶음이 아님 */
        CHECK(gsv_feed(&a, f, n) == GSV_ERROR);
    }

    /* ---- 연습 3 ---- */
    {
        char out[96];
        CHECK(nmea_build(out, sizeof out, "GPGLL,4916.45,N,12311.12,W,225444,A") == 41 &&
              strcmp(out, "$GPGLL,4916.45,N,12311.12,W,225444,A*31\r\n") == 0);
        CHECK(nmea_build(out, 42, "GPGLL,4916.45,N,12311.12,W,225444,A") == 41);       /* 딱 맞음 (NUL 포함 42) */
        CHECK(nmea_build(out, 41, "GPGLL,4916.45,N,12311.12,W,225444,A") == 0);        /* 1바이트 부족 */
        CHECK(nmea_build(out, sizeof out, "GP$LL") == 0 && nmea_build(out, sizeof out, "A*B") == 0);
        char b76[80], b77[80];
        memset(b76, 'A', 76); b76[76] = 0; memset(b77, 'A', 77); b77[77] = 0;
        CHECK(nmea_build(out, sizeof out, b76) == 82 && nmea_build(out, sizeof out, b77) == 0);  /* 82자 한도 */
        /* 왕복: 무작위 본문 50,000개 → build → FSM → 같은 본문 */
        static const char cs[] = "0123456789.,,,ABCDEFGHNSEWMKabc -+/";
        long bad = 0;
        for (int r = 0; r < 50000; r++) {
            char body[80];
            int L = (int)rnd(77);
            for (int i = 0; i < L; i++) body[i] = cs[rnd(sizeof cs - 1)];
            body[L] = 0;
            size_t k = nmea_build(out, sizeof out, body);
            nmea_parser_t P;
            nmea_init(&P, NMEA_MAX_BODY_STD);
            nmea_ev_t last = EV_NONE;
            for (size_t i = 0; i < k; i++) last = nmea_feed(&P, (uint8_t)out[i]);
            bad += !(k == (size_t)L + 6 && last == EV_SENTENCE && strcmp(P.body, body) == 0);
        }
        CHECK(bad == 0);
    }

    /* ---- 연습 4: XOR 체크섬 사각지대 (GGA 예시 본문) ---- */
    {
        const char *s = "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,";
        int L = (int)strlen(s);
        uint8_t alpha[96];
        int na = 0;
        for (int c = 0x20; c <= 0x7E; c++) if (is_body_char((uint8_t)c)) alpha[na++] = (uint8_t)c;
        /* 1글자 치환: 변화량 a^a' != 0 → 항상 잡힘 */
        long s1 = 0, m1 = 0;
        for (int i = 0; i < L; i++) for (int k = 0; k < na; k++) if (alpha[k] != (uint8_t)s[i]) { s1++; m1 += (alpha[k] ^ (uint8_t)s[i]) == 0; }
        /* 2글자 치환 (서로 다른 두 위치, 둘 다 바뀜): (a^a') == (b^b') 이면 못 잡음 */
        long s2 = 0, m2 = 0;
        for (int i = 0; i < L; i++)
            for (int j = i + 1; j < L; j++)
                for (int k = 0; k < na; k++) {
                    if (alpha[k] == (uint8_t)s[i]) continue;
                    uint8_t di = alpha[k] ^ (uint8_t)s[i];
                    for (int t = 0; t < na; t++) {
                        if (alpha[t] == (uint8_t)s[j]) continue;
                        s2++;
                        m2 += (alpha[t] ^ (uint8_t)s[j]) == di;
                    }
                }
        /* 인접 자리 바꿈 (서로 다른 글자) */
        long s3 = 0, m3 = 0;
        for (int i = 0; i + 1 < L; i++) if (s[i] != s[i + 1]) { s3++; m3++; /* XOR 은 순서를 모른다 */ }
        printf("XOR blind spots on a %d-char GGA body (%d body chars):\n", L, na);
        printf("  1-char substitutions : %ld, undetected %ld\n", s1, m1);
        printf("  2-char substitutions : %ld, undetected %ld (%.3f%%)\n", s2, m2, 100.0 * (double)m2 / (double)s2);
        printf("  adjacent swaps       : %ld, undetected %ld (100%%)\n", s3, m3);
        CHECK(m1 == 0 && s1 == (long)L * (na - 1));
        CHECK(m2 > 0 && m2 * 50 < s2);                       /* 2% 미만이지만 0 은 아니다 */
        CHECK(m3 == s3 && s3 > 40);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
