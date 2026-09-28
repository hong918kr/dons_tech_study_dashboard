#include <stdbool.h>

typedef struct {
    uint8_t* buf;
    uint8_t head;
    uint8_t tail;
    uint32_t count;
    uint32_t size;
} rb_t;

void rb_init(rb_t*q, uint8_t* buf_, uint32_t input_size)
{
    //rb_t
    q->buf = buf_;
    q->size = input_size;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
}

//push
void rb_push(rb_t* q, uint8_t data)
{

    if (is_rb_full(q)) return;
    q->buf[q->head] = data;
    q->head = (q->head+1) % q->size;
    q->count++;
}

//pop
bool rb_pop(rb_t* q, uint8_t* out)
{
    if (is_rb_empty(q)) return false;
    *out = q->buf[q->tail];
    q->tail = (q->tail+1) % q->size;
    q->count--;
    return true;
}

//full
bool is_rb_full(rb_t* q)
{
    if (q->count == q->size) return true;
    return false;
}

//empty
bool is_rb_empty(rb_t* q)
{
    if (q->size == 0) return true;
    return false;
}


