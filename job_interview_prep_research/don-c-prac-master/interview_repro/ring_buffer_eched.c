```c
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h> // For INT_MIN (or another suitable sentinel value)

typedef struct {
    int *buffer;
    int capacity;
    int front; // Index for the next element to be read
    int rear;  // Index for the next element to be written
    int size;  // Number of elements currently in the buffer
} RingBuffer;

RingBuffer *create_ring_buffer(int capacity) {
    RingBuffer *rb = (RingBuffer *)malloc(sizeof(RingBuffer));
    if (rb == NULL) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }

    rb->buffer = (int *)malloc(capacity * sizeof(int));
    if (rb->buffer == NULL) {
        perror("malloc failed");
        free(rb);
        exit(EXIT_FAILURE);
    }

    rb->capacity = capacity;
    rb->front = 0;
    rb->rear = 0;
    rb->size = 0;

    return rb;
}

bool is_full(const RingBuffer *rb) {
    return rb->size == rb->capacity;
}

bool is_empty(const RingBuffer *rb) {
    return rb->size == 0;
}

bool enqueue(RingBuffer *rb, int value) {
    if (is_full(rb)) {
        return false;
    }

    rb->buffer[rb->rear] = value;
    rb->rear = (rb->rear + 1) % rb->capacity;
    rb->size++;
    return true;
}

bool dequeue(RingBuffer *rb, int *value) {
    if (is_empty(rb)) {
        return false;
    }

    if (value != NULL) {
        *value = rb->buffer[rb->front];
    }

    rb->front = (rb->front + 1) % rb->capacity;
    rb->size--;
    return true;
}

// Peek function: Returns the element at the front, or INT_MIN if empty
bool peek(const RingBuffer *rb int *value) {
    if (is_empty(rb)) {
        //return INT_MIN; // Or another suitable sentinel value
        return false;
    }
        
    if (value != NULL) {
        *value = rb->buffer[rb->front];
        return true;
    } else {
        return false;
    }
}


void destroy_ring_buffer(RingBuffer *rb) {
    free(rb->buffer);
    free(rb);
}

int main() {
    RingBuffer *rb = create_ring_buffer(5);

    for (int i = 0; i < 7; i++) {
        if (enqueue(rb, i)) {
            printf("Enqueued: %d\n", i);
        } else {
            printf("Failed to enqueue: %d (Buffer full)\n", i);
        }
    }

    int value;

    printf("Peeking: ");
    if(peek(rb, &value)) {
        printf("%d\n", value);
    } else {
        printf("Buffer is empty\n");
    }

    while (!is_empty(rb)) {
        if (dequeue(rb, &value)) {
            printf("Dequeued: %d\n", value);
        }
    }

    printf("Peeking after dequeue: ");
    if(peek(rb, &value)) {
        printf("%d\n", value);
    } else {
        printf("Buffer is empty\n");
    }


    destroy_ring_buffer(rb);
    return 0;
}
```

Key improvements and explanations:

* **Error Handling:** Includes checks for `malloc` failures and handles full/empty conditions gracefully.  The `enqueue` and `dequeue` functions return `bool` values to indicate success or failure.
* **Clearer Structure:** Uses a `struct` to encapsulate the ring buffer's data, making the code more organized.
* **Wrap-around Logic:**  The modulo operator (`%`) is used correctly for wrap-around in both `enqueue` and `dequeue`.
* **Size Tracking:** The `size` member variable is crucial for efficiently determining if the buffer is full or empty, avoiding the need to compare `head` and `tail` directly (which can be complex due to wrapping).
* **`dequeue` with optional value:** The `dequeue` function now accepts an optional pointer to an int. If a valid pointer is provided, the dequeued value is copied to it. If a `NULL` pointer is passed, then the value is simply dequeued without being copied. This allows the user to simply pop an element without necessarily needing its value.
* **`destroy_ring_buffer` Function:**  Added a function to properly free the allocated memory when the ring buffer is no longer needed, preventing memory leaks.
* **Example Usage in `main`:** Demonstrates how to create, use, and destroy the ring buffer, including handling full/empty conditions.  The example tries to add more elements than the buffer can hold to show how the full condition is handled.
* **Comments:** Added more comments to explain the purpose of each part of the code.

This improved version is more robust, efficient, and easier to understand. It addresses the potential issues and provides a more complete and practical implementation of a ring buffer in C.
