/* ============================================================
 * 링버퍼 (Ring Buffer) — 잘못된 버전 vs 모범 버전
 *
 * 잘못된 점:
 * 1. count 필드 사용 → ISR/task 경쟁 조건 (각각 count++ / count--)
 * 2. head/tail이 uint8_t → size > 256이면 오버플로우
 * 3. % size 연산 → ARM에서 20~40 사이클 (느림)
 * 4. NULL 체크 미흡
 * ============================================================ */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ================================================================
 * ❌ 잘못된 구현 (원본)
 * ================================================================ */

typedef struct {
    uint8_t* buf;
    uint8_t head;           /* ❌ 문제: uint8_t (최대 256) */
    uint8_t tail;
    uint32_t size;
    uint32_t count;         /* ❌ 문제: ISR/task 경쟁 조건! */
} rb_wrong_t;

void rb_wrong_init(rb_wrong_t* rb, uint8_t* buf_, uint32_t size_input)
{
    rb->buf = buf_;
    rb->head = 0;
    rb->tail = 0;
    rb->size = size_input;
    rb->count = 0;  /* ❌ count 갱신의 경쟁 조건 원인 */
}

bool rb_wrong_is_empty(rb_wrong_t* rb)
{
    return rb->count == 0;  /* ❌ count 읽기 (ISR이 동시에 쓸 수 있음) */
}

bool rb_wrong_is_full(rb_wrong_t* rb)
{
    return rb->count == rb->size;  /* ❌ count 읽기 */
}

bool rb_wrong_push(rb_wrong_t* rb, uint8_t data)
{
    if (rb == NULL) return false;
    if (rb_wrong_is_full(rb)) return false;
    rb->buf[rb->head] = data;
    rb->head = (rb->head + 1) % rb->size;  /* ❌ % 연산 (느림) */
    rb->count++;  /* ❌ race condition: ISR이 count-- 할 수도 */
    return true;
}

bool rb_wrong_pop(rb_wrong_t* rb, uint8_t* out)
{
    if (rb == NULL) return false;
    if (rb_wrong_is_empty(rb)) return false;
    *out = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % rb->size;  /* ❌ % 연산 */
    rb->count--;  /* ❌ race condition: ISR이 count++ 할 수도 */
    return true;
}

/* ================================================================
 * ✅ 모범 구현 (Level 2)
 * ================================================================ */

typedef struct {
    uint8_t* buf;
    uint32_t head;          /* ✅ uint32_t (free-running) */
    uint32_t tail;          /* ✅ uint32_t (producer/consumer 각각만 수정) */
    uint32_t size;
    /* count 필드 제거! → head - tail로 계산 */
} rb_t;

void rb_init(rb_t* rb, uint8_t* buf_, uint32_t size_input)
{
    if (rb == NULL || buf_ == NULL) return;  /* ✅ NULL 체크 강화 */

    /* ✅ size가 2의 거듭제곱인지 확인 */
    if ((size_input & (size_input - 1)) != 0) return;

    rb->buf = buf_;
    rb->head = 0;
    rb->tail = 0;
    rb->size = size_input;
}

/* ✅ 개수 계산: head - tail (부호 없는 뺄셈이라 wrap도 정확) */
static inline uint32_t rb_count(rb_t* rb)
{
    return rb->head - rb->tail;
}

bool rb_is_empty(rb_t* rb)
{
    return rb->head == rb->tail;  /* ✅ count 대신 head/tail 비교 */
}

bool rb_is_full(rb_t* rb)
{
    /* ✅ head - tail == size이면 full (한 칸 비우는 방식 안 씀) */
    return rb_count(rb) == rb->size;
}

bool rb_push(rb_t* rb, uint8_t data)
{
    if (rb == NULL) return false;
    if (rb_is_full(rb)) return false;

    /* ✅ 비트마스킹: % size → & (size-1) */
    rb->buf[rb->head & (rb->size - 1)] = data;

    /* ✅ producer만 head 수정 (경쟁 조건 없음) */
    rb->head++;
    return true;
}

bool rb_pop(rb_t* rb, uint8_t* out)
{
    if (rb == NULL) return false;
    if (rb_is_empty(rb)) return false;
    if (out == NULL) return false;  /* ✅ out도 NULL 체크 */

    /* ✅ 비트마스킹: % size → & (size-1) */
    *out = rb->buf[rb->tail & (rb->size - 1)];

    /* ✅ consumer만 tail 수정 (경쟁 조건 없음) */
    rb->tail++;
    return true;
}

/* ================================================================
 * 비교: 원본 vs 모범
 * ================================================================
 *
 * 1. count 필드
 *    원본: ❌ ISR/task가 동시에 count++ / count-- → 누락/중복
 *    모범: ✅ head - tail로 계산 → 각각 한쪽만 수정
 *
 * 2. head/tail 타입
 *    원본: ❌ uint8_t (최대 255) → size > 256 오버플로우
 *    모범: ✅ uint32_t free-running → 0 ~ 2^32-1 자동 wrap
 *
 * 3. 나눗셈 연산
 *    원본: ❌ % size (20~40 사이클)
 *    모범: ✅ & (size-1) (1 사이클)
 *
 * 4. NULL 체크
 *    원본: ❌ buf_ NULL 체크 없음
 *    모범: ✅ buf, rb, out 모두 체크
 * ================================================================ */