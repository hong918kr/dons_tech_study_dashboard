/* ALGO-M12 상태 기계 파서: NMEA 문장과 체크섬
 *
 * GNSS 수신기 UART 스트림을 1바이트씩 받는 NMEA 0183 파서 (힙 없음, 재귀 없음, 고정 버퍼):
 *   Part A  nmea_checksum()    '$' 와 '*' 사이 바이트들의 XOR
 *           nmea_feed()        바이트 하나 → 상태 전이(IDLE/BODY/CK1/CK2/CR/LF) → 이벤트 하나
 *   Part B  nmea_split()       제자리(in-place) 필드 분리 — 빈 필드를 지운다/합치지 않는다
 *   Part C  parse_hms_ms()     "hhmmss[.sss]" → UTC 자정 이후 ms
 *           parse_latlon_e7()  "ddmm.mmmm"/"dddmm.mmmm" + N/S/E/W → 1e-7 도 고정소수점 (가장 가까운 값)
 *           nmea_parse_gga()   GGA 필드 15개 → gga_t (시각, 위경도, fix 품질, 위성 수, HDOP, 고도)
 *   (주어진 코드) gps_feed()   DMA/ISR 이 넘긴 바이트 덩어리 → FSM → 필드 분리 → GGA → 최신 위치
 *
 * 테스트: 실제 예시 문장 12개(체크섬 직접 계산), 표 기반 경계 사례, 선언형 참조 스캐너와의 차분 비교,
 *         쓰레기 + 정상 문장 퍼즈(고정 시드 xorshift32): 크래시 없음 / 틀린 체크섬 수락 없음 / 다음 '$' 에서 복구.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NMEA_MAX_BODY_STD 76  /* 82 = '$' + 본문 + "*hh" + CR LF  →  본문 ≤ 76 */
#define NMEA_BODY_CAP     128 /* 본문 버퍼 (NUL 포함). max_body ≤ NMEA_BODY_CAP-1 */
#define NMEA_MAX_FIELDS   24
#define GGA_NFIELDS       15  /* 주소 "xxGGA" + 데이터 14개 */

typedef enum { NS_IDLE, NS_BODY, NS_CK1, NS_CK2, NS_CR, NS_LF } nmea_state_t;

typedef enum {
    NMEA_EV_NONE = 0,      /* 아직 문장 끝이 아님 (IDLE 에서 버린 바이트 포함)        */
    NMEA_EV_SENTENCE,      /* 체크섬이 맞는 완전한 문장. p->body = NUL 종단 본문     */
    NMEA_EV_ERR_CHECKSUM,  /* *hh 와 계산한 XOR 가 다름                             */
    NMEA_EV_ERR_TOOLONG,   /* 본문이 max_body 를 넘음                               */
    NMEA_EV_ERR_TRUNCATED, /* 문장 도중 새 '$' — 앞 문장 버리고 새 문장 시작         */
    NMEA_EV_ERR_BADCHAR,   /* 본문에 인쇄 불가 / 예약 문자                          */
    NMEA_EV_ERR_NOCKSUM,   /* '*' 없이 CR/LF (이 파서는 체크섬 필수)                */
    NMEA_EV_ERR_BADHEX,    /* '*' 뒤가 16진 숫자 2개가 아님                         */
    NMEA_EV_ERR_BADEND,    /* 체크섬 뒤가 CR LF / LF 가 아님                        */
    NMEA_EV__COUNT
} nmea_ev_t;

static const char nmea_ev_letter[NMEA_EV__COUNT] = { '.', 'S', 'C', 'L', 'T', 'B', 'N', 'H', 'E' };
static const char *const nmea_ev_name[NMEA_EV__COUNT] = {
    "NONE", "SENTENCE", "CHECKSUM", "TOOLONG", "TRUNCATED", "BADCHAR", "NOCKSUM", "BADHEX", "BADEND",
};

typedef struct {
    nmea_state_t state;
    uint8_t  max_body;           /* 본문 최대 길이 (표준 76, 고정밀 수신기용으로 늘릴 수 있음) */
    uint8_t  len;                /* body 에 쌓인 글자 수                                       */
    uint8_t  ck;                 /* 지금까지 본문의 XOR                                         */
    uint8_t  ck_rx;              /* 받은 *hh 값                                                 */
    char     body[NMEA_BODY_CAP];
    uint32_t junk;               /* IDLE 에서 '$' 를 기다리며 버린 바이트 수                    */
    uint32_t count[NMEA_EV__COUNT];
} nmea_parser_t;

static void nmea_init(nmea_parser_t *p, uint8_t max_body) {
    memset(p, 0, sizeof *p);
    p->state = NS_IDLE;
    p->max_body = max_body < NMEA_BODY_CAP ? max_body : (uint8_t)(NMEA_BODY_CAP - 1);
}

static int hex_val(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;   /* 소문자도 받는다 (송신 쪽은 대문자) */
    return -1;
}

/* 본문 글자: 인쇄 가능한 ASCII 0x20..0x7E 중 '$' '*' '!' 를 뺀 것.
 * ('$' 는 어느 상태에서든 새 문장, '*' 는 체크섬 시작, '!' 는 캡슐화 문장 시작 문자) */
static bool is_body_char(uint8_t c) {
    return c >= 0x20 && c <= 0x7E && c != '$' && c != '*' && c != '!';
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

/* ---------------- Part A: 체크섬 + 바이트 단위 FSM ---------------- */

/* TODO(A1): s[0..n) 의 XOR 를 돌려준다 ('$' 와 '*' 는 넘기지 않는다고 가정). */
static uint8_t nmea_checksum(const char *s, size_t n) {
    uint8_t x = 0;
    for (size_t i = 0; i < n; i++) x ^= (uint8_t)s[i];
    return x;
}

static nmea_ev_t nmea_finish(nmea_parser_t *p) {
    p->body[p->len] = '\0';
    return p->ck == p->ck_rx ? NMEA_EV_SENTENCE : NMEA_EV_ERR_CHECKSUM;
}

/* TODO(A2): 바이트 c 하나로 상태를 전이하고 이벤트를 돌려준다.
 *   - '$' 는 어느 상태에서든 새 문장 시작 (IDLE 이 아니었으면 TRUNCATED). len, ck 초기화.
 *   - 에러 이벤트(와 SENTENCE)를 낸 뒤 상태는 IDLE. NONE 이 아닌 이벤트는 p->count[ev]++.
 *   - IDLE 에서 '$' 가 아닌 바이트는 p->junk++.
 *   - 본문 max_body 글자까지 저장, 그 다음 본문 글자에서 TOOLONG. 체크섬 비교는 줄 끝(LF)에서.
 *   - nmea_finish() 는 주어진다. */
static nmea_ev_t nmea_feed(nmea_parser_t *p, uint8_t c) {
    nmea_ev_t ev = NMEA_EV_NONE;
    if (c == '$') {                                  /* 재동기: 어느 상태든 새 문장 */
        if (p->state != NS_IDLE) ev = NMEA_EV_ERR_TRUNCATED;
        p->state = NS_BODY;
        p->len = 0;
        p->ck = 0;
        if (ev != NMEA_EV_NONE) p->count[ev]++;
        return ev;
    }
    switch (p->state) {
    case NS_IDLE:
        p->junk++;
        break;
    case NS_BODY:
        if (c == '*') p->state = NS_CK1;
        else if (c == '\r' || c == '\n') ev = NMEA_EV_ERR_NOCKSUM;
        else if (!is_body_char(c)) ev = NMEA_EV_ERR_BADCHAR;
        else if (p->len >= p->max_body) ev = NMEA_EV_ERR_TOOLONG;
        else { p->body[p->len++] = (char)c; p->ck ^= c; }
        break;
    case NS_CK1: {
        int h = hex_val(c);
        if (h < 0) ev = NMEA_EV_ERR_BADHEX;
        else { p->ck_rx = (uint8_t)(h << 4); p->state = NS_CK2; }
        break;
    }
    case NS_CK2: {
        int h = hex_val(c);
        if (h < 0) ev = NMEA_EV_ERR_BADHEX;
        else { p->ck_rx |= (uint8_t)h; p->state = NS_CR; }
        break;
    }
    case NS_CR:
        if (c == '\r') p->state = NS_LF;
        else if (c == '\n') ev = nmea_finish(p);    /* LF 만 쓰는 로그 파일도 받는다 */
        else ev = NMEA_EV_ERR_BADEND;
        break;
    case NS_LF:
        if (c == '\n') ev = nmea_finish(p);
        else ev = NMEA_EV_ERR_BADEND;
        break;
    }
    if (ev != NMEA_EV_NONE) { p->state = NS_IDLE; p->count[ev]++; }
    return ev;
}

/* ---------------- Part B: 필드 분리 ---------------- */

/* TODO(B): body 를 ',' 에서 제자리로 잘라 fields[0..*nf) 에 넣는다. 빈 필드도 하나의 필드.
 * 필드 수 = 쉼표 수 + 1. max_fields 를 넘으면 false (*nf = 0). */
static bool nmea_split(char *body, char *fields[], int max_fields, int *nf) {
    int n = 0;
    char *p = body;
    for (;;) {
        if (n == max_fields) { *nf = 0; return false; }
        fields[n++] = p;
        while (*p != ',' && *p != '\0') p++;
        if (*p == '\0') break;
        *p++ = '\0';
    }
    *nf = n;
    return true;
}

/* ---------------- Part C: GGA ---------------- */

typedef struct {
    bool     has_time;
    uint32_t time_ms;    /* UTC 자정 이후 ms (윤초 23:59:60 허용 → 최대 86,400,999)   */
    bool     has_pos;
    int32_t  lat_e7;     /* 1e-7 도, 북(+)/남(-)                                         */
    int32_t  lon_e7;     /* 1e-7 도, 동(+)/서(-)                                         */
    uint8_t  quality;    /* 0 무효, 1 GPS, 2 DGPS, 4 RTK fixed, 5 RTK float, 6 추측 ... 8 */
    uint8_t  sats;       /* 0..99                                                       */
    bool     has_hdop;
    uint16_t hdop_x100;  /* HDOP × 100                                                  */
    bool     has_alt;
    int32_t  alt_cm;     /* 평균 해수면 고도 (cm)                                        */
} gga_t;

typedef enum {
    GGA_OK = 0, GGA_ERR_NOTGGA, GGA_ERR_FIELDS, GGA_ERR_TIME, GGA_ERR_QUALITY,
    GGA_ERR_LAT, GGA_ERR_LON, GGA_ERR_SATS, GGA_ERR_HDOP, GGA_ERR_ALT, GGA_ERR__COUNT
} gga_err_t;
static const char *const gga_err_name[GGA_ERR__COUNT] = {
    "OK", "NOTGGA", "FIELDS", "TIME", "QUALITY", "LAT", "LON", "SATS", "HDOP", "ALT",
};

/* 주어진 도우미: "[-]d{1,7}[.d+]" → 값 × 10^frac (소수 자리가 더 많으면 버림). */
static bool parse_fixed(const char *s, int frac, bool allow_neg, int32_t *out) {
    bool neg = false;
    if (allow_neg && *s == '-') { neg = true; s++; }
    int64_t v = 0;
    int nd = 0, got = 0;
    while (is_digit(*s)) { if (++nd > 7) return false; v = v * 10 + (*s++ - '0'); }
    if (nd == 0) return false;
    if (*s == '.') {
        s++;
        if (!is_digit(*s)) return false;
        for (; is_digit(*s); s++)
            if (got < frac) { v = v * 10 + (*s - '0'); got++; }
    }
    if (*s != '\0') return false;
    for (; got < frac; got++) v *= 10;
    *out = (int32_t)(neg ? -v : v);
    return true;
}

/* TODO(C1): "hhmmss" + 선택 ".s..." → ms. hh ≤ 23, mm ≤ 59, ss ≤ 60(윤초).
 * 소수는 1자리 이상, 앞 3자리만 쓰고 나머지는 버린다. 형식이 틀리면 false (*out 그대로). */
static bool parse_hms_ms(const char *s, uint32_t *out) {
    for (int i = 0; i < 6; i++)
        if (!is_digit(s[i])) return false;          /* 짧으면 NUL 에서 걸린다 */
    uint32_t hh = (uint32_t)((s[0] - '0') * 10 + (s[1] - '0'));
    uint32_t mm = (uint32_t)((s[2] - '0') * 10 + (s[3] - '0'));
    uint32_t ss = (uint32_t)((s[4] - '0') * 10 + (s[5] - '0'));
    if (hh > 23 || mm > 59 || ss > 60) return false;
    uint32_t ms = 0;
    const char *p = s + 6;
    if (*p == '.') {
        p++;
        if (!is_digit(*p)) return false;
        for (uint32_t scale = 100; is_digit(*p); p++, scale /= 10)
            ms += (uint32_t)(*p - '0') * scale;     /* 4번째 자리부터 scale = 0 → 버림 */
    }
    if (*p != '\0') return false;
    *out = ((hh * 60 + mm) * 60 + ss) * 1000 + ms;
    return true;
}

/* TODO(C2): 위도(deg_digits=2, "N"/"S", ≤ 90°) / 경도(deg_digits=3, "E"/"W", ≤ 180°) → 1e-7 도.
 * 형식: 도 deg_digits 자리 + 분 2자리(00..59) + 선택 "." + 1..8자리. 반구는 정확히 1글자.
 * 값 = 도 + 분/60 을 1e-7 도 단위의 가장 가까운 정수로 (동률이면 0 에서 먼 쪽). 실패 시 false. */
static bool parse_latlon_e7(const char *v, const char *hemi, int deg_digits, int32_t *out) {
    const uint32_t max_deg = deg_digits == 2 ? 90u : 180u;
    const char pos = deg_digits == 2 ? 'N' : 'E', neg = deg_digits == 2 ? 'S' : 'W';
    uint32_t deg = 0, min_int = 0;
    int i = 0;
    for (; i < deg_digits; i++) { if (!is_digit(v[i])) return false; deg = deg * 10 + (uint32_t)(v[i] - '0'); }
    for (; i < deg_digits + 2; i++) { if (!is_digit(v[i])) return false; min_int = min_int * 10 + (uint32_t)(v[i] - '0'); }
    const char *p = v + i;
    uint64_t frac = 0, scale = 1;                    /* 분의 소수부 = frac / scale, scale = 10^k */
    if (*p == '.') {
        p++;
        if (!is_digit(*p)) return false;
        for (; is_digit(*p); p++) {
            if (scale == 100000000u) return false;   /* 9자리 이상 거절 */
            frac = frac * 10 + (uint64_t)(*p - '0');
            scale *= 10;
        }
    }
    if (*p != '\0' || min_int > 59) return false;
    if (deg > max_deg || (deg == max_deg && (min_int != 0 || frac != 0))) return false;
    int sign;
    if (hemi[0] == pos && hemi[1] == '\0') sign = 1;
    else if (hemi[0] == neg && hemi[1] == '\0') sign = -1;
    else return false;
    uint64_t M = (uint64_t)min_int * scale + frac;   /* 단위 10^-k 분 */
    uint64_t D = 60u * scale;                        /* 1도 = 60·10^k 단위 */
    uint64_t fr = (M * 10000000u + D / 2) / D;       /* round(M·10^7 / D), 동률은 올림 */
    uint32_t mag = deg * 10000000u + (uint32_t)fr;   /* ≤ 1,800,000,000 < 2^31 */
    *out = sign > 0 ? (int32_t)mag : -(int32_t)mag;
    return true;
}

/* TODO(C3): GGA 필드 → gga_t. 검사 순서(= 에러 우선순위):
 *   NOTGGA(f[0] 이 대문자 2글자 + "GGA" 가 아님) → FIELDS(n != 15) → TIME(1) → QUALITY(6: 한 자리 0..8)
 *   → LAT(2,3) → LON(4,5) → SATS(7: 1~2자리) → HDOP(8: 비어 있거나 ≤ 99.99) → ALT(9,10: 둘 다 비거나 값 + "M")
 *   - 시각이 비면 has_time=false. 위경도 4필드가 모두 비고 quality==0 이면 has_pos=false (그 외엔 둘 다 유효해야).
 *   - 실패하면 *out 을 0 으로 지운다 (반쯤 채운 값이 새지 않게). parse_fixed() 는 주어진다. */
static gga_err_t nmea_parse_gga(char *const f[], int n, gga_t *out) {
    gga_t g;
    memset(&g, 0, sizeof g);
    memset(out, 0, sizeof *out);
    if (n < 1 || strlen(f[0]) != 5 || f[0][0] < 'A' || f[0][0] > 'Z' || f[0][1] < 'A' || f[0][1] > 'Z' ||
        strcmp(f[0] + 2, "GGA") != 0)
        return GGA_ERR_NOTGGA;
    if (n != GGA_NFIELDS) return GGA_ERR_FIELDS;
    if (f[1][0] != '\0') {
        if (!parse_hms_ms(f[1], &g.time_ms)) return GGA_ERR_TIME;
        g.has_time = true;
    }
    if (!(f[6][0] >= '0' && f[6][0] <= '8' && f[6][1] == '\0')) return GGA_ERR_QUALITY;
    g.quality = (uint8_t)(f[6][0] - '0');
    bool no_pos = f[2][0] == '\0' && f[3][0] == '\0' && f[4][0] == '\0' && f[5][0] == '\0';
    if (!(no_pos && g.quality == 0)) {
        if (!parse_latlon_e7(f[2], f[3], 2, &g.lat_e7)) return GGA_ERR_LAT;
        if (!parse_latlon_e7(f[4], f[5], 3, &g.lon_e7)) return GGA_ERR_LON;
        g.has_pos = true;
    }
    const char *s = f[7];
    if (!(is_digit(s[0]) && (s[1] == '\0' || (is_digit(s[1]) && s[2] == '\0')))) return GGA_ERR_SATS;
    g.sats = (uint8_t)(s[1] == '\0' ? s[0] - '0' : (s[0] - '0') * 10 + (s[1] - '0'));
    if (f[8][0] != '\0') {
        int32_t h;
        if (!parse_fixed(f[8], 2, false, &h) || h > 9999) return GGA_ERR_HDOP;
        g.hdop_x100 = (uint16_t)h;
        g.has_hdop = true;
    }
    if (f[9][0] != '\0' || f[10][0] != '\0') {
        if (!parse_fixed(f[9], 2, true, &g.alt_cm) || strcmp(f[10], "M") != 0) return GGA_ERR_ALT;
        g.has_alt = true;
    }
    *out = g;
    return GGA_OK;
}

/* ---------------- 주어진 코드: GPS 태스크 쪽 접착 코드 ---------------- */

typedef struct {
    nmea_parser_t p;
    gga_t    fix;                                   /* 마지막으로 성공한 GGA */
    uint32_t n_gga, n_gga_err, n_other, n_split_err;
} gps_t;

static void gps_init(gps_t *g, uint8_t max_body) {
    memset(g, 0, sizeof *g);
    nmea_init(&g->p, max_body);
}

/* DMA half/full-transfer 인터럽트나 RX 링 버퍼에서 꺼낸 바이트 덩어리를 그대로 넣는다. */
static void gps_feed(gps_t *g, const uint8_t *buf, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (nmea_feed(&g->p, buf[i]) != NMEA_EV_SENTENCE) continue;
        char *f[NMEA_MAX_FIELDS];
        int nf;
        if (!nmea_split(g->p.body, f, NMEA_MAX_FIELDS, &nf)) { g->n_split_err++; continue; }
        if (strlen(f[0]) == 5 && strcmp(f[0] + 2, "GGA") == 0) {
            gga_t t;
            if (nmea_parse_gga(f, nf, &t) == GGA_OK) { g->fix = t; g->n_gga++; }
            else g->n_gga_err++;
        } else {
            g->n_other++;
        }
    }
}

#ifndef ALGO_M12_NO_MAIN /* demo_bench.c / demo_m4.c 가 이 파일을 include 할 때는 하네스를 뺀다 */
/* ======================= 테스트 하네스 ======================= */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng_state = 0x2026A12Cu;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}
static uint32_t rnd(uint32_t n) { return xorshift32() % n; }

/* 실제 예시 문장 (체크섬은 이 레슨에서 직접 계산해 *hh 와 일치 확인). [11] 은 본문 83자 (> 76). */
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
    "$GPGGA,172814.0,3723.46587704,N,12202.26957864,W,2,6,1.2,18.893,M,-25.669,M,2.0,0031*4F",
};
#define N_SAMPLES ((int)(sizeof k_samples / sizeof k_samples[0]))
#define TRIMBLE   11

/* 문장 하나를 "\r\n" 붙여 처음부터 넣고, NONE 이 아닌 이벤트를 글자로 이어 붙여 돌려준다. */
static void feed_str(nmea_parser_t *p, const char *s, size_t n, char *evs, size_t cap) {
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        nmea_ev_t e = nmea_feed(p, (uint8_t)s[i]);
        if ((unsigned)e < NMEA_EV__COUNT && e != NMEA_EV_NONE && k + 1 < cap) evs[k++] = nmea_ev_letter[e];
    }
    evs[k] = '\0';
}

/* ---- 선언형 참조 스캐너 (테스트 쪽): 버퍼 전체를 보고 수락될 문장을 정규식처럼 찾는다 ----
 * '$' 다음 본문 글자 L개 (L ≤ max_body, 그다음 글자가 본문 글자가 아님) → '*' → 16진 2개 → "\r\n" 또는 "\n",
 * 그리고 XOR == hh. 수락 위치 = 마지막 '\n' 의 인덱스. FSM 과 독립적으로 작성. */
#define REF_MAX 4096
typedef struct { size_t end; char body[NMEA_BODY_CAP]; } ref_hit_t;
static ref_hit_t g_ref[REF_MAX], g_got[REF_MAX];

static int ref_scan(const uint8_t *b, size_t n, int max_body, ref_hit_t *out, int cap) {
    int k = 0;
    for (size_t i = 0; i < n; i++) {
        if (b[i] != '$') continue;
        size_t j = i + 1;
        int L = 0;
        while (j < n && is_body_char(b[j]) && L <= max_body) { j++; L++; }
        if (L > max_body || j + 3 >= n || b[j] != '*') continue;
        int h1 = hex_val(b[j + 1]), h2 = hex_val(b[j + 2]);
        if (h1 < 0 || h2 < 0) continue;
        size_t e;
        if (j + 3 < n && b[j + 3] == '\n') e = j + 3;
        else if (j + 4 < n && b[j + 3] == '\r' && b[j + 4] == '\n') e = j + 4;
        else continue;
        uint8_t x = 0;
        for (size_t t = i + 1; t < i + 1 + (size_t)L; t++) x ^= b[t];
        if (x != (uint8_t)(h1 * 16 + h2)) continue;
        if (k < cap) { out[k].end = e; memcpy(out[k].body, b + i + 1, (size_t)L); out[k].body[L] = '\0'; k++; }
    }
    return k;
}

/* 수락된 문장이 원래 스트림에서 정말 "$body*hh(\r)\n" 이고 hh == XOR(body) 인지 원본 바이트로 확인. */
static bool verify_accept(const uint8_t *b, size_t end, const char *body) {
    size_t L = strlen(body);
    size_t tail = (end >= 1 && b[end] == '\n') ? ((end >= 1 && b[end - 1] == '\r') ? 2 : 1) : 0;
    if (tail == 0 || end + 1 < L + 4 + tail) return false;
    size_t star = end + 1 - tail - 3;                /* '*' 위치 */
    if (b[star] != '*' || star < L + 1 || b[star - L - 1] != '$') return false;
    if (memcmp(b + star - L, body, L) != 0) return false;
    int h1 = hex_val(b[star + 1]), h2 = hex_val(b[star + 2]);
    if (h1 < 0 || h2 < 0) return false;
    uint8_t x = 0;
    for (size_t t = 0; t < L; t++) x ^= (uint8_t)body[t];
    return x == (uint8_t)(h1 * 16 + h2);
}

/* 무작위 본문: 주소 5글자 + ',' 가 잦은 데이터 */
static int rand_body(char *dst, int L) {
    static const char cs[] = "0123456789.,,,,ABCDEFGHNSEWMKTVabcxyz -+/#%&()";
    for (int i = 0; i < L; i++) dst[i] = i < 5 ? (char)('A' + rnd(26)) : cs[rnd((uint32_t)(sizeof cs - 1))];
    dst[L] = '\0';
    return L;
}

/* 정상 문장 하나를 dst 에 쓴다 (CRLF 또는 LF, 대/소문자 hex). 길이 반환. */
static size_t make_sentence(uint8_t *dst, const char *body, bool lf_only, bool lower) {
    size_t L = strlen(body), k = 0;
    dst[k++] = '$';
    memcpy(dst + k, body, L); k += L;
    uint8_t x = 0;                                   /* 테스트 쪽 XOR (nmea_checksum 에 의존하지 않음) */
    for (size_t i = 0; i < L; i++) x ^= (uint8_t)body[i];
    static const char HU[] = "0123456789ABCDEF", HL[] = "0123456789abcdef";
    const char *H = lower ? HL : HU;
    dst[k++] = '*'; dst[k++] = (uint8_t)H[x >> 4]; dst[k++] = (uint8_t)H[x & 15];
    if (!lf_only) dst[k++] = '\r';
    dst[k++] = '\n';
    return k;
}

/* 쓰레기 바이트: mode 0 = 아무 바이트, 1 = NMEA 구분 문자 위주 토큰 수프, 2 = 예시 문장을 변이 */
static size_t make_garbage(uint8_t *dst, size_t cap, int mode) {
    size_t n = rnd((uint32_t)cap);
    if (mode == 0) {
        for (size_t i = 0; i < n; i++) dst[i] = (uint8_t)rnd(256);
    } else if (mode == 1) {
        static const char soup[] = "$$**,,,\r\n\r\n0123456789ABCDEFabcdefGPGGA.NSEW!\x07\xff";
        for (size_t i = 0; i < n; i++) dst[i] = (uint8_t)soup[rnd((uint32_t)(sizeof soup - 1))];
    } else {
        const char *s = k_samples[rnd((uint32_t)N_SAMPLES)];
        size_t L = strlen(s);
        if (n > L + 2) n = L + 2;
        memcpy(dst, s, n < L ? n : L);
        if (n > L) { dst[L] = '\r'; if (n > L + 1) dst[L + 1] = '\n'; }
        for (int m = (int)rnd(4); m >= 0 && n > 0; m--) {
            size_t at = rnd((uint32_t)n);
            switch (rnd(3)) {
            case 0: dst[at] ^= (uint8_t)(1u << rnd(8)); break;           /* 비트 하나 뒤집기 */
            case 1: dst[at] = (uint8_t)rnd(256); break;                  /* 바이트 바꾸기   */
            default: memmove(dst + at, dst + at + 1, n - at - 1); n--; break; /* 바이트 빠짐 */
            }
        }
    }
    return n;
}

#define STREAM_FILL 16000
static uint8_t g_stream[1 << 15];

int main(void) {
    char evs[64];
    nmea_parser_t P;

    /* ---------- Part A: 체크섬 (실제 예시 문장) ---------- */
    {
        int ok = 0;
        for (int i = 0; i < N_SAMPLES; i++) {
            const char *s = k_samples[i];
            const char *star = strchr(s, '*');
            uint8_t want = (uint8_t)(hex_val((uint8_t)star[1]) * 16 + hex_val((uint8_t)star[2]));
            uint8_t got = nmea_checksum(s + 1, (size_t)(star - s - 1));
            if (got == want) ok++;
            else printf("  checksum %s: got %02X want %02X\n", s, got, want);
        }
        printf("sample checksums: %d/%d match the published *hh\n", ok, N_SAMPLES);
        CHECK(ok == N_SAMPLES);
        CHECK(nmea_checksum("", 0) == 0x00);
        CHECK(nmea_checksum("GPGLL,4916.45,N,12311.12,W,225444,A", 35) == 0x31);
    }

    /* ---------- Part A: FSM — 예시 문장 + CRLF ---------- */
    {
        int ok = 0;
        for (int i = 0; i < N_SAMPLES; i++) {
            uint8_t buf[160];
            size_t n = strlen(k_samples[i]);
            memcpy(buf, k_samples[i], n); buf[n++] = '\r'; buf[n++] = '\n';
            nmea_init(&P, NMEA_MAX_BODY_STD);
            feed_str(&P, (const char *)buf, n, evs, sizeof evs);
            const char *want = i == TRIMBLE ? "L" : "S";
            size_t bl = (size_t)(strchr(k_samples[i], '*') - k_samples[i] - 1);
            bool body_ok = i == TRIMBLE || (strlen(P.body) == bl && memcmp(P.body, k_samples[i] + 1, bl) == 0);
            if (strcmp(evs, want) == 0 && body_ok) ok++;
            else printf("  sample %d: events \"%s\" want \"%s\"\n", i, evs, want);
        }
        CHECK(ok == N_SAMPLES);
        /* 고정밀 수신기 문장: 본문 83자 → 한도를 100 으로 올리면 통과 */
        nmea_init(&P, 100);
        feed_str(&P, k_samples[TRIMBLE], strlen(k_samples[TRIMBLE]), evs, sizeof evs);
        feed_str(&P, "\r\n", 2, evs + strlen(evs), sizeof evs - strlen(evs));
        CHECK(strcmp(evs, "S") == 0 && strlen(P.body) == 83);
    }

    /* ---------- Part A: 표 기반 경계 사례 ---------- */
    {
        static char b76[128], b77[128], over[160];
        /* 본문 정확히 76자 / 77자 (XOR 은 테스트 쪽에서 계산) */
        memset(b76, 'A', 76); b76[76] = '\0';
        memset(b77, 'A', 77); b77[77] = '\0';
        uint8_t s76[128], s77[128];
        size_t n76 = make_sentence(s76, b76, false, false), n77 = make_sentence(s77, b77, false, false);
        memset(over, 'Z', 100);
        memcpy(over, "$", 1);
        over[100] = '\0';

        static const char GLL[] = "$GPGLL,4916.45,N,12311.12,W,225444,A*31\r\n";
        struct { const char *in; size_t n; const char *want; const char *what; } T[] = {
            { GLL, 0, "S", "CRLF" },
            { "$GPGLL,4916.45,N,12311.12,W,225444,A*31\n", 0, "S", "LF only" },
            { "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6a\r\n", 0, "S", "lower-case hex" },
            { "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48\r\n", 0, "C", "wrong checksum" },
            { "$GPGLL,4916.45,N,12311.12,W,225444,A\r\n", 0, "N", "no checksum" },
            { "$GPGGA,1235$GPGLL,4916.45,N,12311.12,W,225444,A*31\r\n", 0, "TS", "truncated, then recover" },
            { "\xff\x01junk*,,\r\n$GPGLL,4916.45,N,12311.12,W,225444,A*31\r\n", 0, "S", "garbage first" },
            { "$GPGLL,4916.45,N,12311.12,W,225444,A*3\r\n", 0, "H", "one hex digit" },
            { "$GPGLL,4916.45,N,12311.12,W,225444,A*3G\r\n", 0, "H", "non-hex digit" },
            { "$GPGLL,4916.45,N,12311.12,W,225444,A*31x\r\n", 0, "E", "junk after checksum" },
            { "$GPGLL,4916.45,N,12311.12,W,225444,A*31\r\r\n", 0, "E", "CR CR LF" },
            { "$GPGLL,4916.45,N,12311.12,W,225444,A*31\r$GPGLL,4916.45,N,12311.12,W,225444,A*31\r\n", 0, "TS", "CR then '$'" },
            { "$GPGLL,4916.45,N\x07,12311.12,W,225444,A*31\r\n", 0, "B", "control char" },
            { "$GPGLL,4916.45,N!,12311.12,W,225444,A*31\r\n", 0, "B", "'!' in body" },
            { "$GPGLL,4916.45,N\xb0,12311.12,W,225444,A*31\r\n", 0, "B", "8-bit byte" },
            { "$$", 0, "T", "'$' '$'" },
            { "$*00\r\n", 0, "S", "empty body (framing ok)" },
            { "$GPGLL,4916.45,N,12311.12,W,225444,A*31\r\n\r\n", 0, "S", "extra CRLF = junk" },
            { "$GPGLL,4916.45,N,12311.12,W,225444,A*31\r\n$GPGLL,4916.45,N,12311.12,W,225444,A*31\n", 0, "SS", "back to back" },
            { "*47\r\n,,,\r\n", 0, "", "no '$' at all" },
            { (const char *)s76, n76, "S", "body 76" },
            { (const char *)s77, n77, "L", "body 77" },
            { over, 0, "L", "overlong, no end" },
        };
        int nt = (int)(sizeof T / sizeof T[0]), ok = 0;
        for (int i = 0; i < nt; i++) {
            size_t n = T[i].n ? T[i].n : strlen(T[i].in);
            nmea_init(&P, NMEA_MAX_BODY_STD);
            feed_str(&P, T[i].in, n, evs, sizeof evs);
            if (strcmp(evs, T[i].want) == 0) ok++;
            else printf("  case %-26s events \"%s\" want \"%s\"\n", T[i].what, evs, T[i].want);
        }
        printf("FSM table: %d/%d cases ok\n", ok, nt);
        CHECK(ok == nt);
        /* 몇 가지는 내용까지 */
        nmea_init(&P, NMEA_MAX_BODY_STD);
        feed_str(&P, T[6].in, strlen(T[6].in), evs, sizeof evs);
        CHECK(P.junk == 11 && strcmp(P.body, "GPGLL,4916.45,N,12311.12,W,225444,A") == 0);
        nmea_init(&P, NMEA_MAX_BODY_STD);
        feed_str(&P, T[5].in, strlen(T[5].in), evs, sizeof evs);
        CHECK(P.count[NMEA_EV_ERR_TRUNCATED] == 1 && P.count[NMEA_EV_SENTENCE] == 1 && P.state == NS_IDLE);
        nmea_init(&P, NMEA_MAX_BODY_STD);
        feed_str(&P, "$*00\r\n", 6, evs, sizeof evs);
        CHECK(P.body[0] == '\0' && P.ck_rx == 0);
        /* over + 정상 문장 → TOOLONG 다음 복구 */
        nmea_init(&P, NMEA_MAX_BODY_STD);
        feed_str(&P, over, strlen(over), evs, sizeof evs);
        feed_str(&P, GLL, strlen(GLL), evs, sizeof evs);
        CHECK(strcmp(evs, "S") == 0 && P.count[NMEA_EV_ERR_TOOLONG] == 1);
        /* max_body 는 버퍼 크기로 잘린다 */
        nmea_init(&P, 255);
        CHECK(P.max_body == NMEA_BODY_CAP - 1);
    }

    /* ---------- Part A: 참조 스캐너와 차분 비교 (무작위 스트림) ---------- */
    {
        long streams = 0, sentences = 0, bad = 0, valid_pieces = 0;
        const int maxes[3] = { NMEA_MAX_BODY_STD, 20, NMEA_BODY_CAP - 1 };
        for (int s = 0; s < 1500; s++) {
            size_t n = 0;
            int mb = maxes[s % 3];
            while (n < STREAM_FILL) {
                char body[NMEA_BODY_CAP + 16];
                uint32_t kind = rnd(10);
                if (kind <= 5) {                                   /* 정상 (가끔 한도 근처 길이) */
                    int L = (int)rnd((uint32_t)mb + 1);
                    if (rnd(4) == 0) L = mb - (int)rnd(2 < mb ? 2 : 1);
                    rand_body(body, L);
                    n += make_sentence(g_stream + n, body, rnd(3) == 0, rnd(4) == 0);
                    valid_pieces++;
                } else if (kind == 6) {                            /* 비트 하나 뒤집힘 */
                    size_t L = make_sentence(g_stream + n, (rand_body(body, (int)rnd((uint32_t)mb + 1)), body), false, false);
                    g_stream[n + rnd((uint32_t)L)] ^= (uint8_t)(1u << rnd(8));
                    n += L;
                } else if (kind == 7) {                            /* 잘림 */
                    size_t L = make_sentence(g_stream + n, (rand_body(body, (int)rnd((uint32_t)mb + 1)), body), false, false);
                    n += rnd((uint32_t)L);
                } else if (kind == 8) {                            /* 한도 초과 */
                    int L = mb + 1 + (int)rnd(10);
                    if (L > NMEA_BODY_CAP + 10) L = NMEA_BODY_CAP + 10;
                    rand_body(body, L);
                    n += make_sentence(g_stream + n, body, false, false);
                } else {                                           /* 쓰레기 */
                    n += make_garbage(g_stream + n, 40, (int)rnd(3));
                }
            }
            nmea_init(&P, (uint8_t)mb);
            int ng = 0;
            for (size_t i = 0; i < n; i++)
                if (nmea_feed(&P, g_stream[i]) == NMEA_EV_SENTENCE && ng < REF_MAX) {
                    g_got[ng].end = i;
                    size_t L = strlen(P.body);
                    memcpy(g_got[ng].body, P.body, L); g_got[ng].body[L] = '\0';
                    ng++;
                }
            int nr = ref_scan(g_stream, n, mb, g_ref, REF_MAX);
            bool same = ng == nr;
            for (int i = 0; same && i < ng; i++)
                same = g_got[i].end == g_ref[i].end && strcmp(g_got[i].body, g_ref[i].body) == 0;
            if (!same) { if (bad < 3) printf("  stream %d: fsm %d vs ref %d sentences\n", s, ng, nr); bad++; }
            streams++;
            sentences += nr;
        }
        printf("differential: %ld streams, %ld accepted sentences (%ld valid pieces), mismatches %ld\n",
               streams, sentences, valid_pieces, bad);
        CHECK(bad == 0 && sentences >= valid_pieces);
    }

    /* ---------- Part B: 필드 분리 ---------- */
    {
        char *f[NMEA_MAX_FIELDS];
        int nf = -1;
        char a[] = "GPGGA,123519,,N";
        CHECK(nmea_split(a, f, NMEA_MAX_FIELDS, &nf) && nf == 4 && strcmp(f[0], "GPGGA") == 0 &&
              strcmp(f[1], "123519") == 0 && f[2][0] == '\0' && strcmp(f[3], "N") == 0);
        char e[] = "";
        CHECK(nmea_split(e, f, NMEA_MAX_FIELDS, &nf) && nf == 1 && f[0][0] == '\0');
        char c1[] = ",";
        CHECK(nmea_split(c1, f, NMEA_MAX_FIELDS, &nf) && nf == 2 && f[0][0] == '\0' && f[1][0] == '\0');
        char c3[] = "a,";
        CHECK(nmea_split(c3, f, NMEA_MAX_FIELDS, &nf) && nf == 2 && strcmp(f[0], "a") == 0 && f[1][0] == '\0');
        char m4[] = "a,b,c,d";
        CHECK(nmea_split(m4, f, 4, &nf) && nf == 4 && strcmp(f[3], "d") == 0);
        char m5[] = "a,b,c,d,e";
        nf = 7;
        CHECK(!nmea_split(m5, f, 4, &nf) && nf == 0);
        /* 예시 GGA 들은 필드 15개 */
        int ok = 0, tot = 0;
        for (int i = 0; i < N_SAMPLES; i++) {
            if (strncmp(k_samples[i] + 3, "GGA", 3) != 0) continue;
            char buf[128];
            size_t bl = (size_t)(strchr(k_samples[i], '*') - k_samples[i] - 1);
            memcpy(buf, k_samples[i] + 1, bl); buf[bl] = '\0';
            tot++;
            if (nmea_split(buf, f, NMEA_MAX_FIELDS, &nf) && nf == GGA_NFIELDS) ok++;
        }
        CHECK(tot == 5 && ok == 5);
        /* 전수: {'a', ','} 로 만든 길이 0..14 문자열 32767개, max_fields 8 */
        long cnt = 0, wrong = 0;
        for (int len = 0; len <= 14; len++)
            for (uint32_t m = 0; m < (1u << len); m++) {
                char s[16], orig[16], re[32];
                int commas = 0;
                for (int i = 0; i < len; i++) { s[i] = (m >> i) & 1 ? ',' : 'a'; commas += s[i] == ','; }
                s[len] = '\0';
                memcpy(orig, s, (size_t)len + 1);
                bool r = nmea_split(s, f, 8, &nf);
                bool exp_ok = commas + 1 <= 8;
                bool good = r == exp_ok;
                if (good && r) {                          /* 다시 이어 붙이면 원래 문자열 */
                    good = nf == commas + 1;
                    size_t k = 0;
                    for (int i = 0; good && i < nf; i++) {
                        size_t l = strlen(f[i]);
                        if (i) re[k++] = ',';
                        memcpy(re + k, f[i], l); k += l;
                    }
                    re[k] = '\0';
                    good = good && strcmp(re, orig) == 0;
                }
                wrong += !good;
                cnt++;
            }
        printf("split exhaustive: %ld strings, %ld wrong\n", cnt, wrong);
        CHECK(cnt == 32767 && wrong == 0);
    }

    /* ---------- Part B: XOR 체크섬의 성질 (잡는 것 / 못 잡는 것) ---------- */
    {
        /* 한 글자 치환은 항상 잡는다: 모든 위치 × 다른 본문 글자 전부 */
        const char *s = k_samples[0] + 1;
        size_t L = (size_t)(strchr(k_samples[0], '*') - k_samples[0] - 1);
        uint8_t base = nmea_checksum(s, L);
        long subs = 0, missed = 0;
        char t[128];
        for (size_t i = 0; i < L; i++)
            for (int c = 0x20; c <= 0x7E; c++) {
                if (!is_body_char((uint8_t)c) || c == s[i]) continue;
                memcpy(t, s, L); t[i] = (char)c;
                subs++;
                missed += nmea_checksum(t, L) == base;
            }
        printf("single-char substitutions: %ld tried, %ld undetected\n", subs, missed);
        CHECK(subs > 5000 && missed == 0);
        /* 순서 바꿈은 절대 못 잡는다: 무작위 순열 1000개 */
        long same = 0;
        for (int r = 0; r < 1000; r++) {
            memcpy(t, s, L);
            for (size_t i = L - 1; i > 0; i--) { size_t j = rnd((uint32_t)(i + 1)); char x = t[i]; t[i] = t[j]; t[j] = x; }
            same += nmea_checksum(t, L) == base;
        }
        CHECK(same == 1000);
        /* "4807" → "4870" (인접 숫자 자리 바뀜) 도 체크섬 통과 */
        memcpy(t, s, L); t[15] = '7'; t[16] = '0';
        CHECK(memcmp(t + 13, "4870", 4) == 0 && nmea_checksum(t, L) == base);
    }

    /* ---------- Part C: 시각 ---------- */
    {
        uint32_t ms = 7;
        CHECK(parse_hms_ms("123519", &ms) && ms == 45319000u);
        CHECK(parse_hms_ms("092750.000", &ms) && ms == 34070000u);
        CHECK(parse_hms_ms("092725.00", &ms) && ms == 34045000u);
        CHECK(parse_hms_ms("172814.0", &ms) && ms == 62894000u);
        CHECK(parse_hms_ms("235959.999", &ms) && ms == 86399999u);
        CHECK(parse_hms_ms("235959.9999", &ms) && ms == 86399999u);    /* 4번째 소수 자리 버림 */
        CHECK(parse_hms_ms("235960", &ms) && ms == 86400000u);         /* 윤초 */
        ms = 7;
        CHECK(!parse_hms_ms("240000", &ms) && !parse_hms_ms("126000", &ms) && !parse_hms_ms("123561", &ms) && ms == 7);
        CHECK(!parse_hms_ms("12351", &ms) && !parse_hms_ms("1235190", &ms) && !parse_hms_ms("123519.", &ms));
        CHECK(!parse_hms_ms("12a519", &ms) && !parse_hms_ms("", &ms) && !parse_hms_ms("123519.5x", &ms) && ms == 7);
        /* 전수: hh,mm,ss ∈ 0..99 (100만 개) */
        long wrong = 0;
        for (uint32_t h = 0; h < 100; h++)
            for (uint32_t m = 0; m < 100; m++)
                for (uint32_t sct = 0; sct < 100; sct++) {
                    char b[8] = { (char)('0' + h / 10), (char)('0' + h % 10), (char)('0' + m / 10),
                                  (char)('0' + m % 10), (char)('0' + sct / 10), (char)('0' + sct % 10), 0, 0 };
                    uint32_t got = 0xFFFFFFFFu;
                    bool r = parse_hms_ms(b, &got);
                    bool exp = h < 24 && m < 60 && sct <= 60;
                    if (r != exp || (r && got != ((h * 60 + m) * 60 + sct) * 1000)) wrong++;
                }
        CHECK(wrong == 0);
    }

    /* ---------- Part C: 위도/경도 → 1e-7 도 ---------- */
    {
        int32_t v = 12345;
        CHECK(parse_latlon_e7("4807.038", "N", 2, &v) && v == 481173000);
        CHECK(parse_latlon_e7("01131.000", "E", 3, &v) && v == 115166667);
        CHECK(parse_latlon_e7("5321.6802", "N", 2, &v) && v == 533613367);
        CHECK(parse_latlon_e7("00630.3372", "W", 3, &v) && v == -65056200);
        CHECK(parse_latlon_e7("12202.26957864", "W", 3, &v) && v == -1220378263);
        CHECK(parse_latlon_e7("4807", "N", 2, &v) && v == 481166667);
        CHECK(parse_latlon_e7("9000.0000", "N", 2, &v) && v == 900000000);
        CHECK(parse_latlon_e7("8959.99999999", "N", 2, &v) && v == 900000000);   /* 반올림으로 90° */
        CHECK(parse_latlon_e7("18000.0000", "W", 3, &v) && v == -1800000000);
        CHECK(parse_latlon_e7("0000.000003", "N", 2, &v) && v == 1);             /* 정확히 0.5 → 1 */
        CHECK(parse_latlon_e7("0000.000003", "S", 2, &v) && v == -1);
        v = 12345;
        CHECK(!parse_latlon_e7("9000.0001", "N", 2, &v) && !parse_latlon_e7("18000.00000001", "E", 3, &v));
        CHECK(!parse_latlon_e7("0060.000", "N", 2, &v) && !parse_latlon_e7("4807.038", "E", 2, &v));
        CHECK(!parse_latlon_e7("4807.038", "", 2, &v) && !parse_latlon_e7("4807.038", "NN", 2, &v));
        CHECK(!parse_latlon_e7("4807.", "N", 2, &v) && !parse_latlon_e7("480.7038", "N", 2, &v));
        CHECK(!parse_latlon_e7("4807.123456789", "N", 2, &v) && !parse_latlon_e7("", "N", 2, &v));
        CHECK(!parse_latlon_e7("1131.000", "E", 3, &v) && !parse_latlon_e7("-4807.038", "N", 2, &v) && v == 12345);
        /* 무작위 200,000개: 정의(가장 가까운 정수)를 정수로 직접 확인 + double 참조와 비교 */
        long wrong = 0, dbl_diff = 0;
        for (int r = 0; r < 200000; r++) {
            bool lat = r & 1;
            int dd = lat ? 2 : 3;
            uint32_t maxd = lat ? 90 : 180;
            uint32_t deg = rnd(maxd), mi = rnd(60), k = rnd(9);
            uint64_t sc = 1;
            for (uint32_t i = 0; i < k; i++) sc *= 10;
            uint64_t fr = ((uint64_t)xorshift32() << 32 | xorshift32()) % sc;
            char b[32], hemi[2] = { lat ? (rnd(2) ? 'N' : 'S') : (rnd(2) ? 'E' : 'W'), 0 };
            if (k == 0) snprintf(b, sizeof b, "%0*u%02u", dd, deg, mi);
            else snprintf(b, sizeof b, "%0*u%02u.%0*llu", dd, deg, mi, (int)k, (unsigned long long)fr);
            int32_t got = 0;
            if (!parse_latlon_e7(b, hemi, dd, &got)) { wrong++; continue; }
            int64_t mag = got < 0 ? -(int64_t)got : got;
            int64_t rem = mag - (int64_t)deg * 10000000;          /* 분 기여분 (1e-7 도) */
            int64_t M = (int64_t)mi * (int64_t)sc + (int64_t)fr, D = 60 * (int64_t)sc;
            int64_t err2 = 2 * (rem * D - M * 10000000);         /* |2(r·D − M·10^7)| ≤ D ⇔ 가장 가까움 */
            if (err2 < 0) err2 = -err2;
            bool neg = hemi[0] == 'S' || hemi[0] == 'W';
            if (err2 > D || ((got < 0) != (neg && mag != 0))) wrong++;
            double dv = (deg + (double)M / (double)D) * 1e7;
            long long ref = llround(dv);
            if (ref != mag) dbl_diff++;
            if (llabs(ref - mag) > 1) wrong++;
        }
        printf("lat/lon random: 200000 parsed, %ld wrong, %ld differ from double llround by 1\n", wrong, dbl_diff);
        CHECK(wrong == 0);
    }

    /* ---------- Part C: GGA ---------- */
    {
        struct { int idx; uint8_t mb; gga_t want; } G[] = {
            { 0, 76, { true, 45319000, true, 481173000, 115166667, 1, 8, true, 90, true, 54540 } },
            { 4, 76, { true, 34070000, true, 533613367, -65056200, 1, 8, true, 103, true, 6170 } },
            { 5, 76, { true, 34045000, true, 472852332, 85652650, 1, 8, true, 101, true, 49960 } },
            { 6, 76, { false, 0, false, 0, 0, 0, 0, true, 9999, false, 0 } },
            { TRIMBLE, 100, { true, 62894000, true, 373910980, -1220378263, 2, 6, true, 120, true, 1889 } },
        };
        int ok = 0, ng = (int)(sizeof G / sizeof G[0]);
        for (int i = 0; i < ng; i++) {
            nmea_init(&P, G[i].mb);
            feed_str(&P, k_samples[G[i].idx], strlen(k_samples[G[i].idx]), evs, sizeof evs);
            feed_str(&P, "\r\n", 2, evs, sizeof evs);
            char *f[NMEA_MAX_FIELDS];
            int nf = 0;
            gga_t g;
            memset(&g, 0x5A, sizeof g);
            bool r = strcmp(evs, "S") == 0 && nmea_split(P.body, f, NMEA_MAX_FIELDS, &nf) &&
                     nmea_parse_gga(f, nf, &g) == GGA_OK;
            const gga_t *w = &G[i].want;
            r = r && g.has_time == w->has_time && g.time_ms == w->time_ms && g.has_pos == w->has_pos &&
                g.lat_e7 == w->lat_e7 && g.lon_e7 == w->lon_e7 && g.quality == w->quality && g.sats == w->sats &&
                g.has_hdop == w->has_hdop && g.hdop_x100 == w->hdop_x100 && g.has_alt == w->has_alt &&
                g.alt_cm == w->alt_cm;
            if (r) ok++;
            else printf("  GGA sample %d mismatch (lat %d lon %d t %u)\n", G[i].idx, g.lat_e7, g.lon_e7, g.time_ms);
        }
        CHECK(ok == ng);
        /* 에러 사례: 필드 문자열을 직접 분리 (체크섬 계층과 독립) */
        struct { const char *body; gga_err_t want; } E[] = {
            { "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,", GGA_ERR_FIELDS },
            { "GPRMC,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", GGA_ERR_NOTGGA },
            { "gpGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", GGA_ERR_NOTGGA },
            { "GPGGAX,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", GGA_ERR_NOTGGA },
            { "GPGGA,1235x9,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", GGA_ERR_TIME },
            { "GPGGA,123519,4807.038,N,01131.000,E,9,08,0.9,545.4,M,46.9,M,,", GGA_ERR_QUALITY },
            { "GPGGA,123519,4807.038,N,01131.000,E,,08,0.9,545.4,M,46.9,M,,", GGA_ERR_QUALITY },
            { "GPGGA,123519,,,,,1,08,0.9,545.4,M,46.9,M,,", GGA_ERR_LAT },
            { "GPGGA,123519,4807.038,N,18100.000,E,1,08,0.9,545.4,M,46.9,M,,", GGA_ERR_LON },
            { "GPGGA,123519,4807.038,N,01131.000,,1,08,0.9,545.4,M,46.9,M,,", GGA_ERR_LON },
            { "GPGGA,123519,4807.038,N,01131.000,E,1,100,0.9,545.4,M,46.9,M,,", GGA_ERR_SATS },
            { "GPGGA,123519,4807.038,N,01131.000,E,1,,0.9,545.4,M,46.9,M,,", GGA_ERR_SATS },
            { "GPGGA,123519,4807.038,N,01131.000,E,1,08,abc,545.4,M,46.9,M,,", GGA_ERR_HDOP },
            { "GPGGA,123519,4807.038,N,01131.000,E,1,08,100.0,545.4,M,46.9,M,,", GGA_ERR_HDOP },
            { "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,F,46.9,M,,", GGA_ERR_ALT },
            { "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,,M,46.9,M,,", GGA_ERR_ALT },
            { "GPGGA,1235x9,,,,,1,100,abc,,F,,,,", GGA_ERR_TIME },          /* 여러 개면 왼쪽(우선순위) 먼저 */
            { "GNGGA,123519,4807.038,S,01131.000,W,4,12,0.5,-12.3,M,46.9,M,1.0,0001", GGA_OK },
        };
        int ne = (int)(sizeof E / sizeof E[0]), eok = 0;
        for (int i = 0; i < ne; i++) {
            char buf[128];
            snprintf(buf, sizeof buf, "%s", E[i].body);
            char *f[NMEA_MAX_FIELDS];
            int nf = 0;
            gga_t g;
            memset(&g, 0x5A, sizeof g);
            gga_err_t r = nmea_split(buf, f, NMEA_MAX_FIELDS, &nf) ? nmea_parse_gga(f, nf, &g) : GGA_ERR__COUNT;
            bool zero_on_err = true;
            if (r != GGA_OK) { gga_t z; memset(&z, 0, sizeof z); zero_on_err = memcmp(&g, &z, sizeof g) == 0; }
            if (r == E[i].want && zero_on_err) eok++;
            else printf("  GGA case %d: got %s want %s\n", i,
                        (unsigned)r < GGA_ERR__COUNT ? gga_err_name[r] : "?", gga_err_name[E[i].want]);
        }
        printf("GGA error table: %d/%d cases ok\n", eok, ne);
        CHECK(eok == ne);
        char sbuf[] = "GNGGA,123519,4807.038,S,01131.000,W,4,12,0.5,-12.3,M,46.9,M,1.0,0001";
        char *f[NMEA_MAX_FIELDS];
        int nf = 0;
        gga_t g;
        CHECK(nmea_split(sbuf, f, NMEA_MAX_FIELDS, &nf) && nmea_parse_gga(f, nf, &g) == GGA_OK &&
              g.lat_e7 == -481173000 && g.lon_e7 == -115166667 && g.quality == 4 && g.sats == 12 &&
              g.alt_cm == -1230 && g.hdop_x100 == 50);
    }

    /* ---------- Part C: GPS 로그 스트림 (쓰레기 주입) + 덩어리 크기 무관 ---------- */
    {
        size_t n = 0;
        #define PUT(s, len) do { memcpy(g_stream + n, (s), (len)); n += (len); } while (0)
        #define PUTS(s) PUT((s), strlen(s))
        PUTS("\x01\xff\xfe" "boot v1.2 ok\r\n\x13\x11");                     /* 부팅 배너 (첫 '$' 전 쓰레기) */
        PUTS(k_samples[0]); PUTS("\r\n");                                    /* GGA                         */
        PUTS(k_samples[1]); PUTS("\r\n");                                    /* RMC                         */
        PUTS(k_samples[2]); PUTS("\r\n");                                    /* GSA                         */
        PUTS(k_samples[3]); PUTS("\r\n");                                    /* GSV                         */
        PUT(k_samples[5], 30);                                               /* GGA 가 전원 글리치로 잘림   */
        PUTS(k_samples[5]); PUTS("\r\n");                                    /* GGA (u-blox)                */
        { char t[128]; snprintf(t, sizeof t, "%s\r\n", k_samples[4]); t[20] ^= 0x01; PUTS(t); } /* 비트 오류 */
        PUTS("\xff\xff\xff\xff\xff\xff\xff\xff");                            /* 보레이트 불일치 쓰레기      */
        PUTS(k_samples[TRIMBLE]); PUTS("\r\n");                              /* 82자 초과 → TOOLONG         */
        PUTS(k_samples[9]); PUTS("\n");                                      /* GLL, LF 만                  */
        PUTS(k_samples[10]); PUTS("\r\n");                                   /* VTG                         */
        PUTS(k_samples[6]); PUTS("\r\n");                                    /* GGA no-fix                  */
        PUTS(k_samples[4]); PUTS("\r\n");                                    /* GGA (마지막)                */
        gps_t G;
        gps_init(&G, NMEA_MAX_BODY_STD);
        gps_feed(&G, g_stream, n);
        printf("gps log (%zu bytes): sentences=%u checksum=%u truncated=%u toolong=%u junk=%u | GGA ok=%u other=%u\n",
               n, G.p.count[NMEA_EV_SENTENCE], G.p.count[NMEA_EV_ERR_CHECKSUM], G.p.count[NMEA_EV_ERR_TRUNCATED],
               G.p.count[NMEA_EV_ERR_TOOLONG], G.p.junk, G.n_gga, G.n_other);
        CHECK(G.p.count[NMEA_EV_SENTENCE] == 9 && G.p.count[NMEA_EV_ERR_CHECKSUM] == 1 &&
              G.p.count[NMEA_EV_ERR_TRUNCATED] == 1 && G.p.count[NMEA_EV_ERR_TOOLONG] == 1);
        CHECK(G.n_gga == 4 && G.n_other == 5 && G.n_gga_err == 0 && G.n_split_err == 0);
        CHECK(G.fix.has_pos && G.fix.lat_e7 == 533613367 && G.fix.lon_e7 == -65056200 && G.fix.time_ms == 34070000u);
        /* DMA 덩어리 크기 1..64 무작위로 잘라 넣어도 결과 동일 */
        bool same = true;
        for (int r = 0; r < 200 && same; r++) {
            gps_t H;
            gps_init(&H, NMEA_MAX_BODY_STD);
            for (size_t i = 0; i < n;) {
                size_t c = 1 + rnd(64);
                if (c > n - i) c = n - i;
                gps_feed(&H, g_stream + i, c);
                i += c;
            }
            same = memcmp(H.p.count, G.p.count, sizeof G.p.count) == 0 && H.n_gga == G.n_gga &&
                   memcmp(&H.fix, &G.fix, sizeof G.fix) == 0 && H.p.junk == G.p.junk;
        }
        CHECK(same);
        #undef PUT
        #undef PUTS
    }

    /* ---------- 퍼즈: 쓰레기 + 정상 문장 → 크래시 없음 / 틀린 체크섬 수락 없음 / 다음 '$' 에서 복구 ---------- */
    {
        static const char *const mode_name[3] = { "random bytes", "token soup", "mutated samples" };
        long cases = 0, accepted = 0, bad_accept = 0, no_recover = 0, overlen = 0;
        for (int mode = 0; mode < 3; mode++) {
            long acc_m = 0;
            for (int r = 0; r < 20000; r++) {
                uint8_t mb = (uint8_t)(r % 4 == 0 ? NMEA_BODY_CAP - 1 : NMEA_MAX_BODY_STD);
                size_t n = make_garbage(g_stream, 200, mode);
                size_t g_end = n;
                const char *s = k_samples[rnd(TRIMBLE)];                 /* 76자 이하 문장 */
                memcpy(g_stream + n, s, strlen(s)); n += strlen(s);
                g_stream[n++] = '\r'; g_stream[n++] = '\n';
                nmea_init(&P, mb);
                nmea_ev_t last = NMEA_EV_NONE;
                for (size_t i = 0; i < n; i++) {
                    last = nmea_feed(&P, g_stream[i]);
                    if (P.len > P.max_body) overlen++;
                    if (last == NMEA_EV_SENTENCE) {
                        if (i < g_end) acc_m++;
                        if (!verify_accept(g_stream, i, P.body)) bad_accept++;
                    }
                }
                size_t bl = (size_t)(strchr(s, '*') - s - 1);
                if (!(last == NMEA_EV_SENTENCE && strlen(P.body) == bl && memcmp(P.body, s + 1, bl) == 0)) no_recover++;
                cases++;
            }
            printf("fuzz %-15s: 20000 cases, %ld sentences accepted inside the garbage\n", mode_name[mode], acc_m);
            accepted += acc_m;
        }
        printf("fuzz total: %ld cases, %ld in-garbage accepts, bad accepts %ld, no recovery %ld, len>max %ld\n",
               cases, accepted, bad_accept, no_recover, overlen);
        CHECK(bad_accept == 0);
        CHECK(no_recover == 0);
        CHECK(overlen == 0);
        (void)nmea_ev_name;
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* ALGO_M12_NO_MAIN */
