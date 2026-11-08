/* C-M01 demo_packed.c — 레지스터 구조체에 packed 를 붙이면? (main 없음, 타깃 asm 관찰용)
 * 빌드: clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_packed.c -o -
 *       clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_packed.c -o -
 * packed 는 "정렬 1" 을 뜻한다. 비정렬 접근이 없는 코어(M0)에선 포인터로 받은 32비트 레지스터
 * 쓰기가 바이트 쓰기 4번으로 쪼개진다. (packed 자체는 C-M04 에서) */
#include <stdint.h>
typedef struct __attribute__((packed)) { volatile uint32_t SR, DR; } P;
typedef struct { volatile uint32_t SR, DR; } N;
void wp(uint32_t v) { ((P *)0x40011000u)->DR = v; }   /* 상수 주소: 컴파일러가 정렬을 안다 */
void wn(uint32_t v) { ((N *)0x40011000u)->DR = v; }
void wpp(P *p, uint32_t v) { p->DR = v; }              /* 포인터 인자: 정렬 1 로 가정 */
