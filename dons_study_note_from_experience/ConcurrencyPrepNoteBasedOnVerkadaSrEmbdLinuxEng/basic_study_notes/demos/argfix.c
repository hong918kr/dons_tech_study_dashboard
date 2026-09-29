/* argfix.c — 방법 2(값 캐스팅) / 3(구조체 배열) / 4(malloc 소유권 이전) */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* 2) 작은 정수는 포인터 크기에 실어 보낸다 (주소가 아니라 값) */
static void *by_value(void *arg) {
    long id = (long)(intptr_t)arg;
    printf("by_value   id=%ld\n", id);
    return (void *)(intptr_t)(id * 10);       /* 반환값도 같은 트릭 */
}

/* 3) 스레드마다 다른 슬롯 = 다른 주소 */
struct Job { int id; double gain; };
static void *by_slot(void *arg) {
    struct Job *j = arg;
    printf("by_slot    id=%d gain=%.1f\n", j->id, j->gain);
    return NULL;
}

/* 4) 힙에 만들어 넘기고, 해제 책임까지 넘긴다 */
static void *by_owned(void *arg) {
    struct Job *j = arg;
    printf("by_owned   id=%d gain=%.1f\n", j->id, j->gain);
    free(j);                                   /* 소유권이 여기 있다 */
    return NULL;
}

int main(void) {
    pthread_t t[3];
    struct Job jobs[1] = { { 7, 1.5 } };       /* main 이 join 까지 살아있다 */
    struct Job *owned = malloc(sizeof *owned);
    owned->id = 9; owned->gain = 2.5;

    pthread_create(&t[0], NULL, by_value, (void *)(intptr_t)3);
    pthread_create(&t[1], NULL, by_slot,  &jobs[0]);
    pthread_create(&t[2], NULL, by_owned, owned);

    void *ret = NULL;
    pthread_join(t[0], &ret);
    printf("by_value returned %ld\n", (long)(intptr_t)ret);
    pthread_join(t[1], NULL);
    pthread_join(t[2], NULL);
    return 0;
}
