// 08_data_processing.c — Data Processing & Protocols (데이터 처리 & 프로토콜)
//                        · Q86-95 · REFERENCE SOLUTION
// ---------------------------------------------------------------------------
// 텔레메트리 무결성(checksum/CRC) + 프레이밍/파싱 + 제어루프 수치처리(fixed-point,
// moving-average) + 대역폭 절약(RLE/base64) + 바이트 스트림 패턴 탐색(KMP) 모음.
// Anduril 펌웨어 인터뷰의 "링크 위로 바이트를 안전하게 주고받아라" 유형.
//
// 빌드:  cc -std=c11 -Wall -Wextra solutions/08_data_processing.c -o /tmp/andb_data_processing
// 실행:  /tmp/andb_data_processing
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>   // ssize_t
#include <stdio.h>
#include <string.h>      // 테스트 검증(memcmp/strcmp) + 구현부 memcpy
#include <limits.h>      // INT32_MAX 등

// ============================================================================
// 86. NMEA 문장 파싱 — nmea_validate / nmea_get_field
//     프로토콜 파싱의 정석. NMEA-0183 GPS 문장:  $GPGGA,...,*47<CR><LF>
//     체크섬은 '$'와 '*' 사이 모든 바이트의 XOR, 뒤에 2자리 hex 로 붙는다.
// ============================================================================
static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;                                   // hex 아님
}

// 구조 + 체크섬 검증. '$'로 시작, '*hh' 로 끝(뒤 CRLF 는 허용).
bool nmea_validate(const char *s) {
    if (!s || s[0] != '$') return false;
    uint8_t sum = 0;
    size_t i = 1;
    for (; s[i] && s[i] != '*'; i++) sum ^= (uint8_t)s[i];  // '$'..'*' 사이 XOR
    if (s[i] != '*') return false;               // 체크섬 구분자 '*' 없음
    int hi = hexval(s[i + 1]);                    // s[i+1]=='\0' 면 -1 (short-circuit)
    if (hi < 0) return false;
    int lo = hexval(s[i + 2]);                    // hi 통과했으므로 s[i+2] 접근 안전
    if (lo < 0) return false;
    return sum == (uint8_t)((hi << 4) | lo);
}

// idx 번째 콤마 필드를 out 에 복사(널 종료). field 0 = talker+type("GPGGA").
// 반환: 필드 길이, 오류 시 -1 (NULL / 형식 오류 / out 버퍼 부족 / idx 범위 밖).
int nmea_get_field(const char *s, size_t idx, char *out, size_t out_max) {
    if (!s || !out || out_max == 0 || s[0] != '$') return -1;
    const char *p = s + 1;                        // '$' 다음부터
    size_t cur = 0;
    for (;;) {
        const char *start = p;
        while (*p && *p != ',' && *p != '*' && *p != '\r' && *p != '\n') p++;
        size_t flen = (size_t)(p - start);
        if (cur == idx) {                         // 원하는 필드 도달
            if (flen + 1 > out_max) return -1;    // 널 포함 공간 부족
            memcpy(out, start, flen);
            out[flen] = '\0';
            return (int)flen;
        }
        if (*p == ',') { p++; cur++; continue; }  // 다음 필드로
        return -1;                                // 종료자 도달 = idx 범위 밖
    }
}

// ============================================================================
// 87. XOR 체크섬 — 가장 싼 무결성 검사(오류 검출력 약함). 초기값 0.
// ============================================================================
uint8_t xor_checksum(const uint8_t *data, size_t len) {
    uint8_t c = 0;
    if (!data) return 0;
    for (size_t i = 0; i < len; i++) c ^= data[i];
    return c;
}

// ============================================================================
// 88. 8비트 additive 체크섬 — 바이트 합의 하위 8비트(자연 wrap-around).
// ============================================================================
uint8_t sum8_checksum(const uint8_t *data, size_t len) {
    uint8_t s = 0;
    if (!data) return 0;
    for (size_t i = 0; i < len; i++) s = (uint8_t)(s + data[i]);  // mod 256
    return s;
}

// ============================================================================
// 89. CRC-16 (LUT) — CRC-16/XMODEM: poly 0x1021, init 0x0000, MSB-first, 무반사.
//     LUT 생성: 각 입력 바이트 i 에 대해 i<<8 을 8번 다항식 나눗셈한 잔여를 저장.
//     표준 검증벡터: crc16_lut("123456789", 9) == 0x31C3.
// ============================================================================
static uint16_t g_crc_table[256];
static bool     g_crc_ready = false;

static void crc16_build_table(void) {
    for (int i = 0; i < 256; i++) {
        uint16_t crc = (uint16_t)(i << 8);        // 바이트를 상위에 정렬
        for (int b = 0; b < 8; b++)               // 8비트 나눗셈
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
        g_crc_table[i] = crc;
    }
    g_crc_ready = true;
}

uint16_t crc16_lut(const uint8_t *data, size_t len) {
    if (!g_crc_ready) crc16_build_table();        // 실제 펌웨어는 const 로 미리 굽는다
    uint16_t crc = 0x0000;                        // XMODEM init
    if (!data) return crc;
    for (size_t i = 0; i < len; i++)
        crc = (uint16_t)((crc << 8) ^ g_crc_table[((crc >> 8) ^ data[i]) & 0xFF]);
    return crc;
}

// ============================================================================
// 90. 바이너리 패킷 파싱 — 최소 프레이밍 검증.
//     프레임: [0xAA start][payload...][0xBB end]. 길이>=2, 양끝 마커 확인.
//     (실전 프레임은 여기에 LEN + CRC(#89) 가 더 붙는다.)
// ============================================================================
#define PKT_START 0xAA
#define PKT_END   0xBB
bool parse_packet(const uint8_t *data, size_t len) {
    if (!data || len < 2) return false;           // 최소 start+end
    if (data[0] != PKT_START) return false;
    if (data[len - 1] != PKT_END) return false;
    return true;
}

// ============================================================================
// 91. 고정소수점 연산(Q 포맷, FPU 없이) — fixed_add / fixed_mul
//     값 = raw / 2^q.  덧셈은 같은 Q 라 raw 를 그냥 더한다.
//     곱셈은 raw 곱 후 >>q (스케일 복원). int64 중간연산 + 반올림 + 포화.
// ============================================================================
int32_t fixed_add(int32_t a, int32_t b, uint8_t q) {
    (void)q;                                      // 같은 Q 포맷이면 스케일 동일
    int64_t s = (int64_t)a + (int64_t)b;          // 오버플로 방지
    if (s > INT32_MAX) s = INT32_MAX;             // 포화(saturate)
    if (s < INT32_MIN) s = INT32_MIN;
    return (int32_t)s;
}
int32_t fixed_mul(int32_t a, int32_t b, uint8_t q) {
    int64_t p = (int64_t)a * (int64_t)b;          // Q(2q) 로 스케일업됨
    if (q > 0 && q < 63) p += (int64_t)1 << (q - 1);  // 반올림(round-half-up)
    if (q < 63) p >>= q; else p = 0;              // 스케일 복원(산술 시프트)
    if (p > INT32_MAX) p = INT32_MAX;
    if (p < INT32_MIN) p = INT32_MIN;
    return (int32_t)p;
}

// ============================================================================
// 92. 이동평균 필터(sensor smoothing) — 창 크기 window 의 SMA 시리즈 출력.
//     out[i] = mean(in[i-window+1 .. i]). 초반부(이력 부족)는 있는 만큼 평균.
// ============================================================================
void moving_average(const float *in, size_t len, size_t window, float *out) {
    if (!in || !out || window == 0) return;
    for (size_t i = 0; i < len; i++) {
        size_t start = (i + 1 >= window) ? (i + 1 - window) : 0;  // 창 시작(언더플로 방어)
        float sum = 0.0f;
        size_t cnt = 0;
        for (size_t j = start; j <= i; j++) { sum += in[j]; cnt++; }
        out[i] = sum / (float)cnt;
    }
}

// ============================================================================
// 93. RLE 압축/해제 — rle_compress / rle_decompress
//     포맷: [value][count] 쌍. count 는 1..255 (긴 런은 분할). 반환=쓴 바이트 수.
//     out 부족/형식 오류 시 0 반환.
// ============================================================================
size_t rle_compress(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max) {
    if (!in || !out) return 0;
    size_t o = 0, i = 0;
    while (i < in_len) {
        uint8_t v = in[i];
        size_t run = 1;
        while (i + run < in_len && in[i + run] == v && run < 255) run++;  // count<=255
        if (o + 2 > out_max) return 0;            // 버퍼 부족
        out[o++] = v;
        out[o++] = (uint8_t)run;
        i += run;
    }
    return o;
}
size_t rle_decompress(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max) {
    if (!in || !out) return 0;
    if (in_len % 2 != 0) return 0;                // [value][count] 쌍이어야 함
    size_t o = 0;
    for (size_t i = 0; i < in_len; i += 2) {
        uint8_t v = in[i], run = in[i + 1];
        if (o + run > out_max) return 0;          // 출력 오버플로
        for (uint8_t k = 0; k < run; k++) out[o++] = v;
    }
    return o;
}

// ============================================================================
// 94. Base64 인코딩/디코딩 — base64_encode / base64_decode (표준, '=' 패딩)
//     3바이트 -> 4문자. 반환=출력 길이(널 제외). 버퍼 부족/불량 입력 시 0.
// ============================================================================
static const char B64E[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64dec1(char c) {                       // 문자 -> 6비트 값, 불량이면 -1
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

size_t base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_max) {
    if (!in || !out) return 0;
    size_t olen = 4 * ((in_len + 2) / 3);         // 패딩 포함 출력 길이
    if (out_max < olen + 1) return 0;             // 널 종료 자리까지 확인
    size_t o = 0, i = 0;
    while (i + 3 <= in_len) {                      // 3바이트씩 완전 블록
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = B64E[(v >> 18) & 63];
        out[o++] = B64E[(v >> 12) & 63];
        out[o++] = B64E[(v >> 6) & 63];
        out[o++] = B64E[v & 63];
        i += 3;
    }
    size_t rem = in_len - i;                       // 남은 1 또는 2 바이트
    if (rem == 1) {
        uint32_t v = (uint32_t)in[i] << 16;
        out[o++] = B64E[(v >> 18) & 63];
        out[o++] = B64E[(v >> 12) & 63];
        out[o++] = '='; out[o++] = '=';
    } else if (rem == 2) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[o++] = B64E[(v >> 18) & 63];
        out[o++] = B64E[(v >> 12) & 63];
        out[o++] = B64E[(v >> 6) & 63];
        out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}
size_t base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_max) {
    if (!in || !out) return 0;
    if (in_len == 0 || in_len % 4 != 0) return 0;  // 표준은 4의 배수
    size_t pad = 0;
    if (in[in_len - 1] == '=') pad++;
    if (in[in_len - 2] == '=') pad++;
    size_t olen = in_len / 4 * 3 - pad;
    if (out_max < olen) return 0;
    size_t o = 0;
    for (size_t i = 0; i < in_len; i += 4) {
        int a = b64dec1(in[i]);
        int b = b64dec1(in[i + 1]);
        int c = (in[i + 2] == '=') ? 0 : b64dec1(in[i + 2]);
        int d = (in[i + 3] == '=') ? 0 : b64dec1(in[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) return 0;  // 불량 문자
        uint32_t v = ((uint32_t)a << 18) | ((uint32_t)b << 12) |
                     ((uint32_t)c << 6) | (uint32_t)d;
        if (o < olen) out[o++] = (uint8_t)((v >> 16) & 0xFF);
        if (in[i + 2] != '=' && o < olen) out[o++] = (uint8_t)((v >> 8) & 0xFF);
        if (in[i + 3] != '=' && o < olen) out[o++] = (uint8_t)(v & 0xFF);
    }
    return o;
}

// ============================================================================
// 95. 바이트 패턴 탐색 (KMP) — search_pattern
//     data 안 pattern 첫 등장 인덱스, 없으면 -1, 빈 패턴이면 0.
//     KMP: LPS(longest proper prefix-suffix) 로 O(n+m). LPS 는 고정 버퍼(bounded).
// ============================================================================
#define KMP_MAX_PAT 256
ssize_t search_pattern(const uint8_t *data, size_t len,
                       const uint8_t *pattern, size_t pat_len) {
    if (pat_len == 0) return 0;                    // 빈 패턴 = 위치 0
    if (!data || !pattern) return -1;
    if (pat_len > len) return -1;

    if (pat_len <= KMP_MAX_PAT) {                  // KMP 경로
        size_t lps[KMP_MAX_PAT];
        lps[0] = 0;
        size_t k = 0;                              // 이전 최장 prefix-suffix 길이
        for (size_t i = 1; i < pat_len; i++) {     // LPS 테이블 구축 O(m)
            while (k > 0 && pattern[i] != pattern[k]) k = lps[k - 1];
            if (pattern[i] == pattern[k]) k++;
            lps[i] = k;
        }
        size_t q = 0;                              // 지금까지 매칭된 패턴 길이
        for (size_t i = 0; i < len; i++) {         // 스캔 O(n)
            while (q > 0 && data[i] != pattern[q]) q = lps[q - 1];
            if (data[i] == pattern[q]) q++;
            if (q == pat_len) return (ssize_t)(i - pat_len + 1);
        }
        return -1;
    }
    // 패턴이 상한을 넘으면 naive fallback (추가 메모리 0)
    for (size_t i = 0; i + pat_len <= len; i++) {
        size_t j = 0;
        while (j < pat_len && data[i + j] == pattern[j]) j++;
        if (j == pat_len) return (ssize_t)i;
    }
    return -1;
}

// ============================================================================
// ---- Test harness (PASS/FAIL) ----
// ============================================================================
static int g_pass = 0, g_fail = 0;

static void check_int(const char *call, long got, long want) {
    bool ok = (got == want);
    printf("%-52s -> %ld  %s", call, got, ok ? "[PASS]" : "[FAIL]");
    if (!ok) printf(" (expected %ld)", want);
    printf("\n");
    ok ? g_pass++ : g_fail++;
}
static void check_hex(const char *call, unsigned long got, unsigned long want) {
    bool ok = (got == want);
    printf("%-52s -> 0x%lX  %s", call, got, ok ? "[PASS]" : "[FAIL]");
    if (!ok) printf(" (expected 0x%lX)", want);
    printf("\n");
    ok ? g_pass++ : g_fail++;
}
static void check_cond(const char *call, bool ok, const char *note) {
    printf("%-52s -> %s  %s\n", call, note, ok ? "[PASS]" : "[FAIL]");
    ok ? g_pass++ : g_fail++;
}
static void check_str(const char *call, const char *got, const char *want) {
    bool ok = (got && want && strcmp(got, want) == 0);
    printf("%-52s -> \"%s\"  %s", call, got ? got : "(null)", ok ? "[PASS]" : "[FAIL]");
    if (!ok) printf(" (expected \"%s\")", want);
    printf("\n");
    ok ? g_pass++ : g_fail++;
}
static void check_mem(const char *call, const uint8_t *got, size_t glen,
                      const uint8_t *want, size_t wlen) {
    bool ok = (glen == wlen && memcmp(got, want, wlen) == 0);
    printf("%-52s -> [", call);
    for (size_t i = 0; i < glen; i++) printf("%s0x%02X", i ? " " : "", got[i]);
    printf("]  %s", ok ? "[PASS]" : "[FAIL]");
    if (!ok) {
        printf(" (expected [");
        for (size_t i = 0; i < wlen; i++) printf("%s0x%02X", i ? " " : "", want[i]);
        printf("])");
    }
    printf("\n");
    ok ? g_pass++ : g_fail++;
}
static bool feq(float a, float b) { float d = a - b; return (d < 0 ? -d : d) < 1e-6f; }

int main(void) {
    printf("== 86. NMEA parse (nmea_validate / nmea_get_field) ==\n");
    {
        const char *good = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
        const char *bad  = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48";
        check_cond("nmea_validate(valid GPGGA *47)", nmea_validate(good) == true, "true");
        check_cond("nmea_validate(corrupt csum *48)", nmea_validate(bad) == false, "false");
        check_cond("nmea_validate(no '$')", nmea_validate("GPGGA,x*00") == false, "false");
        check_cond("nmea_validate(NULL)", nmea_validate(NULL) == false, "false");
        char f[32];
        check_int("nmea_get_field(good,0,..) len", nmea_get_field(good, 0, f, sizeof f), 5);
        check_str("  field 0 (talker+type)", f, "GPGGA");
        nmea_get_field(good, 1, f, sizeof f);
        check_str("nmea_get_field(good,1) time", f, "123519");
        nmea_get_field(good, 2, f, sizeof f);
        check_str("nmea_get_field(good,2) lat", f, "4807.038");
        check_int("nmea_get_field(good,99) out-of-range", nmea_get_field(good, 99, f, sizeof f), -1);
        check_int("nmea_get_field(tiny out buf)", nmea_get_field(good, 0, f, 3), -1);
    }

    printf("\n== 87. xor_checksum ==\n");
    {
        uint8_t d[] = {0x01, 0x02, 0x03};
        check_hex("xor_checksum({01,02,03},3)", xor_checksum(d, 3), 0x00);
        uint8_t d2[] = {0xAA, 0x55, 0xFF};
        check_hex("xor_checksum({AA,55,FF},3)", xor_checksum(d2, 3), 0x00);  // AA^55=FF, ^FF=00
        check_hex("xor_checksum(NULL,0)", xor_checksum(NULL, 0), 0x00);
        uint8_t one[] = {0x7E};
        check_hex("xor_checksum({7E},1)", xor_checksum(one, 1), 0x7E);
    }

    printf("\n== 88. sum8_checksum ==\n");
    {
        uint8_t d[] = {0x01, 0x02, 0x03};
        check_hex("sum8_checksum({01,02,03},3)", sum8_checksum(d, 3), 0x06);
        uint8_t d2[] = {0xFF, 0x01};
        check_hex("sum8_checksum({FF,01},2) wrap", sum8_checksum(d2, 2), 0x00);  // 256 mod 256
        uint8_t d3[] = {0x80, 0x80, 0x81};
        check_hex("sum8_checksum({80,80,81},3)", sum8_checksum(d3, 3), 0x81);    // 0x181 & 0xFF
        check_hex("sum8_checksum(NULL,0)", sum8_checksum(NULL, 0), 0x00);
    }

    printf("\n== 89. crc16_lut (CRC-16/XMODEM) ==\n");
    {
        check_hex("crc16_lut(\"123456789\",9) std vector",
                  crc16_lut((const uint8_t *)"123456789", 9), 0x31C3);
        uint8_t d[] = {0x01, 0x02, 0x03};
        check_hex("crc16_lut({01,02,03},3)", crc16_lut(d, 3), 0x6131);
        check_hex("crc16_lut(NULL,0)", crc16_lut(NULL, 0), 0x0000);
        uint8_t z[] = {0x00};
        check_hex("crc16_lut({00},1)", crc16_lut(z, 1), 0x0000);
    }

    printf("\n== 90. parse_packet ==\n");
    {
        uint8_t p[]  = {0xAA, 0x01, 0x02, 0x03, 0xBB};
        uint8_t bad1[] = {0xAB, 0x01, 0xBB};
        uint8_t bad2[] = {0xAA, 0x01, 0xBC};
        uint8_t min[] = {0xAA, 0xBB};
        check_cond("parse_packet(valid frame)", parse_packet(p, 5) == true, "true");
        check_cond("parse_packet(bad start)", parse_packet(bad1, 3) == false, "false");
        check_cond("parse_packet(bad end)", parse_packet(bad2, 3) == false, "false");
        check_cond("parse_packet(min 2 bytes)", parse_packet(min, 2) == true, "true");
        check_cond("parse_packet(len<2)", parse_packet(p, 1) == false, "false");
        check_cond("parse_packet(NULL)", parse_packet(NULL, 5) == false, "false");
    }

    printf("\n== 91. fixed_add / fixed_mul (Q8) ==\n");
    {
        check_int("fixed_add(256,128,8)  1.0+0.5", fixed_add(256, 128, 8), 384);
        check_int("fixed_mul(256,128,8)  1.0*0.5", fixed_mul(256, 128, 8), 128);
        check_int("fixed_mul(256,256,8)  1.0*1.0", fixed_mul(256, 256, 8), 256);
        check_int("fixed_mul(384,384,8)  1.5*1.5", fixed_mul(384, 384, 8), 576);  // 2.25=576
        check_int("fixed_add(-256,128,8) -1.0+0.5", fixed_add(-256, 128, 8), -128);
        check_int("fixed_add(sat) INT32_MAX+1", fixed_add(INT32_MAX, 1, 0), INT32_MAX);
    }

    printf("\n== 92. moving_average (SMA window=3) ==\n");
    {
        float in[] = {1, 2, 3, 4, 5};
        float out[5] = {0};
        moving_average(in, 5, 3, out);
        // out = {1, 1.5, 2, 3, 4}
        check_cond("moving_average out[0]==1.0", feq(out[0], 1.0f), "1.0");
        check_cond("moving_average out[1]==1.5", feq(out[1], 1.5f), "1.5");
        check_cond("moving_average out[2]==2.0", feq(out[2], 2.0f), "2.0");
        check_cond("moving_average out[3]==3.0", feq(out[3], 3.0f), "3.0");
        check_cond("moving_average out[4]==4.0", feq(out[4], 4.0f), "4.0");
    }

    printf("\n== 93. rle_compress / rle_decompress ==\n");
    {
        uint8_t in[]  = {0xAA, 0xAA, 0xAA, 0xBB};
        uint8_t exp[] = {0xAA, 0x03, 0xBB, 0x01};
        uint8_t out[16] = {0};
        size_t n = rle_compress(in, 4, out, sizeof out);
        check_mem("rle_compress({AA,AA,AA,BB})", out, n, exp, 4);

        uint8_t dec[16] = {0};
        size_t m = rle_decompress(out, n, dec, sizeof dec);
        check_mem("rle_decompress(round-trip)", dec, m, in, 4);

        // 260-length run splits at 255
        uint8_t big[260];
        memset(big, 0x5A, sizeof big);
        uint8_t bout[16] = {0};
        size_t bn = rle_compress(big, 260, bout, sizeof bout);
        uint8_t bexp[] = {0x5A, 0xFF, 0x5A, 0x05};  // 255 + 5
        check_mem("rle_compress(260x 0x5A) splits", bout, bn, bexp, 4);

        uint8_t tiny[1] = {0};
        check_int("rle_compress(overflow -> 0)", (long)rle_compress(in, 4, tiny, 1), 0);
        uint8_t oddin[] = {0xAA};
        uint8_t o2[8] = {0};
        check_int("rle_decompress(odd len -> 0)", (long)rle_decompress(oddin, 1, o2, sizeof o2), 0);
    }

    printf("\n== 94. base64_encode / base64_decode ==\n");
    {
        char e[16] = {0};
        check_int("base64_encode(\"Man\",3) len", (long)base64_encode((const uint8_t *)"Man", 3, e, sizeof e), 4);
        check_str("  base64_encode(\"Man\")", e, "TWFu");
        base64_encode((const uint8_t *)"Ma", 2, e, sizeof e);
        check_str("base64_encode(\"Ma\") 1 pad", e, "TWE=");
        base64_encode((const uint8_t *)"M", 1, e, sizeof e);
        check_str("base64_encode(\"M\") 2 pad", e, "TQ==");

        uint8_t d[16] = {0};
        size_t dn = base64_decode("TWFu", 4, d, sizeof d);
        check_mem("base64_decode(\"TWFu\")", d, dn, (const uint8_t *)"Man", 3);
        dn = base64_decode("TWE=", 4, d, sizeof d);
        check_mem("base64_decode(\"TWE=\")", d, dn, (const uint8_t *)"Ma", 2);
        dn = base64_decode("TQ==", 4, d, sizeof d);
        check_mem("base64_decode(\"TQ==\")", d, dn, (const uint8_t *)"M", 1);
        check_int("base64_decode(bad len 3 -> 0)", (long)base64_decode("TWF", 3, d, sizeof d), 0);
        check_int("base64_decode(bad char -> 0)", (long)base64_decode("T@Fu", 4, d, sizeof d), 0);
    }

    printf("\n== 95. search_pattern (KMP) ==\n");
    {
        uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
        uint8_t pat[]  = {0x02, 0x03};
        check_int("search_pattern(...,{02,03})", (long)search_pattern(data, 4, pat, 2), 1);
        uint8_t nope[] = {0x09, 0x09};
        check_int("search_pattern(not found)", (long)search_pattern(data, 4, nope, 2), -1);
        check_int("search_pattern(empty pattern)", (long)search_pattern(data, 4, pat, 0), 0);
        uint8_t at0[] = {0x01};
        check_int("search_pattern(at index 0)", (long)search_pattern(data, 4, at0, 1), 0);
        uint8_t end[] = {0x04};
        check_int("search_pattern(at last)", (long)search_pattern(data, 4, end, 1), 3);
        // KMP 재사용이 필요한 반복 접두사 케이스
        uint8_t hay[]  = {0xAA, 0xAA, 0xAB, 0xAA, 0xAA, 0xAB};
        uint8_t need[] = {0xAA, 0xAA, 0xAB};
        check_int("search_pattern(repeated prefix)", (long)search_pattern(hay, 6, need, 3), 0);
        uint8_t toolong[] = {0x01, 0x02, 0x03};
        check_int("search_pattern(pat>data -> -1)", (long)search_pattern(data, 4, toolong, 5), -1);
    }

    // ---- summary ----
    printf("\n==================================================\n");
    printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    printf("==================================================\n");
    return g_fail ? 1 : 0;
}
