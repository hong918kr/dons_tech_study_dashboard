#include <stdio.h>
#define CAP 8

// typedef struct {
//     uint8_t buf[CAP];
//     int head;
//     int tail;
//     int count;
// } Rb_t;

// void rb_init(Rb_t* rb)
// {
//     memset(rb, 0, sizeof(Rb_t));
//     /*
//     or


//     */
// }


typedef struct {
    uint8_t* buf;
    int head;
    int tail;
    int count;
    int capacity;
} Rb_t;

void rb_init(Rb_t* rb, int capacity)
{
    rb->buf = malloc(capacity * sizeof(uint8_t));
    if (rb->buf ==NULL) return; 
    rb->head=0;
    rb->tail=0;
    rb->count=0;
    rb->capacity = capacity;
}

bool rb_empty(Rb_t* rb) {
    return rb->count == 0;
}

bool rb_full(Rb_t* rb) {
    return rb->count == rb->capacity;
}

void rb_push(Rb_t* rb, int item)
{
    if (rb_full(rb)) return false;
    rb->buf[rb->head] = item;
    rb->count +=1;
    rb->head = (rb->head + 1) % rb->capacity;
}

void rb_pop(Rb_t* rb, int* out)
{
    if (rb_empty(rb)) return false;
    *out = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->count -=1;
}

int rb_peek(Rb_t* rb, int* out)
{
    if (rb_empty(rb)) return false;
    *out = rb->buf[rb->tail];
}





