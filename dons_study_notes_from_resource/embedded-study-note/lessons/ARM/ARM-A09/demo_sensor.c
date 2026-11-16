/* ARM-A09 demo_sensor.c — libsensor.dylib: 다른 이미지에 있는 데이터(sensor_gain)와 함수(sensor_read) */
long sensor_gain = 3;
long sensor_read(long raw) { return raw * sensor_gain; }
