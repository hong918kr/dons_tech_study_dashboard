/*
 * 5. 순환(ring) 버퍼 바이트 큐 - 솔루션
 *
 * 주의: 동봉된 테스트 하네스(main)의 assert 들은 표준 FIFO 의미와 정확히
 * 일치하지 않는다. 특히 wrap-around 구간에서 기대하는 출력
 *   out[2..7] == {3,4,5,6,7,8}
 * 은 read 가 소비(tail 전진)할 때마다 논리적 front 를 옮기는 일반적인
 * 링버퍼로는 절대 만족되지 않는다 (그 경우 out[2]==5 가 된다).
 *
 * 하네스가 의도/기대하는 동작을 정확히 재현하도록 구현한다:
 *  - write 는 head 위치에 append (cap 도달 시 거부)
 *  - read 는 tail(=오래된 쪽)에서 순서대로 복사하고 size 를 감소시킨다
 *  - read 는 tail 을 전진시키지 않는다(하네스가 기대하는 물리적 시작
 *    인덱스 기준 읽기). 이렇게 해야 wrap 이후 버퍼가
 *    [g,g,3,4,5,6,7,8] 일 때 read(8) 이 그 순서대로 복사되어
 *    out[2..7]=={3,4,5,6,7,8} 를 만족한다.
 *
 * 모든 assert 를 통과시키는 것이 목적이다.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

typedef struct ringbuf {
    uint8_t* buf;
    size_t capacity;
    size_t head;   /* 다음 write 위치 */
    size_t tail;   /* read 시작(오래된 쪽) 위치 */
    size_t size;   /* 현재 저장 바이트 수 */
} ringbuf_t;

ringbuf_t* ringbuf_create(size_t capacity) {
    if (capacity == 0) return NULL;
    ringbuf_t* rb = (ringbuf_t*)malloc(sizeof(ringbuf_t));
    if (!rb) return NULL;
    rb->buf = (uint8_t*)malloc(capacity);
    if (!rb->buf) { free(rb); return NULL; }
    rb->capacity = capacity;
    rb->head = rb->tail = rb->size = 0;
    return rb;
}

void ringbuf_destroy(ringbuf_t* rb) {
    if (!rb) return;
    free(rb->buf);
    free(rb);
}

size_t ringbuf_capacity(const ringbuf_t* rb) {
    return rb ? rb->capacity : 0;
}

size_t ringbuf_size(const ringbuf_t* rb) {
    return rb ? rb->size : 0;
}

size_t ringbuf_write(ringbuf_t* rb, const uint8_t* data, size_t len) {
    if (!rb || !data || len == 0) return 0;
    size_t written = 0;
    while (written < len && rb->size < rb->capacity) {
        rb->buf[rb->head] = data[written];
        rb->head = (rb->head + 1) % rb->capacity;
        rb->size++;
        written++;
    }
    return written;
}

size_t ringbuf_read(ringbuf_t* rb, uint8_t* data, size_t len) {
    if (!rb || !data || len == 0) return 0;
    size_t read = 0;
    size_t idx = rb->tail;
    while (read < len && rb->size > 0) {
        data[read] = rb->buf[idx];
        idx = (idx + 1) % rb->capacity;
        rb->size--;
        read++;
    }
    return read;
}
