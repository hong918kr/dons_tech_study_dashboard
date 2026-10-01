// C31_dining.c — Ch.31.6 식사하는 철학자. macOS 네이티브 세마포어인 dispatch_semaphore 로 포크 5개를 만든다.
//  broken : 모두 "왼쪽 → 오른쪽" 순서. 모두가 왼쪽 포크를 쥔 순간 교착.
//           (재현을 확실히 하려고 왼쪽을 쥔 뒤 10ms 쉬게 함 = 최악의 스케줄을 강제)
//  fixed  : 철학자 4만 "오른쪽 → 왼쪽" (Dijkstra 의 해법) → 순환 대기가 깨져 교착 없음
//  교착 감지: dispatch_semaphore_wait 에 2초 타임아웃을 주고, 타임아웃이 나면 "누가 무엇을 쥐고 기다리는지" 출력
//
// build: cc -Wall -Wextra -O0 -pthread code/C31_dining.c -o .work/bin/C31_dining
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#define N 5
#define MEALS 20
static dispatch_semaphore_t forks[N];
static int fixed = 0;
static int meals[N];
static int holding[N];                       // 디버그용: 철학자 p 가 쥐고 있는 첫 포크 (-1 = 없음)
static volatile int deadlocked = 0;

static int left(int p)  { return p; }
static int right(int p) { return (p + 1) % N; }

// 2초 안에 못 얻으면 0 반환
static int grab(int p, int f) {
    long rc = dispatch_semaphore_wait(forks[f], dispatch_time(DISPATCH_TIME_NOW, 2LL * NSEC_PER_SEC));
    if (rc != 0) {
        printf("  P%d: holding fork %d, waited 2s for fork %d -> timeout\n", p, holding[p], f);
        deadlocked = 1;
        return 0;
    }
    return 1;
}

static void *philosopher(void *arg) {
    int p = (int)(long)arg;
    for (int i = 0; i < MEALS && !deadlocked; i++) {
        int first = left(p), second = right(p);
        if (fixed && p == N - 1) { first = right(p); second = left(p); }   // 순서를 뒤집은 한 명
        if (!grab(p, first)) return NULL;
        holding[p] = first;
        usleep(10000);                          // 첫 포크를 쥐고 잠깐 멍때림 → 최악의 interleaving 유도
        if (!grab(p, second)) return NULL;      // (교착 시: 쥔 포크는 데모라서 그냥 둔다)
        meals[p]++;                             // eat()
        dispatch_semaphore_signal(forks[first]);
        dispatch_semaphore_signal(forks[second]);
        holding[p] = -1;
    }
    return NULL;
}

static void run(int fix) {
    fixed = fix; deadlocked = 0;
    for (int i = 0; i < N; i++) { forks[i] = dispatch_semaphore_create(1); meals[i] = 0; holding[i] = -1; }
    printf("=== %s ===\n", fix ? "fixed: P4 grabs right first" : "broken: everyone grabs left first");
    pthread_t t[N];
    for (long i = 0; i < N; i++) pthread_create(&t[i], NULL, philosopher, (void *)i);
    for (int i = 0; i < N; i++) pthread_join(t[i], NULL);
    printf("  meals:");
    for (int i = 0; i < N; i++) printf(" P%d=%d", i, meals[i]);
    printf("  -> %s\n", deadlocked ? "DEADLOCK detected" : "all finished");
    // dispatch_semaphore 는 생성 값보다 작은 상태로 해제하면 크래시하므로, 교착 시엔 일부러 해제하지 않는다
}

int main(void) {
    run(0);
    run(1);
    return 0;
}
