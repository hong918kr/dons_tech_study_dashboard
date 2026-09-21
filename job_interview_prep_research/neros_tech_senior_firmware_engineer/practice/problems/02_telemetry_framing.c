// 02_telemetry_framing.c  —  PRACTICE STUB (직접 채워넣기)
// 텔레메트리 프레이밍 (byte order + CRC-16 + COBS + 스트리밍 수신 FSM)  —  Q1~Q6
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=02_telemetry_framing
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g 02_telemetry_framing.c -o /tmp/n02 && /tmp/n02
//
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 전부 FAIL 로 뜬다.)
//
// Neros 의 링크는 재밍/간섭이 기본값인 전장에서 돈다. UART/radio 는 "바이트 스트림"
// 일 뿐이라 프레임 경계를 스스로 알려주지 않는다 -> delimiter 기반 프레이밍 필요.
// 링크는 언젠가 비트를 뒤집고 바이트를 통째로 삼킨다 -> CRC 로 걸러내야 한다.
// 그리고 한 번 깨진 뒤에도 다음 프레임부터는 반드시 다시 붙어야 한다(resync).
// 이 세 가지가 platform runtime(logging/telemetry/IPC)의 바닥 계층이다.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// ===========================================================================
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// 프로토콜 파라미터 (링크 MTU 를 페이로드 64B 로 잡은 예)
#define FRAME_MAX_PAYLOAD 64
#define FRAME_DELIMITER   0x00
// COBS 최악 오버헤드(+1, 254B 마다 +1) + 말미 0 블록까지 감안한 수신 버퍼
#define RX_BUF_MAX        (FRAME_MAX_PAYLOAD + 2 + 4)

/* ---------------------------------------------------------------------------
 * Q1.  엔디안 안전 바이트 접근자 (put/get u16/u32 little-endian)
 *   KO: 멀티바이트 필드를 uint8_t 버퍼에 "프로토콜이 정한 순서"로 직접 깔고 다시
 *       읽는다. 구조체를 그대로 memcpy 해 링크로 보내면 컴파일러/ABI 의 패딩과
 *       호스트 엔디안이 그대로 전선에 노출된다 -> MCU(LE, ARM)와 Linux 지상국이
 *       다른 툴체인/정렬 규칙을 쓰는 순간 조용히 깨진다. 항상 바이트 단위 직렬화.
 *   EN: Serialize multi-byte fields byte-by-byte in the protocol's byte order;
 *       never memcpy a (packed) struct across the link.
 *   ex: put_u16_le(b, 0xBEEF) -> b[0]=0xEF, b[1]=0xBE ; get_u16_le(b)=0xBEEF
 * ------------------------------------------------------------------------- */
void put_u16_le(uint8_t *p, uint16_t v) {
    (void)p; (void)v;
    // TODO: implement
}

void put_u32_le(uint8_t *p, uint32_t v) {
    (void)p; (void)v;
    // TODO: implement
}

uint16_t get_u16_le(const uint8_t *p) {
    (void)p;
    // TODO: implement
    return 0;  // placeholder
}

uint32_t get_u32_le(const uint8_t *p) {
    (void)p;
    // TODO: implement
    return 0;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q2.  CRC-16/CCITT-FALSE  (poly 0x1021, init 0xFFFF, 무반사, final xor 없음)
 *   KO: 비트 단위(bitwise) 구현. 바이트를 상위 8비트에 XOR 하고 8번 시프트하며
 *       MSB 가 1이면 다항식을 XOR. LUT(256엔트리, 512B) 로 바꾸면 8배 빠르지만
 *       플래시가 아까운 MCU 에서는 이 비트 버전이 기본값.
 *   EN: Bitwise CRC-16/CCITT-FALSE. Table-driven version trades 512B of flash
 *       for ~8x speed; keep the bitwise one as the reference.
 *   ex: crc16_ccitt("123456789", 9) == 0x29B1   (표준 check value)
 * ------------------------------------------------------------------------- */
uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
    (void)data; (void)len;
    // TODO: implement (init 0xFFFF, poly 0x1021, MSB-first, 8회 시프트)
    return 0;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q3.  COBS 인코딩  (Consistent Overhead Byte Stuffing)
 *   KO: 출력에서 0x00 을 완전히 제거해 0x00 을 프레임 delimiter 로 쓸 수 있게
 *       만든다. 블록 단위로 "다음 0 까지의 거리(=데이터수+1)"를 code 바이트에
 *       적고 데이터가 따라온다. code==0xFF 는 "254바이트 꽉 참, 뒤에 0 없음".
 *       오버헤드는 입력 254B 당 1B (<= 0.4%) 로 상한이 고정(consistent)된다.
 *       trailing delimiter 는 붙이지 않는다(호출자 몫). cap 부족이면 0.
 *   EN: COBS-encode `in` so the output contains no 0x00; return encoded length,
 *       0 if `out` capacity is insufficient. No trailing delimiter is written.
 *   ex: {11 22 00 33} -> {03 11 22 02 33}   (0 하나가 code 로 흡수됨)
 *       {}            -> {01}
 * ------------------------------------------------------------------------- */
size_t cobs_encode(const uint8_t *in, size_t len, uint8_t *out, size_t cap) {
    (void)in; (void)len; (void)out; (void)cap;
    // TODO: implement (code 바이트 자리를 예약해두고 블록 단위로 마감)
    return 0;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q4.  COBS 디코딩
 *   KO: code 바이트를 읽어 code-1 개를 그대로 복사하고, code != 0xFF 이고 입력이
 *       아직 남았으면 0x00 을 하나 복원한다("마지막 블록의 암묵적 0"은 없음).
 *       입력 중간에 0x00 이 나오거나 블록이 입력을 넘어가면 malformed -> 0.
 *       cap 부족도 0. (빈 페이로드도 0 이라 호출자는 길이 0 을 "프레임 없음"으로 취급)
 *   EN: Inverse of cobs_encode; returns decoded length, 0 on malformed input or
 *       insufficient capacity.
 *   ex: {03 11 22 02 33} -> {11 22 00 33}
 *       {03 11}          -> 0   (블록이 입력 밖으로 삐져나감)
 * ------------------------------------------------------------------------- */
size_t cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t cap) {
    (void)in; (void)len; (void)out; (void)cap;
    // TODO: implement (code-1 바이트 복사, code!=0xFF 이고 입력이 남았으면 0 복원)
    return 0;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q5.  프레임 빌더:  COBS(payload || crc16_le) || 0x00
 *   KO: CRC 는 "원본 payload" 위에서 계산하고 LE 2바이트로 뒤에 붙인 다음,
 *       그 전체를 COBS 로 감싸고 마지막에 delimiter 0x00 한 바이트를 찍는다.
 *       순서가 중요: CRC 를 COBS 뒤에 붙이면 stuffing 이 깨진 비트를 가려버린다.
 *       payload 가 MTU 를 넘거나 cap 이 모자라면 0 (아무것도 안 보냄).
 *   EN: Build one wire frame = COBS(payload || CRC16-LE) + 0x00 delimiter.
 *       Returns total bytes written, 0 on oversize payload / small capacity.
 *   ex: payload {DE 00 AD 00 07} -> 02 DE 02 AD 04 07 6C 66 00
 *       (6C 66 = CRC 0x666C 의 LE 2바이트, 마지막 00 이 delimiter)
 * ------------------------------------------------------------------------- */
size_t frame_build(const uint8_t *payload, size_t len, uint8_t *out, size_t cap) {
    (void)payload; (void)len; (void)out; (void)cap;
    // TODO: implement (CRC 는 payload 에 대해 계산 -> LE 2B 붙임 -> COBS -> 0x00)
    return 0;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q6.  스트리밍 수신 FSM  (rx_init / rx_feed)   ★ 핵심 문제
 *   KO: UART RX 인터럽트가 바이트를 한 개씩 던져준다. 0x00 을 만날 때까지 모았다가
 *       COBS 디코드 -> CRC 검사 -> 성공하면 payload 를 넘기며 true.
 *       실패해도 절대 멈추면 안 된다: 버퍼를 비우고 "다음 delimiter 부터" 다시 시작.
 *       - 버퍼 초과(oversize/노이즈 폭주)  -> overrun++, 그 프레임은 통째로 폐기,
 *         단 delimiter 가 올 때까지 계속 먹어치우고 조용히 버린다(wedge 금지).
 *       - COBS malformed 또는 CRC 불일치   -> crc_err++, 폐기.
 *       - 연속 delimiter(빈 프레임)         -> 조용히 무시(에러 카운트 오염 금지).
 *   EN: Byte-at-a-time framing receiver. Accumulate until the 0x00 delimiter,
 *       then COBS-decode + verify CRC. Returns true once per good frame and
 *       always resynchronizes cleanly at the next delimiter.
 *   ex: 쓰레기 바이트 -> 0x00 -> 정상 프레임 -> 0x00  이면 두 번째에서 true
 * ------------------------------------------------------------------------- */
typedef struct {
    uint8_t  buf[RX_BUF_MAX];   // delimiter 사이에 모은 raw(=COBS) 바이트
    size_t   n;                 // 현재 채워진 길이
    bool     dropping;          // 이번 프레임은 이미 버리기로 결정됨
    uint32_t frames_ok;         // CRC 통과 프레임 수
    uint32_t crc_err;           // COBS/CRC 검증 실패 수
    uint32_t overrun;           // 버퍼 초과로 버린 프레임 수
} rx_t;

void rx_init(rx_t *rx) {
    (void)rx;
    // TODO: implement (n=0, dropping=false, 카운터 3개 0)
}

bool rx_feed(rx_t *rx, uint8_t byte, uint8_t *payload_out, size_t *len_out) {
    (void)rx; (void)byte; (void)payload_out; (void)len_out;
    // TODO: implement (delimiter 까지 누적 -> COBS 디코드 -> CRC 검사 -> 재동기)
    return false;  // placeholder
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
    // -------- Q1: 엔디안 안전 접근자 --------
    printf("== Q1. endian-safe byte accessors ==\n");
    uint8_t b[8] = {0};
    put_u16_le(b, 0xBEEFu);
    T("put_u16_le(0xBEEF) -> EF BE (wire order) + get round trip",
      b[0] == 0xEF && b[1] == 0xBE && get_u16_le(b) == 0xBEEFu);
    put_u32_le(b, 0xDEADBEEFu);
    T("put_u32_le(0xDEADBEEF) -> EF BE AD DE + get round trip",
      b[0] == 0xEF && b[1] == 0xBE && b[2] == 0xAD && b[3] == 0xDE &&
      get_u32_le(b) == 0xDEADBEEFu);
    put_u32_le(b + 1, 0x01020304u);     // 정렬되지 않은 오프셋 (패킷 안에서는 흔한 일)
    T("unaligned offset is safe (byte-wise, no pointer casts)",
      get_u32_le(b + 1) == 0x01020304u);

    // -------- Q2: CRC-16/CCITT-FALSE --------
    printf("== Q2. crc16_ccitt (0x1021 / init 0xFFFF) ==\n");
    const uint8_t check[9] = {'1','2','3','4','5','6','7','8','9'};
    T("crc16_ccitt(\"123456789\") == 0x29B1 (known answer)",
      crc16_ccitt(check, 9) == 0x29B1u);
    uint8_t flip[9];
    memcpy(flip, check, 9);
    flip[4] ^= 0x01;                    // 비트 하나만 뒤집기
    T("single bit flip changes the CRC",
      crc16_ccitt(flip, 9) != crc16_ccitt(check, 9));

    // -------- Q3/Q4: COBS --------
    printf("== Q3/Q4. COBS encode / decode ==\n");
    const uint8_t raw[4]  = {0x11, 0x22, 0x00, 0x33};
    const uint8_t want[5] = {0x03, 0x11, 0x22, 0x02, 0x33};
    uint8_t enc[32] = {0}, dec[32] = {0};
    size_t n = cobs_encode(raw, 4, enc, sizeof enc);
    T("cobs_encode {11 22 00 33} -> {03 11 22 02 33}",
      n == 5 && memcmp(enc, want, 5) == 0);

    const uint8_t zpay[7] = {0x00, 0xA5, 0x00, 0x00, 0x5A, 0xFF, 0x00};
    uint8_t ze[32] = {0};
    size_t zn = cobs_encode(zpay, 7, ze, sizeof ze);
    bool no_zero = (zn > 0);
    for (size_t i = 0; i < zn; ++i) if (ze[i] == 0x00) no_zero = false;
    size_t dn = cobs_decode(ze, zn, dec, sizeof dec);
    T("cobs round trip over a payload full of 0x00 (encoded has no 0x00)",
      no_zero && dn == 7 && memcmp(dec, zpay, 7) == 0);

    T("cobs_encode -> 0 when output capacity is too small",
      cobs_encode(raw, 4, enc, 4) == 0);
    T("cobs_decode -> 0 on malformed input (truncated block / inner 0x00)",
      cobs_decode((const uint8_t[]){0x03, 0x11}, 2, dec, sizeof dec) == 0 &&
      cobs_decode((const uint8_t[]){0x00, 0x11}, 2, dec, sizeof dec) == 0);

    // -------- Q5: frame_build --------
    printf("== Q5. frame_build (COBS(payload||CRC) + 0x00) ==\n");
    const uint8_t pl[5] = {0xDE, 0x00, 0xAD, 0x00, 0x07};   // 0x00 을 일부러 포함
    uint8_t fr[96] = {0};
    size_t fn = frame_build(pl, 5, fr, sizeof fr);
    bool clean = (fn >= 4) && (fr[fn - 1] == 0x00);
    for (size_t i = 0; i + 1 < fn; ++i) if (fr[i] == 0x00) clean = false;
    T("frame_build: exactly one 0x00 and it is the last byte", clean);
    T("frame_build -> 0 when capacity is too small",
      frame_build(pl, 5, fr, 4) == 0);

    // -------- Q6: 스트리밍 수신 FSM --------
    printf("== Q6. streaming rx FSM (resync + counters) ==\n");
    rx_t rx = {{0}, 0, false, 0, 0, 0};
    rx_init(&rx);
    uint8_t gf[96] = {0};
    size_t  gn = frame_build(pl, 5, gf, sizeof gf);

    uint8_t out_pl[96] = {0};
    size_t  out_len = 0;
    bool    got = false;
    for (size_t i = 0; i < gn; ++i) got |= rx_feed(&rx, gf[i], out_pl, &out_len);
    T("rx: frame_build -> rx_feed round trip (payload with 0x00 intact)",
      got && out_len == 5 && memcmp(out_pl, pl, 5) == 0);
    T("rx: frames_ok == 1, no errors",
      rx.frames_ok == 1 && rx.crc_err == 0 && rx.overrun == 0);

    gf[1] ^= 0x20;                      // 링크에서 비트 하나가 뒤집혔다고 치자
    got = false;
    for (size_t i = 0; i < gn; ++i) got |= rx_feed(&rx, gf[i], out_pl, &out_len);
    T("rx: corrupted byte -> crc_err++, frame not delivered",
      !got && rx.crc_err == 1 && rx.frames_ok == 1);
    gf[1] ^= 0x20;                      // 원복

    const uint8_t junk[4] = {0x55, 0xAA, 0x37, 0x00};   // 노이즈 + delimiter
    for (size_t i = 0; i < 4; ++i) (void)rx_feed(&rx, junk[i], out_pl, &out_len);
    got = false;
    for (size_t i = 0; i < gn; ++i) got |= rx_feed(&rx, gf[i], out_pl, &out_len);
    T("rx: garbage before a frame -> resync, next frame still parses",
      got && out_len == 5 && rx.frames_ok == 2 && rx.crc_err == 2);

    for (int i = 0; i < 300; ++i) (void)rx_feed(&rx, 0x41, out_pl, &out_len);
    (void)rx_feed(&rx, 0x00, out_pl, &out_len);
    T("rx: oversized frame -> overrun++ only (not counted as crc_err)",
      rx.overrun == 1 && rx.crc_err == 2 && rx.frames_ok == 2);

    got = false;
    for (size_t i = 0; i < gn; ++i) got |= rx_feed(&rx, gf[i], out_pl, &out_len);
    T("rx: parser is not wedged after an oversized frame",
      got && rx.frames_ok == 3);

    for (int i = 0; i < 3; ++i) (void)rx_feed(&rx, 0x00, out_pl, &out_len);
    T("rx: stray delimiters are ignored (no bogus error counts)",
      rx.frames_ok == 3 && rx.crc_err == 2 && rx.overrun == 1);

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
