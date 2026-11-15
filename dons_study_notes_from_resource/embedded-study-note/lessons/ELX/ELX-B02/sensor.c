/* ENV: linux */
/* ELX-B02 libsensor.so.1 — -fPIC -shared -Wl,-soname,libsensor.so.1 로 빌드 */
#include "sensor.h"

int sensor_abi(void)
{
	return SENSOR_ABI;
}

const char *sensor_arch(void)
{
#if defined(__aarch64__)
	return "aarch64";
#elif defined(__arm__)
	return "arm";
#elif defined(__x86_64__)
	return "x86_64";
#elif defined(__riscv)
	return "riscv64";
#else
	return "unknown";
#endif
}

long sensor_read_mC(int ch)
{
	return 25000L + 125L * ch;
}
