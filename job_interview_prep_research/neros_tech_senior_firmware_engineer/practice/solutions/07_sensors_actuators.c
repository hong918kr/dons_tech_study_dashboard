// 07_sensors_actuators.c  —  REFERENCE SOLUTION
// 센서 → 필터 → RC 링크 → 믹서 → ESC : 드론 신호 경로 한 바퀴  —  Q1~Q6
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=07_sensors_actuators
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g solutions/07_sensors_actuators.c -o /tmp/n07 && /tmp/n07
//
// 이 세트가 다루는 신호 경로 (FPV 드론 한 프레임, 보통 1~8 kHz 로 반복):
//   [1] gyro/accel --(SPI burst read)--> raw 6바이트
//   [2] BE int16 디코드 -> LSB -> mdps 정수 스케일 (Q2)
//   [3] bias 제거 + moving-avg / median / 1차 IIR 로 프롭 진동 억제 (Q3, Q4)
//   [4] PID (이 세트 밖) -> roll/pitch/yaw 명령
//   [5] RC 링크(CRSF/ELRS) 언팩 -> throttle + failsafe 판정 (Q5)
//   [6] quad-X 믹서 -> DShot/PWM 프레임 -> ESC (Q6)
//
// 왜 Neros 인가: FPV strike drone 은 (a) 재밍당하는 링크 위에서 날고 (b) 프롭 진동이
// 자이로를 뒤덮으며 (c) 루프 한 번이라도 밀리면 기체가 뒤집힌다. 위 6단계 중 한 칸만
// 어긋나도 기체가 떨어지므로, 펌웨어 면접에서는 "각 링크가 무엇을 보장하는가"를 묻는다.
// 그리고 이 경로에는 float 이 없어도 된다 — 전부 정수/고정소수점으로 결정적으로 돈다.
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

// ===========================================================================
// Q1 : 가짜 IMU 디바이스 + 버스 (하네스 — 문제가 아니라 시뮬레이터)
// ---------------------------------------------------------------------------
// 실물 MPU6000/ICM-42688 계열을 흉내낸다. 레지스터 공간 128바이트, 그리고
// "한 번의 트랜잭션"을 세는 카운터 하나. 버스 트랜잭션 횟수가 곧 버스 점유 시간이고,
// 8 kHz 루프에서는 그게 CPU 예산의 절반을 좌우한다.
//
// SPI 규약 : 첫 바이트 = [R/W(1) | reg(7)].  읽기는 MSB=1, 쓰기는 MSB=0.
//            -> 레지스터 주소는 7비트뿐이므로 이 디바이스는 0x00~0x7F.
// I2C 규약 : START + [addr|W] + reg  (레지스터 포인터 세팅) + REPEATED START +
//            [addr|R] + 데이터...  디바이스 안에 "레지스터 포인터"가 있고, 읽을 때마다
//            자동 증가한다. 그래서 burst read 가 공짜로 된다.
// ===========================================================================
#define IMU_NREG            128u
#define IMU_REG_WHO_AM_I    0x75
#define IMU_REG_PWR_MGMT_1  0x6B
#define IMU_REG_CONFIG      0x1A
#define IMU_REG_GYRO_CFG    0x1B
#define IMU_REG_GYRO_XOUT_H 0x43   // 0x43..0x48 = X,Y,Z 각 2바이트 big-endian

#define SPI_READ_BIT 0x80u         // 첫 바이트의 MSB = 1 이면 read

static uint8_t  g_dev_reg[IMU_NREG];
static uint32_t g_bus_txn;          // 버스 트랜잭션(=START..STOP) 횟수

// 한 번 호출 = 한 번의 트랜잭션. tx 가 있으면 write, rx 가 있으면 read.
// 범위를 벗어나면 NACK 처럼 false 를 돌려준다. (건드리지 말 것)
static bool bus_txn(uint8_t reg, const uint8_t *tx, uint8_t *rx, size_t n) {
    if (n == 0) return false;
    if ((size_t)reg + n > IMU_NREG) return false;      // 주소 공간 밖 -> NACK
    g_bus_txn++;                                        // 트랜잭션 1회 과금
    if (tx) { for (size_t i = 0; i < n; ++i) g_dev_reg[reg + i] = tx[i]; return true; }
    if (rx) { for (size_t i = 0; i < n; ++i) rx[i] = g_dev_reg[reg + i]; return true; }
    return false;
}

/* ---------------------------------------------------------------------------
 * Q1.  레지스터 접근 4종 : write / read / read-modify-write / burst read
 *   KO: imu_write_reg/imu_read_reg 는 1바이트짜리 단일 트랜잭션. imu_modify_reg 는
 *       read -> (cur & ~mask) | (value & mask) -> write 의 정석 RMW 로, 마스크 밖
 *       비트를 절대 건드리지 않는다. imu_burst_read 는 n 바이트를 "한 번의"
 *       트랜잭션으로 읽는다.
 *   EN: Register access helpers over a fake bus. modify_reg must be a true
 *       read-modify-write; burst_read must cost exactly one bus transaction.
 *   ex: 자이로 6바이트를 single read 6번으로 읽으면 트랜잭션 6회, burst 면 1회
 *   note: 왜 burst 가 중요한가 —
 *     (1) 원자성: X 를 읽고 Y 를 읽는 사이에 디바이스가 새 샘플을 써버리면 X 는 옛
 *         샘플, Y 는 새 샘플이 되어 "찢어진 샘플(torn sample)"이 나온다. 자세 추정이
 *         존재하지 않는 회전을 본다.
 *     (2) 버스 시간: 트랜잭션마다 주소/오버헤드 바이트가 붙는다. I2C 400kHz 에서
 *         1바이트 읽기 ~ 50µs vs 6바이트 burst ~ 90µs. 8kHz 루프 예산은 125µs다.
 *     (3) 인터럽트/DMA: burst 는 DMA 한 방에 넘길 수 있어 CPU 가 0 사이클을 쓴다.
 * ------------------------------------------------------------------------- */
bool imu_write_reg(uint8_t reg, uint8_t val) {
    // SPI 라면 첫 바이트는 (reg & 0x7F) — write 는 MSB=0.
    return bus_txn(reg, &val, NULL, 1);
}

// 실패를 구분할 수 없는 API 라는 점에 주의(0 이 정상 값일 수 있다).
// 실무에서는 bool imu_read_reg(reg, uint8_t *out) 형태를 쓰는 게 맞다.
uint8_t imu_read_reg(uint8_t reg) {
    uint8_t v = 0;
    // SPI 라면 첫 바이트는 (reg | SPI_READ_BIT) — read 는 MSB=1.
    if (!bus_txn(reg, NULL, &v, 1)) return 0;
    return v;
}

bool imu_modify_reg(uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t cur = 0;
    if (!bus_txn(reg, NULL, &cur, 1)) return false;           // 1) read
    uint8_t next = (uint8_t)((cur & (uint8_t)~mask)           // 2) modify
                           | (value & mask));                 //    마스크 밖 비트 보존
    if (next == cur) return true;                             // 쓸 필요 없으면 버스 절약
    return bus_txn(reg, &next, NULL, 1);                      // 3) write
}

bool imu_burst_read(uint8_t start_reg, uint8_t *out, size_t n) {
    if (!out || n == 0) return false;
    return bus_txn(start_reg, NULL, out, n);                  // 정확히 1 트랜잭션
}

// SPI 첫 바이트(커맨드) 생성 — 규약을 코드로 못박아 둔다.
uint8_t spi_cmd_read(uint8_t reg)
{
    return (uint8_t)(reg | SPI_READ_BIT);     // MSB=1 -> read
}

uint8_t spi_cmd_write(uint8_t reg)
{
    return (uint8_t)(reg & 0x7Fu);            // MSB=0 -> write, 주소는 7비트
}

// ===========================================================================
// Q2 : 샘플 디코드 (big-endian int16) + 정수 스케일링
// ---------------------------------------------------------------------------
// 센서는 거의 항상 big-endian(MSB first)으로 준다. MCU 는 거의 항상 little-endian.
// 그래서 memcpy 로 int16 배열에 부으면 바이트가 뒤집힌 채로 조용히 잘못 난다.
// wire format 은 언제나 바이트 단위로 손으로 조립한다.
// ===========================================================================

/* ---------------------------------------------------------------------------
 * Q2.  raw 6바이트 -> X/Y/Z int16,  그리고 LSB -> milli-dps 변환
 *   KO: raw6 = [XH XL YH YL ZH ZL] (big-endian, 2의 보수). uint16 으로 조립한 뒤
 *       int16 으로 캐스팅한다(부호 확장은 캐스팅이 해준다).
 *       gyro_to_mdps: full-scale ±dps_per_full_scale 가 ±32768 LSB 에 대응하므로
 *       mdps = lsb * dps_fs * 1000 / 32768,  반올림은 half-away-from-zero.
 *   EN: Decode big-endian int16 triplet; convert LSB to milli-degrees/s with
 *       integer math only, correct rounding, and no intermediate overflow.
 *   ex: raw = {0xFF,0xFE, 0x00,0x01, 0x80,0x00} -> x=-2, y=1, z=-32768
 *       gyro_to_mdps(27, 2000) = 1648  (버림이면 1647 — 반올림이 필요한 이유)
 *   note: 곱셈 "전에" 넓힌다. lsb*dps_fs*1000 은 최대 6.5e10 이라 int32 를 넘긴다.
 *         결과는 최대 ±2,000,000 mdps 라 int32 에 안전하게 들어온다.
 *         float 은 쓰지 않는다 — Cortex-M4F 라도 ISR 에서 FPU 컨텍스트 저장은 비싸고,
 *         M0/M3 에는 FPU 가 아예 없다.
 * ------------------------------------------------------------------------- */
void imu_decode_xyz(const uint8_t *raw6, int16_t *x, int16_t *y, int16_t *z) {
    if (!raw6) return;
    if (x) *x = (int16_t)(((uint16_t)raw6[0] << 8) | (uint16_t)raw6[1]);
    if (y) *y = (int16_t)(((uint16_t)raw6[2] << 8) | (uint16_t)raw6[3]);
    if (z) *z = (int16_t)(((uint16_t)raw6[4] << 8) | (uint16_t)raw6[5]);
}

int32_t gyro_to_mdps(int16_t lsb, uint16_t dps_per_full_scale) {
    const int64_t den = 32768;                       // full scale = 2^15 LSB
    int64_t num = (int64_t)lsb * (int64_t)dps_per_full_scale * 1000;  // 먼저 넓힌다
    int64_t q = (num >= 0) ? (num + den / 2) / den   // half-away-from-zero
                           : (num - den / 2) / den;  // (C99 나눗셈은 0 방향 절삭)
    return (int32_t)q;
}

// ===========================================================================
// Q3 : 자이로 bias 캘리브레이션 + 포화 감산
// ---------------------------------------------------------------------------
// 부팅 직후 기체를 가만히 둔 채 N 샘플을 평균 내면 그게 zero-rate offset 이다.
// 이걸 안 빼면 적분값(각도)이 분당 몇 도씩 드리프트한다.
// 온도에 따라 변하므로 실기에서는 "arm 직전에 다시" 재는 게 정석.
// ===========================================================================
typedef struct {
    int32_t  sum_x, sum_y, sum_z;   // 누적 (int32 로 충분: 32767 * 4096 < 1.4e8)
    uint32_t n;                     // 누적 샘플 수
    int16_t  bx, by, bz;            // finish 후의 bias
    bool     ready;                 // finish 가 성공했는가
} bias_t;

/* ---------------------------------------------------------------------------
 * Q3.  bias 누적 / 확정(정수 평균) / 포화 감산
 *   KO: accumulate 는 합과 개수만 늘린다. finish 는 정수 평균(반올림)을 내고
 *       ready=true. n==0 이면 bias 0 + ready=false.
 *       apply_bias_sat 은 int32 로 빼고 INT16_MIN/INT16_MAX 로 clamp 한다.
 *   EN: Accumulate N stationary samples, take an integer (rounded) mean, and
 *       subtract with saturation to int16.
 *   ex: 10,11,11,11 누적 -> 평균 10.75 -> bias 11 (반올림)
 *       apply_bias_sat(32767, -100) = 32767 (랩어라운드 금지)
 *   note: int16 끼리 그냥 빼면 32767 - (-100) = 32867 이 랩되어 -32669 가 된다.
 *         부호 뒤집힌 자이로 = 즉시 크래시. 그래서 "넓혀서 계산, clamp 해서 저장".
 * ------------------------------------------------------------------------- */
void gyro_bias_accumulate(bias_t *b, int16_t x, int16_t y, int16_t z) {
    if (!b) return;
    b->sum_x += x;
    b->sum_y += y;
    b->sum_z += z;
    b->n++;
}

// half-away-from-zero 정수 나눗셈 (C99 의 기본 나눗셈은 0 방향 절삭이라 편향이 생긴다)
int32_t idiv_round(int32_t num, int32_t den) {
    if (den == 0) return 0;
    return (num >= 0) ? (num + den / 2) / den
                      : (num - den / 2) / den;
}

void gyro_bias_finish(bias_t *b) {
    if (!b) return;
    if (b->n == 0) { b->bx = b->by = b->bz = 0; b->ready = false; return; }
    int32_t n = (int32_t)b->n;
    b->bx = (int16_t)idiv_round(b->sum_x, n);
    b->by = (int16_t)idiv_round(b->sum_y, n);
    b->bz = (int16_t)idiv_round(b->sum_z, n);
    b->ready = true;
}

int16_t apply_bias_sat(int16_t sample, int16_t bias) {
    int32_t d = (int32_t)sample - (int32_t)bias;   // int32 로 넓혀서 계산
    if (d > INT16_MAX) d = INT16_MAX;
    if (d < INT16_MIN) d = INT16_MIN;
    return (int16_t)d;
}

// ===========================================================================
// Q4 : 필터 3종 (moving average / median-3 / 1차 IIR low-pass)
// ---------------------------------------------------------------------------
// 왜 쿼드는 필터 없이 못 나는가:
//   프롭이 초당 300~600회전(18k~36k RPM) 한다. 그 불균형이 자이로를 300~600 Hz 대역의
//   진동으로 때린다. 자이로 원신호를 그대로 D 항에 넣으면 D 가 진동을 증폭 -> 모터가
//   같은 주파수로 떨고 -> 열 -> ESC 디싱크/번아웃. 필터는 "있으면 좋은 것"이 아니라
//   비행 가능 조건이다.
// 대가(trade-off): 모든 필터는 위상 지연(latency)을 만든다. 지연은 PID 가 자기가
//   만든 과거를 보고 반응하게 만들어 위상 여유를 깎고, 결국 저주파 진동(wobble)을 낳는다.
//   -> "노이즈를 얼마나 지울 것인가"가 아니라 "몇 ms 의 지연을 지불할 것인가"가 설계 축이다.
//   현대 비행 컨트롤러가 RPM 텔레메트리 기반 노치 필터를 쓰는 이유가 바로 이것이다.
//   노치는 문제의 주파수만 정확히 파내므로, 같은 노이즈 제거에 대해 지연이 훨씬 싸다.
// ===========================================================================
#define MAVG_MAX_LOG2 6                 // 최대 윈도 64
#define MAVG_MAX      (1u << MAVG_MAX_LOG2)

typedef struct {
    int32_t buf[MAVG_MAX];
    uint8_t log2n;      // 윈도 = 1 << log2n  (2의 거듭제곱 -> 나눗셈이 시프트)
    uint8_t idx;        // 다음에 덮어쓸 자리
    bool    seeded;     // 첫 샘플로 버퍼를 채웠는가
    int32_t sum;        // running sum (매번 N개 더하지 않는다)
} mavg_t;

typedef struct {
    int32_t y;          // 상태 = 직전 출력
    int32_t alpha_q15;  // 0 < alpha <= 32768  (32768 = 1.0 = 필터 없음)
    bool    seeded;
} lpf1_t;

/* ---------------------------------------------------------------------------
 * Q4.  moving average(2의 거듭제곱 윈도) / median-3 / 1차 IIR low-pass
 *   KO: mavg_init 은 log2n 범위를 검사하고 상태를 0으로. moving_avg_update 는
 *       running sum 을 "빠진 값 빼고, 새 값 더하고", 평균은 sum >> log2n (나눗셈 없음).
 *       첫 샘플에서는 버퍼 전체를 그 값으로 채워 시작 램프를 없앤다.
 *       median3 은 세 값의 중앙값 — 한 샘플짜리 튐(spike)을 통째로 버린다.
 *       lpf1_update 는 y += ((x - y) * alpha) >> 15 (Q15 고정소수점).
 *   EN: Power-of-two moving average with a running sum (shift, not divide),
 *       a 3-tap median for spike rejection, and a Q15 first-order IIR low-pass.
 *   ex: 윈도 4에 0,4,8,12,16 -> 0,1,3,6,10  /  median3(100,5000,102)=102
 *       alpha=16384(0.5) 일 때 0 -> 1000 입력 -> 500, 750, 875 ...
 *   note: sum >> log2n 은 음수에서 0 이 아니라 -무한 방향으로 내림한다(-1>>2 == -1,
 *         -1/4 == 0). 부호 있는 시프트가 산술 시프트인 것은 gcc/clang 보장 + C23 표준.
 *   note: alpha 가 너무 작으면 (x-y)*alpha >> 15 가 0 으로 내려앉아 y 가 x 에
 *         영원히 도달하지 못한다(고정소수점 dead-band). Q15 대신 Q16/Q20 을 쓰거나
 *         잔차를 누적해 보정한다.
 * ------------------------------------------------------------------------- */
bool mavg_init(mavg_t *f, uint8_t log2n) {
    if (!f || log2n > MAVG_MAX_LOG2) return false;
    memset(f, 0, sizeof *f);
    f->log2n = log2n;
    return true;
}

int32_t moving_avg_update(mavg_t *f, int32_t sample) {
    if (!f) return 0;
    uint32_t n = 1u << f->log2n;
    if (!f->seeded) {                      // 시작 램프 제거: 창을 첫 값으로 채운다
        for (uint32_t i = 0; i < n; ++i) f->buf[i] = sample;
        f->sum    = sample * (int32_t)n;
        f->idx    = 0;
        f->seeded = true;
        return sample;
    }
    f->sum -= f->buf[f->idx];              // 창 밖으로 나가는 값
    f->buf[f->idx] = sample;
    f->sum += sample;                      // 새로 들어오는 값
    f->idx = (uint8_t)((f->idx + 1u) & (n - 1u));
    return f->sum >> f->log2n;             // 2의 거듭제곱이라 나눗셈이 시프트로
}

int32_t median3(int32_t a, int32_t b, int32_t c) {
    // 비교 3번, 분기만으로 중앙값. 정렬하지 않는다.
    if (a > b) { int32_t t = a; a = b; b = t; }   // a <= b
    if (b > c) b = c;                             // b = min(b, c)
    return (a > b) ? a : b;                       // max(a, min(b,c)) = 중앙값
}

void lpf1_init(lpf1_t *f, int32_t alpha_q15) {
    if (!f) return;
    if (alpha_q15 < 1)     alpha_q15 = 1;
    if (alpha_q15 > 32768) alpha_q15 = 32768;
    f->y = 0;
    f->alpha_q15 = alpha_q15;
    f->seeded = false;
}

int32_t lpf1_update(lpf1_t *f, int32_t sample) {
    if (!f) return 0;
    if (!f->seeded) { f->y = sample; f->seeded = true; return f->y; }
    // 곱 전에 넓힌다: (x-y) 가 수십만이면 * 32768 은 int32 를 넘긴다.
    int64_t d = ((int64_t)(sample - f->y) * (int64_t)f->alpha_q15) >> 15;
    f->y += (int32_t)d;
    return f->y;
}

// ===========================================================================
// Q5 : RC 링크 (CRSF 스타일 채널 언팩) + failsafe
// ---------------------------------------------------------------------------
// CRSF(TBS Crossfire / ExpressLRS) 의 RC_CHANNELS_PACKED 페이로드는
// 채널 16개 × 11비트 = 176비트 = 정확히 22바이트다. 비트 순서는 little-endian
// 비트스트림 — 바이트 0 의 LSB 가 채널 0 의 비트 0.
// 값 범위는 172..1811 (=1000..2000µs), 중앙 992.
// 왜 이렇게 빽빽한가: 링크 예산. 재밍/장거리 환경에서 프레임이 짧을수록 살아남는다.
// ===========================================================================
#define CRSF_MIN 172u
#define CRSF_MID 992u
#define CRSF_MAX 1811u

typedef struct {
    uint32_t last_frame_ms;   // 마지막 "유효" 프레임 시각
    uint32_t timeout_ms;      // 이보다 오래되면 failsafe
    bool     have_frame;      // 부팅 후 한 번이라도 받았는가
    bool     failsafe;        // 마지막 판정 결과 (관측용)
} rc_link_t;

/* ---------------------------------------------------------------------------
 * Q5.  11비트 × 16채널 언팩 / CRSF 값 -> µs / 링크 failsafe 판정
 *   KO: rc_unpack_channels 는 22바이트 비트스트림을 LSB-first 로 훑으며 11비트씩
 *       떼어낸다(비트 누산기 패턴). rc_to_us 는 172..1811 -> 1000..2000 µs 선형
 *       매핑 + 범위 밖 clamp. rc_link_ok 는 (now - last) 를 부호없는 뺄셈으로 재서
 *       timeout 을 넘겼거나 아직 프레임을 한 번도 못 받았으면 failsafe.
 *   EN: Unpack 16×11-bit little-endian packed channels, map CRSF units to µs
 *       with integer math, and report failsafe on link timeout.
 *   ex: 22바이트 전부 0xFF -> 모든 채널 2047
 *       rc_to_us(992) = 1500,  rc_to_us(2047) = 2000 (clamp)
 *   note: 시각 비교는 반드시 (now - last) > timeout 형태로. last > now 같은 비교는
 *         32비트 ms 카운터가 49.7일에 랩할 때 영구 failsafe 를 만든다. 부호없는
 *         뺄셈은 랩을 저절로 통과한다.
 *   note: failsafe 는 "값이 이상하다"가 아니라 "시간이 지났다"로 판정한다. 재밍은
 *         보통 조용한 침묵으로 오지, 이상한 값으로 오지 않는다.
 * ------------------------------------------------------------------------- */
void rc_unpack_channels(const uint8_t *buf22, uint16_t *ch16) {
    if (!buf22 || !ch16) return;
    uint32_t acc   = 0;    // 비트 누산기 (최대 11+7 = 18비트만 차므로 32비트면 충분)
    uint8_t  nbits = 0;    // acc 안에 유효한 비트 수
    size_t   bi    = 0;    // 다음에 읽을 바이트
    for (size_t c = 0; c < 16; ++c) {
        while (nbits < 11) {                               // 11비트 모일 때까지 채운다
            acc |= (uint32_t)buf22[bi++] << nbits;         // LSB-first
            nbits = (uint8_t)(nbits + 8);
        }
        ch16[c] = (uint16_t)(acc & 0x7FFu);                // 하위 11비트를 떼고
        acc >>= 11;                                        // 나머지는 다음 채널로
        nbits = (uint8_t)(nbits - 11);
    }
}

int16_t rc_to_us(uint16_t crsf_value) {
    uint32_t v = crsf_value;
    if (v < CRSF_MIN) v = CRSF_MIN;                 // 링크가 준 값은 전부 의심한다
    if (v > CRSF_MAX) v = CRSF_MAX;
    const uint32_t span = CRSF_MAX - CRSF_MIN;      // 1639 counts == 1000 µs
    uint32_t us = 1000u + ((v - CRSF_MIN) * 1000u + span / 2u) / span;   // 반올림
    return (int16_t)us;
}

void rc_link_init(rc_link_t *l, uint32_t timeout_ms) {
    if (!l) return;
    l->last_frame_ms = 0;
    l->timeout_ms    = timeout_ms;
    l->have_frame    = false;
    l->failsafe      = true;        // 부팅 직후는 failsafe 가 기본값이다
}

void rc_link_on_frame(rc_link_t *l, uint32_t now_ms) {
    if (!l) return;
    l->last_frame_ms = now_ms;
    l->have_frame    = true;
}

bool rc_link_ok(rc_link_t *l, uint32_t now_ms) {
    if (!l) return false;
    if (!l->have_frame) { l->failsafe = true; return false; }
    uint32_t age = now_ms - l->last_frame_ms;      // 부호없는 뺄셈 = 랩 안전
    bool ok = (age <= l->timeout_ms);
    l->failsafe = !ok;
    return ok;
}

// ===========================================================================
// Q6 : 모터 출력 (DShot 프레임 / PWM 틱 / quad-X 믹서)
// ---------------------------------------------------------------------------
// DShot 프레임 (16비트, MSB first 로 전송):
//   [ throttle 11bit ][ telemetry 1bit ][ CRC 4bit ]
//   value = (throttle << 1) | telem
//   crc   = (value ^ (value >> 4) ^ (value >> 8)) & 0x0F     (세 니블의 XOR)
//   frame = (value << 4) | crc
// throttle 값: 0 = disarm/정지, 1~47 = 특수 커맨드(비프/방향/저장), 48~2047 = 실제 출력.
// 왜 DShot 인가: 디지털이라 ESC 캘리브레이션이 필요 없고, CRC 가 있어 한 비트 오류가
// 조용한 오출력이 되지 않으며(잘못된 프레임은 ESC 가 버린다), 타이밍이 아날로그 펄스폭에
// 의존하지 않아 온도/전압 드리프트가 없다.
// ===========================================================================
#define MIX_OUT_MIN 0
#define MIX_OUT_MAX 2047

/* ---------------------------------------------------------------------------
 * Q6.  DShot 프레임 / PWM µs -> 타이머 틱 / quad-X 믹서
 *   KO: dshot_frame 은 11비트 스로틀 + 텔레메트리 1비트에 4비트 XOR CRC 를 붙인다.
 *       pwm_us_to_ticks 는 ticks = us * timer_hz / 1e6 (반올림), period 에서 포화.
 *       mixer_quad_x 는 throttle/roll/pitch/yaw 를 모터 4개로 섞되, 한 모터가
 *       포화해도 "모터 간 차이"(= 자세 권한)를 그대로 유지한다.
 *   EN: Build a DShot frame with its 4-bit XOR CRC, convert a PWM pulse width to
 *       timer ticks with saturation, and mix quad-X outputs so attitude authority
 *       survives saturation.
 *   ex: 손으로 계산한 알려진 답 (throttle=1046, telem=0):
 *         1046 = 0x416 -> value = 0x416 << 1 = 0x82C
 *         crc  = (0x82C ^ 0x082 ^ 0x008) & 0xF = 0x8A6 & 0xF = 0x6
 *         frame = (0x82C << 4) | 0x6 = 0x82C6
 *       telem=1 이면 value=0x82D, crc=0x7, frame=0x82D7
 *   note: 믹서의 핵심은 clamp 가 아니라 "throttle 을 양보시키는 것"이다. 단순히
 *         모터별로 clamp 하면 포화한 쪽의 자세 명령이 잘려나가 기체가 그 방향으로
 *         제어를 잃는다(풀스로틀 뒤집힘의 전형적 원인). 대신 mix 의 span 을 먼저
 *         출력 범위에 맞춰 스케일하고, 그 다음 throttle 을 통째로 밀어 넣는다.
 *         이것이 Betaflight 의 airmode / mix-range 처리와 같은 아이디어다.
 * ------------------------------------------------------------------------- */
uint16_t dshot_frame(uint16_t throttle11, bool telemetry_req) {
    uint16_t value = (uint16_t)(((throttle11 & 0x7FFu) << 1) | (telemetry_req ? 1u : 0u));
    uint16_t crc   = (uint16_t)((value ^ (value >> 4) ^ (value >> 8)) & 0x0Fu);
    return (uint16_t)((value << 4) | crc);
}

uint16_t pwm_us_to_ticks(uint16_t pulse_us, uint32_t timer_hz, uint16_t period_ticks) {
    // 64비트로 넓힌다: 65535 * 96e6 는 uint32 를 한참 넘긴다.
    uint64_t ticks = ((uint64_t)pulse_us * (uint64_t)timer_hz + 500000u) / 1000000u;
    if (ticks > (uint64_t)period_ticks) ticks = period_ticks;   // 주기 밖으로 못 나간다
    return (uint16_t)ticks;
}

// 모터 배치 (위에서 본 quad-X, 인덱스는 시계방향):
//   out4[0] = FR (front-right)   out4[1] = RR (rear-right)
//   out4[2] = RL (rear-left)     out4[3] = FL (front-left)
// 대각 쌍 (FR,RL) 과 (FL,RR) 이 서로 반대 방향으로 돈다 -> yaw 는 대각 쌍으로 만든다.
// 부호 규약: roll>0 = 오른쪽으로 롤(오른쪽이 내려감) -> 왼쪽 모터 증가
//            pitch>0 = 기수 올림 -> 뒤쪽 모터 증가
//            yaw>0 = 기수 오른쪽 -> (FR,RL) 증가
void mixer_quad_x(int32_t throttle, int32_t roll, int32_t pitch, int32_t yaw, uint16_t *out4) {
    if (!out4) return;

    int32_t mix[4];
    mix[0] = -roll - pitch + yaw;     // FR
    mix[1] = -roll + pitch - yaw;     // RR
    mix[2] =  roll + pitch + yaw;     // RL
    mix[3] =  roll - pitch - yaw;     // FL

    int32_t mx = mix[0], mn = mix[0];
    for (int i = 1; i < 4; ++i) {
        if (mix[i] > mx) mx = mix[i];
        if (mix[i] < mn) mn = mix[i];
    }

    // 1) 자세 명령의 폭(span)이 출력 범위보다 크면 "전체를 같은 비율로" 줄인다.
    //    개별 clamp 와 달리 모터 간 비율이 보존된다.
    const int32_t range = MIX_OUT_MAX - MIX_OUT_MIN;
    int32_t span = mx - mn;
    if (span > range) {
        for (int i = 0; i < 4; ++i)
            mix[i] = (int32_t)(((int64_t)mix[i] * range) / span);   // 0 방향 절삭 = 좌우 대칭
        mx = mix[0]; mn = mix[0];
        for (int i = 1; i < 4; ++i) {
            if (mix[i] > mx) mx = mix[i];
            if (mix[i] < mn) mn = mix[i];
        }
    }

    // 2) 이제 span <= range 이므로, throttle 을 통째로 밀어 넣으면 전부 범위 안에 들어간다.
    //    스로틀은 양보하고 자세는 지킨다 — 이게 airmode 의 본질.
    int32_t t = throttle;
    if (t + mx > MIX_OUT_MAX) t = MIX_OUT_MAX - mx;
    if (t + mn < MIX_OUT_MIN) t = MIX_OUT_MIN - mn;

    for (int i = 0; i < 4; ++i) {
        int32_t v = t + mix[i];
        if (v < MIX_OUT_MIN) v = MIX_OUT_MIN;    // 방어적 clamp (여기 걸리면 안 된다)
        if (v > MIX_OUT_MAX) v = MIX_OUT_MAX;
        out4[i] = (uint16_t)v;
    }
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
    // -------- Q1 : 레지스터 접근 패턴 --------
    printf("== Q1. I2C/SPI register access (read / RMW / burst) ==\n");
    memset(g_dev_reg, 0, sizeof g_dev_reg);
    // 디바이스 초기 상태를 버스로 직접 심는다 (하네스).
    const uint8_t who = 0x68;
    bus_txn(IMU_REG_WHO_AM_I, &who, NULL, 1);
    const uint8_t gyro_seed[6] = { 0xFF, 0xFE, 0x00, 0x01, 0x80, 0x00 };  // -2, +1, -32768
    bus_txn(IMU_REG_GYRO_XOUT_H, gyro_seed, NULL, 6);

    g_bus_txn = 0;
    bool rw_ok = imu_write_reg(IMU_REG_PWR_MGMT_1, 0x01);
    rw_ok &= (imu_read_reg(IMU_REG_PWR_MGMT_1) == 0x01);
    rw_ok &= (imu_read_reg(IMU_REG_WHO_AM_I) == 0x68);
    rw_ok &= (g_bus_txn == 3);                     // write 1 + read 2
    T("Q1 write/read 왕복 + 단일 접근 = 트랜잭션 1회씩", rw_ok);

    // CONFIG = 0b1010_0101 에서 하위 3비트(DLPF_CFG)만 0b011 로 바꾼다.
    const uint8_t cfg0 = 0xA5;
    bus_txn(IMU_REG_CONFIG, &cfg0, NULL, 1);
    g_bus_txn = 0;
    bool rmw_ok = imu_modify_reg(IMU_REG_CONFIG, 0x07, 0x03);
    rmw_ok &= (imu_read_reg(IMU_REG_CONFIG) == 0xA3);   // 0xA5 -> 상위 5비트 보존, 하위 3비트 011
    rmw_ok &= (g_bus_txn == 3);                          // read + write + 확인 read
    // 값이 이미 같으면 write 를 생략한다 (버스 절약).
    g_bus_txn = 0;
    rmw_ok &= imu_modify_reg(IMU_REG_CONFIG, 0x07, 0x03) && (g_bus_txn == 1);
    T("Q1 modify_reg: 마스크 밖 비트 보존 + 불필요한 write 생략", rmw_ok);

    uint8_t raw6[6] = {0};
    g_bus_txn = 0;
    bool burst_ok = imu_burst_read(IMU_REG_GYRO_XOUT_H, raw6, 6);
    burst_ok &= (g_bus_txn == 1);                        // 6바이트를 트랜잭션 1회로
    burst_ok &= (memcmp(raw6, gyro_seed, 6) == 0);
    g_bus_txn = 0;
    for (uint8_t i = 0; i < 6; ++i) (void)imu_read_reg((uint8_t)(IMU_REG_GYRO_XOUT_H + i));
    burst_ok &= (g_bus_txn == 6);                        // 같은 데이터를 6회로 = 6배 버스 시간
    burst_ok &= (imu_burst_read(0x7E, raw6, 6) == false);           // 0x7E+6 > 0x80 -> 거부
    burst_ok &= (imu_burst_read(IMU_REG_GYRO_XOUT_H, NULL, 6) == false);
    burst_ok &= (imu_burst_read(IMU_REG_GYRO_XOUT_H, raw6, 0) == false);
    burst_ok &= (spi_cmd_read(0x43) == 0xC3);                      // read -> MSB 세팅
    burst_ok &= (spi_cmd_write(0x43) == 0x43);                     // write -> MSB 클리어
    burst_ok &= (spi_cmd_write(0xC3) == 0x43);                     // 주소는 7비트뿐
    T("Q1 burst_read: 6바이트 = 1 트랜잭션, 범위/NULL 거부, SPI R/W 비트 규약", burst_ok);

    // -------- Q2 : 샘플 디코드 + 정수 스케일 --------
    printf("== Q2. IMU sample decode (big-endian) + integer scaling ==\n");
    int16_t gx = 0, gy = 0, gz = 0;
    imu_decode_xyz(raw6, &gx, &gy, &gz);
    bool dec_ok = (gx == -2) && (gy == 1) && (gz == -32768);   // 0xFFFE, 0x0001, 0x8000
    uint8_t pos6[6] = { 0x12, 0x34, 0x7F, 0xFF, 0x00, 0x00 };
    imu_decode_xyz(pos6, &gx, &gy, &gz);
    dec_ok &= (gx == 0x1234) && (gy == 32767) && (gz == 0);
    T("Q2 decode: big-endian int16 3쌍 (음수 부호확장 포함)", dec_ok);

    bool sc_ok = (gyro_to_mdps(16384, 2000) == 1000000);       // 반 스케일 = 1000 dps
    sc_ok &= (gyro_to_mdps(1, 2000)  == 61);                   // 1 LSB = 0.061 dps
    sc_ok &= (gyro_to_mdps(-1, 2000) == -61);                  // 부호 대칭
    sc_ok &= (gyro_to_mdps(27, 2000)  == 1648);                // 버림이면 1647 -> 반올림 확인
    sc_ok &= (gyro_to_mdps(-27, 2000) == -1648);
    sc_ok &= (gyro_to_mdps(-32768, 2000) == -2000000);         // 풀스케일에서도 int32 안전
    sc_ok &= (gyro_to_mdps(100, 250) == 763);                  // ±250dps 레인지
    T("Q2 gyro_to_mdps: 스케일 + half-away-from-zero 반올림 + 오버플로 없음", sc_ok);

    // -------- Q3 : bias 캘리브레이션 + 포화 --------
    printf("== Q3. gyro bias calibration + saturating subtract ==\n");
    bias_t b;
    memset(&b, 0, sizeof b);
    gyro_bias_accumulate(&b, 10, -10, 100);
    gyro_bias_accumulate(&b, 11, -11, 100);
    gyro_bias_accumulate(&b, 11, -11, 100);
    gyro_bias_accumulate(&b, 11, -11, 100);
    gyro_bias_finish(&b);
    bool bias_ok = b.ready && (b.n == 4);
    bias_ok &= (b.bx == 11) && (b.by == -11) && (b.bz == 100);  // 43/4 = 10.75 -> 11
    bias_t empty;
    memset(&empty, 0, sizeof empty);
    gyro_bias_finish(&empty);
    bias_ok &= (!empty.ready && empty.bx == 0);                 // n==0 -> ready=false
    T("Q3 bias 평균: 정수 반올림(10.75 -> 11) + n==0 방어", bias_ok);

    // 정지 상태의 일정한 오프셋이 깨끗하게 지워지는가
    bool sat_ok = true;
    for (int i = 0; i < 8; ++i)
        if (apply_bias_sat((int16_t)(11 + 0), b.bx) != 0) sat_ok = false;
    sat_ok &= (apply_bias_sat(300, b.bx) == 289);
    sat_ok &= (apply_bias_sat(32767, -100) == 32767);   // 32867 -> 랩(-32669) 아님
    sat_ok &= (apply_bias_sat(-32768, 100)  == -32768); // -32868 -> clamp
    sat_ok &= (apply_bias_sat(-32768, -1)   == -32767);
    T("Q3 apply_bias_sat: 상수 오프셋 제거 + INT16 포화(랩 금지)", sat_ok);

    // -------- Q4 : 필터 --------
    printf("== Q4. filters (moving average / median-3 / 1st-order IIR) ==\n");
    mavg_t ma;
    bool ma_ok = mavg_init(&ma, 2) && !mavg_init(&ma, MAVG_MAX_LOG2 + 1);
    ma_ok &= mavg_init(&ma, 2);
    ma_ok &= (moving_avg_update(&ma, 0)  == 0);    // 첫 샘플로 창을 채운다
    ma_ok &= (moving_avg_update(&ma, 4)  == 1);    // (0+0+0+4)/4
    ma_ok &= (moving_avg_update(&ma, 8)  == 3);    // (0+0+4+8)/4
    ma_ok &= (moving_avg_update(&ma, 12) == 6);    // (0+4+8+12)/4
    ma_ok &= (moving_avg_update(&ma, 16) == 10);   // (4+8+12+16)/4
    ma_ok &= (moving_avg_update(&ma, 16) == 13);   // (8+12+16+16)/4
    T("Q4 moving_avg: running sum + 시프트(나눗셈 없음), 창 = 2^log2n", ma_ok);

    bool med_ok = (median3(100, 5000, 102) == 102)   // 한 샘플 스파이크 제거
               && (median3(1, 2, 3) == 2)
               && (median3(3, 1, 2) == 2)
               && (median3(2, 3, 1) == 2)
               && (median3(-5, -1, -3) == -3)
               && (median3(7, 7, 7) == 7);
    T("Q4 median3: 순열 무관 중앙값 = 단발 스파이크 제거", med_ok);

    lpf1_t lp;
    lpf1_init(&lp, 16384);                          // alpha = 0.5 (Q15)
    bool lp_ok = (lpf1_update(&lp, 0) == 0);        // 첫 샘플로 시드
    lp_ok &= (lpf1_update(&lp, 1000) == 500);       // y += (1000-0)*0.5
    lp_ok &= (lpf1_update(&lp, 1000) == 750);
    lp_ok &= (lpf1_update(&lp, 1000) == 875);       // 지수적으로 수렴
    lpf1_t pass;
    lpf1_init(&pass, 32768);                        // alpha = 1.0 -> 필터 없음
    lp_ok &= (lpf1_update(&pass, 0) == 0);
    lp_ok &= (lpf1_update(&pass, 1000) == 1000);
    lp_ok &= (lpf1_update(&pass, -1000) == -1000);
    T("Q4 lpf1: Q15 고정소수점 지수 수렴, alpha=1.0 이면 통과(지연 0)", lp_ok);

    // -------- Q5 : RC 링크 언팩 + failsafe --------
    printf("== Q5. RC link unpack (16ch x 11bit) + failsafe ==\n");
    // 아래 22바이트는 채널 {172,992,1811,1000,0,2047,172,992,1,2,3,4,5,6,7,8} 을
    // 11비트 LSB-first 로 팩한 것이다.
    const uint8_t packed[22] = {
        0xAC, 0x00, 0xDF, 0xC4, 0xD1, 0x07, 0x80, 0xFF,
        0xB3, 0x02, 0x7C, 0x01, 0x10, 0xC0, 0x00, 0x08,
        0x50, 0x00, 0x03, 0x1C, 0x00, 0x01,
    };
    const uint16_t expect[16] = {172, 992, 1811, 1000, 0, 2047, 172, 992,
                                 1, 2, 3, 4, 5, 6, 7, 8};
    uint16_t ch[16];
    memset(ch, 0xAA, sizeof ch);
    rc_unpack_channels(packed, ch);
    bool unpack_ok = (memcmp(ch, expect, sizeof expect) == 0);
    uint8_t allff[22];
    memset(allff, 0xFF, sizeof allff);
    rc_unpack_channels(allff, ch);
    for (int i = 0; i < 16; ++i) if (ch[i] != 2047) unpack_ok = false;   // 11비트 전부 1
    T("Q5 rc_unpack: 22바이트 -> 16채널 11비트 LSB-first (경계 걸친 채널 포함)", unpack_ok);

    bool us_ok = (rc_to_us(172)  == 1000)            // 범위 하단
              && (rc_to_us(992)  == 1500)            // 중앙
              && (rc_to_us(1811) == 2000)            // 범위 상단
              && (rc_to_us(500)  == 1200)
              && (rc_to_us(0)    == 1000)            // clamp (링크 쓰레기 방어)
              && (rc_to_us(2047) == 2000);           // clamp
    T("Q5 rc_to_us: 172..1811 -> 1000..2000µs 정수 매핑 + 범위 밖 clamp", us_ok);

    rc_link_t link;
    rc_link_init(&link, 100);                        // 100 ms 타임아웃
    bool fs_ok = (rc_link_ok(&link, 0) == false) && link.failsafe;   // 부팅 직후 = failsafe
    rc_link_on_frame(&link, 1000);
    fs_ok &= (rc_link_ok(&link, 1000) == true);
    fs_ok &= (rc_link_ok(&link, 1050) == true);
    fs_ok &= (rc_link_ok(&link, 1100) == true);      // age == timeout -> 아직 OK
    fs_ok &= (rc_link_ok(&link, 1101) == false) && link.failsafe;    // 1 ms 초과 -> failsafe
    rc_link_on_frame(&link, 1200);                   // 링크 복구
    fs_ok &= (rc_link_ok(&link, 1250) == true) && !link.failsafe;
    rc_link_on_frame(&link, 0xFFFFFFF0u);            // ms 카운터 랩 직전
    fs_ok &= (rc_link_ok(&link, 0x00000005u) == true);   // age = 21 ms (부호없는 뺄셈)
    fs_ok &= (rc_link_ok(&link, 0x00000100u) == false);  // age = 272 ms -> failsafe
    T("Q5 failsafe: 타임아웃 경계 + 부팅 시 안전측 + ms 랩어라운드 통과", fs_ok);

    // -------- Q6 : 모터 출력 --------
    printf("== Q6. motor output (DShot / PWM ticks / quad-X mixer) ==\n");
    // 손계산: 1046 = 0x416, value = 0x82C, crc = (0x82C^0x082^0x008)&0xF = 0x6
    bool ds_ok = (dshot_frame(1046, false) == 0x82C6);
    ds_ok &= (dshot_frame(1046, true)  == 0x82D7);   // telem 비트 -> value 0x82D, crc 0x7
    ds_ok &= (dshot_frame(0, false)    == 0x0000);   // disarm
    ds_ok &= (dshot_frame(48, false)   == 0x0606);   // 최소 비행 스로틀
    ds_ok &= (dshot_frame(2047, true)  == 0xFFFF);   // 풀스로틀 + telem = 전부 1
    ds_ok &= (dshot_frame(0xF800u | 1046u, false) == 0x82C6);  // 11비트 밖은 마스크
    T("Q6 dshot_frame: 손계산 알려진 답 0x82C6 / telem 0x82D7 / 4비트 XOR CRC", ds_ok);

    bool pwm_ok = (pwm_us_to_ticks(1500, 1000000u, 20000) == 1500)   // 1 MHz = 1 tick/µs
               && (pwm_us_to_ticks(1000, 24000000u, 48000) == 24000) // 24 MHz
               && (pwm_us_to_ticks(1001, 1500000u, 60000) == 1502)   // 1501.5 -> 반올림
               && (pwm_us_to_ticks(25000, 1000000u, 20000) == 20000) // period 에서 포화
               && (pwm_us_to_ticks(0, 1000000u, 20000) == 0);
    T("Q6 pwm_us_to_ticks: 64비트 중간값 + 반올림 + period 포화", pwm_ok);

    uint16_t m[4] = {0, 0, 0, 0};
    mixer_quad_x(1000, 0, 0, 0, m);
    bool mix_ok = (m[0] == 1000 && m[1] == 1000 && m[2] == 1000 && m[3] == 1000);
    mixer_quad_x(1000, 0, 0, 200, m);                 // 순수 yaw: 대각 쌍만 갈린다
    mix_ok &= (m[0] == 1200 && m[2] == 1200 && m[1] == 800 && m[3] == 800);
    T("Q6 mixer: 중립은 4개 동일, 순수 yaw 는 대각 쌍(FR,RL) vs (FL,RR)", mix_ok);

    // 핵심 케이스: throttle 이 거의 최대인데 roll 명령이 들어온다.
    // 단순 clamp 라면 FL 이 2047 에 잘려 FL-FR 차이가 400 -> 247 로 줄어든다(자세 권한 상실).
    // 올바른 믹서는 throttle 을 양보시켜 차이 400 을 그대로 지킨다.
    mixer_quad_x(2000, 200, 0, 0, m);
    bool auth_ok = (m[3] == 2047) && (m[0] == 1647);
    auth_ok &= ((int32_t)m[3] - (int32_t)m[0] == 400);   // 2*roll 그대로 유지
    auth_ok &= ((int32_t)m[2] - (int32_t)m[1] == 400);
    T("Q6 mixer: 모터가 포화해도 상대 자세 권한(모터 간 차이) 보존", auth_ok);

    // mix 폭이 출력 범위(2047)보다 큰 경우: 전체를 비율대로 줄이고 나서 배치한다.
    mixer_quad_x(1000, 2000, 0, 0, m);
    bool scale_ok = true;
    for (int i = 0; i < 4; ++i) if (m[i] > MIX_OUT_MAX) scale_ok = false;
    scale_ok &= (m[0] == 0) && (m[1] == 0) && (m[2] == 2046) && (m[3] == 2046);
    scale_ok &= ((int32_t)m[3] - (int32_t)m[0] == 2046);   // 좌우 대칭 유지
    T("Q6 mixer: span > range 이면 비율 유지한 채 스케일다운, 전부 범위 내", scale_ok);

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
