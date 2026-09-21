/* 10. 모듈화·테스트 가능한 임베디드 드라이버 설계 — 메일에 명시된 주제
 * ------------------------------------------------------------------
 * "하드웨어 없이도 테스트할 수 있는 I2C 센서 드라이버를 설계하라."
 *
 * 설계 원칙 4가지 (면접에서 이 이름들을 말하면서 설명할 것):
 *   1) 의존성 주입: 드라이버는 i2c_ops와 clock_ops '인터페이스'에만 의존한다.
 *      → 테스트에서는 fake, 제품에서는 진짜 HAL을 꽂는다.
 *   2) 시간도 의존성이다: sleep()/HAL_GetTick()을 직접 부르면 테스트가 느려지고
 *      타임아웃 경로를 검증할 수 없다 → now_ms()를 주입해 시간을 '조종'한다.
 *   3) 순수한 정책 분리: 재시도/타임아웃/CRC 같은 정책은 I/O와 분리된 상태머신에.
 *   4) 에러는 값으로 반환: 드라이버는 절대 스스로 죽거나 로그만 찍고 삼키지 않는다.
 *
 * 이렇게 하면 "센서가 NACK → 재시도 2번 → 성공", "계속 실패 → 타임아웃 후 ERROR",
 * "CRC 불일치" 같은 경로를 하드웨어 없이 초 단위로 회귀 테스트할 수 있다.
 * 빌드: cc -std=c11 -Wall -Wextra 10_testable_driver.c   (스레드 불필요)
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------- 1) 주입할 인터페이스 (제품/테스트가 각각 구현) ---------- */
typedef enum { IO_OK = 0, IO_NACK = -1, IO_BUS_ERR = -2 } IoStatus;

typedef struct {
    IoStatus (*read)(void *ctx, uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
    IoStatus (*write)(void *ctx, uint8_t addr, uint8_t reg, uint8_t val);
    void     *ctx;
} I2cOps;

typedef struct {
    uint32_t (*now_ms)(void *ctx);
    void     *ctx;
} ClockOps;

/* ---------- 2) 드라이버 (하드웨어를 전혀 모른다) ---------- */
typedef enum { SENS_RESET, SENS_READY, SENS_ERROR } SensorState;

typedef enum {
    SENS_OK = 0, SENS_EAGAIN = 1, SENS_ETIMEDOUT = 2, SENS_ECRC = 3, SENS_ESTATE = 4
} SensorErr;

typedef struct {
    I2cOps      i2c;
    ClockOps    clk;
    uint8_t     addr;
    SensorState state;
    uint32_t    op_start_ms;
    uint32_t    timeout_ms;
    unsigned    retries;        /* 통계: 누적 재시도 횟수 */
    unsigned    crc_errors;
} Sensor;

#define REG_STATUS  0x00
#define REG_DATA    0x02
#define STATUS_RDY  0x01

void      sensor_init(Sensor *s, I2cOps i2c, ClockOps clk, uint8_t addr, uint32_t timeout_ms);
SensorErr sensor_start_measure(Sensor *s);
/* 논블로킹 폴링: SENS_EAGAIN이면 아직 준비 안 됨(다시 호출), OK면 out에 값 */
SensorErr sensor_poll(Sensor *s, int16_t *out_ppm);

static uint8_t crc8(const uint8_t *d, size_t n);  /* 테스트에서도 쓰므로 선언은 밖에 */

/* ------------------------------------------------------------------
 * 여기부터 직접 구현한다. 위의 선언부와 아래 self-test는 그대로 두고,
 * 아래 함수 목록을 채운 뒤 `make run N=10` 로 검증한다.
 *
 * 구현할 함수:
 *   - sensor_init()
 *   - sensor_start_measure()
 *   - sensor_poll()
 *
 * 막히면 ../solutions/10_testable_driver.c 를 열되, 먼저 15분은 스스로 해볼 것.
 * ------------------------------------------------------------------ */

/* ---------- 3) 테스트용 fake (하드웨어 없음) ---------- */
typedef struct {
    int      not_ready_times;   /* 이 횟수만큼 "아직 준비 안 됨"을 반환 */
    int      nack_times;        /* 이 횟수만큼 NACK */
    bool     corrupt_crc;
    int16_t  value;
    unsigned reads, writes;
} FakeI2c;

static IoStatus fake_read(void *ctx, uint8_t addr, uint8_t reg, uint8_t *buf, size_t n)
{
    (void)addr;
    FakeI2c *f = ctx;
    f->reads++;
    if (f->nack_times > 0) { f->nack_times--; return IO_NACK; }

    if (reg == REG_STATUS && n == 1) {
        buf[0] = (f->not_ready_times > 0) ? 0x00 : STATUS_RDY;
        if (f->not_ready_times > 0) f->not_ready_times--;
        return IO_OK;
    }
    if (reg == REG_DATA && n == 3) {
        buf[0] = (uint8_t)(f->value >> 8);
        buf[1] = (uint8_t)(f->value & 0xFF);
        buf[2] = crc8(buf, 2);
        if (f->corrupt_crc) buf[2] ^= 0xFF;
        return IO_OK;
    }
    return IO_BUS_ERR;
}

static IoStatus fake_write(void *ctx, uint8_t addr, uint8_t reg, uint8_t val)
{
    (void)addr; (void)reg; (void)val;
    ((FakeI2c *)ctx)->writes++;
    return IO_OK;
}

typedef struct { uint32_t ms; } FakeClock;           /* 시간을 테스트가 조종한다 */
static uint32_t fake_now(void *ctx) { return ((FakeClock *)ctx)->ms; }

static void setup(Sensor *s, FakeI2c *f, FakeClock *c)
{
    I2cOps io   = { fake_read, fake_write, f };
    ClockOps ck = { fake_now, c };
    sensor_init(s, io, ck, 0x62, 1000);
}

/* ------------------------------- self test ------------------------------- */
int main(void)
{
    /* case 1: 정상 — 두 번 EAGAIN 후 값 획득 */
    {
        Sensor s; FakeI2c f = { .not_ready_times = 2, .value = 812 }; FakeClock c = { 0 };
        setup(&s, &f, &c);
        assert(sensor_start_measure(&s) == SENS_OK);
        int16_t ppm = 0;
        assert(sensor_poll(&s, &ppm) == SENS_EAGAIN);
        c.ms += 10;
        assert(sensor_poll(&s, &ppm) == SENS_EAGAIN);
        c.ms += 10;
        assert(sensor_poll(&s, &ppm) == SENS_OK);
        assert(ppm == 812 && s.retries == 2);
        printf("  case1 ok: ppm=%d retries=%u\n", ppm, s.retries);
    }
    /* case 2: 일시적 NACK 후 복구 */
    {
        Sensor s; FakeI2c f = { .nack_times = 3, .value = 400 }; FakeClock c = { 0 };
        setup(&s, &f, &c);
        assert(sensor_start_measure(&s) == SENS_OK);
        int16_t ppm = 0;
        for (int i = 0; i < 3; i++) { assert(sensor_poll(&s, &ppm) == SENS_EAGAIN); c.ms += 5; }
        assert(sensor_poll(&s, &ppm) == SENS_OK && ppm == 400);
        printf("  case2 ok: NACK 3회 후 복구, retries=%u\n", s.retries);
    }
    /* case 3: 타임아웃 — 시계만 앞으로 돌리면 즉시 검증된다(실제 대기 0초) */
    {
        Sensor s; FakeI2c f = { .not_ready_times = 10000 }; FakeClock c = { 0 };
        setup(&s, &f, &c);
        assert(sensor_start_measure(&s) == SENS_OK);
        int16_t ppm = 0;
        assert(sensor_poll(&s, &ppm) == SENS_EAGAIN);
        c.ms = 1000;                                   /* 타임아웃 경계 */
        assert(sensor_poll(&s, &ppm) == SENS_ETIMEDOUT);
        assert(s.state == SENS_ERROR);
        assert(sensor_poll(&s, &ppm) == SENS_ESTATE);  /* 에러 상태 고착 확인 */
        puts("  case3 ok: 타임아웃 → ERROR 고착");
    }
    /* case 4: CRC 불일치 → 값을 쓰지 않고 에러, 재측정 가능 상태로 */
    {
        Sensor s; FakeI2c f = { .corrupt_crc = true, .value = 1234 }; FakeClock c = { 0 };
        setup(&s, &f, &c);
        assert(sensor_start_measure(&s) == SENS_OK);
        int16_t ppm = -1;
        assert(sensor_poll(&s, &ppm) == SENS_ECRC);
        assert(ppm == -1);                             /* 오염된 값이 새어나가지 않음 */
        assert(s.crc_errors == 1 && s.state == SENS_RESET);
        assert(sensor_start_measure(&s) == SENS_OK);   /* 복구되어 재측정 가능 */
        puts("  case4 ok: CRC 오류 시 값 미반영 + 재측정 가능");
    }
    /* case 5: 순수 함수 단독 테스트 */
    {
        uint8_t d[2] = { 0xBE, 0xEF };
        assert(crc8(d, 2) == crc8(d, 2));
        uint8_t e[2] = { 0xBE, 0xEE };
        assert(crc8(d, 2) != crc8(e, 2));
        puts("  case5 ok: crc8 순수 함수");
    }
    puts("10 PASS");
    return 0;
}
