// C32_deadlock_detect.c — Ch.32.3 교착 상태를 진짜로 만들고, "탐지(detect)" 하고, 예방책 2가지로 비교한다.
//  모드 (argv[1]):
//   deadlock : T1 = lock(L1); lock(L2)   T2 = lock(L2); lock(L1)   (첫 락 후 잠깐 쉬어서 최악 순서 강제)
//   ordered  : 락 주소 순서대로 잡기 (TIP: ENFORCE LOCK ORDERING BY LOCK ADDRESS) → circular wait 제거
//   trylock  : lock(first); trylock(second) 실패 시 first 를 놓고 랜덤 백오프 후 재시도 → no-preemption 우회
//  감시자(watchdog) 스레드: 각 스레드가 "쥔 락 / 기다리는 락" 을 표에 기록하면, 100ms 마다
//  wait-for 그래프(T → 그 락을 쥔 T') 를 만들고 사이클을 찾는다 (DB 의 deadlock detector 와 같은 원리).
//  macOS 에는 pthread_mutex_timedlock 이 없어서 이런 바깥 감시자가 실용적인 방법이다.
//
// build: cc -Wall -Wextra -O0 -pthread code/C32_deadlock_detect.c -o .work/bin/C32_deadlock_detect
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NT 2
#define NL 2
#define LOOPS 100000

static pthread_mutex_t L[NL] = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER };
static pthread_mutex_t table_m = PTHREAD_MUTEX_INITIALIZER;   // 아래 두 표 보호
static int owner[NL]   = { -1, -1 };   // 락 l 을 쥔 스레드
static int waiting[NT] = { -1, -1 };   // 스레드 t 가 기다리는 락
static long progress[NT];
static long retries = 0;
static enum { DEADLOCK, ORDERED, TRYLOCK } mode;

static void tracked_lock(int t, int l) {
    pthread_mutex_lock(&table_m); waiting[t] = l; pthread_mutex_unlock(&table_m);
    pthread_mutex_lock(&L[l]);
    pthread_mutex_lock(&table_m); waiting[t] = -1; owner[l] = t; pthread_mutex_unlock(&table_m);
}
static int tracked_trylock(int t, int l) {
    if (pthread_mutex_trylock(&L[l]) != 0) return 0;
    pthread_mutex_lock(&table_m); owner[l] = t; pthread_mutex_unlock(&table_m);
    return 1;
}
static void tracked_unlock(int t, int l) {
    (void)t;
    pthread_mutex_lock(&table_m); owner[l] = -1; pthread_mutex_unlock(&table_m);
    pthread_mutex_unlock(&L[l]);
}

static void *worker(void *arg) {
    int t = (int)(long)arg;
    int a = (t == 0) ? 0 : 1, b = (t == 0) ? 1 : 0;   // T0: L0→L1,  T1: L1→L0  (서로 반대 순서)
    if (mode == ORDERED && (uintptr_t)&L[a] > (uintptr_t)&L[b]) { int x = a; a = b; b = x; }
    unsigned seed = (unsigned)t + 1;
    for (long i = 0; i < LOOPS; i++) {
        if (mode == TRYLOCK) {
            for (;;) {
                tracked_lock(t, a);
                if (tracked_trylock(t, b)) break;
                tracked_unlock(t, a);                      // 쥔 걸 내려놓고(스스로 선점) 다시
                __atomic_fetch_add(&retries, 1, __ATOMIC_RELAXED);
                usleep(rand_r(&seed) % 50);                // 랜덤 백오프 → livelock 확률 낮춤
            }
        } else {
            tracked_lock(t, a);
            if (i == 0 && mode == DEADLOCK) usleep(10000); // 첫 락을 쥔 채 잠깐 → 상대도 첫 락을 잡게 됨
            tracked_lock(t, b);
        }
        progress[t]++;                                      // critical section
        tracked_unlock(t, b);
        tracked_unlock(t, a);
    }
    return NULL;
}

// wait-for 그래프에서 사이클 찾기: T → (T 가 기다리는 락의 owner)
static int find_cycle(char *out, size_t n) {
    for (int start = 0; start < NT; start++) {
        int t = start, steps = 0;
        char buf[256]; int len = 0;
        while (steps <= NT) {
            int l = waiting[t];
            if (l < 0) break;
            int o = owner[l];
            if (o < 0) break;
            len += snprintf(buf + len, sizeof buf - len, "T%d -(wants L%d held by)-> ", t, l);
            t = o; steps++;
            if (t == start) { snprintf(out, n, "%sT%d", buf, t); return 1; }
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    const char *m = argc > 1 ? argv[1] : "deadlock";
    mode = !strcmp(m, "ordered") ? ORDERED : !strcmp(m, "trylock") ? TRYLOCK : DEADLOCK;
    printf("mode=%s, %d threads x %d iterations, opposite lock order\n", m, NT, LOOPS);

    pthread_t th[NT];
    for (long i = 0; i < NT; i++) pthread_create(&th[i], NULL, worker, (void *)i);

    for (int tick = 1; tick <= 50; tick++) {          // 최대 5초 감시
        usleep(100000);
        if (progress[0] == LOOPS && progress[1] == LOOPS) break;
        char cyc[256];
        pthread_mutex_lock(&table_m);
        int found = find_cycle(cyc, sizeof cyc);
        pthread_mutex_unlock(&table_m);
        if (found) {
            printf("watchdog @%dms: progress T0=%ld T1=%ld\n", tick * 100, progress[0], progress[1]);
            printf("watchdog: CYCLE in wait-for graph: %s\n", cyc);
            printf("watchdog: recovery = kill & restart (like a DB aborting a victim); exiting with status 2\n");
            exit(2);
        }
    }
    for (int i = 0; i < NT; i++) pthread_join(th[i], NULL);
    printf("done: T0=%ld T1=%ld iterations", progress[0], progress[1]);
    if (mode == TRYLOCK) printf(", trylock retries=%ld", retries);
    printf("\n");
    return 0;
}
