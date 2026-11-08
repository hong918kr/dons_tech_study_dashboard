/* C-A02 demo_litmus.c — MP(message passing) 리트머스: Apple Silicon(ARMv8) 이 store/load 순서를 바꾸는가
 *
 *   cc -std=c11 -O2 -pthread demo_litmus.c -o demo_litmus && ./demo_litmus 0 && ./demo_litmus 1
 *
 * writer:  data[i] = 1;  [fence]  flag[i] = 1;        (디스크립터 필드 → OWN 과 같은 모양)
 * reader:  f = flag[i];  [fence]  d = data[i];         (OWN 확인 → 필드 읽기)
 * "f == 1 이고 d == 0" 이면 하드웨어가 순서를 바꾼 것이다 (컴파일러는 소스 순서대로 냈다 — md 의 asm 참고).
 * 모든 접근은 relaxed 원자라 C11 data race(UB)는 없다. mode 0 = fence 없음, mode 1 = release/acquire fence.
 * 결과는 실행마다 다르다 — 이 파일은 관찰용이고 테스트(CHECK)에는 쓰지 않는다.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define N (1u << 20)
#define ROUNDS 50
static _Atomic uint32_t *data, *flag;
static atomic_int go;
static int mode;
static long weak, seen;

static void *writer(void *a) {
    (void)a;
    while (!atomic_load(&go)) { }
    for (uint32_t i = 0; i < N; i++) {
        atomic_store_explicit(&data[i], 1u, memory_order_relaxed);
        if (mode == 1) atomic_thread_fence(memory_order_release);
        atomic_store_explicit(&flag[i], 1u, memory_order_relaxed);
    }
    return NULL;
}
static void *reader(void *a) {
    (void)a;
    while (!atomic_load(&go)) { }
    for (uint32_t i = 0; i < N; i++) {
        uint32_t f = atomic_load_explicit(&flag[i], memory_order_relaxed);
        if (mode == 1) atomic_thread_fence(memory_order_acquire);
        uint32_t d = atomic_load_explicit(&data[i], memory_order_relaxed);
        if (f) { seen++; if (!d) weak++; }
    }
    return NULL;
}
int main(int argc, char **argv) {
    mode = argc > 1 ? atoi(argv[1]) : 0;
    data = calloc(N, sizeof *data);
    flag = calloc(N, sizeof *flag);
    if (!data || !flag) return 1;
    long tw = 0, ts = 0;
    for (int r = 0; r < ROUNDS; r++) {
        for (uint32_t i = 0; i < N; i++) { atomic_init(&data[i], 0u); atomic_init(&flag[i], 0u); }
        weak = seen = 0;
        atomic_store(&go, 0);
        pthread_t a, b;
        pthread_create(&a, NULL, writer, NULL);
        pthread_create(&b, NULL, reader, NULL);
        atomic_store(&go, 1);
        pthread_join(a, NULL);
        pthread_join(b, NULL);
        tw += weak; ts += seen;
    }
    printf("mode=%d (%s): flag=1 seen %ld times, of which data==0: %ld\n",
           mode, mode ? "rel/acq fence" : "no fence", ts, tw);
    free(data); free(flag);
    return 0;
}
