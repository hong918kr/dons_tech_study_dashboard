// CppUTest + CppUTestExt mocks: hal_i2c_read is a hand-written stub that reports to mock().
#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

extern "C" {
#include "hal_i2c.h"
#include "sensor.h"

int hal_i2c_read(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n)
{
    return mock().actualCall("hal_i2c_read")
        .withParameter("addr", addr)
        .withParameter("reg", reg)
        .withOutputParameter("buf", buf)
        .withParameter("n", n)
        .returnIntValueOrDefault(0);
}
}

TEST_GROUP(Sensor)
{
    void teardown() override
    {
        mock().checkExpectations();   // 기대한 호출이 다 일어났나
        mock().clear();
    }
};

TEST(Sensor, ReadsTemperature)
{
    const uint8_t raw[2] = {0x09, 0x60};
    int16_t t;
    mock().expectOneCall("hal_i2c_read")
        .withParameter("addr", (uint8_t)SENSOR_ADDR)
        .withParameter("reg", (uint8_t)SENSOR_REG_TEMP)
        .withOutputParameterReturning("buf", raw, sizeof raw)
        .withParameter("n", (size_t)2)
        .andReturnValue(0);
    LONGS_EQUAL(0, sensor_read_temp_centi(&t));
    LONGS_EQUAL(2400, t);
}

TEST(Sensor, RetriesOnceThenSucceeds)
{
    const uint8_t raw[2] = {0x00, 0x64};
    int16_t t;
    mock().expectOneCall("hal_i2c_read").ignoreOtherParameters().andReturnValue(-5);
    mock().expectOneCall("hal_i2c_read").withOutputParameterReturning("buf", raw, sizeof raw)
        .ignoreOtherParameters().andReturnValue(0);
    LONGS_EQUAL(0, sensor_read_temp_centi(&t));
    LONGS_EQUAL(100, t);
}

TEST(Sensor, GivesUpAfterTwoErrors)
{
    int16_t t = 7;
    mock().expectNCalls(2, "hal_i2c_read").ignoreOtherParameters().andReturnValue(-5);
    LONGS_EQUAL(-1, sensor_read_temp_centi(&t));
    LONGS_EQUAL(7, t);
}
