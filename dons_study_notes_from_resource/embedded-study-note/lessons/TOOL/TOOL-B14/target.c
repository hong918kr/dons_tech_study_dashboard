/* TOOL-B14 target.c — 여러 타깃으로 크로스 컴파일할 같은 소스 (freestanding)
 * 헤더는 <stdint.h> 만 쓴다: libc 없이도 컴파일되는 범위 (-ffreestanding). */
#include <stdint.h>

struct sample {
    uint16_t raw;     /* ADC 원시값 */
    uint8_t  ch;      /* 채널 번호 */
    float    gain;    /* 채널별 보정 계수 */
};

/* float 곱셈: 하드 float(FPU 명령) vs 소프트 float(라이브러리 호출) 차이가 드러나는 곳 */
float scale(const struct sample *s) {
    return (float)s->raw * s->gain;
}

/* 정수 루프: 아키텍처별 load/add/branch 모양 비교 */
uint32_t checksum(const uint8_t *p, uint32_t n) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < n; i++)
        sum = (sum << 1 | sum >> 31) ^ p[i];
    return sum;
}
