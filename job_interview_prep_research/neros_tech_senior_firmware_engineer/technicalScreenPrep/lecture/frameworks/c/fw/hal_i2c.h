/* HAL boundary: on target this talks to the STM32 I2C peripheral; in host tests it is mocked. */
#ifndef HAL_I2C_H
#define HAL_I2C_H
#include <stddef.h>
#include <stdint.h>

int hal_i2c_read(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);   /* 0 = ok, <0 = error */
#endif
