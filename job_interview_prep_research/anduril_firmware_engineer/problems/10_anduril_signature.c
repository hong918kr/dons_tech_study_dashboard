// 10_anduril_signature.c  —  PRACTICE STUB (직접 채워넣기)
// Anduril 시그니처 & 드론 응용 (Signature & drone-flavored)  —  Q1~Q9
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=10_anduril_signature
//   또는:     cc -std=c11 -Wall -Wextra 10_anduril_signature.c -o /tmp/andb_anduril_signature && /tmp/andb_anduril_signature
//
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다.)
//
// 실제 재현된 Anduril 2문항(my_atoi, reverseBits)에서 출발해, 드론/펌웨어
// 감각의 문제 6개(레지스터 RMW, UART 링버퍼, CRC-8 프레임, 상보필터, 디바운스,
// 스틱→PWM)를 붙였다.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <ctype.h>
#include <math.h>

// ===========================================================================
// 테스트 하네스 (건드리지 말 것)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)
#define TI(label, got, want) do {                                         \
    long _g = (long)(got), _w = (long)(want);                             \
    if (_g == _w) { printf("  [PASS] %s => %ld\n", (label), _g); g_pass++; } \
    else { printf("  [FAIL] %s => %ld (expected %ld)\n", (label), _g, _w); g_fail++; } \
} while (0)
#define TX(label, got, want) do {                                              \
    unsigned long _g = (unsigned long)(got), _w = (unsigned long)(want);       \
    if (_g == _w) { printf("  [PASS] %s => 0x%lX\n", (label), _g); g_pass++; }  \
    else { printf("  [FAIL] %s => 0x%lX (expected 0x%lX)\n", (label), _g, _w); g_fail++; } \
} while (0)

/* ---------------------------------------------------------------------------
 * Q1.  my_atoi  — 문자열 → int (오버플로 클램핑)
 *   KO: 선행 공백 스킵 → 선택적 부호 → 연속 숫자만 변환(첫 비숫자에서 정지).
 *       오버플로 시 INT_MAX/INT_MIN 클램핑. NULL/빈문자열/숫자없음 → 0.
 *   EN: Skip leading spaces, optional sign, convert digits until a non-digit;
 *       clamp overflow to INT_MAX/INT_MIN; NULL/empty/no-digits -> 0.
 *   ex: my_atoi("   -42")=-42, my_atoi("4193 with words")=4193,
 *       my_atoi("2147483648")=INT_MAX, my_atoi(NULL)=0
 *   힌트: isspace((unsigned char)c). 곱하기 *전에* 오버플로를 선검사.
 * ------------------------------------------------------------------------- */
int my_atoi(const char *str) {
    (void)str;
    // TODO: implement
    return 0;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q2.  reverseBits  — 32비트 순서 반전 (기본 루프)
 *   KO: n 의 비트를 완전히 뒤집는다. LSB 를 하나씩 뽑아 reversed 위로 밀어넣기.
 *   EN: Reverse the 32 bits of n with a simple 32-iteration loop.
 *   ex: reverseBits(0x00000001)=0x80000000, reverseBits(0x00000005)=0xA0000000
 * ------------------------------------------------------------------------- */
uint32_t reverseBits(uint32_t n) {
    (void)n;
    // TODO: implement
    return 0;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q3.  reverseBitsOptimized  — O(log n) 분할정복 mask/shift
 *   KO: 블록 크기 16→8→4→2→1 로 인접 블록을 스왑. 루프/분기 없음.
 *       마스크: 0xFF00FF00/0x00FF00FF, 0xF0.../0x0F..., 0xCC.../0x33...,
 *               0xAA.../0x55...
 *   EN: Divide-and-conquer swap of adjacent bit blocks (halving each step).
 *   ex: 모든 입력에서 reverseBits 와 동일한 결과.
 * ------------------------------------------------------------------------- */
uint32_t reverseBitsOptimized(uint32_t n) {
    (void)n;
    // TODO: implement
    return 0;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q4.  reg_set_field  — I2C/MMIO 레지스터 read-modify-write
 *   KO: mask 로 지정된 필드만 value 로 갈아끼우고 나머지 비트는 보존.
 *       new = (reg & ~mask) | ((value<<shift) & mask). shift>=32 는 원본 유지.
 *   EN: Replace only the masked field with value (positioned by shift),
 *       preserving all other bits.
 *   ex: reg_set_field(0xFF, 0x30, 4, 0x2)=0xEF  (필드[5:4]=0b10, 나머지 유지)
 * ------------------------------------------------------------------------- */
uint32_t reg_set_field(uint32_t reg, uint32_t mask, uint8_t shift, uint32_t value) {
    (void)reg; (void)mask; (void)shift; (void)value;
    // TODO: implement
    return 0;   // placeholder
}

// --- Q5. UART 링버퍼 구조체 (건드리지 말 것) ---
#define URB_SIZE 16u   // 2의 거듭제곱
typedef struct {
    uint8_t  buf[URB_SIZE];
    volatile uint32_t head;   // 생산자(ISR)만 store
    volatile uint32_t tail;   // 소비자(main)만 store
} uart_rb_t;

/* ---------------------------------------------------------------------------
 * Q5.  UART 링버퍼 — ISR 생산자 / main 소비자 (SPSC)
 *   KO: put=생산자(ISR)가 바이트 삽입(가득 차면 false), get=소비자(main)가 소비.
 *       power-of-two 크기 + 마스킹, head==tail=empty, 한 칸 희생(용량 SIZE-1).
 *   EN: Single-producer/single-consumer byte ring. put fills, get drains FIFO.
 *   ex: 15개까지 put 성공, 16번째 false; get 은 넣은 순서대로.
 * ------------------------------------------------------------------------- */
void uart_rb_init(uart_rb_t *rb) {
    if (rb) { rb->head = 0; rb->tail = 0; }   // (초기화는 제공 — put/get 을 구현하라)
    // TODO: (필요 시) 추가 초기화
}
bool uart_rb_put(uart_rb_t *rb, uint8_t byte) {
    (void)rb; (void)byte;
    // TODO: implement
    return false;   // placeholder
}
bool uart_rb_get(uart_rb_t *rb, uint8_t *out) {
    (void)rb; (void)out;
    // TODO: implement
    return false;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q6.  crc8  — CRC-8 (poly 0x07, init 0x00, MSB-first)
 *   KO: data[0..len-1] 위로 CRC-8 계산. 각 바이트 XOR 후 8회 시프트/폴리 XOR.
 *   EN: Compute CRC-8 over the buffer (poly 0x07, init 0x00).
 *   ex: crc8("123456789",9)=0xF4, crc8(NULL/empty)=0x00
 * ------------------------------------------------------------------------- */
uint8_t crc8(const uint8_t *data, size_t len) {
    (void)data; (void)len;
    // TODO: implement
    return 0;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q6b. frame_validate  — 텔레메트리 프레임 검증 (MAVLink 풍)
 *   KO: 레이아웃 [0]START(0xFE) [1]payload_len [2..]payload [last]crc.
 *       crc 는 {len+payload} = frame[1..total_len-2] 위에서 계산.
 *       start/길이일관성/crc 모두 맞아야 true. NULL/너무짧음 → false.
 *   EN: Validate framing, length field, and trailing CRC-8.
 *   ex: 올바른 프레임 → true, crc 1비트 손상 → false
 * ------------------------------------------------------------------------- */
#define FRAME_START 0xFEu
bool frame_validate(const uint8_t *frame, size_t total_len) {
    (void)frame; (void)total_len;
    // TODO: implement
    return false;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q7.  complementary_filter  — IMU 기울기 추정 (센서 퓨전)
 *   KO: angle = alpha*(prev + gyro_rate*dt) + (1-alpha)*accel_angle.
 *       자이로 적분(고주파) + 가속도계(저주파 보정). alpha 보통 0.98.
 *   EN: Fuse gyro integration and accelerometer angle via a 1st-order blend.
 *   ex: alpha=1 → 순수 gyro 적분, alpha=0 → accel_angle 그대로.
 * ------------------------------------------------------------------------- */
float complementary_filter(float prev_angle, float gyro_rate,
                           float accel_angle, float dt, float alpha) {
    (void)prev_angle; (void)gyro_rate; (void)accel_angle; (void)dt; (void)alpha;
    // TODO: implement
    return 0.0f;   // placeholder
}

// --- Q8. 디바운스 구조체 (건드리지 말 것) ---
typedef struct {
    uint8_t history;   // 최근 8개 샘플
    bool    state;     // 디바운스된 안정 상태(true=눌림)
} debounce_t;

/* ---------------------------------------------------------------------------
 * Q8.  GPIO 디바운스 — 시프트 레지스터 히스토리
 *   KO: 매 tick raw 샘플을 8비트 history 에 밀어넣기.
 *       history==0xFF → 눌림 확정, 0x00 → 뗌 확정, 그 사이는 직전 상태 유지.
 *   EN: Shift raw sample into an 8-bit history; stable-high(0xFF)=pressed,
 *       stable-low(0x00)=released, otherwise hold. Return debounced state.
 *   ex: 8회 연속 high → true, 중간 1회 glitch 는 무시.
 * ------------------------------------------------------------------------- */
void debounce_init(debounce_t *d) {
    if (d) { d->history = 0x00u; d->state = false; }   // (초기화 제공)
    // TODO: (필요 시) 추가 초기화
}
bool debounce_update(debounce_t *d, bool raw) {
    (void)d; (void)raw;
    // TODO: implement
    return false;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q9.  stick_to_pwm  — 스틱 입력 → PWM 모터 명령 (saturating map)
 *   KO: 스틱 [-1000..+1000] → PWM [1000us..2000us] 선형 매핑. 중립=1500.
 *       범위 밖은 포화(clamp).  pwm = 1500 + stick/2 (먼저 클램프).
 *   EN: Linearly map & clamp RC stick to PWM pulse width.
 *   ex: 0→1500, 1000→2000, -1000→1000, 5000→2000(clamp)
 * ------------------------------------------------------------------------- */
uint16_t stick_to_pwm(int16_t stick) {
    (void)stick;
    // TODO: implement
    return 0;   // placeholder
}

// ===========================================================================
// main : 전체 PASS/FAIL  (하네스 — 건드리지 말 것)
// ===========================================================================
int main(void) {
    // -------- Q1. my_atoi --------
    printf("== Q1. my_atoi ==\n");
    TI("my_atoi(\"42\")",               my_atoi("42"),               42);
    TI("my_atoi(\"   -42\")",           my_atoi("   -42"),           -42);
    TI("my_atoi(\"4193 with words\")",  my_atoi("4193 with words"),  4193);
    TI("my_atoi(\"words and 987\")",    my_atoi("words and 987"),    0);
    TI("my_atoi(\"+123\")",             my_atoi("+123"),             123);
    TI("my_atoi(\"   +0123\")",         my_atoi("   +0123"),         123);
    TI("my_atoi(\"  \")",               my_atoi("  "),               0);
    TI("my_atoi(\"\")",                 my_atoi(""),                 0);
    TI("my_atoi(NULL)",                 my_atoi(NULL),               0);
    TI("my_atoi(\"+-2\")",              my_atoi("+-2"),              0);
    TI("my_atoi(\"-0\")",               my_atoi("-0"),               0);
    TI("my_atoi(\"2147483647\")",       my_atoi("2147483647"),       INT_MAX);
    TI("my_atoi(\"2147483648\") clamp", my_atoi("2147483648"),       INT_MAX);
    TI("my_atoi(\"-2147483648\")",      my_atoi("-2147483648"),      INT_MIN);
    TI("my_atoi(\"-91283472332\") clamp", my_atoi("-91283472332"),   INT_MIN);
    TI("my_atoi(\"99999999999\") clamp",  my_atoi("99999999999"),    INT_MAX);

    // -------- Q2/Q3. reverseBits --------
    printf("== Q2/Q3. reverseBits (basic & optimized) ==\n");
    TX("reverseBits(0x00000001)", reverseBits(0x00000001u), 0x80000000u);
    TX("reverseBits(0x80000000)", reverseBits(0x80000000u), 0x00000001u);
    TX("reverseBits(0xFFFFFFFF)", reverseBits(0xFFFFFFFFu), 0xFFFFFFFFu);
    TX("reverseBits(0x00000000)", reverseBits(0x00000000u), 0x00000000u);
    TX("reverseBits(0x00000005)", reverseBits(0x00000005u), 0xA0000000u);
    TX("reverseBits(0x0000000D)", reverseBits(0x0000000Du), 0xB0000000u);
    bool same = true;
    uint32_t seeds[] = {0u, 1u, 43261596u, 0xDEADBEEFu, 0x12345678u,
                        0x80000000u, 0xFFFFFFFFu, 0xA5A5A5A5u, 0x0F0F0F0Fu};
    for (size_t i = 0; i < sizeof(seeds)/sizeof(seeds[0]); i++)
        if (reverseBits(seeds[i]) != reverseBitsOptimized(seeds[i])) same = false;
    T("reverseBits == reverseBitsOptimized (9 seeds)", same);
    T("reverse(reverse(x)) == x", reverseBits(reverseBits(0x12345678u)) == 0x12345678u);

    // -------- Q4. reg_set_field --------
    printf("== Q4. reg_set_field (register RMW) ==\n");
    TX("set field[5:4]=2 in 0xFF", reg_set_field(0xFFu, 0x30u, 4, 0x2u), 0xEFu);
    TX("set field[3:0]=5 in 0xAA", reg_set_field(0xAAu, 0x0Fu, 0, 0x5u), 0xA5u);
    TX("oversized value truncated", reg_set_field(0x00u, 0x30u, 4, 0xFu), 0x30u);
    TX("clear field[7:4]", reg_set_field(0xFFu, 0xF0u, 4, 0x0u), 0x0Fu);
    TX("shift>=32 guarded", reg_set_field(0x1234u, 0xFFu, 40, 0x5u), 0x1234u);

    // -------- Q5. UART ring buffer --------
    printf("== Q5. UART ring buffer (SPSC) ==\n");
    uart_rb_t rb;
    uart_rb_init(&rb);
    uint8_t bv = 0;
    T("get on empty -> false", uart_rb_get(&rb, &bv) == false);
    T("put(NULL) -> false",    uart_rb_put(NULL, 1) == false);
    bool fill_ok = true;
    for (uint8_t i = 1; i <= 15; i++) fill_ok &= uart_rb_put(&rb, i);
    T("fill 15 (capacity SIZE-1)", fill_ok);
    T("put 16th -> false (full)",  uart_rb_put(&rb, 99) == false);
    bool order_ok = true;
    for (uint8_t i = 1; i <= 15; i++) { uart_rb_get(&rb, &bv); if (bv != i) order_ok = false; }
    T("drain 15 -> FIFO 1..15", order_ok);
    T("empty after drain", uart_rb_get(&rb, &bv) == false);
    bool interleave_ok = true;
    uint8_t expect = 0;
    for (uint16_t round = 0; round < 100; round++) {
        for (int k = 0; k < 10; k++) uart_rb_put(&rb, (uint8_t)((round * 10 + k) & 0xFF));
        for (int k = 0; k < 10; k++) {
            if (!uart_rb_get(&rb, &bv) || bv != (uint8_t)(expect & 0xFF)) interleave_ok = false;
            expect++;
        }
    }
    T("ISR/main interleave 1000 bytes, FIFO, wrap", interleave_ok);

    // -------- Q6. CRC-8 + frame_validate --------
    printf("== Q6. CRC-8 telemetry frame ==\n");
    TX("crc8(\"123456789\")", crc8((const uint8_t *)"123456789", 9), 0xF4u);
    TX("crc8(empty)", crc8((const uint8_t *)"", 0), 0x00u);
    uint8_t payload[] = {0x11, 0x22, 0x33};
    uint8_t frame[8];
    frame[0] = FRAME_START;
    frame[1] = (uint8_t)sizeof(payload);
    for (size_t i = 0; i < sizeof(payload); i++) frame[2 + i] = payload[i];
    size_t flen = 2 + sizeof(payload) + 1;
    frame[flen - 1] = crc8(&frame[1], flen - 2);
    T("valid frame accepted", frame_validate(frame, flen) == true);
    uint8_t badc = frame[flen - 1]; frame[flen - 1] ^= 0xFF;
    T("corrupted crc rejected", frame_validate(frame, flen) == false);
    frame[flen - 1] = badc;
    frame[2] ^= 0x01;
    T("corrupted payload rejected", frame_validate(frame, flen) == false);
    frame[2] ^= 0x01;
    frame[0] = 0x00;
    T("bad start byte rejected", frame_validate(frame, flen) == false);
    frame[0] = FRAME_START;
    T("length mismatch rejected", frame_validate(frame, flen + 1) == false);
    T("NULL frame rejected", frame_validate(NULL, 6) == false);
    T("too-short frame rejected", frame_validate(frame, 2) == false);

    // -------- Q7. complementary filter --------
    printf("== Q7. IMU complementary filter ==\n");
    T("alpha=1 -> pure gyro integrate",
      fabsf(complementary_filter(10.0f, 2.0f, 45.0f, 0.01f, 1.0f) - 10.02f) < 1e-4f);
    T("alpha=0 -> pure accel",
      fabsf(complementary_filter(10.0f, 2.0f, 45.0f, 0.01f, 0.0f) - 45.0f) < 1e-4f);
    T("alpha=0.98 blend",
      fabsf(complementary_filter(10.0f, 100.0f, 20.0f, 0.01f, 0.98f) - 11.18f) < 1e-3f);
    float ang = 0.0f;
    for (int i = 0; i < 2000; i++) ang = complementary_filter(ang, 0.0f, 30.0f, 0.01f, 0.98f);
    T("converges toward accel angle", fabsf(ang - 30.0f) < 0.5f);

    // -------- Q8. GPIO debounce --------
    printf("== Q8. GPIO debounce (shift-register) ==\n");
    debounce_t db;
    debounce_init(&db);
    T("init -> released", db.state == false);
    debounce_update(&db, true); debounce_update(&db, false); debounce_update(&db, true);
    T("chatter -> still released", db.state == false);
    bool pressed = false;
    for (int i = 0; i < 8; i++) pressed = debounce_update(&db, true);
    T("8x high -> pressed", pressed == true);
    T("single glitch low -> hold pressed", debounce_update(&db, false) == true);
    bool rel = true;
    for (int i = 0; i < 8; i++) rel = debounce_update(&db, false);
    T("8x low -> released", rel == false);
    T("debounce_update(NULL) -> false", debounce_update(NULL, true) == false);

    // -------- Q9. stick_to_pwm --------
    printf("== Q9. stick_to_pwm (saturating map) ==\n");
    TI("stick 0 -> 1500 (neutral)",  stick_to_pwm(0),      1500);
    TI("stick 1000 -> 2000 (max)",   stick_to_pwm(1000),   2000);
    TI("stick -1000 -> 1000 (min)",  stick_to_pwm(-1000),  1000);
    TI("stick 500 -> 1750",          stick_to_pwm(500),    1750);
    TI("stick -500 -> 1250",         stick_to_pwm(-500),   1250);
    TI("stick 5000 -> 2000 (clamp)", stick_to_pwm(5000),   2000);
    TI("stick -5000 -> 1000 (clamp)",stick_to_pwm(-5000),  1000);

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
