/* ctypes shim: exports onsitePrep/code/ring_buffer.c (static functions) so pytest can drive the C ring buffer. */
#define main rb_selftest_main            /* the original file has its own test main(); keep it, rename it */
#include "../../onsitePrep/code/ring_buffer.c"
#undef main

/* Non-static wrappers: the originals are static, so they have no exported symbol. */
size_t   shim_sizeof(void)                         { return sizeof(rb_t); }
uint32_t shim_capacity(void)                       { return RB_SIZE; }
void     shim_init(rb_t *rb)                       { rb_init(rb); }
uint32_t shim_count(const rb_t *rb)                { return rb_count(rb); }
int      shim_push(rb_t *rb, uint8_t b)            { return rb_push(rb, b); }
int      shim_pop(rb_t *rb, uint8_t *out)          { return rb_pop(rb, out); }
int      shim_peek(const rb_t *rb, uint8_t *out)   { return rb_peek(rb, out); }
size_t   shim_write(rb_t *rb, const uint8_t *s, size_t n) { return rb_write(rb, s, n); }
size_t   shim_read(rb_t *rb, uint8_t *d, size_t n)        { return rb_read(rb, d, n); }
int      shim_selftest(void)                       { return rb_selftest_main(); }
