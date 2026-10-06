// GoogleTest + gMock: a C HAL function forwards to a mock object (the usual "C function → C++ mock" bridge).
#include <gmock/gmock.h>
#include <gtest/gtest.h>

extern "C" {
#include "hal_i2c.h"
#include "sensor.h"
}

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArrayArgument;

class MockI2c {
public:
    MOCK_METHOD(int, read, (uint8_t addr, uint8_t reg, uint8_t *buf, size_t n));
};

static MockI2c *g_i2c;                               // 테스트 동안만 유효

extern "C" int hal_i2c_read(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n)
{
    return g_i2c->read(addr, reg, buf, n);
}

class SensorTest : public ::testing::Test {
protected:
    void SetUp() override { g_i2c = &i2c; }
    void TearDown() override { g_i2c = nullptr; }
    ::testing::StrictMock<MockI2c> i2c;              // 예상 밖 호출은 실패
};

TEST_F(SensorTest, ReadsTemperature)
{
    const uint8_t raw[2] = {0x09, 0x60};
    EXPECT_CALL(i2c, read(SENSOR_ADDR, SENSOR_REG_TEMP, _, 2))
        .WillOnce(DoAll(SetArrayArgument<2>(raw, raw + 2), Return(0)));
    int16_t t;
    EXPECT_EQ(0, sensor_read_temp_centi(&t));
    EXPECT_EQ(2400, t);
}

TEST_F(SensorTest, RetriesOnceThenSucceeds)
{
    const uint8_t raw[2] = {0x00, 0x64};
    ::testing::InSequence seq;                       // 순서까지 검사
    EXPECT_CALL(i2c, read(_, _, _, _)).WillOnce(Return(-5));
    EXPECT_CALL(i2c, read(_, _, _, _))
        .WillOnce(DoAll(SetArrayArgument<2>(raw, raw + 2), Return(0)));
    int16_t t;
    EXPECT_EQ(0, sensor_read_temp_centi(&t));
    EXPECT_EQ(100, t);
}

TEST_F(SensorTest, GivesUpAfterTwoErrors)
{
    EXPECT_CALL(i2c, read(_, _, _, _)).Times(2).WillRepeatedly(Return(-5));
    int16_t t = 7;
    EXPECT_EQ(-1, sensor_read_temp_centi(&t));
    EXPECT_EQ(7, t);
}
