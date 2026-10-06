#ifndef SENSOR_H
#define SENSOR_H
#include <stdint.h>

#define SENSOR_ADDR     0x48
#define SENSOR_REG_TEMP 0x00

/* Temperature in 0.01 degC. Retries the I2C read once. 0 = ok, -1 = bus error, -2 = out of range. */
int sensor_read_temp_centi(int16_t *out);
#endif
