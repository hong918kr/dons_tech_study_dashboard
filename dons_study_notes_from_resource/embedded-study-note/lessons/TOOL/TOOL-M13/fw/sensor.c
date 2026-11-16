/* ENV: cross — check.sh / demo_*.sh 가 arm-none-eabi-gcc 로 빌드 (호스트 실행 금지) */
/* TOOL-M13 sensor.c — 32칸 링 버퍼 평균/최소/최대. libdrv.a 의 멤버.
 * sensor_selftest / sensor_calibrate / g_cal_table(.data 256 B) 는 아무도 안 쓴다.
 */
#include "fw.h"

#define NSAMP 32u
static int16_t ring[NSAMP];
static uint32_t head, count;

void sensor_push(int16_t raw) {
    ring[head] = raw;
    head = (head + 1u) % NSAMP;
    if (count < NSAMP) count++;
}

int32_t sensor_avg(void) {
    int32_t s = 0;
    for (uint32_t i = 0; i < count; i++) s += ring[i];
    return count ? s / (int32_t)count : 0;
}

int16_t sensor_min(void) {
    int16_t m = INT16_MAX;
    for (uint32_t i = 0; i < count; i++)
        if (ring[i] < m) m = ring[i];
    return m;
}

int16_t sensor_max(void) {
    int16_t m = INT16_MIN;
    for (uint32_t i = 0; i < count; i++)
        if (ring[i] > m) m = ring[i];
    return m;
}

/* ---- dead: 공장 보정 표와 자가 진단 (양산 펌웨어에서는 호출 안 함) ---- */
int32_t g_cal_table[64] = {
    1000, 1003, 1006, 1009, 1012, 1015, 1018, 1021, 1024, 1027, 1030, 1033, 1036, 1039, 1042, 1045,
    1048, 1051, 1054, 1057, 1060, 1063, 1066, 1069, 1072, 1075, 1078, 1081, 1084, 1087, 1090, 1093,
    1096, 1099, 1102, 1105, 1108, 1111, 1114, 1117, 1120, 1123, 1126, 1129, 1132, 1135, 1138, 1141,
    1144, 1147, 1150, 1153, 1156, 1159, 1162, 1165, 1168, 1171, 1174, 1177, 1180, 1183, 1186, 1189,
};

void sensor_calibrate(int32_t gain) {
    for (uint32_t i = 0; i < 64u; i++) g_cal_table[i] = g_cal_table[i] * gain / 1000;
}

int sensor_selftest(void) {
    int16_t save[NSAMP];
    for (uint32_t i = 0; i < NSAMP; i++) save[i] = ring[i];
    int bad = 0;
    for (int16_t v = -300; v <= 300; v += 7) {
        sensor_push(v);
        if (sensor_min() > v || sensor_max() < v) bad++;
        if (sensor_avg() < -300 || sensor_avg() > 300) bad++;
    }
    for (uint32_t i = 0; i < NSAMP; i++) ring[i] = save[i];
    return bad;
}
