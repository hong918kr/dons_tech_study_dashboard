/* C-A03 demo_sb_litmus.c — Store Buffering(SB) 리트머스: "x=1; r1=y  ||  y=1; r2=x" 에서 r1==0 && r2==0 ?
 * 순차 일관성(SC)에서는 불가능한 결과. release store + acquire load 는 이걸 금지하지 않는다 (store→load).
 * 반복 i 마다 A: x=i; r1[i]=y   B: y=i; r2[i]=x.  r1[i] < i && r2[i] < i 이면 SC 위반 관찰.
 * build: cc -std=c11 -O2 -pthread -DMO=0 demo_sb_litmus.c -o sb && ./sb     (0 relaxed, 1 acq/rel, 2 seq_cst)
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef MO
#define MO 0
#endif
#if MO == 0
#define ST memory_order_relaxed
#define LD memory_order_relaxed
#define NAME "relaxed"
#elif MO == 1
#define ST memory_order_release
#define LD memory_order_acquire
#define NAME "release/acquire"
#else
#define ST memory_order_seq_cst
#define LD memory_order_seq_cst
#define NAME "seq_cst"
#endif
#define N 5000000u

static _Alignas(128) atomic_uint x;
static _Alignas(128) atomic_uint y;
static atomic_int go;
static unsigned *r1, *r2;

static void *ta(void *arg) {
    (void)arg;
    while (!atomic_load(&go)) { }
    for (unsigned i = 1; i <= N; i++) { atomic_store_explicit(&x, i, ST); r1[i] = atomic_load_explicit(&y, LD); }
    return NULL;
}
static void *tb(void *arg) {
    (void)arg;
    while (!atomic_load(&go)) { }
    for (unsigned i = 1; i <= N; i++) { atomic_store_explicit(&y, i, ST); r2[i] = atomic_load_explicit(&x, LD); }
    return NULL;
}
int main(void) {
    r1 = calloc(N + 1u, sizeof *r1);
    r2 = calloc(N + 1u, sizeof *r2);
    if (!r1 || !r2) return 1;
    pthread_t a, b;
    pthread_create(&a, NULL, ta, NULL);
    pthread_create(&b, NULL, tb, NULL);
    atomic_store(&go, 1);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    unsigned long sb = 0;
    for (unsigned i = 1; i <= N; i++) sb += (r1[i] < i && r2[i] < i);
    printf("%-15s iterations=%u  SC-violations(r1<i && r2<i)=%lu\n", NAME, N, sb);
    free(r1);
    free(r2);
    return 0;
}
