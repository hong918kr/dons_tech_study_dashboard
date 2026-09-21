/*
65. Discuss thread safety in a single-producer, single-consumer scenario.
*/
// 65
/*
In a single-producer, single-consumer scenario, 
thread safety can be achieved without locks
if only the producer modifies the head and only the consumer modifies the tail. 
The count variable must be updated atomically if accessed by both threads. 
For multi-producer or multi-consumer, additional synchronization is required.
*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define CB_SIZE 8
/*
요약
SPSC: 생산자/소비자가 각각 head/tail만 접근하면 락 없이 빠르고 안전하게 동작.
MPMC: 여러 스레드가 동시에 접근하므로 mutex/condition variable로 동기화 필요.
실제 임베디드/멀티스레드 환경에서 매우 자주 쓰이는 패턴입니다.
코드의 핵심:

SPSC는 구조적으로 경쟁 조건이 없으므로 락이 필요 없고,
MPMC는 경쟁 조건이 있으므로 락과 조건변수가 반드시 필요합니다.

*/
typedef struct {
    uint8_t buffer[CB_SIZE];
    size_t size;
    size_t head;
    size_t tail;
    size_t count;
    // For multi-producer/consumer
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
} cb_t;

// --- Single-producer, single-consumer (SPSC) functions ---
/*
1. 원형 버퍼 구조체(cb_t)
buffer: 실제 데이터 저장 공간
head: 데이터가 들어갈 위치(생산자)
tail: 데이터가 나올 위치(소비자)
count: 현재 저장된 데이터 개수
mutex, not_full, not_empty: MPMC(멀티스레드)용 동기화 도구
*/
void cb_init(cb_t *cb) {
    cb->size = CB_SIZE;
    cb->head = 0;
    cb->tail = 0;
    cb->count = 0;
    pthread_mutex_init(&cb->mutex, NULL);
    pthread_cond_init(&cb->not_full, NULL);
    pthread_cond_init(&cb->not_empty, NULL);
}
/*
2. SPSC (Single-Producer, Single-Consumer) 함수
특징:
- 생산자만 head와 count를 증가, 소비자만 tail과 count를 감소
- 락(lock) 없이 안전하게 동작 (단, count를 동시에 접근하면 원자성 보장 필요)
용도:
- 한 쪽에서만 데이터를 넣고, 한 쪽에서만 데이터를 빼는 경우 (예: ISR ↔ 메인루프)
*/
bool cb_push_spsc(cb_t *cb, uint8_t data) {
    // No lock needed for SPSC if only producer calls this
    if (cb->count == cb->size) return false; // full
    cb->buffer[cb->head] = data;
    cb->head = (cb->head + 1) % cb->size;
    cb->count++;
    return true;
}

bool cb_pop_spsc(cb_t *cb, uint8_t *data) {
    // No lock needed for SPSC if only consumer calls this
    if (cb->count == 0) return false; // empty
    *data = cb->buffer[cb->tail];
    cb->tail = (cb->tail + 1) % cb->size;
    cb->count--;
    return true;
}

// --- Multi-producer, multi-consumer (MPMC) functions ---
/*
3. MPMC (Multi-Producer, Multi-Consumer) 함수
특징:
- 여러 스레드가 동시에 push/pop 할 수 있으므로 mutex로 보호
- 버퍼가 가득/비었을 때는 condition variable로 대기/알림
용도:
- 여러 생산자/소비자가 동시에 접근하는 환경 (예: 여러 스레드가 동시에 로그 기록 등)
*/
bool cb_push_mpmc(cb_t *cb, uint8_t data) {
    pthread_mutex_lock(&cb->mutex);
    while (cb->count == cb->size) {
        pthread_cond_wait(&cb->not_full, &cb->mutex);
    }
    cb->buffer[cb->head] = data;
    cb->head = (cb->head + 1) % cb->size;
    cb->count++;
    pthread_cond_signal(&cb->not_empty);
    pthread_mutex_unlock(&cb->mutex);
    return true;
}

bool cb_pop_mpmc(cb_t *cb, uint8_t *data) {
    pthread_mutex_lock(&cb->mutex);
    while (cb->count == 0) {
        pthread_cond_wait(&cb->not_empty, &cb->mutex);
    }
    *data = cb->buffer[cb->tail];
    cb->tail = (cb->tail + 1) % cb->size;
    cb->count--;
    pthread_cond_signal(&cb->not_full);
    pthread_mutex_unlock(&cb->mutex);
    return true;
}

// --- Test code ---
/*
4. 테스트 코드(main)
SPSC 테스트:
- 생산자/소비자 각각 1개 스레드로 동작
- 락 없이 안전하게 데이터가 주고받아짐
MPMC 테스트:
- 생산자/소비자 각각 2개 스레드로 동작
- mutex/condition variable로 동기화
*/
// SPSC producer/consumer
void* spsc_producer(void *arg) {
    cb_t *cb = (cb_t*)arg;
    for (uint8_t i = 1; i <= 10; ++i) {
        while (!cb_push_spsc(cb, i)) usleep(1000);
        printf("[SPSC Producer] Pushed: %u\n", i);
        usleep(10000);
    }
    return NULL;
}

void* spsc_consumer(void *arg) {
    cb_t *cb = (cb_t*)arg;
    uint8_t val;
    for (int i = 1; i <= 10; ++i) {
        while (!cb_pop_spsc(cb, &val)) usleep(1000);
        printf("[SPSC Consumer] Popped: %u\n", val);
        usleep(15000);
    }
    return NULL;
}

// MPMC producer/consumer
void* mpmc_producer(void *arg) {
    cb_t *cb = (cb_t*)arg;
    for (uint8_t i = 1; i <= 5; ++i) {
        cb_push_mpmc(cb, i);
        printf("[MPMC Producer %lu] Pushed: %u\n", pthread_self(), i);
        usleep(8000);
    }
    return NULL;
}

void* mpmc_consumer(void *arg) {
    cb_t *cb = (cb_t*)arg;
    uint8_t val;
    for (int i = 1; i <= 5; ++i) {
        cb_pop_mpmc(cb, &val);
        printf("[MPMC Consumer %lu] Popped: %u\n", pthread_self(), val);
        usleep(12000);
    }
    return NULL;
}

int main(void) {
    printf("=== Single-producer, Single-consumer (SPSC) Test ===\n");
    cb_t cb_spsc;
    cb_init(&cb_spsc);

    pthread_t prod, cons;
    pthread_create(&prod, NULL, spsc_producer, &cb_spsc);
    pthread_create(&cons, NULL, spsc_consumer, &cb_spsc);
    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    printf("\n=== Multi-producer, Multi-consumer (MPMC) Test ===\n");
    cb_t cb_mpmc;
    cb_init(&cb_mpmc);

    pthread_t prods[2], consm[2];
    pthread_create(&prods[0], NULL, mpmc_producer, &cb_mpmc);
    pthread_create(&prods[1], NULL, mpmc_producer, &cb_mpmc);
    pthread_create(&consm[0], NULL, mpmc_consumer, &cb_mpmc);
    pthread_create(&consm[1], NULL, mpmc_consumer, &cb_mpmc);

    for (int i = 0; i < 2; ++i) {
        pthread_join(prods[i], NULL);
        pthread_join(consm[i], NULL);
    }

    return 0;
}

/*
------------------ Explanation ------------------

[SPSC]
- 단일 생산자/소비자에서는 head/tail을 각자만 접근하면 락 없이 안전합니다.
- count 변수도 한쪽에서만 증가/감소하므로 경쟁 조건이 없습니다.

[MPMC]
- 여러 생산자/소비자가 동시에 접근하므로 mutex와 condition variable로 동기화합니다.
- push/pop 시 mutex로 보호하고, 버퍼가 가득/비었을 때는 cond wait/signal로 대기/알림합니다.

------------------ Example Output ------------------

=== Single-producer, Single-consumer (SPSC) Test ===
[SPSC Producer] Pushed: 1
[SPSC Consumer] Popped: 1
...
[SPSC Producer] Pushed: 10
[SPSC Consumer] Popped: 10

=== Multi-producer, Multi-consumer (MPMC) Test ===
[MPMC Producer ...] Pushed: 1
[MPMC Consumer ...] Popped: 1
...
(Producer/Consumer 여러 개가 번갈아가며 동작)

----------------------------------------------------
*/