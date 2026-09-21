#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#define CB_SIZE 8

/*
56. Implement a circular buffer for uint8_t.
*/
typedef struct {
    uint8_t *buffer;
    size_t size;
    size_t head;
    size_t tail;
    size_t count;
} cb_t;

/*
57. Implement a circular buffer initialization function.
*/
void cb_init(cb_t *cb, uint8_t *buffer, size_t size) {
    cb->buffer = buffer;
    cb->size = size;
    cb->head = 0;
    cb->tail = 0;
    cb->count = 0;
}

/*
58. Implement a data push/enqueue function.
*/
bool cb_push(cb_t *cb, uint8_t data) {
    if (cb_is_full(cb)) {
        return false; // Buffer full
    }
    cb->buffer[cb->head] = data;
    cb->head = (cb->head + 1) % cb->size;
    cb->count++;
    return true;
}

/*
59. Implement a data pop/dequeue function.
*/
bool cb_pop(cb_t *cb, uint8_t *data) {
    if (cb_is_empty(cb)) {
        return false; // Buffer empty
    }
    *data = cb->buffer[cb->tail];
    cb->tail = (cb->tail + 1) % cb->size;
    cb->count--;
    return true;
}

/*
60. Implement a function to check if the buffer is empty.
*/
bool cb_is_empty(const cb_t *cb) {
    return cb->count == 0;
}

/*
61. Implement a function to check if the buffer is full.
*/
bool cb_is_full(const cb_t *cb) {
    return cb->count == cb->size;
}

/*
62. Implement full/empty state management using a counter variable.
*/
// See struct and usage of cb->count above

/*
63. Implement a function to return the current number of items.
*/
size_t cb_size(const cb_t *cb) {
    return cb->count;
}

/*
64. Implement a function to flush the circular buffer.
*/
void cb_flush(cb_t *cb) {
    cb->head = 0;
    cb->tail = 0;
    cb->count = 0;
}

/*
65. Discuss thread safety in a single-producer, single-consumer scenario.
*/
// 65
/*
In a single-producer, single-consumer scenario, 
thread safety can be achieved without locks
if only the producer modifies the head and only the consumer modifies the tail. 
The count variable must be updated atomically if accessed by both threads. 
For multi-producer or multi-consumer, additional synchronization is required.
*/

// --- Test code ---
void print_cb(const cb_t *cb) {
    printf("CB: ");
    size_t idx = cb->tail;
    for (size_t i = 0; i < cb->count; ++i) {
        printf("%u ", cb->buffer[idx]);
        idx = (idx + 1) % cb->size;
    }
    printf("\n");
}

int main(void) {
    uint8_t buffer[CB_SIZE];
    cb_t cb;

    cb_init(&cb, buffer, CB_SIZE);

    printf("Push 1~8 to circular buffer:\n");
    for (uint8_t i = 1; i <= 8; ++i) {
        if (cb_push(&cb, i))
            printf("Pushed %u\n", i);
        else
            printf("Buffer full at %u\n", i);
    }
    print_cb(&cb);

    printf("Pop 3 elements:\n");
    for (int i = 0; i < 3; ++i) {
        uint8_t val;
        if (cb_pop(&cb, &val))
            printf("Popped %u\n", val);
        else
            printf("Buffer empty\n");
    }
    print_cb(&cb);

    printf("Flush buffer\n");
    cb_flush(&cb);
    print_cb(&cb);

    return 0;
}

/*
------------------ Expected Result ------------------

Push 1~8 to circular buffer:
Pushed 1
Pushed 2
Pushed 3
Pushed 4
Pushed 5
Pushed 6
Pushed 7
Pushed 8
CB: 1 2 3 4 5 6 7 8 

Pop 3 elements:
Popped 1
Popped 2
Popped 3
CB: 4 5 6 7 8 

Flush buffer
CB: 

-----------------------------------------------------
*/