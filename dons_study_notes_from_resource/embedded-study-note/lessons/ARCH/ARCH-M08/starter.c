/* ARCH-M08 False Sharing 측정 — 체크 테스트 (결정적인 것만)
 *
 *   Part A  레이아웃 산수: granules_touched · share_granule · round_up, 한 칸 = 한 라인인 slot_t
 *   Part B  스윕 기계: bump (atomic ldadd / volatile 저장 / volatile 읽고 쓰기), stride 4..512 에서
 *           스레드마다 자기 칸만 올렸을 때 정확한 합계 + 이웃 바이트 오염 없음, estimate_granule (표 → 경계)
 *   Part C  구조체 설계: layout_violations (필드 표 → 규칙 위반 수), 듀얼 코어 패킷 큐 pq_t 를 규칙에 맞게
 *
 * 시간은 CHECK 하지 않는다 → demo_bench.c (stride 스윕, 코어 종류 쌍, 스레드 수, 구조체 전/후).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
 * 새니타이저 빌드에서는 증가 횟수를 1/10 로 줄인다 (SAN_SCALE).
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GRANULE 128   /* sysctl hw.cachelinesize = 128 (Apple M6). Cortex-A 보통 64, Cortex-M7 D-cache 32 */

#if defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#define SAN_SCALE 10
#endif
#endif
#ifndef SAN_SCALE
#define SAN_SCALE 1
#endif

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

/* ======================= Part A — 레이아웃 산수 ======================= */
/* g 는 2의 거듭제곱. 바이트 범위 [addr, addr+size) 가 걸치는 g-블록(granule) 수. size 0 → 0. */
static size_t granules_touched(uintptr_t addr, size_t size, size_t g) {
    (void)addr; (void)size; (void)g;
    return 0; /* TODO: 첫 블록 번호와 마지막 블록 번호 (addr+size-1) 의 차 + 1 */
}

/* 두 범위가 같은 g-블록을 하나라도 같이 쓰나 (겹치지 않아도 같은 블록이면 true). 크기 0 → false. */
static bool share_granule(uintptr_t a, size_t sa, uintptr_t b, size_t sb, size_t g) {
    (void)a; (void)sa; (void)b; (void)sb; (void)g;
    return false; /* TODO: 블록 번호 구간 [a0,a1] 과 [b0,b1] 이 겹치나 */
}

/* n 을 g 의 배수로 올림 (g 는 2의 거듭제곱). */
static size_t round_up(size_t n, size_t g) {
    (void)g;
    return n; /* TODO: 비트 마스크로 올림 */
}

/* 스레드 하나가 혼자 쓰는 카운터 칸. 칸 하나가 128 B 라인을 혼자 차지해야 한다. */
typedef struct {
    _Atomic uint32_t v;   /* TODO: 정렬 지정자로 칸 = 1 라인, 그리고 _Static_assert 두 줄 (크기·정렬) */
} slot_t;

/* ======================= Part B — 스윕 기계 ======================= */
enum { MODE_ATOMIC, MODE_VSTORE, MODE_VRMW };

/* slot 이 가리키는 uint32 를 n 번 "올린다". 모드마다 다른 명령이 나와야 한다 (demo_asm.c 참고).
 *   MODE_ATOMIC : relaxed atomic_fetch_add                   → arm64 ldadd
 *   MODE_VSTORE : 지역 변수 local 을 올리고 volatile 로 저장만 → str (읽지 않는다. 시작값 = 지금 칸 값)
 *   MODE_VRMW   : volatile 로 읽고 1 더해 쓰기               → ldr; add; str (원자적이지 않음 — 칸 주인이 하나라 정확)
 * 컴파일러가 루프를 "한 번에 n 더하기" 로 접지 못하게 하는 것이 핵심. */
static void bump(void *slot, int mode, uint32_t n) {
    (void)slot; (void)mode; (void)n;
    /* TODO: 세 모드. 평범한 uint32_t * 로 *p += 1 을 n 번 하면 -O2 가 루프를 지운다 (demo_asm.c inc_plain) */
}

/* ---- 하네스 (주어짐): nthr 스레드가 base + i*stride 의 칸을 각자 iters 번 올린다 ---- */
typedef struct { unsigned char *base; size_t stride; int idx, mode; uint32_t iters; } job_t;

static void *job_main(void *arg) {
    job_t *j = arg;
    bump(j->base + (size_t)j->idx * j->stride, j->mode, j->iters);
    return NULL;
}

static uint32_t read_u32(const unsigned char *p, int mode) {
    if (mode == MODE_ATOMIC) return atomic_load_explicit((_Atomic uint32_t *)(uintptr_t)p, memory_order_relaxed);
    uint32_t v;
    memcpy(&v, p, sizeof v);
    return v;
}

/* 모든 칸 == iters 이고, 칸이 아닌 바이트는 전부 0 (이웃 오염 없음) 이면 true. */
static bool run_threads(unsigned char *base, size_t span, int nthr, size_t stride, int mode, uint32_t iters) {
    pthread_t th[16];
    job_t jobs[16];
    memset(base, 0, span);
    for (int i = 0; i < nthr; i++) {
        jobs[i] = (job_t){ base, stride, i, mode, iters };
        if (pthread_create(&th[i], NULL, job_main, &jobs[i]) != 0) return false;
    }
    for (int i = 0; i < nthr; i++) pthread_join(th[i], NULL);
    for (int i = 0; i < nthr; i++) if (read_u32(base + (size_t)i * stride, mode) != iters) return false;
    for (size_t off = 0; off < span; off++) {
        bool in_slot = off % stride < 4 && off / stride < (size_t)nthr;
        if (!in_slot && base[off] != 0) return false;
    }
    return true;
}

/* 측정표 → false sharing 이 끝나는 거리(= 일관성 단위의 추정치).
 *   strides[] 는 오름차순 2의 거듭제곱, ns[] 는 그 거리의 ns/증가.
 *   기준(base) = stride >= 256 인 항목들의 평균 (그런 항목이 없으면 0 을 돌려준다).
 *   가장 큰 stride 부터 내려오며 ns <= tol*base 인 항목이 "끊기지 않고" 이어지는 가장 작은 stride 를 돌려준다.
 *   (중간에 우연히 빠른 점이 있어도 위에서부터 처음 느려지는 곳에서 멈춘다)
 *   잘못된 입력(n < 1, tol < 1, 오름차순 아님) → 0. */
static size_t estimate_granule(const size_t *strides, const double *ns, int n, double tol) {
    (void)strides; (void)ns; (void)n; (void)tol;
    return 0; /* TODO: 입력 검사 → 기준 평균 → 위에서부터 내려오며 tol 안쪽이 이어지는 동안 */
}

/* ======================= Part C — 구조체 설계 규칙 ======================= */
/* 필드마다 "누가 쓰나" 그룹:  G_COLD = 거의 안 씀(어디 있어도 됨), G_RO = 읽기 위주(초기화 때만 씀),
 * 1 이상 = 그 번호의 쓰는 주체(코어 0 → 1, 코어 1 → 2, DMA 엔진 → 3 ...).
 * 규칙: 같은 g-블록 안에 (1) 서로 다른 쓰기 그룹이 같이 있으면 위반, (2) RO 와 쓰기 그룹이 같이 있으면 위반.
 * 위반 수 = 규칙을 어기며 같은 블록을 쓰는 필드 쌍(i<j)의 수. */
enum { G_COLD = -1, G_RO = 0 };
typedef struct { const char *name; size_t off, size; int group; } field_t;

static int layout_violations(const field_t *f, int n, size_t g) {
    (void)f; (void)n; (void)g;
    return 0; /* TODO: 모든 쌍 (i<j) — COLD 는 건너뛰고, RO-RO·같은 그룹은 괜찮고, 나머지가 같은 블록이면 위반 */
}

/* 듀얼 코어 SoC 의 패킷 큐 제어 블록.
 *   core0 = RX ISR(생산자): head, rx_packets, tail_cache 를 쓴다
 *   core1 = 프로토콜 스택(소비자): tail, rx_handled, head_cache 를 쓴다
 *   ring/mask/irq = 초기화 때만 쓰고 둘 다 읽는다 (RO).  name/err_overrun = 거의 안 씀 (COLD).
 * 목표: layout_violations(.., 128) == 0, 크기는 4 라인(512 B) 이하. */
typedef struct {   /* TODO: 그룹별로 모으고 쓰기 그룹마다 라인 시작 (+ _Static_assert 로 못 박기) */
    _Atomic uint32_t head;            /* core0 */
    _Atomic uint32_t tail;            /* core1 */
    uint8_t *ring;                    /* RO   */
    uint32_t mask;                    /* RO   */
    uint32_t irq;                     /* RO   */
    uint32_t rx_packets;              /* core0 */
    uint32_t rx_handled;              /* core1 */
    uint32_t tail_cache;              /* core0 */
    uint32_t head_cache;              /* core1 */
    char name[16];                    /* COLD */
    uint32_t err_overrun;             /* COLD */
} pq_t;

#define FLD(T, m, grp) { #m, offsetof(T, m), sizeof(((T *)0)->m), grp }
static const field_t pq_fields[] = {
    FLD(pq_t, ring, G_RO), FLD(pq_t, mask, G_RO), FLD(pq_t, irq, G_RO),
    FLD(pq_t, head, 1), FLD(pq_t, rx_packets, 1), FLD(pq_t, tail_cache, 1),
    FLD(pq_t, tail, 2), FLD(pq_t, rx_handled, 2), FLD(pq_t, head_cache, 2),
    FLD(pq_t, name, G_COLD), FLD(pq_t, err_overrun, G_COLD),
};
#define NPQ ((int)(sizeof pq_fields / sizeof pq_fields[0]))

/* 두 "코어" 가 각자 자기 그룹 필드만 iters 번 갱신 (하네스, 주어짐) */
typedef struct { pq_t *q; int core; uint32_t iters; } pqjob_t;
static void *pq_core(void *arg) {
    pqjob_t *j = arg;
    for (uint32_t i = 0; i < j->iters; i++) {
        if (j->core == 0) {
            atomic_fetch_add_explicit(&j->q->head, 1, memory_order_release);
            j->q->rx_packets++;
            j->q->tail_cache = i;
        } else {
            atomic_fetch_add_explicit(&j->q->tail, 1, memory_order_release);
            j->q->rx_handled++;
            j->q->head_cache = i;
        }
    }
    return NULL;
}

/* ======================= 테스트 ======================= */
static void test_part_a(void) {
    printf("-- Part A: layout arithmetic --\n");
    CHECK(granules_touched(0, 1, 128) == 1);
    CHECK(granules_touched(127, 2, 128) == 2);        /* 라인 경계를 넘는 2바이트 */
    CHECK(granules_touched(128, 128, 128) == 1);
    CHECK(granules_touched(120, 24, 64) == 2);
    CHECK(granules_touched(0, 0, 64) == 0);
    CHECK(share_granule(0, 4, 4, 4, 128) == true);    /* 인접 카운터 = 같은 라인 */
    CHECK(share_granule(0, 4, 64, 4, 128) == true);   /* 64 B 떨어져도 128 B 라인 안 */
    CHECK(share_granule(0, 4, 64, 4, 64) == false);
    CHECK(share_granule(0, 4, 128, 4, 128) == false);
    CHECK(share_granule(100, 40, 130, 4, 128) == true); /* 첫 범위가 경계를 넘어 다음 라인에 닿음 */
    CHECK(share_granule(0, 0, 0, 4, 128) == false);
    CHECK(round_up(1, 128) == 128 && round_up(128, 128) == 128 && round_up(129, 128) == 256 && round_up(0, 32) == 0);
    CHECK(sizeof(slot_t) == GRANULE);
    CHECK(alignof(slot_t) == GRANULE);
    static slot_t slots[4];
    bool distinct = true;
    for (int i = 0; i < 4; i++)
        for (int k = i + 1; k < 4; k++)
            if (share_granule((uintptr_t)&slots[i].v, 4, (uintptr_t)&slots[k].v, 4, GRANULE)) distinct = false;
    CHECK(distinct);
    slot_t *heap = aligned_alloc(alignof(slot_t), 8 * sizeof(slot_t));   /* malloc 은 16 B 정렬뿐 */
    CHECK(heap != NULL && (uintptr_t)heap % GRANULE == 0 && (uintptr_t)&heap[7] % GRANULE == 0);
    free(heap);
}

static void test_part_b(void) {
    printf("-- Part B: sweep machinery (exact totals, no neighbour corruption) --\n");
    static const size_t strides[] = { 4, 8, 16, 32, 64, 128, 256, 512 };
    const size_t span = 2 * 4096;             /* aligned_alloc: 크기는 정렬의 배수여야 (6144 면 NULL) */
    unsigned char *base = aligned_alloc(4096, span);
    if (!base) { CHECK(base != NULL); return; }
    const uint32_t iters = 100000 / SAN_SCALE;
    static const char *mn[] = { "atomic", "vstore", "vrmw" };
    for (int m = 0; m < 3; m++) {
        bool ok2 = true, ok4 = true;
        for (int s = 0; s < 8; s++) {
            if (!run_threads(base, span, 2, strides[s], m, iters)) ok2 = false;
            if (!run_threads(base, span, 4, strides[s], m, iters / 2)) ok4 = false;
        }
        printf("      mode %s\n", mn[m]);
        CHECK(ok2);
        CHECK(ok4);
    }
    CHECK(run_threads(base, span, 12, 4, MODE_ATOMIC, iters / 4));   /* 12 칸이 한 라인(48 B) 안 */
    free(base);

    /* estimate_granule — 이 맥에서 잰 표 (demo_bench.c sweep, atomic 열) 를 그대로 넣는다 */
    static const double ui[] = { 10.71, 11.50, 11.55, 11.41, 1.41, 1.37, 1.36, 1.37 };  /* S/P 코어끼리 */
    static const double bg[] = { 17.75, 10.84, 17.95, 16.98, 14.76, 2.27, 2.07, 2.15 }; /* E 코어가 낌 */
    static const double flat[] = { 1.4, 1.4, 1.4, 1.4, 1.4, 1.4, 1.4, 1.4 };
    static const double dip[] = { 12.0, 1.5, 12.0, 12.0, 12.0, 1.4, 1.4, 1.4 };       /* 8 B 우연히 빠름 */
    static const double allslow[] = { 9.0, 9.0, 9.0, 9.0, 9.0, 9.0, 1.4, 9.0 };
    CHECK(estimate_granule(strides, ui, 8, 1.5) == 64);
    CHECK(estimate_granule(strides, bg, 8, 1.5) == 128);
    CHECK(estimate_granule(strides, flat, 8, 1.5) == 4);   /* 절벽 없음: 가장 작은 stride */
    CHECK(estimate_granule(strides, dip, 8, 1.5) == 128);  /* 위에서부터 끊기면 멈춘다 */
    CHECK(estimate_granule(strides, allslow, 8, 1.5) == 0);/* 512 가 기준(5.2)의 1.5배 넘음 → 0 */
    CHECK(estimate_granule(strides, ui, 5, 1.5) == 0);     /* >= 256 항목 없음 */
    static const size_t unsorted[] = { 4, 64, 32, 256 };
    CHECK(estimate_granule(unsorted, flat, 4, 1.5) == 0);
}

static void test_part_c(void) {
    printf("-- Part C: struct layout rules --\n");
    /* 손으로 만든 표로 checker 자체를 검사 */
    static const field_t naive[] = {            /* 흔한 "의미 순서" 배치: head, tail, mask 가 한 라인 */
        { "head", 0, 4, 1 }, { "tail", 4, 4, 2 }, { "mask", 8, 4, G_RO }, { "name", 12, 16, G_COLD },
    };
    CHECK(layout_violations(naive, 4, 128) == 3);   /* head-tail, head-mask, tail-mask */
    CHECK(layout_violations(naive, 4, 4) == 0);     /* 4 B 블록이면 아무도 안 겹침 */
    static const field_t ok[] = {
        { "mask", 0, 4, G_RO }, { "ring", 8, 8, G_RO }, { "name", 16, 16, G_COLD }, { "head", 128, 4, 1 },
        { "rxp", 132, 4, 1 }, { "tail", 256, 4, 2 },
    };
    CHECK(layout_violations(ok, 6, 128) == 0);
    CHECK(layout_violations(ok, 6, 256) == 4);      /* 256 B 블록이면 RO 둘(mask, ring) × 쓰기(head, rxp) = 4 쌍 */
    static const field_t dma32[] = { { "rx_dma", 0, 40, 3 }, { "isr_cnt", 40, 4, 1 } };
    CHECK(layout_violations(dma32, 2, 32) == 1);    /* Cortex-M7: DMA 버퍼 꼬리와 ISR 변수가 32 B 라인 공유 */

    /* 학생이 고친 pq_t */
    int v128 = layout_violations(pq_fields, NPQ, 128);
    printf("      pq_t: sizeof %zu, alignof %zu, head@%zu tail@%zu, violations@128 = %d\n",
           sizeof(pq_t), alignof(pq_t), offsetof(pq_t, head), offsetof(pq_t, tail), v128);
    CHECK(v128 == 0);
    CHECK(layout_violations(pq_fields, NPQ, 64) == 0);    /* 128 에서 맞으면 64 에서도 */
    CHECK(alignof(pq_t) == GRANULE);
    CHECK(sizeof(pq_t) % GRANULE == 0 && sizeof(pq_t) <= 4 * GRANULE);
    CHECK(offsetof(pq_t, head) % GRANULE == 0 && offsetof(pq_t, tail) % GRANULE == 0);

    pq_t *q = aligned_alloc(alignof(pq_t), sizeof(pq_t));
    if (!q) { CHECK(q != NULL); return; }
    memset(q, 0, sizeof *q);
    const uint32_t iters = 200000 / SAN_SCALE;
    pthread_t a, b;
    pqjob_t ja = { q, 0, iters }, jb = { q, 1, iters };
    pthread_create(&a, NULL, pq_core, &ja);
    pthread_create(&b, NULL, pq_core, &jb);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    CHECK(atomic_load(&q->head) == iters && q->rx_packets == iters && q->tail_cache == iters - 1);
    CHECK(atomic_load(&q->tail) == iters && q->rx_handled == iters && q->head_cache == iters - 1);
    free(q);
}

int main(void) {
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
