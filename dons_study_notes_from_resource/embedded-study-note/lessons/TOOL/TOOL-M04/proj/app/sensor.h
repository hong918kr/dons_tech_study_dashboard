/* sensor.h — app-private fake ADC (lives next to the app sources). */
#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>

void    sensor_init(uint32_t seed);
uint8_t sensor_read(void);

#endif
