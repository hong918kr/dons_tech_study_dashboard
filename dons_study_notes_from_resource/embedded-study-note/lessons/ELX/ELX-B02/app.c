/* ENV: linux */
/* ELX-B02 app: libsensor.so.1 을 동적 링크하는 타깃 프로그램.
 * 출력 한 줄을 check.sh 가 정확히 비교한다. */
#include <stdio.h>
#include <string.h>
#include "sensor.h"

static const char *app_arch(void)
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

int main(void)
{
	long t0 = sensor_read_mC(0), t3 = sensor_read_mC(3);
	int ok = sensor_abi() == SENSOR_ABI && strcmp(sensor_arch(), app_arch()) == 0 &&
		 t0 == 25000 && t3 == 25375;

	printf("app: arch=%s long=%zu ptr=%zu lib_arch=%s abi=%d t0=%ld t3=%ld %s\n",
	       app_arch(), sizeof(long), sizeof(void *), sensor_arch(), sensor_abi(),
	       t0, t3, ok ? "OK" : "BAD");
	return ok ? 0 : 1;
}
