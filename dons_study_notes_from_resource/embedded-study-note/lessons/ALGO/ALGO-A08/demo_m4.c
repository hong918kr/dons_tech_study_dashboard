/* ALGO-A08 demo: Cortex-M4F 에서 이 레슨의 핵심 함수가 어떤 명령어가 되나 (freestanding, libc 없음).
 * build (asm 보기):
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -O2 -S demo_m4.c -o -
 * solution.c 와 같은 본문을 복사했다 (이 파일은 <stdint.h> 만 쓴다).
 */
#include <stdint.h>

int32_t asr_floor(int32_t v, unsigned k) { return v >= 0 ? v >> k : ~(~v >> k); }

int32_t ema_shift_step(int32_t y, int32_t x, unsigned k) { return y + asr_floor(x - y, k); }

typedef struct { int32_t acc; unsigned k; } ema_fx_t;
int32_t ema_fx_step(ema_fx_t *e, int32_t x) {
    e->acc += x - asr_floor(e->acc, e->k);
    return asr_floor(e->acc, e->k);
}

/* k 가 컴파일 시간 상수일 때 (흔한 펌웨어 형태) */
int32_t ema_fx_step_k4(int32_t *acc, int32_t x) {
    *acc += x - asr_floor(*acc, 4);
    return asr_floor(*acc, 4);
}

typedef struct { uint32_t n; float mean, m2; } welford_f_t;
void welford_f_push(welford_f_t *w, float x) {
    w->n++;
    float d = x - w->mean;
    w->mean += d / (float)w->n;
    w->m2 += d * (x - w->mean);
}

typedef struct { uint32_t n; double mean, m2; } welford_t;
void welford_push(welford_t *w, double x) {
    w->n++;
    double d = x - w->mean;
    w->mean += d / (double)w->n;
    w->m2 += d * (x - w->mean);
}

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
/* rand_below(bits = 32) 를 xorshift32 에 고정한 형태 */
uint32_t rand_below32(uint32_t *s, uint32_t bound) {
    uint32_t thresh = (0u - bound) % bound;      /* = 2^32 mod bound */
    for (;;) { uint32_t r = xs32(s); if (r >= thresh) return r % bound; }
}
