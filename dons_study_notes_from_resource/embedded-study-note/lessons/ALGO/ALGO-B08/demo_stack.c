/* ALGO-B08 demo_stack — 카운트 배열을 어디에 두나: Cortex-M4 스택 프레임 크기 확인
 *
 * 이 파일은 MCU 용으로만 컴파일해 본다 (libc 헤더 없이 <stdint.h>/<stddef.h> 만):
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S demo_stack.c -o - | grep -E 'sub(w|\.w)?\s+sp|^[a-z_]+:'
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -c -fstack-usage demo_stack.c -o /tmp/ALGO-B08_ds.o && cat /tmp/ALGO-B08_ds.su
 *   (ENV: 크로스 컴파일 시연용. 실행 파일이 아니다 — main 없음)
 */
#include <stddef.h>
#include <stdint.h>

#define ADC_RANGE 4096u

/* (1) 지역 배열: 16 KB 가 호출할 때마다 스택에 잡힌다. 태스크 스택 1~4 KB 면 즉시 넘친다. */
void count_sort_local(uint16_t *a, size_t n) {
    uint32_t cnt[ADC_RANGE];
    for (uint32_t v = 0; v < ADC_RANGE; v++) cnt[v] = 0;
    for (size_t i = 0; i < n; i++) cnt[a[i] & 0xFFFu]++;
    size_t k = 0;
    for (uint32_t v = 0; v < ADC_RANGE; v++)
        for (uint32_t c = cnt[v]; c > 0; c--) a[k++] = (uint16_t)v;
}

/* (2) static: .bss 16 KB 고정 (링커 맵에 보인다). 스택은 거의 0. 대신 재진입 불가 (두 태스크 동시 호출 금지). */
static uint32_t s_cnt[ADC_RANGE];
void count_sort_static(uint16_t *a, size_t n) {
    for (uint32_t v = 0; v < ADC_RANGE; v++) s_cnt[v] = 0;
    for (size_t i = 0; i < n; i++) s_cnt[a[i] & 0xFFFu]++;
    size_t k = 0;
    for (uint32_t v = 0; v < ADC_RANGE; v++)
        for (uint32_t c = s_cnt[v]; c > 0; c--) a[k++] = (uint16_t)v;
}

/* (3) 기수 정렬 한 패스의 256 칸 카운트: 1 KB. 스택에 둬도 현실적인 크기. */
void radix_pass_local(const uint32_t *src, uint32_t *dst, size_t n, unsigned shift) {
    uint32_t cnt[256];
    for (unsigned d = 0; d < 256u; d++) cnt[d] = 0;
    for (size_t i = 0; i < n; i++) cnt[(src[i] >> shift) & 0xFFu]++;
    uint32_t sum = 0;
    for (unsigned d = 0; d < 256u; d++) { uint32_t c = cnt[d]; cnt[d] = sum; sum += c; }
    for (size_t i = 0; i < n; i++) dst[cnt[(src[i] >> shift) & 0xFFu]++] = src[i];
}
