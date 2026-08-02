/*
5. 순환(ring) 버퍼 바이트 큐 - 실제 인터뷰 스타일 상세 설명

상황 설명:
당신은 자동차 ECU 또는 IoT 센서 노드의 UART 통신 드라이버를 개발 중입니다.  
센서 데이터, 진단 메시지, 로그 등 다양한 바이트 스트림을 실시간으로 송수신해야 하며,  
하드웨어 인터럽트(생산자)와 메인 루프(소비자)가 동시에 데이터를 처리합니다.

이때, 생산자와 소비자가 동시에 접근해도 안전하고,  
메모리 사용량이 고정되어 예측 가능한 "순환(ring) 버퍼"가 널리 사용됩니다.  
ring buffer는 FIFO(선입선출) 큐로,  
버퍼가 가득 차면 더 이상 쓰기를 거부하거나,  
버퍼가 비면 더 이상 읽기를 거부해야 합니다.

면접관이 평가하고자 하는 포인트:
- 인덱스 wrap-around(순환) 처리의 정확성
- 경계 조건(가득/비어있음) 처리
- 메모리 안전성, API 설계, 테스트 케이스 작성 능력

문제:
아래 시그니처에 따라 바이트 단위 ring buffer를 구현하세요.
- ringbuf_create: capacity 크기의 버퍼 생성
- ringbuf_destroy: 버퍼 해제
- ringbuf_write: 최대 len 바이트 쓰기, 실제 쓴 바이트 수 반환(가득 차면 일부만)
- ringbuf_read: 최대 len 바이트 읽기, 실제 읽은 바이트 수 반환(비어있으면 일부만)
- ringbuf_size: 현재 저장된 바이트 수
- ringbuf_capacity: 전체 용량

아래는 구현 예시와 테스트 코드입니다.
*/

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

typedef struct ringbuf {
    uint8_t* buf;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t size;
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
    while (read < len && rb->size > 0) {
        data[read] = rb->buf[rb->tail];
        rb->tail = (rb->tail + 1) % rb->capacity;
        rb->size--;
        read++;
    }
    return read;
}

// 테스트 코드
int main() {
    ringbuf_t* rb = ringbuf_create(8);
    assert(rb);
    assert(ringbuf_capacity(rb) == 8);
    assert(ringbuf_size(rb) == 0);

    // 1. 기본 쓰기/읽기
    uint8_t in[8] = {1,2,3,4,5,6,7,8};
    uint8_t out[8] = {0};
    size_t n = ringbuf_write(rb, in, 4);
    assert(n == 4 && ringbuf_size(rb) == 4);

    n = ringbuf_read(rb, out, 2);
    assert(n == 2 && out[0] == 1 && out[1] == 2 && ringbuf_size(rb) == 2);

    // 2. wrap-around 테스트
    n = ringbuf_write(rb, in+4, 6); // 6개 쓰기(총 8개)
    assert(n == 6 && ringbuf_size(rb) == 8);

    n = ringbuf_write(rb, in, 1); // 가득 차면 더 못 씀
    assert(n == 0);

    n = ringbuf_read(rb, out, 8);
    assert(n == 8 && ringbuf_size(rb) == 0);
    // out[0..1]는 이전에 읽었으니, out[2..7]만 확인
    assert(out[2] == 3 && out[3] == 4 && out[4] == 5 && out[5] == 6 && out[6] == 7 && out[7] == 8);

    // 3. 부분 읽기/쓰기
    n = ringbuf_write(rb, in, 3);
    assert(n == 3 && ringbuf_size(rb) == 3);
    n = ringbuf_read(rb, out, 2);
    assert(n == 2 && ringbuf_size(rb) == 1);
    n = ringbuf_read(rb, out, 2);
    assert(n == 1 && ringbuf_size(rb) == 0);

    // 4. NULL/0 처리
    assert(ringbuf_write(NULL, in, 1) == 0);
    assert(ringbuf_read(NULL, out, 1) == 0);
    assert(ringbuf_write(rb, NULL, 1) == 0);
    assert(ringbuf_read(rb, NULL, 1) == 0);

    ringbuf_destroy(rb);
    printf("All ring buffer tests passed!\n");
    return 0;
}