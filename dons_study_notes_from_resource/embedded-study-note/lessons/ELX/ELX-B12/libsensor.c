/* ENV: qemu */
/* ELX-B12 Part C: the board team's sensor library (SONAME libsensor.so.1, installed in /usr/lib/sensor) */
const char *sensor_version(void)
{
	return "1.0";
}

int sensor_read(void)
{
	return 42;	/* a fake register value */
}
