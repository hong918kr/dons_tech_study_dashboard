/* OS-B04 demo_race.c — 스레드 N 개가 공유 카운터를 각 ITERS 번 증가: lost update 를 실제로 본다.
 * 사용: ./a.out [nthreads=4] [iters=1000000]
 * build: cc -std=c11 -O0 -pthread demo_race.c -o race_o0 && ./race_o0          # 매 반복 ldr/add/str
 *        cc -std=c11 -O2 -pthread demo_race.c -o race_o2 && ./race_o2          # 컴파일러가 루프를 합침
 *        cc -std=c11 -O2 -pthread -DVOL demo_race.c -o race_vol && ./race_vol  # volatile: 매 반복 ldr/add/str
 *        cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_race.c -o race_t && ./race_t   # TSan 첫 만남
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef VOL
static volatile long counter;     /* volatile: 접근을 지우지 못하게 할 뿐, 원자성은 없다 */
#else
static long counter;              /* 모든 스레드가 같은 주소를 본다 (공유 주소 공간) */
#endif
static long iters = 1000000;

static void *worker(void *arg) {
    (void)arg;
    for (long i = 0; i < iters; i++)
        counter++;                /* load → add → store : 원자적이지 않다 */
    return NULL;
}

int main(int argc, char **argv) {
    int nt = argc > 1 ? atoi(argv[1]) : 4;
    if (argc > 2) iters = atol(argv[2]);
    if (nt < 1 || nt > 64) nt = 4;
    pthread_t t[64];
    for (int i = 0; i < nt; i++)
        if (pthread_create(&t[i], NULL, worker, NULL) != 0) { perror("pthread_create"); return 1; }
    for (int i = 0; i < nt; i++) pthread_join(t[i], NULL);
    long expect = (long)nt * iters;
    printf("threads=%d iters=%ld expect=%ld got=%ld lost=%ld (%.1f%%)\n", nt, iters, expect,
           (long)counter, expect - (long)counter, 100.0 * (double)(expect - (long)counter) / (double)expect);
    return 0;
}
