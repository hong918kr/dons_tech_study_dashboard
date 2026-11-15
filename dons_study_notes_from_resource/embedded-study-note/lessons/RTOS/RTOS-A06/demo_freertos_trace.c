/* BUILD: freertos
 * RTOS-A06 데모 — 진짜 FreeRTOS (V11.3.2, POSIX/GCC 포트, macOS) 에 trace 매크로를 꽂아
 * solution.c 의 레코더(Part A)로 링에 기록 → 공유 텍스트 포맷으로 덤프 → solution.c 의 디코더(Part B)로
 * CPU 표를 만들고, 커널 자신의 실행 시간 통계(configGENERATE_RUN_TIME_STATS, ulRunTimeCounter)와 대조한다.
 * 텍스트 파일은 timeline.py 의 입력이다.
 *
 * 시나리오 (STM32 모터 제어기 모양, 숫자가 클수록 급함):
 *   Report (5) : vTaskDelay(600) 뒤 기록을 멈추고 결과를 출력, exit(0)
 *   Ctrl   (4) : xTaskDelayUntil 5 tick 주기, 매 주기 1.2 ms 계산 (전류/속도 루프 흉내)
 *   Comm   (3) : rxq 큐에서 바이트를 기다림 → 0.3 ms 파싱 → spi 뮤텍스 잡고 0.2 ms 송신
 *   Log    (2) : xTaskDelayUntil 20 tick 주기, spi 뮤텍스 잡고 2.5 ms 플래시 쓰기 + 1 ms 정리
 *   IDLE   (0)
 *   UART_RX "ISR": 틱 훅(틱 ISR 안)에서 7 tick 마다 xQueueSendFromISR(rxq)  (RTOS-B08 의 ISR 대용)
 *
 * 타임스탬프 = tick × 1000 µs + sub-tick(마지막 tick 이후 CLOCK_MONOTONIC 경과, 0..999 로 자름).
 * POSIX 포트의 tick 은 타이머 스레드의 usleep(1000) + SIGALRM 이라 실제 1 ms 가 아니다 → 이 시간은 "커널 시간 +
 * 대략적 sub-tick" 이지 실시간 측정이 아니다. 순서와 귀속 규칙을 확인하는 용도.
 *
 * 빌드 (md 그대로, 레슨 폴더에서):
 *   K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 *   cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_trace.c \
 *      $K/{tasks,list,queue}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c -o /tmp/RTOS-A06_dt
 *   /tmp/RTOS-A06_dt [cap=8192] [stop|overwrite] [out=/tmp/RTOS-A06_trace.txt]
 */
#define A06_LIB_ONLY
#include "solution.c"            /* Part A 레코더 + 텍스트 내보내기, Part B 디코더 (main 은 빠진다) */

#include <pthread.h>
#include <signal.h>
#include <time.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define RUN_TICKS 600

static trace_ring_t g_ring;
static trace_rec_t g_buf[16384];
static const char *g_names[48];
static uint32_t g_nnames;
static volatile int g_on;                         /* 기록 켜짐 */

/* --- 시계 --- */
static uint64_t g_tick = configINITIAL_TICK_COUNT, g_tick_ns;
static uint32_t g_last_t, g_sub_clamp, g_back_clamp, g_ticks_seen;
static uint32_t g_sw_t, g_epoch;
static int g_sw_valid, g_epoch_set;
static uint64_t mono_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static uint32_t ts_now(void) {                    /* 잠금 안에서만 부른다 */
    uint64_t sub = (mono_ns() - g_tick_ns) / 1000u;
    if (sub > 999) { sub = 999; g_sub_clamp++; }  /* 다음 tick 이 늦게 옴 (호스트 부하) */
    uint32_t t = (uint32_t)(g_tick * 1000u + sub);
    if ((int32_t)(t - g_last_t) < 0) { t = g_last_t; g_back_clamp++; }   /* 단조 증가 보장 */
    g_last_t = t;
    return t;
}

/* --- "인터럽트 끄기" = 이 스레드의 모든 시그널 막기 (PRIMASK 저장/복원 대용). 틱 ISR 안에서도 안전하게
 *     중첩된다: 이전 마스크를 그대로 되돌린다. 타임스탬프 읽기 + 링 쓰기를 한 덩어리로 묶는 게 핵심. --- */
typedef sigset_t lock_t;
static void lock(lock_t *old) { sigset_t all; sigfillset(&all); pthread_sigmask(SIG_BLOCK, &all, old); }
static void unlock(const lock_t *old) { pthread_sigmask(SIG_SETMASK, old, NULL); }

static uint8_t name_id(const char *n) {           /* 이름표 (태스크 이름은 TCB 안 배열 → 태스크를 지우지 않으니 유효) */
    for (uint32_t i = 0; i < g_nnames; i++) if (g_names[i] == n || strcmp(g_names[i], n) == 0) return (uint8_t)i;
    if (g_nnames == sizeof g_names / sizeof g_names[0]) return 0;
    g_names[g_nnames] = n;
    return (uint8_t)g_nnames++;
}
static void rec_locked(uint32_t t, int ev, const char *who, const char *argname) {
    if (!g_on) return;
    tr_record(&g_ring, t, (uint8_t)ev, name_id(who), argname ? ARG_NAME : ARG_NONE, argname ? name_id(argname) : 0);
}
static void rec(int ev, const char *who, const char *argname) {
    lock_t o; lock(&o);
    rec_locked(ts_now(), ev, who, argname);
    unlock(&o);
}

/* --- trace 훅 (FreeRTOSConfig.h 의 매크로가 부른다) --- */
void a06_tick(unsigned long long new_tick) {
    lock_t o; lock(&o);
    g_ticks_seen++;
    if (new_tick > g_tick) { g_tick = new_tick; g_tick_ns = mono_ns(); }   /* 스케줄러 정지 중 중복 호출은 무시 */
    unlock(&o);
}
void a06_switch_out(const char *task) {
    lock_t o; lock(&o);
    g_sw_t = ts_now(); g_sw_valid = 1;            /* OUT, 커널 실행시간 카운터, IN 이 같은 시각을 본다 */
    rec_locked(g_sw_t, EV_SWITCH_OUT, task, NULL);
    unlock(&o);
}
void a06_switch_in(const char *task) {
    lock_t o; lock(&o);
    uint32_t t = g_sw_valid ? g_sw_t : ts_now();
    g_sw_valid = 0;
    if (!g_epoch_set) { g_epoch = t; g_epoch_set = 1; }   /* tasks.c 3809: 첫 태스크 = 실행시간 0 시점 */
    rec_locked(t, EV_SWITCH_IN, task, NULL);
    unlock(&o);
}
unsigned int a06_runtime(void) {
    lock_t o; lock(&o);
    uint32_t t = g_sw_valid ? g_sw_t : ts_now();
    unlock(&o);
    return t - g_epoch;
}
void a06_block(const char *task, const char *why) { rec(EV_BLOCK, task, why); }
void a06_ready(const char *task) { rec(EV_UNBLOCK, task, NULL); }

static void *g_obj[4]; static const char *g_obj_name[4]; static int g_nobj;
static const char *obj_name(void *h) { for (int i = 0; i < g_nobj; i++) if (g_obj[i] == h) return g_obj_name[i]; return NULL; }
void a06_queue(int op, void *obj, int is_mutex) {
    const char *on = obj_name(obj);
    if (!on || xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) return;   /* 생성 중의 내부 send 등 */
    const char *me = pcTaskGetName(NULL);          /* queue.c 에서는 pxCurrentTCB 가 안 보인다 */
    if (op == 0) rec(is_mutex ? EV_BLOCK : EV_Q_RECV_BLOCK, me, on);     /* 뮤텍스 대기 = BLOCK <task> spi */
    else if (op == 1) rec(EV_Q_SEND_BLOCK, me, on);
    else if (op == 2 && is_mutex) rec(EV_MUTEX_TAKE, me, on);
    else if (op == 3 && is_mutex) rec(EV_MUTEX_GIVE, me, on);
}

/* --- 애플리케이션 --- */
static QueueHandle_t g_rxq;
static SemaphoreHandle_t g_spi;
static uint32_t g_isr_n, g_isr_sent;
static void busy_us(uint32_t us) { uint64_t end = mono_ns() + (uint64_t)us * 1000u; while (mono_ns() < end) { } }

void vApplicationTickHook(void) {                  /* 틱 ISR 안 (SIGALRM 핸들러) */
    if (++g_isr_n % 7u) return;
    rec(EV_ISR_ENTER, "UART_RX", NULL);
    uint8_t byte = (uint8_t)g_isr_n;
    BaseType_t woken = pdFALSE;                    /* 틱 ISR 끝에서 xYieldPendings 로 전환된다 (RTOS-B08) */
    if (xQueueSendFromISR(g_rxq, &byte, &woken) == pdPASS) g_isr_sent++;
    rec(EV_ISR_EXIT, "UART_RX", NULL);
}

static void ctrl_task(void *p) {
    (void)p;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        xTaskDelayUntil(&last, 5);
        rec(EV_RELEASE, "Ctrl", NULL);                 /* 애플리케이션 마커: 이 주기의 job 시작 */
        busy_us(1200);
    }
}
static void comm_task(void *p) {
    (void)p;
    uint8_t b;
    for (;;) {
        if (xQueueReceive(g_rxq, &b, portMAX_DELAY) != pdPASS) continue;
        busy_us(300);
        xSemaphoreTake(g_spi, portMAX_DELAY);
        busy_us(200);
        xSemaphoreGive(g_spi);
    }
}
static void log_task(void *p) {
    (void)p;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        xTaskDelayUntil(&last, 20);
        rec(EV_RELEASE, "Log", NULL);
        xSemaphoreTake(g_spi, portMAX_DELAY);
        busy_us(2500);                                 /* 플래시 쓰기 동안 spi 를 쥐고 있다 */
        xSemaphoreGive(g_spi);
        busy_us(1000);
    }
}

static char g_txt[1 << 20];
static dec_t g_dec;
static const char *g_out = "/tmp/RTOS-A06_trace.txt";

static void report_task(void *p) {
    (void)p;
    uint64_t w0 = mono_ns();
    vTaskDelay(RUN_TICKS);
    g_on = 0;                                          /* 기록 정지: 마지막 이벤트 = 이 태스크의 SWITCH_IN */
    uint64_t wall_us = (mono_ns() - w0) / 1000u;

    TaskStatus_t st[8]; configRUN_TIME_COUNTER_TYPE total = 0;
    UBaseType_t n = uxTaskGetSystemState(st, 8, &total);

    tr_text_t x; trace_rec_t r;
    tx_begin(&x, g_txt, sizeof g_txt, g_names, g_nnames);
    while (tr_pop(&g_ring, &r)) tx_record(&x, &r);
    tx_end(&x, &g_ring);
    FILE *f = fopen(g_out, "w");
    if (f) { fputs(g_txt, f); fclose(f); }
    int rc = trace_decode(g_txt, &g_dec);

    printf("ring cap=%u mode=%s  events=%u lines=%u dropped=%u  text=%zu B overflow=%d  -> %s\n",
           g_ring.cap, g_ring.mode == TR_OVERWRITE ? "overwrite" : "stop", g_ring.next_seq, g_dec.n_events,
           g_ring.dropped, x.len, x.overflow, g_out);
    printf("ticks: %d (kernel) in %llu us wall; tick-hook calls=%u; sub-tick clamps=%u back-clamps=%u; "
           "UART_RX isr=%u sent=%u\n", RUN_TICKS, (unsigned long long)wall_us, g_ticks_seen, g_sub_clamp,
           g_back_clamp, g_isr_n / 7u, g_isr_sent);
    printf("decode rc=%d (0=OK)\n", rc);
    dec_print_table(&g_dec);
    printf("\n%-8s %10s %10s %10s %12s %6s\n", "task", "trace_run", "+stolen", "=sum", "kernel_ctr", "diff");
    for (UBaseType_t i = 0; i < n; i++) {
        int c = dec_find(&g_dec, st[i].pcTaskName);
        uint64_t run = c < 0 ? 0 : g_dec.c[c].run_us, sto = c < 0 ? 0 : g_dec.c[c].stolen_us;
        printf("%-8s %10llu %10llu %10llu %12u %6lld\n", st[i].pcTaskName, (unsigned long long)run,
               (unsigned long long)sto, (unsigned long long)(run + sto), (unsigned)st[i].ulRunTimeCounter,
               (long long)(run + sto) - (long long)st[i].ulRunTimeCounter);
    }
    printf("kernel total runtime=%u us, trace span=%llu us (unknown before first SWITCH_IN=%llu)\n",
           (unsigned)total, (unsigned long long)g_dec.span_us, (unsigned long long)g_dec.unknown_us);
    fflush(stdout);
    exit(0);
}

int main(int argc, char **argv) {
    uint32_t cap = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 8192u;
    tr_mode_t mode = (argc > 2 && strcmp(argv[2], "overwrite") == 0) ? TR_OVERWRITE : TR_STOP_WHEN_FULL;
    if (argc > 3) g_out = argv[3];
    if (cap > sizeof g_buf / sizeof g_buf[0] || !tr_init(&g_ring, g_buf, cap, mode)) {
        fprintf(stderr, "cap must be a power of two <= 16384\n");
        return 2;
    }
    g_tick_ns = mono_ns();
    g_last_t = (uint32_t)(g_tick * 1000u);         /* 0 으로 두면 첫 시각 4294667000 이 "2^31 넘게 뒤로" 로 보여 막힌다 */
    g_rxq = xQueueCreate(16, 1);
    g_spi = xSemaphoreCreateMutex();
    g_obj[0] = g_rxq; g_obj_name[0] = "rxq";
    g_obj[1] = g_spi; g_obj_name[1] = "spi";
    g_nobj = 2;
    g_on = 1;
    xTaskCreate(report_task, "Report", configMINIMAL_STACK_SIZE, NULL, 5, NULL);
    xTaskCreate(ctrl_task, "Ctrl", configMINIMAL_STACK_SIZE, NULL, 4, NULL);
    xTaskCreate(comm_task, "Comm", configMINIMAL_STACK_SIZE, NULL, 3, NULL);
    xTaskCreate(log_task, "Log", configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    vTaskStartScheduler();
    return 1;
}
