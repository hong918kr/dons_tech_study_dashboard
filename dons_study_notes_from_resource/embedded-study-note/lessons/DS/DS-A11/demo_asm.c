/* DS-A11 demo_asm — 같은 커널을 arm64 / Cortex-M4 / Cortex-M55 로 컴파일해 비교한다. (freestanding)
 *
 * cc -O2 -S -o - demo_asm.c                                                       # arm64 (이 맥)
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S -o - demo_asm.c
 * clang --target=thumbv8.1m.main-none-eabi -mcpu=cortex-m55 -mthumb -O2 -S -o - demo_asm.c   # MVE 있음
 */
#include <stdint.h>

typedef struct {
    uint64_t t_us;
    uint32_t x, y, z, vx, vy, vz, seq;
    uint16_t temp;
    uint8_t flags, health;
    uint32_t cal[6];
} Rec;

/* 부분 필드 읽기 */
uint32_t aos_sum_x(const Rec *a, uint32_t n) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += a[i].x;
    return s;
}
uint32_t soa_sum_x(const uint32_t *x, uint32_t n) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += x[i];
    return s;
}
/* 위치 갱신 (필드 6개 읽고 3개 씀) */
void aos_update_pos(Rec *restrict a, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        a[i].x += a[i].vx; a[i].y += a[i].vy; a[i].z += a[i].vz;
    }
}
void soa_update_pos(uint32_t *restrict x, uint32_t *restrict y, uint32_t *restrict z,
                    const uint32_t *restrict vx, const uint32_t *restrict vy, const uint32_t *restrict vz,
                    uint32_t n) {
    for (uint32_t i = 0; i < n; i++) x[i] += vx[i];
    for (uint32_t i = 0; i < n; i++) y[i] += vy[i];
    for (uint32_t i = 0; i < n; i++) z[i] += vz[i];
}
