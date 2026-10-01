// C29_list_queue_hash.c — OSTEP Ch.29 의 리스트 · 큐 · 해시를 직접 만들어 재 본다.
//  실험 1 (Fig 29.11): 4 스레드가 각자 N 개 insert. 단일 락 리스트 vs 버킷별 락 해시(101 버킷)
//  실험 2: 1000 노드 리스트에서 4 스레드 lookup. 단일 락 vs hand-over-hand(노드별 락)
//  실험 3 (Fig 29.9): Michael & Scott 2-락 큐 vs 큰 락 1개 큐. 생산자 2 + 소비자 2, 합계 검증
// build: cc -Wall -Wextra -O2 -pthread code/C29_list_queue_hash.c -o .work/bin/C29_list_queue_hash
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// =============================================================== 리스트 (Fig 29.8)
typedef struct node { int key; struct node *next; } node_t;
typedef struct { node_t *head; pthread_mutex_t lock; } list_t;

static void List_Init(list_t *L) { L->head = NULL; pthread_mutex_init(&L->lock, NULL); }

static int List_Insert(list_t *L, int key) {
    node_t *new = malloc(sizeof(*new));          // malloc 은 락 밖에서 (스레드 안전)
    if (new == NULL) { perror("malloc"); return -1; }
    new->key = key;
    pthread_mutex_lock(&L->lock);                // 진짜 임계 구역만 잠근다
    new->next = L->head;
    L->head = new;
    pthread_mutex_unlock(&L->lock);
    return 0;
}

static int List_Lookup(list_t *L, int key) {
    int rv = -1;
    pthread_mutex_lock(&L->lock);
    for (node_t *c = L->head; c; c = c->next)
        if (c->key == key) { rv = 0; break; }    // return 대신 break → 출구 하나
    pthread_mutex_unlock(&L->lock);
    return rv;
}

static long List_Count(list_t *L) {
    long n = 0;
    for (node_t *c = L->head; c; c = c->next) n++;
    return n;
}

// =============================================================== 해시 (Fig 29.10)
#define BUCKETS 101
typedef struct { list_t lists[BUCKETS]; } hash_t;
static void Hash_Init(hash_t *H) { for (int i = 0; i < BUCKETS; i++) List_Init(&H->lists[i]); }
static int Hash_Insert(hash_t *H, int key) { return List_Insert(&H->lists[key % BUCKETS], key); }

// =============================================================== hand-over-hand 리스트
typedef struct hnode { int key; struct hnode *next; pthread_mutex_t lock; } hnode_t;
typedef struct { hnode_t *head; pthread_mutex_t lock; } hlist_t;   // lock: head 포인터 보호

static int HList_Lookup(hlist_t *L, int key) {
    pthread_mutex_lock(&L->lock);
    hnode_t *c = L->head;
    if (!c) { pthread_mutex_unlock(&L->lock); return -1; }
    pthread_mutex_lock(&c->lock);                // 다음 것 잡고
    pthread_mutex_unlock(&L->lock);              // 이전 것 놓기 (손 바꿔 잡기)
    while (c) {
        if (c->key == key) { pthread_mutex_unlock(&c->lock); return 0; }
        hnode_t *n = c->next;
        if (n) pthread_mutex_lock(&n->lock);
        pthread_mutex_unlock(&c->lock);
        c = n;
    }
    return -1;
}

// =============================================================== M&S 2-락 큐 (Fig 29.9)
typedef struct qnode { int value; struct qnode *next; } qnode_t;
typedef struct {
    qnode_t *head, *tail;
    pthread_mutex_t headLock, tailLock;
} queue_t;

static void Queue_Init(queue_t *q) {
    qnode_t *tmp = malloc(sizeof(*tmp));         // 더미 노드: head 와 tail 을 떼어 놓는 장치
    assert(tmp);
    tmp->next = NULL;
    q->head = q->tail = tmp;
    pthread_mutex_init(&q->headLock, NULL);
    pthread_mutex_init(&q->tailLock, NULL);
}
static void Queue_Enqueue(queue_t *q, int value) {
    qnode_t *tmp = malloc(sizeof(*tmp));
    assert(tmp);
    tmp->value = value;
    tmp->next = NULL;
    pthread_mutex_lock(&q->tailLock);
    q->tail->next = tmp;
    q->tail = tmp;
    pthread_mutex_unlock(&q->tailLock);
}
static int Queue_Dequeue(queue_t *q, int *value) {
    pthread_mutex_lock(&q->headLock);
    qnode_t *tmp = q->head;
    qnode_t *newHead = tmp->next;
    if (newHead == NULL) { pthread_mutex_unlock(&q->headLock); return -1; }
    *value = newHead->value;
    q->head = newHead;                           // newHead 가 새 더미가 된다
    pthread_mutex_unlock(&q->headLock);
    free(tmp);
    return 0;
}

// 비교용: 큰 락 하나 큐 (head/tail 둘 다 같은 락)
static int one_lock_mode;
static pthread_mutex_t bigLock = PTHREAD_MUTEX_INITIALIZER;
static void Q_Enq(queue_t *q, int v) {
    if (!one_lock_mode) { Queue_Enqueue(q, v); return; }
    qnode_t *tmp = malloc(sizeof(*tmp)); assert(tmp);
    tmp->value = v; tmp->next = NULL;
    pthread_mutex_lock(&bigLock);
    q->tail->next = tmp; q->tail = tmp;
    pthread_mutex_unlock(&bigLock);
}
static int Q_Deq(queue_t *q, int *v) {
    if (!one_lock_mode) return Queue_Dequeue(q, v);
    pthread_mutex_lock(&bigLock);
    qnode_t *tmp = q->head, *nh = tmp->next;
    if (!nh) { pthread_mutex_unlock(&bigLock); return -1; }
    *v = nh->value; q->head = nh;
    pthread_mutex_unlock(&bigLock);
    free(tmp);
    return 0;
}

// =============================================================== 워커들
#define NT 4
static list_t gL;
static hash_t gH;
static hlist_t gHL;
static int per_thread;
static int use_hash;
static atomic_int go;

static void *insert_worker(void *arg) {
    int base = (int)(long)arg * per_thread;
    while (!atomic_load(&go)) ;
    for (int i = 0; i < per_thread; i++) {
        if (use_hash) Hash_Insert(&gH, base + i);
        else List_Insert(&gL, base + i);
    }
    return NULL;
}

#define LIST_N 1000
#define LOOKUPS 20000
static int use_hoh;
static void *lookup_worker(void *arg) {
    unsigned seed = (unsigned)(long)arg + 1;
    int found = 0;
    while (!atomic_load(&go)) ;
    for (int i = 0; i < LOOKUPS; i++) {
        int key = (int)(rand_r(&seed) % LIST_N);
        found += (use_hoh ? HList_Lookup(&gHL, key) : List_Lookup(&gL, key)) == 0;
    }
    return (void *)(long)found;
}

#define QITEMS 500000
static queue_t gQ;
static atomic_long consumed_sum, consumed_cnt;
static atomic_int producers_done;
static void *producer(void *arg) {
    int base = (int)(long)arg * QITEMS;
    while (!atomic_load(&go)) ;
    for (int i = 1; i <= QITEMS; i++) Q_Enq(&gQ, base + i);
    atomic_fetch_add(&producers_done, 1);
    return NULL;
}
static void *consumer(void *arg) {
    (void)arg;
    long sum = 0, cnt = 0;
    int v;
    while (!atomic_load(&go)) ;
    for (;;) {
        int done = atomic_load(&producers_done) == 2;  // 먼저 "생산 끝"을 확인하고
        if (Q_Deq(&gQ, &v) == 0) { sum += v; cnt++; }
        else if (done) break;                          // 그 뒤에도 비었으면 진짜 끝
    }
    atomic_fetch_add(&consumed_sum, sum);
    atomic_fetch_add(&consumed_cnt, cnt);
    return NULL;
}

static double run_threads(void *(*f)(void *), int n, long *ret_sum) {
    pthread_t t[NT];
    atomic_store(&go, 0);
    for (long i = 0; i < n; i++) pthread_create(&t[i], NULL, f, (void *)i);
    double t0 = now();
    atomic_store(&go, 1);
    long s = 0;
    for (int i = 0; i < n; i++) { void *r; pthread_join(t[i], &r); s += (long)r; }
    if (ret_sum) *ret_sum = s;
    return now() - t0;
}

int main(void) {
    // ---- 실험 1
    printf("== 실험 1: %d 스레드 동시 insert (스레드당 N 개)\n", NT);
    printf("      N   single-lock list(s)   hash, 101 locks(s)   speedup\n");
    for (int n = 10000; n <= 50000; n += 10000) {
        per_thread = n;
        List_Init(&gL); use_hash = 0;
        double tl = run_threads(insert_worker, NT, NULL);
        long cl = List_Count(&gL);
        Hash_Init(&gH); use_hash = 1;
        double th = run_threads(insert_worker, NT, NULL);
        long ch = 0;
        for (int b = 0; b < BUCKETS; b++) ch += List_Count(&gH.lists[b]);
        assert(cl == (long)NT * n && ch == (long)NT * n);
        printf("%7d   %19.4f   %18.4f   %6.1fx\n", n, tl, th, tl / th);
    }

    // ---- 실험 2
    List_Init(&gL);
    for (int k = LIST_N - 1; k >= 0; k--) List_Insert(&gL, k);
    pthread_mutex_init(&gHL.lock, NULL);
    gHL.head = NULL;
    for (int k = LIST_N - 1; k >= 0; k--) {
        hnode_t *h = malloc(sizeof(*h)); assert(h);
        h->key = k; h->next = gHL.head; pthread_mutex_init(&h->lock, NULL);
        gHL.head = h;
    }
    printf("\n== 실험 2: 노드 %d 개 리스트, %d 스레드가 각 %d 회 lookup\n", LIST_N, NT, LOOKUPS);
    long f1, f2;
    use_hoh = 0; double ts = run_threads(lookup_worker, NT, &f1);
    use_hoh = 1; double th = run_threads(lookup_worker, NT, &f2);
    printf("single lock   : %.3f s  (found %ld)\n", ts, f1);
    printf("hand-over-hand: %.3f s  (found %ld)  → %.1fx %s\n", th, f2,
           th > ts ? th / ts : ts / th, th > ts ? "느림" : "빠름");

    // ---- 실험 3
    printf("\n== 실험 3: 큐, 생산자 2 (각 %d 개) + 소비자 2\n", QITEMS);
    long expect = 0;
    for (long p = 0; p < 2; p++) for (long i = 1; i <= QITEMS; i++) expect += p * QITEMS + i;
    for (one_lock_mode = 1; one_lock_mode >= 0; one_lock_mode--) {
        Queue_Init(&gQ);
        atomic_store(&consumed_sum, 0); atomic_store(&consumed_cnt, 0);
        atomic_store(&producers_done, 0);
        pthread_t t[4];
        atomic_store(&go, 0);
        pthread_create(&t[0], NULL, producer, (void *)0L);
        pthread_create(&t[1], NULL, producer, (void *)1L);
        pthread_create(&t[2], NULL, consumer, NULL);
        pthread_create(&t[3], NULL, consumer, NULL);
        double t0 = now();
        atomic_store(&go, 1);
        for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
        double dt = now() - t0;
        printf("%-22s: %.3f s  items=%ld sum=%ld %s\n",
               one_lock_mode ? "one big lock" : "Michael&Scott 2-lock", dt,
               atomic_load(&consumed_cnt), atomic_load(&consumed_sum),
               atomic_load(&consumed_sum) == expect ? "(OK)" : "(MISMATCH!)");
    }
    return 0;
}
