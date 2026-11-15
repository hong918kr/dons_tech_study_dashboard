/* RTOS-M08 — 펌웨어 같은 호출 그래프 (freestanding: <stdint.h> <stddef.h> <stdarg.h> 만 사용)
 *
 * 이 파일은 "도구가 숫자를 내는 대상" 이다. 같은 소스를 두 번 컴파일한다:
 *   Cortex-M4F : clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=hard -O2 -fstack-usage -c fw_graph.c
 *   arm64(맥)  : cc -O2 -fstack-usage -c fw_graph.c        (demo_paint_measure.c 가 이 객체로 실측)
 * → fw_graph.su (함수별 프레임 바이트). 호출 관계는 clang 이 주지 않으므로 llvm-objdump 의 bl 로 뽑는다 (md 5. 풀이).
 *
 * 규칙: 모든 함수에 noinline — 인라인되면 그 함수의 프레임이 호출자에 합쳐져 .su 에서 사라지고
 *       "소스의 호출 그래프" 와 "바이너리의 호출 그래프" 가 달라진다 (md 7. 자주 하는 실수).
 *       지역 버퍼는 반드시 주소가 다른 함수로 넘어가거나 변수 인덱스로 접근된다 — 그렇지 않으면
 *       최적화기가 안 쓰는 부분을 지워 .su 가 버퍼 크기보다 작게 나온다 (md 5. 풀이 Part A 의 "4 바이트 미스터리").
 * 하드웨어 레지스터 대신 전역 배열을 쓴다 → 호스트에서도 그대로 실행된다 (주소 0x4001_3000 같은 걸 쓰면 맥에서 즉사).
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "fw_graph.h"

#define NOINLINE __attribute__((noinline))

/* ---------- 가짜 주변장치 ---------- */
volatile uint8_t  g_spi_fifo[32];      /* IMU SPI 버스트 읽기 소스 (SPI1->DR 대용) */
volatile uint32_t g_uart_dr;           /* USART2->TDR 대용 */
volatile uint32_t g_pwm_ccr[3];        /* TIM1->CCR1..3 대용 */
volatile uint16_t g_adc_dma[16];       /* ADC DMA 버퍼 */
uint8_t  g_uart_rx_ring[64];
uint32_t g_uart_rx_head, g_uart_rx_tail, g_uart_tx_count, g_tick_count, g_adc_avg;

/* ---------- 제어 태스크 경로: read → filter → pid → log ---------- */
NOINLINE void spi_burst_read(uint8_t reg, uint8_t *dst, size_t n) {
    g_spi_fifo[0] = reg;                                  /* "주소 바이트 전송" */
    for (size_t i = 0; i < n; i++) dst[i] = g_spi_fifo[(reg + i) & 31u];
}

NOINLINE int imu_read(imu_raw_t *out) {
    uint8_t rx[14];                                       /* ACCEL_XOUT_H..GYRO_ZOUT_L 버스트 */
    spi_burst_read(0x3B, rx, sizeof rx);                  /* 주소가 넘어간다 → 14 B 가 실제로 잡힌다 */
    int16_t v[7];
    for (int i = 0; i < 7; i++) v[i] = (int16_t)((rx[2 * i] << 8) | rx[2 * i + 1]);
    out->ax = v[0]; out->ay = v[1]; out->az = v[2]; out->temp = v[3];
    out->gx = v[4]; out->gy = v[5]; out->gz = v[6];
    return (v[3] != 0x7fff);
}

NOINLINE void filter_update(att_t *s, const imu_raw_t *r) {
    const float a = 0.98f, dt = 0.001f;                   /* 상보 필터 1 kHz */
    s->roll  = a * (s->roll  + (float)r->gx * dt) + (1.0f - a) * (float)r->ay;
    s->pitch = a * (s->pitch + (float)r->gy * dt) + (1.0f - a) * (float)r->ax;
    s->hist[s->n++ & 7u] = (float)r->gz;                  /* yaw rate 8-tap 이동 평균 */
    float acc = 0.0f;
    for (int i = 0; i < 8; i++) acc += s->hist[i];
    s->yaw_rate = acc * 0.125f;
}

NOINLINE float pid_step(pid_ctl_t *p, float sp, float meas) {
    float e = sp - meas;
    p->integ += p->ki * e;
    if (p->integ > p->hi) p->integ = p->hi;
    if (p->integ < p->lo) p->integ = p->lo;
    float u = p->kp * e + p->integ + p->kd * (e - p->prev);
    p->prev = e;
    return u < p->lo ? p->lo : (u > p->hi ? p->hi : u);
}

/* printf 비슷한 포매터: %d %u %x %s 만 (펌웨어 로그는 float 를 안 쓴다 — 고정소수점 정수로 찍는다) */
NOINLINE size_t fmt_u32(char *dst, uint32_t v, unsigned base) {
    char tmp[12];                                         /* 32비트 10진 최대 10자리 + 여유 */
    size_t n = 0;
    do { unsigned d = v % base; tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= base; } while (v);
    for (size_t i = 0; i < n; i++) dst[i] = tmp[n - 1 - i];
    return n;
}

NOINLINE size_t log_vformat(char *dst, size_t cap, const char *fmt, va_list ap) {
    size_t n = 0;
    char num[16];
    for (; *fmt && n + 12 < cap; fmt++) {
        if (*fmt != '%') { dst[n++] = *fmt; continue; }
        size_t k = 0;
        switch (*++fmt) {
        case 'd': { int32_t v = va_arg(ap, int32_t);
                    if (v < 0) { dst[n++] = '-'; k = fmt_u32(num, (uint32_t)0 - (uint32_t)v, 10); }
                    else { k = fmt_u32(num, (uint32_t)v, 10); }
                    break; }
        case 'u': k = fmt_u32(num, va_arg(ap, uint32_t), 10); break;
        case 'x': k = fmt_u32(num, va_arg(ap, uint32_t), 16); break;
        case 's': { const char *s = va_arg(ap, const char *); while (*s && n + 12 < cap) dst[n++] = *s++; break; }
        default:  dst[n++] = *fmt; if (!*fmt) return n; break;
        }
        for (size_t i = 0; i < k; i++) dst[n++] = num[i];
    }
    return n;
}

NOINLINE void uart_write(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) g_uart_dr = (uint8_t)s[i];   /* while(!(SR & TXE)); DR = c; 대용 */
    g_uart_tx_count += (uint32_t)n;
}

NOINLINE void log_printf(const char *fmt, ...) {
    char line[96];                                        /* 한 줄 버퍼: 이 함수 프레임의 대부분 */
    va_list ap;
    va_start(ap, fmt);
    size_t n = log_vformat(line, sizeof line, fmt, ap);
    va_end(ap);
    uart_write(line, n);
}

NOINLINE void control_step(ctl_state_t *st) {
    imu_raw_t raw;
    if (!imu_read(&raw)) return;
    filter_update(&st->att, &raw);
    float u0 = pid_step(&st->pid[0], 0.0f, st->att.roll);
    float u1 = pid_step(&st->pid[1], 0.0f, st->att.pitch);
    float u2 = pid_step(&st->pid[2], 0.0f, st->att.yaw_rate);
    g_pwm_ccr[0] = (uint32_t)(int32_t)(u0 * 100.0f + 500.0f);
    g_pwm_ccr[1] = (uint32_t)(int32_t)(u1 * 100.0f + 500.0f);
    g_pwm_ccr[2] = (uint32_t)(int32_t)(u2 * 100.0f + 500.0f);
    if ((st->cycle++ & 0xffu) == 0)                      /* 256 주기마다 로그 (최악 경로엔 포함!) */
        log_printf("c=%u r=%d p=%d u=%x %s\n", st->cycle, (int32_t)(st->att.roll * 1000.0f),
                   (int32_t)(st->att.pitch * 1000.0f), g_pwm_ccr[0], "ok");
}

/* ---------- 통신 태스크 경로: 패킷 → CRC → COBS → UART ---------- */
NOINLINE uint16_t crc16_ccitt(const uint8_t *p, size_t n) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)(p[i] << 8);
        for (int b = 0; b < 8; b++) crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}

NOINLINE size_t cobs_encode(const uint8_t *in, size_t n, uint8_t *out) {
    size_t code_at = 0, o = 1;
    uint8_t code = 1;
    for (size_t i = 0; i < n; i++) {
        if (in[i] == 0) { out[code_at] = code; code_at = o++; code = 1; }
        else { out[o++] = in[i]; if (++code == 0xff) { out[code_at] = code; code_at = o++; code = 1; } }
    }
    out[code_at] = code;
    out[o++] = 0;
    return o;
}

NOINLINE void comms_send_telemetry(const ctl_state_t *st, uint8_t seq) {
    uint8_t pkt[48];                                      /* 헤더 + 페이로드 + CRC */
    uint8_t enc[56];                                      /* COBS 출력 (n + n/254 + 2) */
    size_t n = 0;
    pkt[n++] = 0xA5; pkt[n++] = seq;
    for (int i = 0; i < 3; i++) {
        int32_t v = (int32_t)(st->pid[i].prev * 1000.0f);
        for (int b = 0; b < 4; b++) pkt[n++] = (uint8_t)(v >> (8 * b));
    }
    for (int i = 0; i < 16; i++) pkt[n++] = (uint8_t)(g_adc_dma[i] >> 4);
    uint16_t crc = crc16_ccitt(pkt, n);
    pkt[n++] = (uint8_t)crc; pkt[n++] = (uint8_t)(crc >> 8);
    size_t m = cobs_encode(pkt, n, enc);
    uart_write((const char *)enc, m);
}

/* ---------- 명령 수신 태스크 경로: 링 버퍼 → 프레임 → CRC 확인 (FPU 안 씀) ---------- */
NOINLINE int rx_parse_once(void) {
    uint8_t frame[32];
    size_t n = 0;
    uint32_t tail = g_uart_rx_tail;
    while (tail != g_uart_rx_head && n < sizeof frame) {
        uint8_t b = g_uart_rx_ring[tail++ & 63u];
        if (b == 0) break;                                /* 0x00 = 프레임 끝 (COBS 구분자) */
        frame[n++] = b;
    }
    g_uart_rx_tail = tail;
    if (n < 3) return -1;
    uint16_t crc = crc16_ccitt(frame, n - 2);
    return crc == (uint16_t)(frame[n - 2] | frame[n - 1] << 8) ? frame[0] : -2;
}

/* ---------- ISR 들 (Cortex-M 에선 MSP 위에서 돈다) ---------- */
NOINLINE void isr_uart_rx(void) {                        /* USART2_IRQHandler: 1 바이트 → 링 버퍼 */
    g_uart_rx_ring[g_uart_rx_head++ & 63u] = (uint8_t)g_uart_dr;
}

NOINLINE uint32_t adc_average(const volatile uint16_t *buf, size_t n) {
    uint16_t sorted[16];                                  /* 중앙값 근처 평균: 정렬 후 양 끝 4개씩 버림 */
    for (size_t i = 0; i < n; i++) {
        uint16_t v = buf[i];
        size_t j = i;
        while (j > 0 && sorted[j - 1] > v) { sorted[j] = sorted[j - 1]; j--; }
        sorted[j] = v;
    }
    uint32_t acc = 0;
    for (size_t i = 4; i + 4 < n; i++) acc += sorted[i];
    return acc / (uint32_t)(n - 8);
}

NOINLINE void isr_adc_dma(void) {                        /* DMA1_Channel1_IRQHandler: 변환 16개 완료 */
    g_adc_avg = adc_average(g_adc_dma, 16);
}

NOINLINE void isr_tick(void) {                           /* SysTick 대용 (실제는 xPortSysTickHandler) */
    g_tick_count++;
}

/* ---------- 재귀의 예 (Part B 의 "재귀 → 에러" 대상, 연습 1) ---------- */
NOINLINE size_t fmt_u32_rec(char *dst, uint32_t v) {
    size_t n = 0;
    if (v >= 10) n = fmt_u32_rec(dst, v / 10);
    dst[n] = (char)('0' + v % 10);
    return n + 1;
}

/* ---------- 태스크 진입점 (호스트 실측에서 "한 번 돌기" 를 위해 for(;;) 대신 1회) ---------- */
NOINLINE void ctrl_task_once(ctl_state_t *st) {
    st->cycle = 0;                                        /* cycle 0 → 로그 경로까지 탄다 = 최악 경로 */
    control_step(st);
}

NOINLINE void comms_task_once(const ctl_state_t *st) {
    comms_send_telemetry(st, (uint8_t)g_tick_count);
}
