/* sensor.c — 센서 초기화 */
#include "sensor.h"
#include "adc.h"

int sensor_selftest(void) { return adc_read_raw() == 2048 ? 0 : -1; }
