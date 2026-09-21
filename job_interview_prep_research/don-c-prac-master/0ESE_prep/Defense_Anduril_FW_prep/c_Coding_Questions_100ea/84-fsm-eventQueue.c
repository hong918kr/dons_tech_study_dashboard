/*
Implement an FSM using an Event Queue.
    Circular buffer, event struct.
    Asynchronous event handling, decoupling ISR from FSM.
*/

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#define EVENT_QUEUE_SIZE 8

// Define event types
typedef enum {
    EVENT_NONE,
    EVENT_START,
    EVENT_STOP,
    EVENT_ERROR,
    EVENT_MAX
} event_type_t;

// Event structure
typedef struct {
    event_type_t type;
    int value;
} event_t;

// Circular buffer for event queue
typedef struct {
    event_t buffer[EVENT_QUEUE_SIZE];
    int head;
    int tail;
    int count;
} event_queue_t;

// FSM states
typedef enum {
    STATE_IDLE,
    STATE_RUN,
    STATE_ERROR,
    STATE_MAX
} fsm_state_t;

// FSM structure
typedef struct {
    fsm_state_t state;
} fsm_t;

// Event queue functions
void event_queue_init(event_queue_t *q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
}

bool event_queue_push(event_queue_t *q, event_t ev) {
    if (q->count == EVENT_QUEUE_SIZE) return false; // full
    q->buffer[q->head] = ev;
    q->head = (q->head + 1) % EVENT_QUEUE_SIZE;
    q->count++;
    return true;
}

bool event_queue_pop(event_queue_t *q, event_t *ev) {
    if (q->count == 0) return false; // empty
    *ev = q->buffer[q->tail];
    q->tail = (q->tail + 1) % EVENT_QUEUE_SIZE;
    q->count--;
    return true;
}

// FSM event handler
void fsm_handle_event(fsm_t *fsm, event_t *ev) {
    switch (fsm->state) {
        case STATE_IDLE:
            if (ev->type == EVENT_START) {
                fsm->state = STATE_RUN;
                printf("FSM: Enter RUN\n");
            }
            break;
        case STATE_RUN:
            if (ev->type == EVENT_STOP) {
                fsm->state = STATE_IDLE;
                printf("FSM: Enter IDLE\n");
            } else if (ev->type == EVENT_ERROR) {
                fsm->state = STATE_ERROR;
                printf("FSM: Enter ERROR\n");
            }
            break;
        case STATE_ERROR:
            if (ev->type == EVENT_STOP) {
                fsm->state = STATE_IDLE;
                printf("FSM: Recover to IDLE\n");
            }
            break;
    }
}

// --- Test code ---
int main(void) {
    event_queue_t queue;
    event_queue_init(&queue);

    fsm_t fsm;
    fsm.state = STATE_IDLE;

    // Simulate ISR pushing events
    event_queue_push(&queue, (event_t){EVENT_START, 0});
    event_queue_push(&queue, (event_t){EVENT_ERROR, 42});
    event_queue_push(&queue, (event_t){EVENT_STOP, 0});
    event_queue_push(&queue, (event_t){EVENT_START, 0});
    event_queue_push(&queue, (event_t){EVENT_STOP, 0});

    printf("Event | Value | FSM State Output\n");
    printf("-------------------------------\n");
    event_t ev;
    while (event_queue_pop(&queue, &ev)) {
        printf("%5d | %5d | ", ev.type, ev.value);
        fsm_handle_event(&fsm, &ev);
    }
    return 0;
}

/*
------------------ Example Input/Output ------------------

Event | Value | FSM State Output
-------------------------------
    1 |     0 | FSM: Enter RUN
    3 |    42 | FSM: Enter ERROR
    2 |     0 | FSM: Recover to IDLE
    1 |     0 | FSM: Enter RUN
    2 |     0 | FSM:
*/