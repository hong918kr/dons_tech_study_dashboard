/* RTOS-B01 demo — 같은 협력형 스케줄러를 "진짜 시계" 로 (HOST 맛보기, 비결정적)
 *
 *   시계: clock_gettime(CLOCK_MONOTONIC) 를 ms 로 자른 uint32_t + 일부러 큰 offset 을 더해
 *         실행 1 초 무렵 32-bit wrap 이 일어나게 한다 (HAL_GetTick 이 49.7 일 뒤 넘어가는 것의 축소판).
 *   작업: cost ms 만큼 바쁜 대기(spin) = CPU 를 정말로 쓴다. 할 일 없으면 nanosleep(200 us) = WFI 대용.
 *   macOS 는 실시간 OS 가 아니다 → 숫자는 실행마다 조금씩 다르다. 비교(긴 작업 vs 쪼갠 작업)만 본다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_host_clock.c -o demo_host_clock && ./demo_host_clock
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint32_t g_tick_offset;
static uint32_t now_ms(void) {                     /* HAL_GetTick() 자리: 32-bit ms, wrap 함 */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u) + g_tick_offset;
}
static void spin_ms(uint32_t ms) {                 /* 블로킹 작업: 정말로 CPU 를 ms 동안 쓴다 */
    uint32_t t = now_ms();
    while (now_ms() - t < ms) { }
}
static bool time_due(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

typedef enum { JOB_DONE, JOB_MORE } job_ret_t;
typedef struct { const char *name; job_ret_t (*fn)(void); uint32_t period, next_run; bool in_progress;
                 uint32_t runs, lat_min, lat_max; } job_t;

static job_ret_t sensor(void)  { spin_ms(1); return JOB_DONE; }
static job_ret_t control(void) { spin_ms(4); return JOB_DONE; }
static job_ret_t telem_long(void) { spin_ms(30); return JOB_DONE; }
static int g_chunk = -1;                           /* -1 = FORMAT 단계 */
static job_ret_t telem_split(void) {
    if (g_chunk < 0) { spin_ms(2); g_chunk = 0; return JOB_MORE; }
    spin_ms(4);
    if (++g_chunk == 7) { g_chunk = -1; return JOB_DONE; }
    return JOB_MORE;
}

static void run(const char *label, job_ret_t (*telem)(void), uint32_t duration) {
    job_t J[3] = { { "SENSOR", sensor, 10, 0, false, 0, 0, 0 },
                   { "CONTROL", control, 50, 0, false, 0, 0, 0 },
                   { "TELEM", telem, 100, 0, false, 0, 0, 0 } };
    g_chunk = -1;
    uint32_t start = now_ms();
    for (int i = 0; i < 3; i++) J[i].next_run = start;
    bool wrapped = false; uint32_t prev = start;
    while (now_ms() - start < duration) {
        uint32_t now = now_ms();
        if (now < prev) wrapped = true;
        prev = now;
        int ran = -1;
        for (int i = 0; i < 3 && ran < 0; i++) {
            job_t *j = &J[i];
            if (j->in_progress) { j->in_progress = (j->fn() == JOB_MORE); ran = i; break; }
            if (time_due(now, j->next_run)) {
                uint32_t lat = now - j->next_run;
                if (j->runs == 0 || lat < j->lat_min) j->lat_min = lat;
                if (lat > j->lat_max) j->lat_max = lat;
                j->runs++;
                j->next_run += j->period;
                j->in_progress = (j->fn() == JOB_MORE);
                ran = i;
            }
        }
        if (ran < 0) nanosleep(&(struct timespec){ 0, 200000 }, NULL);   /* WFI 대용 */
    }
    printf("%-14s (wrap seen: %s)\n", label, wrapped ? "yes" : "no");
    for (int i = 0; i < 3; i++)
        printf("  %-7s runs %3u/%-3u  lat max %2u ms  jitter %2u ms\n", J[i].name, J[i].runs,
               duration / J[i].period, J[i].lat_max, J[i].lat_max - J[i].lat_min);
}

int main(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint32_t raw = (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
    g_tick_offset = 0xFFFFFFFFu - raw - 1000u + 1u;     /* now_ms() ≈ 0xFFFFFC18 → 약 1 초 뒤 wrap */
    run("long (30 ms)", telem_long, 2000);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    raw = (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
    g_tick_offset = 0xFFFFFFFFu - raw - 1000u + 1u;
    run("split (2+7x4)", telem_split, 2000);
    return 0;
}
