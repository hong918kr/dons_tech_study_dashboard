// L3_generic.c  —  SOLUTION (정답 + 해설)
// 레벨 3 — 타입에 무관한 링버퍼 (void* + elem_size, 그리고 매크로 방식)  —  Q1~Q6
// ---------------------------------------------------------------------------
// 빌드/실행:  make sol N=L3_generic
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g solutions/L3_generic.c -o build/L3_generic_sol && ./build/L3_generic_sol
//
// 이 레벨에서 배우는 것:
//   - 레벨 0~2 는 전부 uint8_t 전용이었다. 실무에서는 imu_sample_t, can_frame_t,
//     cmd_msg_t 처럼 **구조체를 담는 큐**가 필요하다. C 에는 템플릿이 없으니
//     방법은 두 가지뿐이다.
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
// 테스트 하네스 (PASS/FAIL)
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
// 같은 코드 한 벌로 담을 수 있다. 대신 컴파일러는 타입을 모르므로
// rbg_push(&int_queue, &some_float) 같은 실수를 잡아 주지 못한다.
//
// cap 은 2의 거듭제곱일 필요가 없다 (레벨 2 의 제약을 여기서는 일부러 푼다).
// 대신 인덱스 전진은 `if (++i == cap) i = 0;` — 나눗셈도 마스킹도 없이 1 비교.
// head/tail 만으로는 empty/full 을 구분할 수 없으므로 count 필드를 다시 들었다.
// (레벨 4 의 SPSC 로 갈 때 이 count 가 다시 문제가 된다 — 그때 free-running 으로 돌아간다)
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
 *   hint: esz * cap 이 size_t 를 넘치지 않는지도 검사해 두면 좋다.
 * ------------------------------------------------------------------------- */
// 왜 메모리를 안에서 malloc 하지 않는가?
//   - 펌웨어에서 동적 할당은 기본 금지다(단편화, 실패 처리, 비결정적 지연).
//     호출자가 .bss 에 잡은 배열을 넘기게 하면 링크 시점에 RAM 사용량이 확정된다.
//   - 그 대가로 "넘겨준 메모리가 esz*cap 바이트 이상인가"와 "정렬이 맞는가"를
//     라이브러리가 검증할 수 없다. 그래서 아래 Q3 의 정렬 주석이 중요해진다.
bool rbg_init(rbg_t *q, void *mem, size_t esz, size_t cap) {
    if (!q || !mem) return false;
    if (esz == 0u || cap == 0u) return false;
    if (esz > (size_t)-1 / cap) return false;   // esz*cap 오버플로 방어
    q->buf   = (uint8_t *)mem;
    q->esz   = esz;
    q->cap   = cap;
    q->head  = 0u;
    q->tail  = 0u;
    q->count = 0u;
    return true;
}

size_t rbg_count(const rbg_t *q) { return q ? q->count : 0u; }
size_t rbg_free (const rbg_t *q) { return q ? q->cap - q->count : 0u; }
bool   rbg_is_empty(const rbg_t *q) { return !q || q->count == 0u; }
bool   rbg_is_full (const rbg_t *q) { return q && q->count == q->cap; }

/* ---------------------------------------------------------------------------
 * Q2.  push / pop  —  memcpy 로 원소를 통째로 복사
 *   KO: push 는 buf + head*esz 에 esz 바이트를 memcpy 하고 head 를 전진시킨다.
 *       pop 은 buf + tail*esz 에서 out 으로 복사하고 tail 전진 + count--.
 *       out 이 NULL 이면 "버리기(pop 후 값 무시)"로 동작해도 된다.
 *   EN: Copy whole elements in and out with memcpy; advance indices modulo cap.
 *   ex: int 4칸 큐 -> push(&x) 4번 성공, 5번째는 false
 *   hint: 인덱스 전진은 `if (++i == cap) i = 0;` (나눗셈 불필요)
 * ------------------------------------------------------------------------- */
// esz 가 컴파일타임 상수가 아니므로 memcpy 는 libc 호출로 남는다(인라인 안 됨).
// 4바이트 int 하나 넣는 데 함수 호출 + 곱셈 + 분기 -> 매크로 방식의 단순 대입보다
// 5~10배 느리다. 이 비용이 아까운 경로(고속 ISR)라면 3-2 의 매크로를 쓴다.
bool rbg_push(rbg_t *q, const void *elem) {
    if (!q || !q->buf || !elem) return false;
    if (q->count == q->cap) return false;                 // full
    memcpy(q->buf + q->head * q->esz, elem, q->esz);
    if (++q->head == q->cap) q->head = 0u;                // 나눗셈 없이 랩
    q->count++;
    return true;
}

bool rbg_pop(rbg_t *q, void *out) {
    if (!q || !q->buf) return false;
    if (q->count == 0u) return false;                     // empty
    if (out) memcpy(out, q->buf + q->tail * q->esz, q->esz);
    if (++q->tail == q->cap) q->tail = 0u;
    q->count--;
    return true;
}

// peek 은 tail 을 전진시키지 않는다 — "다음에 뭐가 오는지 보고 처리 여부를 정하는"
// 디스패처 패턴에서 쓴다 (예: 헤더만 보고 이 메시지를 지금 처리할지 미룰지 결정).
bool rbg_peek(const rbg_t *q, void *out) {
    if (!q || !q->buf || !out) return false;
    if (q->count == 0u) return false;
    memcpy(out, q->buf + q->tail * q->esz, q->esz);
    return true;
}

/* ---------------------------------------------------------------------------
 * Q3.  구조체 원소와 정렬(alignment)
 *   KO: 별도 함수는 없다. Q2 의 push/pop 이 구조체를 **패딩까지 포함해** 바이트
 *       단위로 온전히 복사하는지 검증한다. 스토리지는 반드시 타입이 있는 배열
 *       (static imu_sample_t storage[N]) 로 잡아 정렬을 컴파일러에게 맡긴다.
 *   EN: Verify that whole structs (padding included) survive a push/pop round trip.
 *   ex: {t_us=1000, gx=-1, gy=2, gz=-3} 을 넣었다 빼면 모든 필드가 그대로
 *   hint: memset 으로 패딩까지 눈에 보이는 값으로 채운 뒤 memcmp 로 비교한다.
 * ------------------------------------------------------------------------- */
typedef struct {
    uint32_t t_us;          /* offset 0  */
    int16_t  gx, gy, gz;    /* offset 4, 6, 8  -> 10~11 은 패딩 */
} imu_sample_t;             /* sizeof == 12, _Alignof == 4 (일반적인 ABI 기준) */

// 정렬(alignment) — void* 방식의 진짜 함정:
//   uint8_t raw[64]; rbg_init(&q, raw + 1, sizeof(imu_sample_t), 4);
//   -> raw+1 은 4바이트 정렬이 아니다. memcpy 자체는 바이트 복사라 살아남지만,
//      호출자가 rbg_at() 로 받은 포인터를 imu_sample_t* 로 캐스팅해 필드를 읽는 순간
//      Cortex-M0/M3 + -mno-unaligned-access 환경에서 **HardFault** 가 난다.
//      (M3/M4 는 LDR 은 비정렬을 허용하지만 LDM/STM, VLDR 은 안 된다)
//   -> 그래서 스토리지는 항상 `static imu_sample_t storage[N]` 처럼 타입 있는
//      배열로 잡는다. 굳이 바이트 배열을 써야 한다면 _Alignas(imu_sample_t) 를 붙인다.

/* ---------------------------------------------------------------------------
 * Q4.  rbg_at  —  tail 기준 i 번째 원소의 포인터
 *   KO: 논리적 순서(가장 오래된 것이 0번)로 i 번째 원소의 주소를 리턴한다.
 *       i >= count 면 NULL. 랩어라운드 이후에도 논리 순서가 유지되어야 한다.
 *   EN: Index into the ring by logical position (0 == oldest); NULL if out of range.
 *   ex: count=3 이면 at(0),at(1),at(2) 유효, at(3) == NULL
 *   hint: idx = tail + i; if (idx >= cap) idx -= cap;  (`%` 대신 뺄셈 1회)
 * ------------------------------------------------------------------------- */
// 용도: "최근 N개 샘플의 이동평균", "큐를 비우지 않고 훑어보며 조건 검사" 같은
// 읽기 전용 순회. 복사가 없으니 공짜지만, push/pop 이 일어나면 즉시 무효가 된다.
void *rbg_at(const rbg_t *q, size_t i) {
    if (!q || !q->buf) return NULL;
    if (i >= q->count) return NULL;          // 범위 밖 -> NULL (호출자가 검사하게)
    size_t idx = q->tail + i;
    if (idx >= q->cap) idx -= q->cap;        // tail+i 는 최대 2*cap-2 -> 뺄셈 1회면 충분
    return q->buf + idx * q->esz;
}

/* ---------------------------------------------------------------------------
 * Q5.  rbg_push_n  —  여러 원소를 한 번에 (부분 성공 허용)
 *   KO: src 를 esz 간격의 배열로 보고 최대 n 개를 넣는다. 공간이 모자라면
 *       들어가는 만큼만 넣고 실제로 넣은 개수를 리턴한다. n=0 이면 0.
 *   EN: Bulk push; copies min(n, free) elements and returns that count.
 *   ex: cap 4, 3개 차 있을 때 push_n(src, 3) -> 1 리턴
 *   hint: const void* 는 포인터 연산이 안 된다. const uint8_t* 로 캐스팅할 것.
 * ------------------------------------------------------------------------- */
// 여기서는 rbg_push 를 n 번 부르는 단순 구현을 쓴다. 레벨 2 처럼 memcpy 2회로
// 최적화할 수도 있지만(연속 구간 계산 -> cap-head 개와 나머지), esz 가 런타임
// 값이라 이득이 작고 코드는 눈에 띄게 복잡해진다. 큰 원소를 대량으로 넣는
// 프로파일이 실제로 나왔을 때만 바꾼다.
size_t rbg_push_n(rbg_t *q, const void *src, size_t n) {
    if (!q || !q->buf || !src || n == 0u) return 0u;
    const uint8_t *p = (const uint8_t *)src;   // void* 는 산술 불가 -> 바이트 포인터로
    size_t i = 0;
    while (i < n && rbg_push(q, p + i * q->esz)) i++;
    return i;
}

// ===========================================================================
// 3-2. 매크로 방식 (타입 안전 + 동적 할당 없음)
// ---------------------------------------------------------------------------
// RB_DECLARE(imuq, imu_sample_t, 4) 한 줄이
//     typedef struct {...} imuq_t;  static imuq_t imuq_q;
//     imuq_push(imu_sample_t v) / imuq_pop(imu_sample_t *out) / imuq_count(void)
// 를 통째로 만들어낸다. C 에 템플릿이 없으니 전처리기가 그 역할을 대신한다.
//
// 장점:
//   - 타입 체크가 **컴파일 타임**에 된다. imuq_push(3.14) 는 빌드가 깨진다.
//     void* 방식이라면 그냥 통과해서 런타임에 쓰레기 데이터가 들어간다.
//   - memcpy 대신 **대입(=)**. esz 가 컴파일타임 상수 sizeof(type) 이므로
//     컴파일러가 12바이트 복사를 3개의 STR 명령으로 펼친다. 함수 호출도 없다.
//     -> 인라인되어 void* 방식보다 훨씬 빠르다 (M4 기준 대략 5~10배).
//   - capacity 도 컴파일타임 상수라 `count == 4` 비교가 즉시 상수 비교가 된다.
//   - 정렬은 컴파일러가 알아서 맞춘다 (type buf[cap] 이므로 애초에 틀릴 수 없다).
// 단점:
//   - 인스턴스화할 때마다 코드가 **복제**된다. 큐를 10종류 만들면 push/pop 코드가
//     10벌 -> flash 사용량 증가. (void* 방식은 몇 종류를 쓰든 코드 1벌)
//   - 디버깅이 괴롭다: 전처리 결과가 한 줄로 뭉개져 브레이크포인트/스텝이 안 맞고,
//     에러 메시지가 매크로 전개 위치로 나온다. (`cc -E` 로 펼쳐 보는 습관 필요)
//   - 매크로 위생: 인자를 항상 괄호로 감싸고, 내부 임시 변수 이름이 충돌하지
//     않도록 주의해야 한다.
// ===========================================================================
#define RB_DECLARE(name, type, capacity)                                       \
    typedef struct {                                                           \
        type   buf[(capacity)];   /* 정적 스토리지: 동적 할당 0 */              \
        size_t head, tail, count;                                              \
    } name##_t;                                                                \
    static name##_t name##_q;                                                  \
                                                                               \
    static inline size_t name##_count(void) { return name##_q.count; }         \
                                                                               \
    static inline bool name##_push(type v) {                                   \
        if (name##_q.count == (capacity)) return false;                        \
        name##_q.buf[name##_q.head] = v;      /* memcpy 아니라 구조체 대입 */   \
        if (++name##_q.head == (capacity)) name##_q.head = 0u;                 \
        name##_q.count++;                                                      \
        return true;                                                           \
    }                                                                          \
                                                                               \
    static inline bool name##_pop(type *out) {                                 \
        if (name##_q.count == 0u) return false;                                \
        if (out) *out = name##_q.buf[name##_q.tail];                           \
        if (++name##_q.tail == (capacity)) name##_q.tail = 0u;                 \
        name##_q.count--;                                                      \
        return true;                                                           \
    }

/* ---------------------------------------------------------------------------
 * Q6.  RB_DECLARE 로 만든 타입 안전 큐
 *   KO: imu_sample_t 를 4개 담는 큐를 매크로 한 줄로 선언하고, push/pop/count 가
 *       동작하는지 확인한다. sizeof 로 정적 스토리지 크기도 검증한다.
 *   EN: Instantiate a type-safe queue via the macro and check it behaves.
 *   ex: RB_DECLARE(imuq, imu_sample_t, 4) -> imuq_push(s), imuq_pop(&r)
 *   hint: 매크로 인자는 항상 괄호로. `##` 는 토큰 붙이기(imuq + _push).
 * ------------------------------------------------------------------------- */
RB_DECLARE(imuq, imu_sample_t, 4)

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
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

// ===========================================================================
// 정리 — 레벨 3 의 트레이드오프 (면접에서 한 문장씩 말할 수 있어야 한다)
// ---------------------------------------------------------------------------
// 1) void* 방식의 대가
//    - 원소마다 `memcpy` **함수 호출** + `head * esz` **곱셈**이 붙는다.
//      esz 가 런타임 값이라 컴파일러가 memcpy 를 인라인 펼치지 못한다
//      (sizeof 가 상수면 `memcpy(d,s,4)` 는 LDR/STR 한 쌍으로 펼쳐진다).
//    - 4바이트 원소에서는 이 오버헤드가 데이터 자체보다 크다. 큰 구조체
//      (32바이트 이상)로 갈수록 상대적 손해는 줄어든다.
//    - 타입 안전성이 전혀 없다: rbg_push(&int_q, &a_float) 가 조용히 컴파일된다.
//      그래서 실무에서는 얇은 래퍼(`static inline bool imu_push(const imu_sample_t*)`)
//      를 씌워 타입을 되살리는 절충을 자주 쓴다.
//
// 2) 정렬(alignment)
//    - 사용자가 넘긴 mem 이 원소 타입의 정렬 요구(_Alignof(imu_sample_t))를
//      만족해야 한다. 라이브러리는 이를 검사할 방법이 사실상 없다.
//    - 그래서 예제처럼 **타입이 있는 배열**로 스토리지를 잡는 것이 안전하다:
//          static imu_sample_t storage[N];
//      바이트 배열을 써야 한다면 `_Alignas(imu_sample_t) static uint8_t s[N*sz];`.
//    - 어긋난 정렬 + `-mno-unaligned-access` (Cortex-M0/M0+ 는 선택지도 없음)
//      = 첫 필드 접근에서 **HardFault**. 필드 오프셋이 우연히 맞는 동안은
//      멀쩡히 돌다가 구조체를 한 번 고치면 터지는, 최악의 재현 난이도 버그다.
//
// 3) 실무 선택 기준
//    - **바이트 스트림(UART RX/TX, 로그)** -> 레벨 2.
//      원소가 uint8_t 라 esz 추상화가 순수 낭비이고, 벌크 memcpy 2회가 핵심이다.
//    - **고정 크기 메시지 큐(IMU 샘플, CAN 프레임, 커맨드)** -> 레벨 3 의 void* 방식.
//      큐 종류가 여럿이라 코드 1벌로 재사용하는 이득이 크고, 메시지 하나당
//      수십~수백 사이클의 처리 비용에 비하면 memcpy 오버헤드는 묻힌다.
//    - **성능이 극한(고속 ISR, 샘플당 수 사이클)** -> 매크로 방식.
//      대입 한 번으로 끝나고 전부 인라인된다. 대신 flash 를 코드 복제로 지불한다.
//    - 일단은 void* 로 시작해서, 프로파일러가 그 큐를 지목했을 때 매크로로
//      바꾸는 것이 옳은 순서다. 처음부터 매크로로 도배하면 flash 와
//      디버깅 가능성을 미리 잃는다.
// ===========================================================================
