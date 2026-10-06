#include "sensor.h"
#include "hal_i2c.h"

int sensor_read_temp_centi(int16_t *out)
{
    uint8_t b[2];
    int rc = hal_i2c_read(SENSOR_ADDR, SENSOR_REG_TEMP, b, sizeof b);
    if (rc != 0)
        rc = hal_i2c_read(SENSOR_ADDR, SENSOR_REG_TEMP, b, sizeof b);   /* one retry */
    if (rc != 0)
        return -1;
    int16_t t = (int16_t)((b[0] << 8) | b[1]);
    if (t < -4000 || t > 12500)
        return -2;
    *out = t;
    return 0;
}
