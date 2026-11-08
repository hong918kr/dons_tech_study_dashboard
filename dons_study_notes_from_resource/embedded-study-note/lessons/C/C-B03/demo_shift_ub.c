/* C-B03 demo — 시프트 UB / 구현 정의 동작을 실제로 관찰한다.
 * 시프트 양은 volatile 로 읽어서 컴파일러가 미리 계산(상수 폴딩)하지 못하게 한다
 * → CPU의 실제 시프트 명령이 실행된다.
 *   cc -std=c11 -O0 demo_shift_ub.c -o demo && ./demo                       (arm64 네이티브)
 *   cc -std=c11 -O0 -arch x86_64 demo_shift_ub.c -o demo_x86 && ./demo_x86   (Rosetta 2 = x86-64)
 *   cc -std=c11 -g -O0 -fsanitize=undefined demo_shift_ub.c -o demo_ub && ./demo_ub
 */
#include <stdint.h>
#include <stdio.h>

static volatile int      v32  = 32, v33 = 33, v31 = 31, vneg = -1, v1 = 1, v256 = 256;
static volatile uint32_t vx   = 0x12345679u;
static volatile int      vone = 1, vm1 = -1;
static volatile uint8_t  vb   = 0xABu, vb80 = 0x80u;
static volatile uint16_t vh   = 0xFFFFu;

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);   /* UBSan 메시지와 출력 순서를 맞춘다 */
    uint32_t x = vx;
    printf("[1] x << 32          = 0x%08X\n", x << v32);            /* UB: 폭 초과        */
    printf("[2] x << 33          = 0x%08X\n", x << v33);            /* UB: 폭 초과        */
    printf("[3] x >> 32          = 0x%08X\n", x >> v32);            /* UB: 폭 초과        */
    printf("[3b] x << 256        = 0x%08X\n", x << v256);          /* UB: 폭 초과        */
    printf("[4] 1 << 31          = %d\n", vone << v31);             /* UB: signed 오버플로 */
    printf("[5] x << -1          = 0x%08X\n", x << vneg);           /* UB: 음수 시프트 양  */
    printf("[6] -1 << 1          = %d\n", vm1 << v1);               /* UB: 음수 왼쪽 시프트 */
    printf("[7] -1 >> 1          = %d\n", vm1 >> v1);               /* 구현 정의 (UB 아님) */
    printf("[8] (uint8_t)0xAB<<8 = 0x%X\n", vb << 8);               /* 정의됨: int 로 승격  */
    printf("[9] (uint8_t)0x80<<24= %d\n", vb80 << 24);              /* UB: int 부호 비트    */
    printf("[10] (uint32_t)b<<32 = 0x%08X\n", (uint32_t)vb << v32); /* UB: 캐스트가 모자람 */
    printf("[11] (uint64_t)b<<32 = 0x%016llX\n",
           (unsigned long long)((uint64_t)vb << v32));              /* 정의됨              */
    printf("[12] (uint16_t)0xFFFF<<16 = %d\n", vh << 16);            /* UB: int 로 승격 후 넘침 */
    return 0;
}
