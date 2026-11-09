/* LLAT-M03 demo_uaf.c — "꺼내자마자 free" 의 use-after-free 를 진짜 스레드 2 개 + 강제 스케줄로 재현.
 *
 *   MODE=0  HP 없는 M-S dequeue + 즉시 free   → ASan: heap-use-after-free
 *   MODE=1  solution.c 의 HP dequeue (R=5)     → 같은 스케줄인데 깨끗
 * 스케줄: 스레드 0(reader) 이 head 포인터 A 를 읽고 "역참조 직전" 에 멈춘다 → 스레드 1 이 A 를 꺼내고
 *         해제(MODE 0) / retire+scan(MODE 1) 하고, 같은 크기 노드를 하나 새로 할당한다 → 스레드 0 이 A->next 를 읽는다.
 *
 * cc -std=c11 -O2 -pthread -DMODE=0 demo_uaf.c -o uaf0 && ./uaf0
 * cc -std=c11 -g -O1 -fno-inline -fsanitize=address -fno-omit-frame-pointer -pthread -DMODE=0 demo_uaf.c -o uaf0a && ./uaf0a
 * cc -std=c11 -g -O1 -fno-inline -fsanitize=address -fno-omit-frame-pointer -pthread -DMODE=1 demo_uaf.c -o uaf1a && ./uaf1a
 */
#define USE_MALLOC 1           /* 정적 풀 대신 진짜 malloc/free */
#define main solution_main
#include "solution.c"
#undef main
#ifndef MODE
#define MODE 0
#endif

static hp_dom_t D;
static msq_t Q;
static atomic_int go_t1, t1_done;

static void gate_hook(int pt, int tid) {
    if (tid == 0 && pt == HK_DEQ_HEAD) {           /* 스레드 0: A 를 쥐고 여기서 멈춤 */
        atomic_store(&go_t1, 1);
        while (!atomic_load(&t1_done)) sched_yield();
    }
}

/* HP 없는 원래 M-S dequeue (Michael & Scott 1996 의 모양, 카운터 없는 포인터) + 즉시 free */
static bool naive_dequeue(msq_t *q, int tid, uint64_t *out) {
    for (;;) {
        node_t *h = atomic_load_explicit(&q->head, memory_order_acquire);
        HOOK(HK_DEQ_HEAD, tid);
        node_t *t = atomic_load_explicit(&q->tail, memory_order_acquire);
        node_t *next = atomic_load_explicit(&h->next, memory_order_acquire);   /* ← h 가 이미 해제됐다면 UAF */
        if (atomic_load(&q->head) != h) {
            printf("reader: read A->next = %p from freed memory, head changed -> retry\n", (void *)next);
            continue;
        }
        if (next == NULL) return false;
        if (h == t) { atomic_compare_exchange_strong(&q->tail, &t, next); continue; }
        uint64_t v = next->val;
        node_t *e = h;
        if (atomic_compare_exchange_strong(&q->head, &e, next)) {
            node_free(h);                              /* 즉시 free: 다른 스레드가 h 를 쥐고 있을 수 있다 */
            *out = v;
            return true;
        }
    }
}
static bool deq(int tid, uint64_t *v) { return MODE == 0 ? naive_dequeue(&Q, tid, v) : msq_dequeue(&Q, tid, v); }

static void *reader(void *arg) {
    (void)arg;
    uint64_t v = 0;
    bool ok = deq(0, &v);
    printf("reader: dequeue -> %s %llu\n", ok ? "got" : "empty", (unsigned long long)v);
    return NULL;
}

int main(void) {
    hp_init(&D, 2, 5, node_free);
    msq_init(&Q, &D);
    for (uint64_t i = 1; i <= 3; i++) msq_enqueue(&Q, 1, i);
    node_t *A = atomic_load(&Q.head);
    printf("mode %d (%s): head dummy A = %p\n", MODE, MODE == 0 ? "no HP, free now" : "hazard pointers", (void *)A);
    if (MODE == 1) for (int i = 0; i < 4; i++) hp_retire(&D, 1, node_alloc(70));   /* 다음 retire 에서 scan */
    g_hook = gate_hook;
    pthread_t th;
    pthread_create(&th, NULL, reader, NULL);
    while (!atomic_load(&go_t1)) sched_yield();
    uint64_t v;
    deq(1, &v);                                        /* 스레드 1: A 를 꺼낸다 (MODE 0: free(A)) */
    printf("writer: dequeued %llu, scans=%ld freed=%ld, A %s\n", (unsigned long long)v, D.rec[1].nscan,
           D.rec[1].nfreed, MODE == 0 ? "freed" : (D.rec[1].n == 1 && D.rec[1].list[0] == A ? "kept (hp0)" : "?"));
    msq_enqueue(&Q, 1, 77);                            /* 같은 크기 할당: 해제된 A 자리를 malloc 이 재사용할 수 있다 */
    node_t *n77 = atomic_load(&Q.tail);
    printf("writer: new node 77 at %p (%s A)\n", (void *)n77, n77 == A ? "SAME address as" : "not");
    atomic_store(&t1_done, 1);
    pthread_join(th, NULL);
    g_hook = NULL;
    for (int t = 0; t < 2; t++) hp_scan(&D, t);
    printf("done: live nodes %ld\n", atomic_load(&g_live));
    return 0;
}
