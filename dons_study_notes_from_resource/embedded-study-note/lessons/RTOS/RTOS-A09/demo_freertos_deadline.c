/* BUILD: freertos
 * RTOS-A09 데모 — 진짜 FreeRTOS(V11.3.2, POSIX/GCC 포트, macOS)에 버그 3종을 심고, trace 매크로로
 * RTOS-A06 포맷 트레이스를 파일로 남긴다 → `./solution 파일` 분석기가 같은 진단을 내는지 본다.
 *
 *   ./demo inv   out.txt   S1 우선순위 역전: spi 락 = xSemaphoreCreateBinary (상속 없음)
 *   ./demo inv_fix out.txt  같은 시나리오, 락 = xSemaphoreCreateMutex (우선순위 상속)
 *   ./demo rm    out.txt   S4 BMS(T=40 tick) 가 IMU(T=5 tick) 보다 높은 우선순위
 *   ./demo rm_fix out.txt   RM 순서로 바꿈
 *   ./demo queue out.txt   S3 logq 깊이 2, LOGGER(p1) 가 COMM(p2) 버스트 동안 굶음
 *   ./demo queue_fix out.txt LOGGER 를 p3 으로
 *
 * 시간: POSIX 포트에 실시간성은 없다. 1 tick = 10 ms (configTICK_RATE_HZ 100). 타임스탬프 =
 *   tick × 10000 µs + (마지막 틱 이후 clock_gettime 경과, 0..9999 µs 로 자름), 단조 증가로 보정.
 * "일" = CPU 틱 회계: 틱 인터럽트가 왔을 때 돌고 있던 태스크(pxCurrentTCB)의 카운터를 1 올린다.
 *   work(n) 은 "내 카운터가 n 오를 때까지 돈다" — 선점당한 시간은 세지 않으니 실행 시간 C 를 틱 단위로 흉내 낸다.
 * RELEASE / DEADLINE_MISS 는 틱 훅(틱 ISR 안, tasks.c 5000-5013)에서 판정한다 — 모델(solution.c)과 같은 정책:
 *   마감 = 다음 출시 (D = T), 작업이 아직 돌고 있으면 그 출시는 건너뛴다.
 * ISR 과다(S2)는 여기서 재현하지 않는다: POSIX 포트의 "ISR" 은 시그널 핸들러라 그 안에서 돈 CPU 를 틱 회계가
 *   태스크 몫으로 센다 → 모델(solution.c)에서만 다룬다 (md 5. 풀이).
 *
 * 빌드 (md 그대로):
 *   K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
 *   cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_deadline.c \
 *      $K/{tasks,list,queue}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c -o /tmp/RTOS-A09_demo
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define TICK_US 10000u
#define MAXEV   4096
#define MAXP    4

/* ---------------- 트레이스 기록 (시그널 핸들러 안에서도 불린다: 배열에만) ---------------- */
enum { E_RELEASE, E_SWITCH_IN, E_SWITCH_OUT, E_BLOCK, E_UNBLOCK, E_MUTEX_TAKE, E_MUTEX_GIVE, E_Q_SEND_BLOCK,
       E_Q_RECV_BLOCK, E_DEADLINE_MISS };
static const char * const k_ev[] = { "RELEASE", "SWITCH_IN", "SWITCH_OUT", "BLOCK", "UNBLOCK", "MUTEX_TAKE",
                                     "MUTEX_GIVE", "Q_SEND_BLOCK", "Q_RECV_BLOCK", "DEADLINE_MISS" };
typedef struct { uint32_t t; uint8_t ev; const char *who, *obj; uint32_t num; int has_num; } rec_t;
static rec_t g_rec[MAXEV];
static _Atomic int g_nrec;
static volatile int g_on = 1;
static volatile uint64_t g_tick, g_tick_ns;
static const char *g_pending_out;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);               /* async-signal-safe (POSIX) */
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
static uint32_t now_us(void) {
    uint64_t sub = (now_ns() - g_tick_ns) / 1000u;
    if (sub > TICK_US - 1) sub = TICK_US - 1;
    return (uint32_t)(g_tick * TICK_US + sub);
}
static void rec(int ev, const char *who, const char *obj, uint32_t num, int has_num) {
    if (!g_on) return;
    int k = g_nrec++;
    if (k >= MAXEV) return;
    g_rec[k] = (rec_t){ now_us(), (uint8_t)ev, who, obj, num, has_num };
}

/* ---------------- 시나리오 표 ---------------- */
typedef struct { const char *name; int prio, first, period; void (*job)(int me); } ptask_t;
static ptask_t g_pt[MAXP];
static int g_np;
static TaskHandle_t g_h[MAXP + 2];                 /* 주기 태스크들 + LOGGER */
static volatile uint32_t g_cpu[MAXP + 2];          /* 태스크별 CPU 틱 */
static volatile int g_active[MAXP];
static uint64_t g_next_rel[MAXP], g_deadline[MAXP];
static uint64_t g_last_counted;
static int g_end_tick = 80;
static SemaphoreHandle_t g_spi;
static QueueHandle_t g_logq;
static int g_spi_is_mutex;
static int g_nmiss;

typedef struct { void *q; const char *name; int is_lock; } obj_t;
static obj_t g_obj[4];
static int g_nobj;

void a09_switch_out(const char *name) { g_pending_out = name; }
void a09_switch_in(const char *name) {
    if (g_pending_out && !strcmp(g_pending_out, name)) { g_pending_out = NULL; return; }   /* 같은 태스크로 복귀: 생략 */
    if (g_pending_out) rec(E_SWITCH_OUT, g_pending_out, NULL, 0, 0);
    g_pending_out = NULL;
    rec(E_SWITCH_IN, name, NULL, 0, 0);
}
void a09_tick(uint64_t t, void *cur) {
    if (t == g_last_counted) return;                /* 스케줄러 정지 중 펜딩 틱이 다시 풀릴 때 두 번 세지 않게 */
    g_last_counted = t;
    for (int i = 0; i < g_np + 1; i++) if (g_h[i] && (void *)g_h[i] == cur) g_cpu[i]++;
    g_tick = t; g_tick_ns = now_ns();
}
void a09_q(int what, void *q) {
    const obj_t *o = NULL;
    for (int i = 0; i < g_nobj; i++) if (g_obj[i].q == q) o = &g_obj[i];
    if (!o || xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) return;
    const char *me = pcTaskGetName(NULL);
    if (o->is_lock) {
        if (what == 0) rec(E_MUTEX_TAKE, me, o->name, 0, 0);
        else if (what == 1) rec(E_MUTEX_GIVE, me, o->name, 0, 0);
        else if (what == 2) rec(E_BLOCK, me, o->name, 0, 0);
    } else {
        if (what == 2) rec(E_Q_RECV_BLOCK, me, o->name, 0, 0);
        else if (what == 3) rec(E_Q_SEND_BLOCK, me, o->name, 0, 0);
    }
}
void a09_ready(const char *name) { if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) rec(E_UNBLOCK, name, NULL, 0, 0); }
void a09_delay_until(const char *name) { rec(E_BLOCK, name, "delay", 0, 0); }

/* 틱 ISR 안: 마감 먼저, 그다음 출시 (모델 sim_run 의 3 → 4 순서) */
void vApplicationTickHook(void) {
    if (g_tick >= (uint64_t)g_end_tick) { g_on = 0; return; }
    for (int i = 0; i < g_np; i++)
        if (g_active[i] && g_deadline[i] == g_tick) {
            rec(E_DEADLINE_MISS, g_pt[i].name, NULL, (uint32_t)(g_deadline[i] * TICK_US), 1);
            g_nmiss++;
        }
    for (int i = 0; i < g_np; i++)
        if (g_next_rel[i] == g_tick) {
            if (!g_active[i]) {
                g_active[i] = 1;
                g_deadline[i] = g_tick + (uint64_t)g_pt[i].period;
                rec(E_RELEASE, g_pt[i].name, NULL, (uint32_t)(g_deadline[i] * TICK_US), 1);
            }
            g_next_rel[i] += (uint64_t)g_pt[i].period;   /* 돌고 있으면 이번 출시는 건너뜀 */
        }
}

static void work(int me, uint32_t n) {
    uint32_t target = g_cpu[me] + n;
    while (g_cpu[me] < target) { }
}
static void lock(void) { xSemaphoreTake(g_spi, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(g_spi); }

/* ---- S1: CTRL(p3, T=10) · COMM(p2, T=50) · LOG(p1, T=100) ---- */
static void job_ctrl_inv(int me) { work(me, 1); lock(); work(me, 1); unlock(); work(me, 1); }
static void job_comm_inv(int me) { work(me, 10); }
static void job_log_inv(int me)  { lock(); work(me, 6); unlock(); work(me, 1); }
/* ---- S4: BMS(T=40, C=12) · IMU(T=5, C=1) · TELEM(T=100, C=5) ---- */
static void job_bms(int me)   { work(me, 12); }
static void job_imu(int me)   { work(me, 1); }
static void job_telem(int me) { work(me, 5); }
/* ---- S3: CTRL(p4, T=5) 매 주기 로그 1개 · COMM(p2) 25 tick 버스트 · LOGGER 이벤트 구동 ---- */
static void job_ctrl_q(int me) { uint32_t rec_ = (uint32_t)me; work(me, 1); xQueueSend(g_logq, &rec_, portMAX_DELAY); }
static void job_comm_q(int me) { work(me, 25); }

static void periodic(void *p) {
    int me = (int)(intptr_t)p;
    TickType_t last = 0;
    xTaskDelayUntil(&last, (TickType_t)g_pt[me].first);
    for (;;) {
        g_pt[me].job(me);
        g_active[me] = 0;
        TickType_t now = xTaskGetTickCount();
        while (last + (TickType_t)g_pt[me].period <= now) last += (TickType_t)g_pt[me].period;  /* 놓친 출시 건너뛰기 */
        xTaskDelayUntil(&last, (TickType_t)g_pt[me].period);
    }
}
static void logger(void *p) {
    (void)p;
    uint32_t item;
    for (;;) xQueueReceive(g_logq, &item, portMAX_DELAY);   /* 처리 시간 ≈ 0: 핵심은 "언제 CPU 를 받느냐" */
}

static const char *g_out;
static void supervisor(void *p) {
    (void)p;
    vTaskDelay((TickType_t)g_end_tick + 1);
    g_on = 0;
    FILE *f = fopen(g_out, "w");
    if (!f) { perror(g_out); exit(2); }
    fprintf(f, "# rtos-trace v1 unit=us\n# source FreeRTOS V11.3.2 POSIX port, 1 tick = %u us\n", TICK_US);
    for (int i = 0; i < g_np; i++)
        fprintf(f, "# task %s prio=%d period=%u\n", g_pt[i].name, g_pt[i].prio, (unsigned)g_pt[i].period * TICK_US);
    if (g_h[g_np]) fprintf(f, "# task LOGGER prio=%d period=0\n", (int)uxTaskPriorityGet(g_h[g_np]));
    int n = g_nrec < MAXEV ? g_nrec : MAXEV;
    uint32_t last = 0;
    for (int i = 0; i < n; i++) {
        rec_t *r = &g_rec[i];
        if (r->t < last) r->t = last;                /* 기록 인덱스와 시각이 한 칸 엇갈린 경우 단조로 보정 */
        last = r->t;
        if (r->has_num) fprintf(f, "%u %s %s %u\n", r->t, k_ev[r->ev], r->who, r->num);
        else if (r->obj) fprintf(f, "%u %s %s %s\n", r->t, k_ev[r->ev], r->who, r->obj);
        else fprintf(f, "%u %s %s\n", r->t, k_ev[r->ev], r->who);
    }
    fclose(f);
    printf("%s: %d events, %d deadline misses (tick %d..%d)\n", g_out, n, g_nmiss, 0, g_end_tick);
    for (int i = 0; i < g_np; i++) printf("  %-6s prio %d  cpu ticks %u\n", g_pt[i].name, g_pt[i].prio, (unsigned)g_cpu[i]);
    fflush(stdout);
    exit(0);
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s inv|inv_fix|rm|rm_fix|queue|queue_fix out.txt\n", argv[0]); return 2; }
    const char *sc = argv[1];
    g_out = argv[2];
    g_tick_ns = now_ns();                           /* 첫 틱 전 이벤트의 µs 기준 */
    int logger_prio = -1;
    if (!strncmp(sc, "inv", 3)) {
        g_spi_is_mutex = !strcmp(sc, "inv_fix");
        g_pt[0] = (ptask_t){ "CTRL", 3, 7, 10, job_ctrl_inv };
        g_pt[1] = (ptask_t){ "COMM", 2, 8, 50, job_comm_inv };
        g_pt[2] = (ptask_t){ "LOG", 1, 5, 100, job_log_inv };
        g_np = 3; g_end_tick = 60;
        g_spi = g_spi_is_mutex ? xSemaphoreCreateMutex() : xSemaphoreCreateBinary();
        if (!g_spi_is_mutex) xSemaphoreGive(g_spi);   /* 이진 세마포어는 비어서 태어난다 → 한 번 채워 "열림" */
        g_obj[g_nobj++] = (obj_t){ g_spi, "spi", 1 };
    } else if (!strncmp(sc, "rm", 2)) {
        int fix = !strcmp(sc, "rm_fix");
        g_pt[0] = (ptask_t){ "BMS", fix ? 2 : 3, 5, 40, job_bms };
        g_pt[1] = (ptask_t){ "IMU", fix ? 3 : 2, 5, 5, job_imu };
        g_pt[2] = (ptask_t){ "TELEM", 1, 6, 100, job_telem };
        g_np = 3; g_end_tick = 60;
    } else if (!strncmp(sc, "queue", 5)) {
        g_pt[0] = (ptask_t){ "CTRL", 4, 6, 5, job_ctrl_q };
        g_pt[1] = (ptask_t){ "COMM", 2, 12, 200, job_comm_q };
        g_np = 2; g_end_tick = 70;
        g_logq = xQueueCreate(2, sizeof(uint32_t));
        g_obj[g_nobj++] = (obj_t){ g_logq, "logq", 0 };
        logger_prio = !strcmp(sc, "queue_fix") ? 3 : 1;
    } else { fprintf(stderr, "unknown scenario %s\n", sc); return 2; }
    for (int i = 0; i < g_np; i++) {
        g_next_rel[i] = (uint64_t)g_pt[i].first;
        xTaskCreate(periodic, g_pt[i].name, configMINIMAL_STACK_SIZE, (void *)(intptr_t)i, (UBaseType_t)g_pt[i].prio, &g_h[i]);
    }
    if (logger_prio > 0) xTaskCreate(logger, "LOGGER", configMINIMAL_STACK_SIZE, NULL, (UBaseType_t)logger_prio, &g_h[g_np]);
    xTaskCreate(supervisor, "SUP", configMINIMAL_STACK_SIZE, NULL, 5, NULL);
    vTaskStartScheduler();
    return 1;
}
