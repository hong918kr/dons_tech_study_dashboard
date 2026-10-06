// GoogleTest: fixture class (TEST_F) + value-parameterized test (TEST_P), like pytest fixture/parametrize.
#include <gtest/gtest.h>

extern "C" {
#include "rb.h"
}

class RingBufferTest : public ::testing::Test {
protected:
    void SetUp() override { rb_init(&rb); }
    rb_t rb;
};

TEST_F(RingBufferTest, NewBufferIsEmpty)
{
    uint8_t v;
    EXPECT_EQ(0u, rb_count(&rb));
    EXPECT_FALSE(rb_pop(&rb, &v));
}

TEST_F(RingBufferTest, All16SlotsUsableThenFull)
{
    for (int i = 0; i < 16; i++)
        ASSERT_TRUE(rb_push(&rb, static_cast<uint8_t>(i))) << "push #" << i;
    EXPECT_FALSE(rb_push(&rb, 99)) << "17th push must fail (drop-new)";
}

// 같은 테스트를 시작 인덱스만 바꿔 여러 번 (pytest.mark.parametrize 와 같은 역할)
class RingBufferWrap : public ::testing::TestWithParam<uint32_t> {};

TEST_P(RingBufferWrap, FifoFromAnyStartIndex)
{
    rb_t rb;
    uint8_t v;
    rb.head = rb.tail = GetParam();
    for (uint8_t i = 0; i < 10; i++)
        rb_push(&rb, i);
    ASSERT_EQ(10u, rb_count(&rb));
    for (uint8_t i = 0; i < 10; i++) {
        ASSERT_TRUE(rb_pop(&rb, &v));
        EXPECT_EQ(i, v);
    }
}

INSTANTIATE_TEST_SUITE_P(StartIndex, RingBufferWrap,
                         ::testing::Values(0u, 5u, UINT32_MAX - 3, UINT32_MAX));
