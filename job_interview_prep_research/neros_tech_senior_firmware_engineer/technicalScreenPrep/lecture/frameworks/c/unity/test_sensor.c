/* Unity + CMock: hal_i2c_read is replaced by a generated mock (mock_hal_i2c.h). */
#include "unity.h"
#include "sensor.h"
#include "mock_hal_i2c.h"

void setUp(void)    {}
void tearDown(void) {}

void test_reads_temperature(void)
{
    uint8_t raw[2] = {0x09, 0x60};                 /* 0x0960 = 2400 → 24.00 degC */
    int16_t t;
    hal_i2c_read_ExpectAndReturn(SENSOR_ADDR, SENSOR_REG_TEMP, NULL, 2, 0);
    hal_i2c_read_IgnoreArg_buf();
    hal_i2c_read_ReturnArrayThruPtr_buf(raw, 2);
    TEST_ASSERT_EQUAL(0, sensor_read_temp_centi(&t));
    TEST_ASSERT_EQUAL_INT16(2400, t);
}

void test_retries_once_then_succeeds(void)
{
    uint8_t raw[2] = {0x00, 0x64};                 /* 1.00 degC */
    int16_t t;
    hal_i2c_read_ExpectAnyArgsAndReturn(-5);       /* 1st read: bus error */
    hal_i2c_read_ExpectAnyArgsAndReturn(0);        /* retry: ok */
    hal_i2c_read_ReturnArrayThruPtr_buf(raw, 2);
    TEST_ASSERT_EQUAL(0, sensor_read_temp_centi(&t));
    TEST_ASSERT_EQUAL_INT16(100, t);
}

void test_gives_up_after_two_errors(void)
{
    int16_t t = 7;
    hal_i2c_read_ExpectAnyArgsAndReturn(-5);
    hal_i2c_read_ExpectAnyArgsAndReturn(-5);
    TEST_ASSERT_EQUAL(-1, sensor_read_temp_centi(&t));
    TEST_ASSERT_EQUAL_INT16(7, t);                 /* output untouched on error */
}

void test_out_of_range_rejected(void)
{
    uint8_t raw[2] = {0x7F, 0xFF};                 /* 327.67 degC */
    int16_t t;
    hal_i2c_read_ExpectAnyArgsAndReturn(0);
    hal_i2c_read_ReturnArrayThruPtr_buf(raw, 2);
    TEST_ASSERT_EQUAL(-2, sensor_read_temp_centi(&t));
}
