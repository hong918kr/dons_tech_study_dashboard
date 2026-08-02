// 08_data_processing.c — Data Processing & Protocols (데이터 처리 & 프로토콜)
//                        · Q86-95 · PRACTICE STUB (직접 채워넣기)
// ---------------------------------------------------------------------------
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다.)
//
// 빌드:  cc -std=c11 -Wall -Wextra problems/08_data_processing.c -o /tmp/andb_data_processing
// 실행:  /tmp/andb_data_processing
//   또는: make prob N=08_data_processing
//
// 주제: 텔레메트리 무결성(checksum/CRC) + 프레이밍/파싱 + 제어루프 수치처리
//       (fixed-point, moving-average) + 대역폭 절약(RLE/base64) + 패턴탐색(KMP).
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>   // ssize_t
#include <stdio.h>
#include <string.h>      // 테스트 검증(memcmp/strcmp)용
#include <limits.h>      // INT32_MAX 등

/* ---------------------------------------------------------------------------
 * 86. NMEA 문장 파싱 (nmea_validate / nmea_get_field)
 *   KO: NMEA-0183 GPS 문장 "$GPGGA,...,*47" 을 다룬다. 체크섬은 '$'와 '*' 사이
 *       모든 바이트의 XOR 이며 뒤에 2자리 hex 로 붙는다.
 *       - nmea_validate: 구조('$'..'*hh')와 체크섬이 맞으면 true.
 *       - nmea_get_field: idx 번째 콤마 필드를 out 에 복사(널 종료), 길이 반환.
 *         field 0 = talker+type("GPGGA"). 오류 시 -1.
 *   EN: Parse NMEA sentences. Checksum = XOR of bytes between '$' and '*'.
 *   ex: nmea_validate("$GPGGA,...,*47") -> true
 *       nmea_get_field("$GPGGA,123519,...", 1, out, n) -> "123519", returns 6
 * ------------------------------------------------------------------------- */
bool nmea_validate(const char *s) {
    (void)s;
    // TODO: implement
    return false;
}
int nmea_get_field(const char *s, size_t idx, char *out, size_t out_max) {
    (void)s; (void)idx; (void)out; (void)out_max;
    // TODO: implement
    return -1;
}

/* ---------------------------------------------------------------------------
 * 87. XOR 체크섬
 *   KO: data 의 모든 바이트를 XOR 한 8비트 값을 반환. 초기값 0. NULL 안전.
 *   EN: XOR all bytes; return 8-bit checksum. Init 0.
 *   ex: xor_checksum({0x01,0x02,0x03}, 3) -> 0x00
 * ------------------------------------------------------------------------- */
uint8_t xor_checksum(const uint8_t *data, size_t len) {
    (void)data; (void)len;
    // TODO: implement
    return 0;
}

/* ---------------------------------------------------------------------------
 * 88. 8비트 additive 체크섬 (sum-to-8)
 *   KO: 바이트 합의 하위 8비트(자연 wrap-around) 반환.
 *   EN: 8-bit sum of bytes (mod 256).
 *   ex: sum8_checksum({0x01,0x02,0x03}, 3) -> 0x06
 * ------------------------------------------------------------------------- */
uint8_t sum8_checksum(const uint8_t *data, size_t len) {
    (void)data; (void)len;
    // TODO: implement
    return 0;
}

/* ---------------------------------------------------------------------------
 * 89. CRC-16 (룩업 테이블)
 *   KO: CRC-16/XMODEM (poly 0x1021, init 0x0000, MSB-first, 무반사) 를 256엔트리
 *       LUT 로 구현. LUT[i] = (i<<8) 를 8번 다항식 나눗셈한 잔여.
 *   EN: CRC-16/XMODEM via a 256-entry lookup table.
 *   ex: crc16_lut("123456789", 9) -> 0x31C3  (표준 검증 벡터)
 *       crc16_lut({0x01,0x02,0x03}, 3) -> 0x6131
 * ------------------------------------------------------------------------- */
uint16_t crc16_lut(const uint8_t *data, size_t len) {
    (void)data; (void)len;
    // TODO: implement (힌트: 테이블을 static 으로 1회 생성)
    return 0;
}

/* ---------------------------------------------------------------------------
 * 90. 바이너리 패킷 파싱
 *   KO: 프레임 [0xAA start][payload...][0xBB end] 의 최소 유효성 검사.
 *       길이>=2 이고 양끝 마커가 맞으면 true.
 *   EN: Validate a minimal framed packet: start=0xAA, end=0xBB, len>=2.
 *   ex: parse_packet({0xAA,0x01,0x02,0x03,0xBB}, 5) -> true
 * ------------------------------------------------------------------------- */
bool parse_packet(const uint8_t *data, size_t len) {
    (void)data; (void)len;
    // TODO: implement
    return false;
}

/* ---------------------------------------------------------------------------
 * 91. 고정소수점 연산 (Q 포맷, FPU 없이) — fixed_add / fixed_mul
 *   KO: 값 = raw / 2^q. 덧셈은 raw 를 그냥 더한다. 곱셈은 raw 곱 후 >>q 로 스케일
 *       복원(int64 중간연산 + 반올림 + int32 포화).
 *   EN: Q-format fixed point. add = a+b; mul = (a*b) >> q with rounding+saturation.
 *   ex: fixed_add(256, 128, 8) -> 384   (1.0 + 0.5 = 1.5)
 *       fixed_mul(256, 128, 8) -> 128   (1.0 * 0.5 = 0.5)
 * ------------------------------------------------------------------------- */
int32_t fixed_add(int32_t a, int32_t b, uint8_t q) {
    (void)a; (void)b; (void)q;
    // TODO: implement
    return 0;
}
int32_t fixed_mul(int32_t a, int32_t b, uint8_t q) {
    (void)a; (void)b; (void)q;
    // TODO: implement
    return 0;
}

/* ---------------------------------------------------------------------------
 * 92. 이동평균 필터 (Moving Average, sensor smoothing)
 *   KO: 창 크기 window 의 단순이동평균(SMA) 시리즈를 out 에 쓴다.
 *       out[i] = mean(in[i-window+1 .. i]). 초반부(이력 부족)는 있는 만큼 평균.
 *   EN: Simple moving average of window size; write series to out.
 *   ex: moving_average({1,2,3,4,5}, 5, 3, out) -> out = {1, 1.5, 2, 3, 4}
 * ------------------------------------------------------------------------- */
void moving_average(const float *in, size_t len, size_t window, float *out) {
    (void)in; (void)len; (void)window; (void)out;
    // TODO: implement
}

/* ---------------------------------------------------------------------------
 * 93. RLE 압축/해제 — rle_compress / rle_decompress
 *   KO: 포맷 [value][count] 쌍. count 는 1..255(긴 런은 분할). 반환=쓴 바이트 수.
 *       out 부족/형식 오류 시 0.
 *   EN: Run-length encode/decode as [value][count] pairs (count<=255).
 *   ex: rle_compress({0xAA,0xAA,0xAA,0xBB}, 4, out, 16) -> {0xAA,3,0xBB,1}, 4
 *       rle_decompress({0xAA,3,0xBB,1}, 4, out, 16) -> {0xAA,0xAA,0xAA,0xBB}, 4
 * ------------------------------------------------------------------------- */
size_t rle_compress(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max) {
    (void)in; (void)in_len; (void)out; (void)out_max;
    // TODO: implement
    return 0;
}
size_t rle_decompress(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max) {
    (void)in; (void)in_len; (void)out; (void)out_max;
    // TODO: implement
    return 0;
}

/* ---------------------------------------------------------------------------
 * 94. Base64 인코딩/디코딩 — base64_encode / base64_decode
 *   KO: 표준 base64('=' 패딩). 3바이트->4문자. 반환=출력 길이(널 제외).
 *       버퍼 부족/불량 입력 시 0.
 *   EN: Standard base64 with '=' padding. Return output length (excl. NUL).
 *   ex: base64_encode("Man", 3, out, 16) -> "TWFu", 4
 *       base64_decode("TWFu", 4, out, 16) -> {'M','a','n'}, 3
 * ------------------------------------------------------------------------- */
size_t base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_max) {
    (void)in; (void)in_len; (void)out; (void)out_max;
    // TODO: implement
    return 0;
}
size_t base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_max) {
    (void)in; (void)in_len; (void)out; (void)out_max;
    // TODO: implement
    return 0;
}

/* ---------------------------------------------------------------------------
 * 95. 바이트 패턴 탐색 (KMP)
 *   KO: data 안에서 pattern 의 첫 등장 인덱스. 없으면 -1, 빈 패턴이면 0.
 *       KMP(LPS 테이블)로 O(n+m).
 *   EN: First index of pattern in data, or -1. Empty pattern -> 0. KMP.
 *   ex: search_pattern({0x01,0x02,0x03,0x04}, 4, {0x02,0x03}, 2) -> 1
 * ------------------------------------------------------------------------- */
ssize_t search_pattern(const uint8_t *data, size_t len,
                       const uint8_t *pattern, size_t pat_len) {
    (void)data; (void)len; (void)pattern; (void)pat_len;
    // TODO: implement
    return -1;
}

// ============================================================================
// ---- Test harness (건드리지 말 것: 구현을 채우면 FAIL -> PASS) ----
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
