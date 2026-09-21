// L3_generic.c  —  PRACTICE STUB (직접 채워넣기)
// 레벨 3 — 타입에 무관한 링버퍼 (void* + elem_size, 그리고 매크로 방식)  —  Q1~Q6
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=L3_generic
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g problems/L3_generic.c -o build/L3_generic_prob && ./build/L3_generic_prob
//
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다.)
//
// 이 레벨에서 배우는 것:
//   - 레벨 0~2 는 전부 uint8_t 전용이었다. 실무에서는 imu_sample_t, can_frame_t
//     같은 **구조체를 담는 큐**가 필요하다. C 에는 템플릿이 없으니 방법은 둘뿐:
//       (A) 런타임 방식: void* + elem_size + memcpy  -> 코드 1벌, 타입 체크 없음
//       (B) 매크로 방식: RB_DECLARE(name, type, cap) -> 타입 안전, 코드 N벌
//   - 둘의 trade-off (flash vs. 타입안전성 vs. 속도)를 숫자로 설명할 수 있어야 한다.
//   - **정렬(alignment)**: void* 방식은 사용자가 넘긴 메모리의 정렬을 컴파일러가
//     검사해 줄 수 없다. 여기서 ARM HardFault 가 난다.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// ===========================================================================
// 테스트 하네스 (건드리지 말 것)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// ===========================================================================
// 3-1. 런타임 방식 (void* + esz + memcpy)
// ---------------------------------------------------------------------------
// 원소를 "esz 바이트짜리 불투명한 덩어리"로만 취급한다. 그래서 어떤 타입이든
// 같은 코드 한 벌로 담을 수 있다. 대신 컴파일러가 타입을 모르므로
// rbg_push(&int_queue, &some_float) 같은 실수를 잡아 주지 못한다.
//
// cap 은 2의 거듭제곱일 필요가 없다 (레벨 2 의 제약을 여기서는 일부러 푼다).
// 대신 인덱스 전진은 `if (++i == cap) i = 0;` — 나눗셈도 마스킹도 없이 1 비교.
// head/tail 만으로는 empty/full 을 구분할 수 없으므로 count 필드를 다시 들었다.
// ===========================================================================
typedef struct {
    uint8_t *buf;     /* cap * esz 바이트 */
    size_t   esz;     /* 원소 하나의 크기 */
    size_t   cap;     /* 원소 개수 (2의 거듭제곱 아니어도 됨) */
    size_t   head;
    size_t   tail;
    size_t   count;
} rbg_t;

/* ---------------------------------------------------------------------------
 * Q1.  rbg_init / count / free / is_empty / is_full
 *   KO: mem 이 NULL, esz 가 0, cap 이 0 이면 false 를 리턴하고 아무것도 건드리지
 *       않는다. 성공 시 head/tail/count 는 0. count 는 "원소 개수"이지 바이트가 아니다.
 *   EN: Initialize a generic ring over caller-supplied memory; reject esz==0/cap==0.
 *   ex: rbg_init(&q, storage, sizeof(int), 4) -> count 0, free 4
 *   hint: esz * cap 이 size_t 를 넘치지 않는지도 검사해 두면 좋다
 *         (`if (esz > (size_t)-1 / cap) return false;`).
 * ------------------------------------------------------------------------- */
bool rbg_init(rbg_t *q, void *mem, size_t esz, size_t cap) {
    (void)q; (void)mem; (void)esz; (void)cap;
    // TODO: implement (NULL/0 거부, 곱셈 오버플로 검사, 필드 세팅)
    return false;  // placeholder
}

size_t rbg_count(const rbg_t *q) {
    (void)q;
    // TODO: implement
    return 0;  // placeholder
}

size_t rbg_free(const rbg_t *q) {
    (void)q;
    // TODO: implement (cap - count)
    return 0;  // placeholder
}

bool rbg_is_empty(const rbg_t *q) {
    (void)q;
    // TODO: implement
    return false;  // placeholder
}

bool rbg_is_full(const rbg_t *q) {
    (void)q;
    // TODO: implement
    return false;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q2.  push / pop / peek  —  memcpy 로 원소를 통째로 복사
 *   KO: push 는 buf + head*esz 에 esz 바이트를 memcpy 하고 head 를 전진시킨다.
 *       pop 은 buf + tail*esz 에서 out 으로 복사하고 tail 전진 + count--.
 *       out 이 NULL 이면 "버리기(pop 후 값 무시)"로 동작해도 된다.
 *       peek 은 복사만 하고 tail 을 전진시키지 않는다.
 *   EN: Copy whole elements in and out with memcpy; advance indices modulo cap.
 *   ex: int 4칸 큐 -> push(&x) 4번 성공, 5번째는 false
 *   hint: 인덱스 전진은 `if (++i == cap) i = 0;` (나눗셈 불필요)
 *         실패 경로에서는 out 을 건드리지 말 것.
 * ------------------------------------------------------------------------- */
bool rbg_push(rbg_t *q, const void *elem) {
    (void)q; (void)elem;
    // TODO: implement (full 이면 false, memcpy(buf + head*esz, elem, esz), head 전진, count++)
    return false;  // placeholder
}

bool rbg_pop(rbg_t *q, void *out) {
    (void)q; (void)out;
    // TODO: implement (empty 면 false, memcpy(out, buf + tail*esz, esz), tail 전진, count--)
    return false;  // placeholder
}

bool rbg_peek(const rbg_t *q, void *out) {
    (void)q; (void)out;
    // TODO: implement (tail 을 전진시키지 않는 것 말고는 pop 과 동일)
    return false;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q3.  구조체 원소와 정렬(alignment)
 *   KO: 별도로 구현할 함수는 없다. Q2 의 push/pop 이 구조체를 **패딩까지 포함해**
 *       바이트 단위로 온전히 복사하는지 검증한다. 스토리지는 반드시 타입이 있는
 *       배열(static imu_sample_t storage[N])로 잡아 정렬을 컴파일러에 맡긴다.
 *   EN: Verify that whole structs (padding included) survive a push/pop round trip.
 *   ex: {t_us=1000, gx=-1, gy=2, gz=-3} 을 넣었다 빼면 모든 필드가 그대로
 *   hint: 테스트는 memset 으로 패딩까지 채운 뒤 memcmp 로 비교한다.
 *         esz 를 sizeof(imu_sample_t) 로 주면 패딩도 자동으로 따라온다.
 * ------------------------------------------------------------------------- */
typedef struct {
    uint32_t t_us;          /* offset 0  */
    int16_t  gx, gy, gz;    /* offset 4, 6, 8  -> 10~11 은 패딩 */
} imu_sample_t;             /* sizeof == 12, _Alignof == 4 (일반적인 ABI 기준) */

// 정렬(alignment) — void* 방식의 진짜 함정:
//   uint8_t raw[64]; rbg_init(&q, raw + 1, sizeof(imu_sample_t), 4);
//   -> raw+1 은 4바이트 정렬이 아니다. memcpy 자체는 살아남지만, 호출자가
//      rbg_at() 로 받은 포인터를 imu_sample_t* 로 캐스팅해 읽는 순간
//      Cortex-M0/M3 + -mno-unaligned-access 에서 **HardFault**.
//   -> 스토리지는 항상 `static imu_sample_t storage[N]` 처럼 타입 있는 배열로.

/* ---------------------------------------------------------------------------
 * Q4.  rbg_at  —  tail 기준 i 번째 원소의 포인터
 *   KO: 논리적 순서(가장 오래된 것이 0번)로 i 번째 원소의 주소를 리턴한다.
 *       i >= count 면 NULL. 랩어라운드 이후에도 논리 순서가 유지되어야 한다.
 *   EN: Index into the ring by logical position (0 == oldest); NULL if out of range.
 *   ex: count=3 이면 at(0),at(1),at(2) 유효, at(3) == NULL
 *   hint: idx = tail + i; if (idx >= cap) idx -= cap;  (`%` 대신 뺄셈 1회면 충분)
 * ------------------------------------------------------------------------- */
void *rbg_at(const rbg_t *q, size_t i) {
    (void)q; (void)i;
    // TODO: implement (범위 밖이면 NULL, 아니면 buf + ((tail+i) 랩) * esz)
    return NULL;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q5.  rbg_push_n  —  여러 원소를 한 번에 (부분 성공 허용)
 *   KO: src 를 esz 간격의 배열로 보고 최대 n 개를 넣는다. 공간이 모자라면
 *       들어가는 만큼만 넣고 실제로 넣은 개수를 리턴한다. n=0 이면 0.
 *   EN: Bulk push; copies min(n, free) elements and returns that count.
 *   ex: cap 4, 3개 차 있을 때 push_n(src, 3) -> 1 리턴
 *   hint: const void* 는 포인터 연산이 안 된다. const uint8_t* 로 캐스팅한 뒤
 *         p + i*esz 를 rbg_push 에 넘기면 된다.
 * ------------------------------------------------------------------------- */
size_t rbg_push_n(rbg_t *q, const void *src, size_t n) {
    (void)q; (void)src; (void)n;
    // TODO: implement (rbg_push 가 실패할 때까지 최대 n 번 반복, 넣은 개수 리턴)
    return 0;  // placeholder
}

// ===========================================================================
// 3-2. 매크로 방식 (타입 안전 + 동적 할당 없음)
// ---------------------------------------------------------------------------
// RB_DECLARE(imuq, imu_sample_t, 4) 한 줄이
//     typedef struct {...} imuq_t;  static imuq_t imuq_q;
//     imuq_push(imu_sample_t v) / imuq_pop(imu_sample_t *out) / imuq_count(void)
// 를 통째로 만들어낸다. C 에 템플릿이 없으니 전처리기가 그 역할을 대신한다.
//
// 장점: 타입 체크가 컴파일 타임 (imuq_push(3.14) 는 빌드가 깨진다),
//       memcpy 대신 대입(=) 이라 인라인되어 훨씬 빠르다, 정렬 걱정이 없다.
// 단점: 인스턴스마다 코드가 복제된다(= flash 사용량), 전처리 결과가 한 줄로
//       뭉개져 브레이크포인트/에러 메시지가 안 맞는다(= 디버깅이 괴롭다).
//
// 아래 골격에서 구조체와 static 인스턴스는 주어져 있다. `##` 로 이름을 붙이는
// 부분과 세 함수의 본문을 직접 채울 것. (백슬래시 줄바꿈 정렬에 주의)
// ===========================================================================
#define RB_DECLARE(name, type, capacity)                                       \
    typedef struct {                                                           \
        type   buf[(capacity)];   /* 정적 스토리지: 동적 할당 0 */              \
        size_t head, tail, count;                                              \
    } name##_t;                                                                \
    static name##_t name##_q;                                                  \
                                                                               \
    static inline size_t name##_count(void) {                                  \
        /* TODO: name##_q.count 를 리턴 */                                     \
        return 0u;                                                             \
    }                                                                          \
                                                                               \
    static inline bool name##_push(type v) {                                   \
        (void)v; (void)name##_q;   /* 미구현 상태의 경고 방지 — 구현하면 지울 것 */ \
        /* TODO: 가득 찼으면 false, 아니면 buf[head] = v; head 전진; count++ */ \
        return false;                                                          \
    }                                                                          \
                                                                               \
    static inline bool name##_pop(type *out) {                                 \
        (void)out;                                                             \
        /* TODO: 비었으면 false, 아니면 *out = buf[tail]; tail 전진; count-- */ \
        return false;                                                          \
    }

/* ---------------------------------------------------------------------------
 * Q6.  RB_DECLARE 로 만든 타입 안전 큐
 *   KO: imu_sample_t 를 4개 담는 큐를 매크로 한 줄로 선언하고, push/pop/count 가
 *       동작하는지 확인한다. sizeof 로 정적 스토리지 크기도 검증한다.
 *   EN: Instantiate a type-safe queue via the macro and check it behaves.
 *   ex: RB_DECLARE(imuq, imu_sample_t, 4) -> imuq_push(s), imuq_pop(&r)
 *   hint: 매크로 인자는 항상 괄호로. `##` 는 토큰 붙이기(imuq + _push).
 *         `cc -E problems/L3_generic.c | sed -n '/imuq_t/,+30p'` 로 전개 결과를 볼 수 있다.
 * ------------------------------------------------------------------------- */
RB_DECLARE(imuq, imu_sample_t, 4)

// ===========================================================================
// main : 모든 케이스 PASS/FAIL (건드리지 말 것)
// ===========================================================================
int main(void) {
    // -------- Q1 : init 거부 + count/free --------
    printf("\n[Q1] rbg_init 검증 / count / free\n");
    static int istore[4];
    rbg_t iq = {0};

    bool init_ok = (rbg_init(&iq, istore, 0, 4) == false)              // esz=0 거부
                && (rbg_init(&iq, istore, sizeof(int), 0) == false)    // cap=0 거부
                && (rbg_init(&iq, NULL, sizeof(int), 4) == false)      // mem NULL 거부
                && (rbg_init(&iq, istore, sizeof(int), 4) == true);
    T("Q1 init: esz=0 / cap=0 / mem NULL 거부, 정상 인자 수용", init_ok);
    T("Q1 초기 상태: count 0, free 4, is_empty",
      rbg_count(&iq) == 0u && rbg_free(&iq) == 4u && rbg_is_empty(&iq));
    T("Q1 빈 큐는 is_full 이 아니다", !rbg_is_full(&iq));

    // -------- Q2 : int 로 push/pop --------
    printf("\n[Q2] int 원소 push/pop\n");
    bool p_ok = true;
    for (int i = 0; i < 4; ++i) { int x = 100 + i; p_ok &= rbg_push(&iq, &x); }
    T("Q2 cap 만큼(4개) push 성공 + is_full", p_ok && rbg_is_full(&iq));

    int y = 999;
    T("Q2 가득 찬 뒤 push 는 실패 (덮어쓰기 안 함)", !rbg_push(&iq, &y));

    int got = 0;
    bool peek_ok = rbg_peek(&iq, &got) && got == 100 && rbg_count(&iq) == 4u;
    T("Q2 peek 는 값만 보고 꺼내지 않는다", peek_ok);

    bool fifo_ok = true;
    for (int i = 0; i < 4; ++i)
        if (!rbg_pop(&iq, &got) || got != 100 + i) fifo_ok = false;
    T("Q2 pop 은 FIFO 순서", fifo_ok);
    T("Q2 빈 큐 pop/peek 는 false", !rbg_pop(&iq, &got) && !rbg_peek(&iq, &got));

    // -------- Q3 : 구조체 원소 (패딩까지 온전히) --------
    printf("\n[Q3] 구조체 원소 — sizeof(imu_sample_t)=%zu (패딩 포함)\n",
           sizeof(imu_sample_t));
    static imu_sample_t sstore[3];   // 타입 있는 배열 -> 정렬은 컴파일러가 보장
    rbg_t sq = {0};
    rbg_init(&sq, sstore, sizeof(imu_sample_t), 3);

    imu_sample_t s;
    memset(&s, 0xCD, sizeof s);      // 패딩 바이트까지 눈에 보이는 값으로 채운다
    s.t_us = 1000u; s.gx = -1; s.gy = 2; s.gz = -3;

    imu_sample_t r;
    memset(&r, 0x00, sizeof r);
    bool st_ok = rbg_push(&sq, &s) && (rbg_count(&sq) == 1u);
    st_ok &= rbg_pop(&sq, &r);
    T("Q3 구조체 push/pop 자체가 성공", st_ok);
    T("Q3 필드별 검증: t_us / gx / gy / gz 전부 일치",
      r.t_us == 1000u && r.gx == -1 && r.gy == 2 && r.gz == -3);
    T("Q3 패딩 바이트까지 포함해 전체가 비트 단위로 동일 (memcmp)",
      memcmp(&r, &s, sizeof(imu_sample_t)) == 0);

    bool multi_ok = true;
    for (uint32_t i = 0; i < 3; ++i) {
        imu_sample_t e = {.t_us = i * 10u, .gx = (int16_t)i,
                          .gy = (int16_t)(-(int)i), .gz = 7};
        multi_ok &= rbg_push(&sq, &e);
    }
    multi_ok &= !rbg_push(&sq, &s);        // cap 3 초과
    for (uint32_t i = 0; i < 3; ++i) {
        imu_sample_t e;
        if (!rbg_pop(&sq, &e) || e.t_us != i * 10u || e.gx != (int16_t)i
            || e.gz != 7) multi_ok = false;
    }
    T("Q3 구조체 3개 왕복 + 가득 참 거부", multi_ok);

    // -------- Q4 : rbg_at 인덱싱 + 랩어라운드 --------
    printf("\n[Q4] rbg_at — 논리적 인덱싱, 랩어라운드 후에도 순서 유지\n");
    rbg_init(&iq, istore, sizeof(int), 4);
    for (int i = 0; i < 4; ++i) { int x = 10 + i; rbg_push(&iq, &x); }

    const int *a0 = (const int *)rbg_at(&iq, 0);
    const int *a3 = (const int *)rbg_at(&iq, 3);
    T("Q4 at(0)=가장 오래된 값, at(3)=가장 최근 값",
      a0 && a3 && *a0 == 10 && *a3 == 13);
    T("Q4 범위 밖(at(4)) 은 NULL", rbg_at(&iq, 4) == NULL);
    T("Q4 빈 큐의 at(0) 도 NULL", rbg_at(&sq, 0) == NULL);

    // 2개 빼고 3개 더 -> tail=2, head=3 으로 물리적 랩 발생
    rbg_pop(&iq, &got); rbg_pop(&iq, &got);
    for (int i = 0; i < 3; ++i) { int x = 20 + i; rbg_push(&iq, &x); }
    int exp4[4] = {12, 13, 20, 21};
    bool at_ok = (rbg_count(&iq) == 4u);
    for (size_t i = 0; i < 4; ++i) {
        const int *pv = (const int *)rbg_at(&iq, i);
        if (!pv || *pv != exp4[i]) at_ok = false;
    }
    T("Q4 랩 이후에도 at(i) 는 논리적 순서(오래된 것부터)", at_ok);

    // -------- Q5 : rbg_push_n 부분 성공 --------
    printf("\n[Q5] rbg_push_n — 부분 성공\n");
    rbg_init(&iq, istore, sizeof(int), 4);
    const int src5[5] = {1, 2, 3, 4, 5};
    T("Q5 빈 큐(cap 4)에 5개 요청 -> 4개만 들어간다",
      rbg_push_n(&iq, src5, 5) == 4u && rbg_is_full(&iq));

    bool pn_ok = true;
    for (int i = 0; i < 4; ++i)
        if (!rbg_pop(&iq, &got) || got != i + 1) pn_ok = false;
    T("Q5 push_n 으로 넣은 순서가 그대로 보존", pn_ok);

    rbg_init(&iq, istore, sizeof(int), 4);
    rbg_push_n(&iq, src5, 3);                       // 3개 채움
    T("Q5 3/4 찬 상태에서 3개 요청 -> 1개만",
      rbg_push_n(&iq, src5, 3) == 1u && rbg_count(&iq) == 4u);
    T("Q5 가득 찬 뒤엔 0, n=0 이면 0",
      rbg_push_n(&iq, src5, 2) == 0u && rbg_push_n(&iq, src5, 0) == 0u);

    // -------- Q6 : 매크로 방식 --------
    printf("\n[Q6] RB_DECLARE 매크로 — 타입 안전 큐 (imuq)\n");
    bool mq_ok = (imuq_count() == 0u);
    for (uint32_t i = 0; i < 4; ++i) {
        imu_sample_t e = {.t_us = 500u + i, .gx = (int16_t)(i + 1),
                          .gy = 0, .gz = -9};
        mq_ok &= imuq_push(e);                      // 값 전달 (포인터 아님)
    }
    T("Q6 매크로 큐: 4개 push 성공 + count 4", mq_ok && imuq_count() == 4u);

    imu_sample_t dummy = {.t_us = 0, .gx = 0, .gy = 0, .gz = 0};
    T("Q6 매크로 큐: 가득 차면 push 실패", !imuq_push(dummy));

    bool mpop_ok = true;
    for (uint32_t i = 0; i < 4; ++i) {
        imu_sample_t e;
        if (!imuq_pop(&e) || e.t_us != 500u + i || e.gx != (int16_t)(i + 1)
            || e.gz != -9) mpop_ok = false;
    }
    T("Q6 매크로 큐: FIFO 순서 + 필드 보존", mpop_ok);
    T("Q6 매크로 큐: 비면 pop 실패, count 0",
      !imuq_pop(&dummy) && imuq_count() == 0u);

    T("Q6 정적 스토리지: buf 는 정확히 cap*sizeof(type)",
      sizeof imuq_q.buf == 4u * sizeof(imu_sample_t));
    T("Q6 큐 전체 = 원소 배열 + 인덱스 3개 (힙 사용 0)",
      sizeof(imuq_t) >= 4u * sizeof(imu_sample_t) + 3u * sizeof(size_t));
    printf("      (sizeof(imuq_t) = %zu bytes, 전부 .bss 에 정적 할당)\n",
           sizeof(imuq_t));

    // -------- 결과 --------
    printf("\n== 레벨 3 — 타입에 무관한 링버퍼 ==  PASS %d / FAIL %d\n",
           g_pass, g_fail);
    return g_fail ? 1 : 0;
}
