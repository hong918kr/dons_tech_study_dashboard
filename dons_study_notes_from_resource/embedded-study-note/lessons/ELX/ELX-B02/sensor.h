/* ENV: linux */
/* ELX-B02 libsensor: 크로스 빌드 실습용 아주 작은 공유 라이브러리 (soname libsensor.so.1) */
#ifndef SENSOR_H
#define SENSOR_H

#define SENSOR_ABI 1

int sensor_abi(void);                 /* 라이브러리 ABI 버전 */
const char *sensor_arch(void);        /* 라이브러리가 빌드된 아키텍처 */
long sensor_read_mC(int ch);          /* 가짜 온도 (milli-Celsius) */

#endif
