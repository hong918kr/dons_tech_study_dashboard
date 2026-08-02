// 10_anduril_signature.c  —  REFERENCE SOLUTION
// Anduril 시그니처 & 드론 응용 (Signature & drone-flavored)  —  Q1~Q9
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 10_anduril_signature.c -o /tmp/andb_anduril_signature
//
// 실제로 재현된 Anduril 인터뷰 2문항에서 출발한다:
//   Q1. my_atoi  — INT 오버플로 클램핑(INT_MAX/INT_MIN), 공백/부호/꼬리 잡음/NULL
//   Q2. reverseBits           — 32비트 반전, 기본 루프
//   Q3. reverseBitsOptimized  — O(log n) 분할정복 mask/shift 트릭
// 여기에 Anduril TRS(트래킹/무선/센서) 펌웨어 감각의 드론 문제 6개를 붙인다:
//   Q4. I2C 레지스터 read-modify-write (다른 필드 안 건드리고 한 필드만 설정)
//   Q5. UART 링버퍼 ISR 생산자 + main 소비자 (SPSC)
//   Q6. CRC-8 텔레메트리 프레임 검증 (MAVLink 풍 header+payload+crc)
//   Q7. IMU 상보 필터(complementary filter)로 기울기 추정 (센서 퓨전)
//   Q8. GPIO 디바운스 — 시프트 레지스터 히스토리
//   Q9. 스틱 입력 → PWM 모터 명령 매핑 (saturating clamp/scale)
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <ctype.h>
#include <math.h>

// ===========================================================================
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
// 불리언/조건 검사
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)
// 정수 검사 — 결과와 기대값을 함께 출력
#define TI(label, got, want) do {                                         \
    long _g = (long)(got), _w = (long)(want);                             \
    if (_g == _w) { printf("  [PASS] %s => %ld\n", (label), _g); g_pass++; } \
    else { printf("  [FAIL] %s => %ld (expected %ld)\n", (label), _g, _w); g_fail++; } \
} while (0)
// 16진 검사
#define TX(label, got, want) do {                                              \
    unsigned long _g = (unsigned long)(got), _w = (unsigned long)(want);       \
    if (_g == _w) { printf("  [PASS] %s => 0x%lX\n", (label), _g); g_pass++; }  \
    else { printf("  [FAIL] %s => 0x%lX (expected 0x%lX)\n", (label), _g, _w); g_fail++; } \
} while (0)

// ===========================================================================
// Q1.  my_atoi  — 문자열 → int, 오버플로 클램핑
// ---------------------------------------------------------------------------
// 규칙(LeetCode strtol 계열):
//   1) 선행 공백 스킵
//   2) 선택적 부호(+/-) 1개
//   3) 연속된 숫자만 변환, 첫 비숫자에서 정지 (꼬리 잡음 무시)
//   4) 오버플로 시 INT_MAX / INT_MIN 으로 클램핑
//   5) NULL / 빈 문자열 / 숫자 없음 → 0
// ===========================================================================
int my_atoi(const char *str) {
    if (str == NULL) return 0;

    int i = 0;
    // 1) 선행 공백. isspace 인자는 unsigned char 로 캐스팅(음수 char UB 회피).
    while (isspace((unsigned char)str[i])) i++;

    // 2) 부호
    int sign = 1;
    if (str[i] == '+' || str[i] == '-') {
        if (str[i] == '-') sign = -1;
        i++;
    }

    // 3) 숫자 변환 + 4) 곱셈 전에 오버플로 선검사
    int result = 0;
    while (isdigit((unsigned char)str[i])) {
        int digit = str[i] - '0';
        // result*10 + digit 가 INT_MAX 를 넘는가?  곱하기 전에 검사한다.
        if (result > INT_MAX / 10 ||
            (result == INT_MAX / 10 && digit > INT_MAX % 10)) {
            return (sign == 1) ? INT_MAX : INT_MIN;
        }
        result = result * 10 + digit;
        i++;
    }
    return result * sign;
}

// ===========================================================================
// Q2.  reverseBits  — 32비트 비트 순서 반전, 기본 루프 O(32)
// ---------------------------------------------------------------------------
// n 의 LSB 를 하나씩 뽑아 reversed 의 위쪽으로 밀어 넣는다.
// ===========================================================================
uint32_t reverseBits(uint32_t n) {
    uint32_t reversed = 0;
    for (int i = 0; i < 32; i++) {
        reversed = (reversed << 1) | (n & 1u); // reversed 왼쪽으로, n 의 최하위 비트 삽입
        n >>= 1;                                // n 오른쪽으로
    }
    return reversed;
}

// ===========================================================================
// Q3.  reverseBitsOptimized  — 분할정복 O(log n), 마스크/시프트 트릭
// ---------------------------------------------------------------------------
// "인접 블록 스왑"을 블록 크기 16→8→4→2→1 로 반씩 줄이며 반복.
// 각 단계에서 마스크로 좌/우 절반을 골라 서로 반대 방향으로 시프트해 교환한다.
//   0xAAAAAAAA = 홀수 위치 비트,  0x55555555 = 짝수 위치 비트 (1비트 스왑)
//   0xCCCCCCCC / 0x33333333 = 2비트 블록,  0xF0.. / 0x0F.. = 4비트(니블) …
// 총 5단계로 32비트를 완전히 뒤집는다. 루프/분기 없음.
// ===========================================================================
uint32_t reverseBitsOptimized(uint32_t n) {
    n = (n >> 16) | (n << 16);                                   // 16비트 반쪽 스왑
    n = ((n & 0xFF00FF00u) >> 8)  | ((n & 0x00FF00FFu) << 8);    // 8비트(바이트)
    n = ((n & 0xF0F0F0F0u) >> 4)  | ((n & 0x0F0F0F0Fu) << 4);    // 4비트(니블)
    n = ((n & 0xCCCCCCCCu) >> 2)  | ((n & 0x33333333u) << 2);    // 2비트
    n = ((n & 0xAAAAAAAAu) >> 1)  | ((n & 0x55555555u) << 1);    // 1비트
    return n;
}

// ===========================================================================
// Q4.  reg_set_field  — I2C/MMIO 레지스터 read-modify-write
// ---------------------------------------------------------------------------
// mask 로 지정된 "필드"만 새 value 로 갈아끼우고 나머지 비트는 보존한다.
//   new = (reg & ~mask) | ((value << shift) & mask)
// 실무: volatile 하드웨어 레지스터를 읽어 필드만 바꾸고 다시 쓰는 정석 패턴.
// shift >= 32 는 시프트폭 UB 이므로 방어(필드 없음으로 취급, 원본 유지).
// ===========================================================================
uint32_t reg_set_field(uint32_t reg, uint32_t mask, uint8_t shift, uint32_t value) {
    if (shift >= 32u) return reg;                 // 시프트폭 가드
    uint32_t placed = (value << shift) & mask;    // value 를 필드 위치로, 넘치는 비트는 mask 로 절삭
    return (reg & ~mask) | placed;                // 필드 클리어 후 새 값 OR
}

// ===========================================================================
// Q5.  UART 링버퍼 — ISR 생산자 / main 소비자 (SPSC)
// ---------------------------------------------------------------------------
// 실기체: UART RX 인터럽트가 바이트를 put, main loop 가 get 으로 소비.
// head 는 생산자(ISR)만, tail 은 소비자(main)만 쓴다 → 단일코어에선 무락.
//   * 실제 하드웨어에선 head/tail 을 volatile 로 두고 "데이터 먼저, 인덱스 나중"
//     순서를 지킨다(멀티코어는 acquire/release 필요). 여기선 단일스레드 검증.
// power-of-two 크기 + 마스킹, 한 칸을 희생해 full/empty 구분(head==tail=empty).
// ===========================================================================
#define URB_SIZE 16u                 // 반드시 2의 거듭제곱
typedef struct {
    uint8_t  buf[URB_SIZE];
    volatile uint32_t head;          // 생산자(ISR)만 store
    volatile uint32_t tail;          // 소비자(main)만 store
} uart_rb_t;

void uart_rb_init(uart_rb_t *rb) {
    if (!rb) return;
    rb->head = 0;
    rb->tail = 0;
}

// 생산자(ISR) 컨텍스트. 가득 차면 false(드롭). 사용 용량 = SIZE-1.
bool uart_rb_put(uart_rb_t *rb, uint8_t byte) {
    if (!rb) return false;
    uint32_t next = (rb->head + 1u) & (URB_SIZE - 1u);
    if (next == (rb->tail & (URB_SIZE - 1u))) return false;   // full
    rb->buf[rb->head & (URB_SIZE - 1u)] = byte;               // 1) 데이터
    rb->head = next;                                          // 2) 인덱스 공개
    return true;
}

// 소비자(main) 컨텍스트. 비면 false.
bool uart_rb_get(uart_rb_t *rb, uint8_t *out) {
    if (!rb || !out) return false;
    if ((rb->head & (URB_SIZE - 1u)) == (rb->tail & (URB_SIZE - 1u)))
        return false;                                          // empty
    *out = rb->buf[rb->tail & (URB_SIZE - 1u)];
    rb->tail = (rb->tail + 1u) & (URB_SIZE - 1u);
    return true;
}

// ===========================================================================
// Q6.  CRC-8 + 텔레메트리 프레임 검증
// ---------------------------------------------------------------------------
// CRC-8 (poly 0x07, init 0x00, MSB-first) — 텔레메트리 무결성 검사에 흔함.
// 프레임 레이아웃(MAVLink 풍):
//   [0] START(0xFE)  [1] payload_len  [2..] payload…  [last] crc
//   crc 는 START 를 제외한 { len 바이트 + payload } 위에서 계산.
// ===========================================================================
uint8_t crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    if (!data) return crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80u) crc = (uint8_t)((crc << 1) ^ 0x07u);
            else             crc = (uint8_t)(crc << 1);
        }
    }
    return crc;
}

#define FRAME_START 0xFEu
bool frame_validate(const uint8_t *frame, size_t total_len) {
    if (!frame) return false;
    if (total_len < 3) return false;                 // 최소: start+len+crc
    if (frame[0] != FRAME_START) return false;       // 시작 바이트
    size_t payload_len = frame[1];
    if (payload_len + 3u != total_len) return false; // 길이 필드 일관성
    // crc 는 frame[1 .. total_len-2] (len + payload), 마지막 바이트가 기대 crc
    uint8_t want = frame[total_len - 1];
    uint8_t got  = crc8(&frame[1], total_len - 2);
    return got == want;
}

// ===========================================================================
// Q7.  IMU 상보 필터 (complementary filter) — 기울기 각도 추정
// ---------------------------------------------------------------------------
// 자이로(gyro) 적분은 단기 정확·장기 드리프트, 가속도계(accel)는 장기 정확·단기 노이즈.
// 둘을 섞는다:  angle = a*(prev + gyro*dt) + (1-a)*accel_angle
//   a(alpha) 는 보통 0.98 부근 — 자이로에 무게, 저주파는 accel 로 보정.
// ===========================================================================
float complementary_filter(float prev_angle, float gyro_rate,
                           float accel_angle, float dt, float alpha) {
    float gyro_est = prev_angle + gyro_rate * dt;          // 자이로 적분(고주파)
    return alpha * gyro_est + (1.0f - alpha) * accel_angle; // accel 로 저주파 보정
}

// ===========================================================================
// Q8.  GPIO 디바운스 — 시프트 레지스터 히스토리
// ---------------------------------------------------------------------------
// 매 tick 원시 샘플(raw)을 8비트 히스토리에 밀어 넣는다.
//   history==0xFF (연속 8회 1) → 눌림 확정,  0x00 (연속 8회 0) → 뗌 확정,
//   그 사이(바운싱)에는 직전 확정 상태 유지.  기계식 스위치 채터 제거의 정석.
// ===========================================================================
typedef struct {
    uint8_t history;   // 최근 8개 샘플
    bool    state;     // 디바운스된 안정 상태(true=눌림)
} debounce_t;

void debounce_init(debounce_t *d) {
    if (!d) return;
    d->history = 0x00u;
    d->state   = false;
}

bool debounce_update(debounce_t *d, bool raw) {
    if (!d) return false;
    d->history = (uint8_t)((d->history << 1) | (raw ? 1u : 0u));
    if (d->history == 0xFFu)      d->state = true;   // 안정적 눌림
    else if (d->history == 0x00u) d->state = false;  // 안정적 뗌
    // else: 바운싱 구간 → 이전 상태 유지
    return d->state;
}

// ===========================================================================
// Q9.  스틱 입력 → PWM 모터 명령 (saturating map/clamp)
// ---------------------------------------------------------------------------
// RC 스틱 [-1000 .. +1000] 을 PWM 펄스폭 [1000us .. 2000us] 로 선형 매핑.
//   중립(0)=1500us, 최대=2000us, 최소=1000us.  범위 밖은 포화(clamp).
//   pwm = 1500 + stick/2   (stick 을 먼저 [-1000,1000] 로 클램프)
// ===========================================================================
uint16_t stick_to_pwm(int16_t stick) {
    if (stick < -1000) stick = -1000;   // 하한 포화
    if (stick >  1000) stick =  1000;   // 상한 포화
    return (uint16_t)(1500 + stick / 2);
}

// ===========================================================================
// main : 전체 PASS/FAIL
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
    TI("my_atoi(\"+-2\")",              my_atoi("+-2"),              0);   // 부호 뒤 비숫자
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
    // 두 구현이 임의 입력에서 완전히 일치해야 한다
    bool same = true;
    uint32_t seeds[] = {0u, 1u, 43261596u, 0xDEADBEEFu, 0x12345678u,
                        0x80000000u, 0xFFFFFFFFu, 0xA5A5A5A5u, 0x0F0F0F0Fu};
    for (size_t i = 0; i < sizeof(seeds)/sizeof(seeds[0]); i++)
        if (reverseBits(seeds[i]) != reverseBitsOptimized(seeds[i])) same = false;
    T("reverseBits == reverseBitsOptimized (9 seeds)", same);
    // 이중 반전 = 원본 (involution)
    T("reverse(reverse(x)) == x", reverseBits(reverseBits(0x12345678u)) == 0x12345678u);

    // -------- Q4. reg_set_field --------
    printf("== Q4. reg_set_field (register RMW) ==\n");
    // reg=0xFF 에서 필드 [5:4](mask 0x30, shift 4) 를 0b10 으로 → 0xEF
    TX("set field[5:4]=2 in 0xFF", reg_set_field(0xFFu, 0x30u, 4, 0x2u), 0xEFu);
    // 다른 비트 보존: reg=0xAA 에서 필드 [3:0] 를 0x5 로 → 상위 니블 0xA 유지
    TX("set field[3:0]=5 in 0xAA", reg_set_field(0xAAu, 0x0Fu, 0, 0x5u), 0xA5u);
    // 값이 필드보다 크면 mask 로 절삭(넘침 비트가 이웃을 안 건드림)
    TX("oversized value truncated", reg_set_field(0x00u, 0x30u, 4, 0xFu), 0x30u);
    // 필드 클리어(value=0)
    TX("clear field[7:4]", reg_set_field(0xFFu, 0xF0u, 4, 0x0u), 0x0Fu);
    // shift>=32 방어 → 원본 유지
    TX("shift>=32 guarded", reg_set_field(0x1234u, 0xFFu, 40, 0x5u), 0x1234u);

    // -------- Q5. UART ring buffer --------
    printf("== Q5. UART ring buffer (SPSC) ==\n");
    uart_rb_t rb;
    uart_rb_init(&rb);
    uint8_t bv = 0;
    T("get on empty -> false", uart_rb_get(&rb, &bv) == false);
    T("put(NULL) -> false",    uart_rb_put(NULL, 1) == false);
    // 용량 = SIZE-1 = 15
    bool fill_ok = true;
    for (uint8_t i = 1; i <= 15; i++) fill_ok &= uart_rb_put(&rb, i);
    T("fill 15 (capacity SIZE-1)", fill_ok);
    T("put 16th -> false (full)",  uart_rb_put(&rb, 99) == false);
    // FIFO 순서 소비
    bool order_ok = true;
    for (uint8_t i = 1; i <= 15; i++) { uart_rb_get(&rb, &bv); if (bv != i) order_ok = false; }
    T("drain 15 -> FIFO 1..15", order_ok);
    T("empty after drain", uart_rb_get(&rb, &bv) == false);
    // ISR/main 인터리브 시뮬레이션: 반씩 넣고 빼며 랩어라운드 통과
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
    // 알려진 벡터: CRC-8/SMBUS("123456789") = 0xF4
    TX("crc8(\"123456789\")", crc8((const uint8_t *)"123456789", 9), 0xF4u);
    TX("crc8(empty)", crc8((const uint8_t *)"", 0), 0x00u);
    // 유효 프레임 구성: payload {0x11,0x22,0x33}
    uint8_t payload[] = {0x11, 0x22, 0x33};
    uint8_t frame[8];
    frame[0] = FRAME_START;
    frame[1] = (uint8_t)sizeof(payload);           // len=3
    for (size_t i = 0; i < sizeof(payload); i++) frame[2 + i] = payload[i];
    size_t flen = 2 + sizeof(payload) + 1;         // start+len+payload+crc = 6
    frame[flen - 1] = crc8(&frame[1], flen - 2);   // crc over len+payload
    T("valid frame accepted", frame_validate(frame, flen) == true);
    // crc 손상 → 거부
    uint8_t bad = frame[flen - 1]; frame[flen - 1] ^= 0xFF;
    T("corrupted crc rejected", frame_validate(frame, flen) == false);
    frame[flen - 1] = bad;
    // 페이로드 1비트 손상 → 거부
    frame[2] ^= 0x01;
    T("corrupted payload rejected", frame_validate(frame, flen) == false);
    frame[2] ^= 0x01;
    // 잘못된 시작 바이트
    frame[0] = 0x00;
    T("bad start byte rejected", frame_validate(frame, flen) == false);
    frame[0] = FRAME_START;
    // 길이 필드 불일치
    T("length mismatch rejected", frame_validate(frame, flen + 1) == false);
    T("NULL frame rejected", frame_validate(NULL, 6) == false);
    T("too-short frame rejected", frame_validate(frame, 2) == false);

    // -------- Q7. complementary filter --------
    printf("== Q7. IMU complementary filter ==\n");
    // 순수 gyro (alpha=1): prev=10, rate=2 rad? deg/s, dt=0.01 → 10 + 0.02 = 10.02
    T("alpha=1 -> pure gyro integrate",
      fabsf(complementary_filter(10.0f, 2.0f, 45.0f, 0.01f, 1.0f) - 10.02f) < 1e-4f);
    // 순수 accel (alpha=0) → accel_angle 그대로
    T("alpha=0 -> pure accel",
      fabsf(complementary_filter(10.0f, 2.0f, 45.0f, 0.01f, 0.0f) - 45.0f) < 1e-4f);
    // 혼합: 0.98*(10 + 100*0.01) + 0.02*20 = 0.98*11 + 0.4 = 10.78 + 0.4 = 11.18
    T("alpha=0.98 blend",
      fabsf(complementary_filter(10.0f, 100.0f, 20.0f, 0.01f, 0.98f) - 11.18f) < 1e-3f);
    // 수렴: accel 고정값으로 반복하면 그 값으로 수렴 (gyro_rate=0)
    float ang = 0.0f;
    for (int i = 0; i < 2000; i++) ang = complementary_filter(ang, 0.0f, 30.0f, 0.01f, 0.98f);
    T("converges toward accel angle", fabsf(ang - 30.0f) < 0.5f);

    // -------- Q8. GPIO debounce --------
    printf("== Q8. GPIO debounce (shift-register) ==\n");
    debounce_t db;
    debounce_init(&db);
    T("init -> released", db.state == false);
    // 바운싱 채터: 몇 번 튀어도 아직 확정 아님
    debounce_update(&db, true); debounce_update(&db, false); debounce_update(&db, true);
    T("chatter -> still released", db.state == false);
    // 8회 연속 1 → 눌림 확정
    bool pressed = false;
    for (int i = 0; i < 8; i++) pressed = debounce_update(&db, true);
    T("8x high -> pressed", pressed == true);
    // 한 번 튀어도(짧은 low) 유지
    T("single glitch low -> hold pressed", debounce_update(&db, false) == true);
    // 8회 연속 0 → 뗌 확정
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
