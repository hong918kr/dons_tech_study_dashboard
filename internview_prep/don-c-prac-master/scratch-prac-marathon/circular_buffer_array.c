#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define BUFFER_CAPACITY 16 // 버퍼 크기를 매크로로 정의하여 유연성을 높입니다.

// 원형 버퍼의 상태를 관리하는 구조체
typedef struct {
    uint8_t buffer[BUFFER_CAPACITY];
    size_t capacity;
    size_t head;     // 데이터를 넣을 위치 (생산자)
    size_t tail;     // 데이터를 뺄 위치 (소비자)
    size_t count;    // 현재 버퍼에 저장된 요소의 수
} circular_buffer_t;

// 버퍼를 초기 상태로 설정합니다.
void buffer_init(circular_buffer_t *cb) {
    cb->capacity = BUFFER_CAPACITY;
    cb->head = 0;
    cb->tail = 0;
    cb->count = 0;
}

// 버퍼가 가득 찼는지 확인합니다.
bool buffer_is_full(const circular_buffer_t *cb) {
    return cb->count == cb->capacity;
}

// 버퍼가 비어있는지 확인합니다.
bool buffer_is_empty(const circular_buffer_t *cb) {
    return cb->count == 0;
}

// 버퍼에 데이터를 추가합니다. (Push/Enqueue)
bool buffer_push(circular_buffer_t *cb, uint8_t data) {
    // (설명) "먼저 버퍼가 가득 찼는지 확인합니다. 가득 찼다면 실패를 의미하는 false를 반환합니다."
    if (buffer_is_full(cb)) {
        return false; // 버퍼 오버플로우 방지
    }

    // (설명) "head 위치에 데이터를 저장하고, head 인덱스를 다음 위치로 옮깁니다.
    // 모듈로(%) 연산을 사용해 인덱스가 배열 끝에 도달하면 다시 0으로 돌아가도록 합니다."
    cb->buffer[cb->head] = data;
    cb->head = (cb->head + 1) % cb->capacity;
    cb->count++;

    return true;
}

// 버퍼에서 데이터를 꺼냅니다. (Pop/Dequeue)
bool buffer_pop(circular_buffer_t *cb, uint8_t *data) {
    // (설명) "먼저 버퍼가 비어있는지 확인합니다. 비었다면 실패를 의미하는 false를 반환합니다."
    if (buffer_is_empty(cb)) {
        return false; // 버퍼 언더플로우 방지
    }

    // (설명) "tail 위치의 데이터를 포인터를 통해 반환하고, tail 인덱스를 다음 위치로 옮깁니다.
    // 마찬가지로 모듈로 연산을 사용해 순환 구조를 구현합니다."
    *data = cb->buffer[cb->tail];
    cb->tail = (cb->tail + 1) % cb->capacity;
    cb->count--;

    return true;
}

// 테스트를 위한 main 함수
int main() {
    circular_buffer_t my_buffer;
    buffer_init(&my_buffer);

    printf("Pushing 10 elements...\n");
    for (uint8_t i = 0; i < 10; ++i) {
        buffer_push(&my_buffer, i);
    }
    printf("Count: %zu\n", my_buffer.count);

    printf("\nPopping 5 elements...\n");
    uint8_t value;
    for (int i = 0; i < 5; ++i) {
        if (buffer_pop(&my_buffer, &value)) {
            printf("Popped: %u\n", value);
        }
    }
    printf("Count: %zu\n", my_buffer.count);

    printf("\nPushing to full...\n");
    for (uint8_t i = 10; i < 25; ++i) {
        if (!buffer_push(&my_buffer, i)) {
            printf("Buffer is full! Failed to push %u.\n", i);
            break;
        }
    }
    printf("Count: %zu, Is full? %s\n", my_buffer.count, buffer_is_full(&my_buffer)? "Yes" : "No");

    return 0;
}