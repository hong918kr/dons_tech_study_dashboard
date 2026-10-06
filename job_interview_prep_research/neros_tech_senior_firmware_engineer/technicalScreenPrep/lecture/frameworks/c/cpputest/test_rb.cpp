// CppUTest: TEST_GROUP holds per-test state; setup()/teardown() run around each TEST.
#include "CppUTest/TestHarness.h"

extern "C" {
#include "rb.h"
}

TEST_GROUP(RingBuffer)
{
    rb_t rb;
    void setup() override { rb_init(&rb); }
};

TEST(RingBuffer, NewBufferIsEmpty)
{
    uint8_t v;
    UNSIGNED_LONGS_EQUAL(0, rb_count(&rb));
    CHECK_FALSE(rb_pop(&rb, &v));
}

TEST(RingBuffer, All16SlotsUsableThenFull)
{
    for (int i = 0; i < 16; i++)
        CHECK_TRUE(rb_push(&rb, (uint8_t)i));
    CHECK_FALSE_TEXT(rb_push(&rb, 99), "17th push must fail (drop-new)");
}

TEST(RingBuffer, FifoAcrossUint32Wrap)
{
    uint8_t v;
    rb.head = rb.tail = UINT32_MAX - 3;
    for (uint8_t i = 0; i < 10; i++)
        rb_push(&rb, i);
    UNSIGNED_LONGS_EQUAL(10, rb_count(&rb));
    for (uint8_t i = 0; i < 10; i++) {
        CHECK_TRUE(rb_pop(&rb, &v));
        BYTES_EQUAL(i, v);
    }
}
