/* ENV: qemu */
/* ELX-B12 Part C: app linked with -Wl,-rpath,'$ORIGIN/../lib/sensor' -> DT_RUNPATH, found without ld.so.cache */
#include <stdio.h>

const char *sensor_version(void);
int sensor_read(void);

int main(void)
{
	printf("sensor: libsensor %s reading=%d\n", sensor_version(), sensor_read());
	return 0;
}
