#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

// --- 타입 정의 및 헤더 ---

#define RTOS_MAX_TASKS      16
#define RTOS_MAX_PRIORITIES 32

typedef uint32_t rtos_tick_t;

typedef enum { TASK_READY, TASK_BLOCKED, TASK_SLEEPING, TASK_RUNNING } task_state_t;
typedef void (*task_entry_t)(void*);

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

typedef struct {
    rtos_task_t tasks[RTOS_MAX_TASKS];
    uint32_t    ready_bitmap;
    uint8_t     current_tid;
    uint32_t    default_time_slice;
} fp_sched_t;

// --- 구현 ---

static inline void set_ready(fp_sched_t* s, uint8_t prio) {
    s->ready_bitmap |= (1U << prio);
}
static inline void clr_ready(fp_sched_t* s, uint8_t prio) {
    s->ready_bitmap &= ~(1U << prio);
}
static inline int find_highest_ready(uint32_t bitmap) {
    for (int i = 0; i < RTOS_MAX_PRIORITIES; ++i)
        if (bitmap & (1U << i)) return i;
    return -1;
}

void fp_sched_init(fp_sched_t* s, uint32_t default_time_slice) {
    memset(s, 0, sizeof(*s));
    s->default_time_slice = default_time_slice;
}

int fp_task_create(fp_sched_t* s, const char* name, task_entry_t fn, void* arg, uint8_t prio) {
    for (int i = 0; i < RTOS_MAX_TASKS; ++i) {
        if (!s->tasks[i].used) {
            rtos_task_t* t = &s->tasks[i];
            memset(t, 0, sizeof(*t));
            t->used = 1;
            t->prio = prio;
            t->state = TASK_READY;
            t->entry = fn;
            t->arg = arg;
            strncpy(t->name, name, sizeof(t->name)-1);
            t->time_slice_rem = s->default_time_slice;
            set_ready(s, prio);
            return i;
        }
    }
    return -1;
}

void fp_task_block(fp_sched_t* s, int tid) {
    rtos_task_t* t = &s->tasks[tid];
    if (t->used && t->state == TASK_READY) {
        t->state = TASK_BLOCKED;
        clr_ready(s, t->prio);
    }
}

void fp_task_wake(fp_sched_t* s, int tid) {
    rtos_task_t* t = &s->tasks[tid];
    if (t->used && t->state == TASK_BLOCKED) {
        t->state = TASK_READY;
        set_ready(s, t->prio);
        t->time_slice_rem = s->default_time_slice;
    }
}

void fp_task_sleep_until(fp_sched_t* s, int tid, rtos_tick_t wake_tick) {
    rtos_task_t* t = &s->tasks[tid];
    if (t->used && t->state == TASK_READY) {
        t->state = TASK_SLEEPING;
        t->wake_tick = wake_tick;
        clr_ready(s, t->prio);
    }
}

void fp_tick_isr(fp_sched_t* s, rtos_tick_t now) {
    // 슬립 만료 처리
    for (int i = 0; i < RTOS_MAX_TASKS; ++i) {
        rtos_task_t* t = &s->tasks[i];
        if (t->used && t->state == TASK_SLEEPING && now >= t->wake_tick) {
            t->state = TASK_READY;
            set_ready(s, t->prio);
            t->time_slice_rem = s->default_time_slice;
        }
    }
    // 타임슬라이스 감소
    int tid = s->current_tid;
    if (tid < RTOS_MAX_TASKS && s->tasks[tid].used && s->tasks[tid].state == TASK_RUNNING) {
        if (s->tasks[tid].time_slice_rem > 0)
            s->tasks[tid].time_slice_rem--;
        if (s->tasks[tid].time_slice_rem == 0) {
            // 타임슬라이스 만료: READY로 돌리고 스케줄링
            s->tasks[tid].state = TASK_READY;
            set_ready(s, s->tasks[tid].prio);
        }
    }
}

int fp_schedule(fp_sched_t* s) {
    int prio = find_highest_ready(s->ready_bitmap);
    if (prio < 0) return -1;
    // 같은 우선순위 내 라운드로빈
    for (int i = 0; i < RTOS_MAX_TASKS; ++i) {
        rtos_task_t* t = &s->tasks[i];
        if (t->used && t->prio == prio && t->state == TASK_READY) {
            t->state = TASK_RUNNING;
            clr_ready(s, prio);
            s->current_tid = i;
            return i;
        }
    }
    return -1;
}

rtos_task_t* fp_current(fp_sched_t* s) {
    int tid = s->current_tid;
    if (tid < RTOS_MAX_TASKS && s->tasks[tid].used)
        return &s->tasks[tid];
    return NULL;
}

// --- 테스트 코드 ---

void dummy_task(void* arg) {
    (void)arg;
}

void test_fp_scheduler() {
    fp_sched_t sched;
    fp_sched_init(&sched, 5);

    int t0 = fp_task_create(&sched, "T0", dummy_task, NULL, 0); // prio 0
    int t1 = fp_task_create(&sched, "T1", dummy_task, NULL, 1); // prio 1
    int t2 = fp_task_create(&sched, "T2", dummy_task, NULL, 2); // prio 2

    assert(t0 >= 0 && t1 >= 0 && t2 >= 0);

    // 우선순위 0이 먼저 실행
    int tid = fp_schedule(&sched);
    assert(tid == t0);
    assert(sched.tasks[tid].state == TASK_RUNNING);

    // 타임슬라이스 5, tick 5번 호출 후 READY로 돌아감
    for (int i = 0; i < 5; ++i) {
        fp_tick_isr(&sched, i);
    }
    assert(sched.tasks[t0].state == TASK_READY);

    // 다시 스케줄: prio 0이 다시 실행
    tid = fp_schedule(&sched);
    assert(tid == t0);

    // prio 0을 BLOCKED, prio 1이 실행
    fp_task_block(&sched, t0);
    tid = fp_schedule(&sched);
    assert(tid == t1);

    // prio 1을 SLEEPING, prio 2가 실행
    fp_task_sleep_until(&sched, t1, 10);
    tid = fp_schedule(&sched);
    assert(tid == t2);

    // tick 10에서 prio 1이 깨어남
    fp_tick_isr(&sched, 10);
    assert(sched.tasks[t1].state == TASK_READY);

    // prio 0을 다시 READY로
    fp_task_wake(&sched, t0);
    tid = fp_schedule(&sched);
    assert(tid == t0);

    printf("All fp_scheduler tests passed!\n");
}

#ifndef UNITTEST_MAIN
#define UNITTEST_MAIN
int main() {
    test_fp_scheduler();
    return 0;
}
#endif