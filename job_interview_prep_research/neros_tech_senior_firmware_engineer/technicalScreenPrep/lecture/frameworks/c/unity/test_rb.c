/* Unity: plain C unit tests for rb.c — setUp/tearDown run around every test. */
#include "unity.h"
#include "rb.h"

static rb_t rb;

void setUp(void)    { rb_init(&rb); }
void tearDown(void) {}

void test_new_buffer_is_empty(void)
{
    uint8_t v;
    TEST_ASSERT_EQUAL_UINT32(0, rb_count(&rb));
    TEST_ASSERT_FALSE(rb_pop(&rb, &v));
}

void test_all_16_slots_usable_then_full(void)
{
    for (int i = 0; i < 16; i++)
        TEST_ASSERT_TRUE(rb_push(&rb, (uint8_t)i));
    TEST_ASSERT_FALSE_MESSAGE(rb_push(&rb, 99), "17th push must fail (drop-new)");
}

void test_fifo_across_uint32_wrap(void)
{
    uint8_t v;
    rb.head = rb.tail = UINT32_MAX - 3;      /* indices wrap during the test */
    for (uint8_t i = 0; i < 10; i++)
        rb_push(&rb, i);
    TEST_ASSERT_EQUAL_UINT32(10, rb_count(&rb));
    for (uint8_t i = 0; i < 10; i++) {
        TEST_ASSERT_TRUE(rb_pop(&rb, &v));
        TEST_ASSERT_EQUAL_UINT8(i, v);
    }
}
