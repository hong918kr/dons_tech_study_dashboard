/* rb_internal.h — PRIVATE to the ringbuf library (and its white-box test).
 * Application code must not be able to include this file. */
#ifndef RB_INTERNAL_H
#define RB_INTERNAL_H

#include "ringbuf.h"

#define RB_MASK (RB_CAP - 1u)

static inline uint32_t rb_idx(uint32_t i) { return i & RB_MASK; }

#if RB_HAVE_CRC
uint16_t rb_crc_step(uint16_t crc, uint8_t b);   /* one byte of CRC-16/CCITT-FALSE */
#endif

/* RB_ASSERTS: library-only debug checks (a PRIVATE definition). */
#if defined(RB_ASSERTS) && RB_ASSERTS
void rb_assert_fail(const char *file, int line);
#define RB_ASSERT(x) ((x) ? (void)0 : rb_assert_fail(__FILE__, __LINE__))
#else
#define RB_ASSERT(x) ((void)0)
#endif

#endif
