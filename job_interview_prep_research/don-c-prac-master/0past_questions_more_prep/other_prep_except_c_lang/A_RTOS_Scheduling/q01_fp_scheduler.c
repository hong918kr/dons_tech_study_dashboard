/*
 * 1. 고정 우선순위 프리엠프 스케줄러 (Ready Bitmap O(1) + Time Slice)
 *
 * 상황 설명:
 * 임베디드 RTOS에서 여러 태스크를 우선순위 기반으로 스케줄링할 때,
 * Ready Bitmap을 활용해 O(1)로 가장 높은 우선순위 태스크를 선택하고,
 * Time Slice(할당 시간) 기반 선점(preemption)을 지원해야 한다.
 * 각 태스크는 READY/BLOCKED/SLEEPING/RUNNING 상태를 가지며,
 * Tick ISR에서 슬립/타임슬라이스/우선순위 처리가 필요하다.
 *
 * 주요 연습 포인트:
 * - 태스크 등록/삭제, 상태 관리
 * - Ready Bitmap 기반 O(1) 우선순위 선택
 * - Time Slice 만료 시 라운드로빈
 * - Tick ISR에서 wake/sleep 처리
 * - 태스크 구조체/스케줄러 구조체 분리
 */

#ifndef FP_SCHEDULER_H
#define FP_SCHEDULER_H
#include "rtos_types.h"

#define RTOS_MAX_TASKS      16
#define RTOS_MAX_PRIORITIES 32

typedef enum { TASK_READY, TASK_BLOCKED, TASK_SLEEPING, TASK_RUNNING } task_state_t;

typedef void (*task_entry_t)(void*);

/*
 * rtos_task_t
 * - RTOS의 각 태스크를 나타내는 구조체
 * - used: 태스크 슬롯 사용 여부
 * - prio: 태스크 우선순위 (0이 가장 높음)
 * - state: 현재 태스크 상태
 * - wake_tick: SLEEPING 상태에서 깨어날 tick
 * - entry: 태스크 진입 함수 포인터
 * - arg: 태스크 진입 함수 인자
 * - sp: 스택 포인터(문맥 전환용)
 * - name: 태스크 이름
 * - time_slice_rem: 남은 타임슬라이스 tick
 */
typedef struct {
    uint8_t       used;
    uint8_t       prio;          // 0 = highest
    task_state_t  state;
    rtos_tick_t   wake_tick;
    task_entry_t  entry;
    void*         arg;
    void*         sp;
    char          name[12];
    uint32_t      time_slice_rem;
} rtos_task_t;

/*
 * fp_sched_t
 * - 전체 스케줄러 상태를 나타내는 구조체
 * - tasks: 태스크 배열
 * - ready_bitmap: READY 상태 태스크의 우선순위 비트맵
 * - current_tid: 현재 실행 중인 태스크 인덱스
 * - default_time_slice: 기본 타임슬라이스 tick 수
 */
typedef struct {
    rtos_task_t tasks[RTOS_MAX_TASKS];
    uint32_t    ready_bitmap;
    uint8_t     current_tid;
    uint32_t    default_time_slice;
} fp_sched_t;

/*
 * fp_sched_init
 * - 스케줄러 구조체를 초기화하고, 기본 타임슬라이스를 설정한다.
 */
void fp_sched_init(fp_sched_t* s, uint32_t default_time_slice);

/*
 * fp_task_create
 * - 새로운 태스크를 등록한다.
 * - name: 태스크 이름
 * - fn: 태스크 진입 함수
 * - arg: 진입 함수 인자
 * - prio: 우선순위(0이 가장 높음)
 * - 반환값: 태스크 인덱스(성공), 음수(실패)
 */
int  fp_task_create(fp_sched_t* s, const char* name, task_entry_t fn, void* arg, uint8_t prio);

/*
 * fp_task_block
 * - 지정한 태스크를 BLOCKED 상태로 전환한다.
 */
void fp_task_block(fp_sched_t* s, int tid);

/*
 * fp_task_wake
 * - BLOCKED 상태의 태스크를 READY로 전환한다.
 */
void fp_task_wake(fp_sched_t* s, int tid);

/*
 * fp_task_sleep_until
 * - 태스크를 SLEEPING 상태로 전환하고, wake_tick에 지정한 tick까지 슬립시킨다.
 */
void fp_task_sleep_until(fp_sched_t* s, int tid, rtos_tick_t wake_tick);

/*
 * fp_tick_isr
 * - Tick 인터럽트에서 호출.
 * - 슬립 만료, 타임슬라이스 감소, READY 상태 전환 등을 처리한다.
 */
void fp_tick_isr(fp_sched_t* s, rtos_tick_t now);

/*
 * fp_schedule
 * - Ready Bitmap을 이용해 실행할 태스크를 선택한다.
 * - 반환값: 선택된 태스크 인덱스
 */
int  fp_schedule(fp_sched_t* s);

/*
 * fp_current
 * - 현재 실행 중인 태스크 구조체 포인터를 반환한다.
 */
rtos_task_t* fp_current(fp_sched_t* s);

#endif