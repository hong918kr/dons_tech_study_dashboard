/* Task 1 (2026-10-08 screen, 재구성): 외부 버퍼를 받는 byte ring buffer. API 는 Don 이 기억한 시그니처 그대로. */
#ifndef RB_H
#define RB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// --- struct ---
typedef struct {
    uint8_t *storage;      /* 호출자가 준 버퍼 (정적 배열 등) — rb 는 소유하지 않는다 */
    size_t   capacity;
    size_t   head;         /* 다음에 쓸 칸 */
    size_t   tail;         /* 다음에 읽을 칸 */
    size_t   count;        /* 들어 있는 바이트 수 → full/empty 구분 */
} rb_t;

void rb_init(rb_t *rb, uint8_t *rb_buf, uint8_t capacity);
bool rb_is_empty(const rb_t *rb);
bool rb_is_full(const rb_t *rb);
bool rb_push_byte(rb_t *rb, uint8_t byte);
bool rb_pop_byte(rb_t *rb, uint8_t *out);
bool rb_push(rb_t *rb, const uint8_t *items, size_t len);   /* 전부 들어갈 자리 없으면 false, 아무것도 안 넣음 */
bool rb_pop(rb_t *rb, uint8_t *items, size_t len);          /* len 개 미만이면 false, 아무것도 안 뺌 */
bool rb_peek(const rb_t *rb, uint8_t offset, uint8_t *out);  /* tail 에서 offset 번째를 꺼내지 않고 보기 */
// --- end ---
#endif
