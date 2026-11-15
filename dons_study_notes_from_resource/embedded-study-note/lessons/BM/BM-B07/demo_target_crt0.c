/* ENV: cross — arm-none-eabi 컴파일 전용 (링크·실행은 BM-B08 에서 QEMU 로)
 *
 * BM-B07 demo: 호스트 시뮬레이션의 reset_init() 을 "진짜 타깃 모양" 으로 쓴 Reset_Handler.
 * 링커 심볼은 변수가 아니라 "주소" 다 — extern 으로 선언하고 & 로만 쓴다 (값을 읽으면 쓰레기).
 *
 * 이 함수가 지켜야 하는 것:
 *   - 전역/정적 변수를 쓰지 않는다 (.data/.bss 는 아직 초기화 전). 지역 변수는 레지스터/스택 — 스택은
 *     Cortex-M 하드웨어가 리셋 때 vector[0] 으로 MSP 를 이미 잡아 주었으므로 C 로 써도 된다.
 *   - 라이브러리 함수(memcpy/memset)에 기대지 않는다 — 그런데 GCC -O2 는 루프를 memcpy/memset 호출로
 *     바꾼다 (-ftree-loop-distribute-patterns). -nostdlib 링크면 undefined reference. 5. 풀이 참고.
 *
 * Mac 에서:  clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -Wall -Wextra -Werror -S demo_target_crt0.c -o -
 * VM 에서:   arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c11 -O2 -Wall -Wextra -Werror -c demo_target_crt0.c
 *            (+ -fno-tree-loop-distribute-patterns 또는 -ffreestanding 로 비교)
 */
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;      /* 링커 스크립트가 정의 */
typedef void (*init_fn_t)(void);
extern init_fn_t __init_array_start[], __init_array_end[];
int main(void);

void Reset_Handler(void) {
    const uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; dst < &_edata; ) *dst++ = *src++;     /* .data: LMA → VMA */
    for (uint32_t *dst = &_sbss; dst < &_ebss; ) *dst++ = 0u;           /* .bss: 0 */
    for (init_fn_t *f = __init_array_start; f < __init_array_end; f++) (*f)();   /* constructor */
    (void)main();
    for (;;) { }                                                         /* main 이 돌아오면 여기서 멈춤 */
}
