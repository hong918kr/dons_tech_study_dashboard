/* C-B11 데모: 64 KB 배열 하나를 어떻게 선언하느냐에 따라 섹션 크기가 어떻게 바뀌나
 * VARIANT 0 = 기준, 1 = 0 아닌 초기값, 2 = 초기값 없음(0), 3 = const   — size_demo.sh 참고 */
#include <stdint.h>

#if VARIANT == 1
uint8_t big[65536] = { 1 };          /* → __DATA,__data   / ELF .data   */
#elif VARIANT == 2
uint8_t big[65536];                  /* → __DATA,__common / ELF .bss    */
#elif VARIANT == 3
const uint8_t big[65536] = { 1 };    /* → __TEXT,__const  / ELF .rodata */
#endif

int main(int argc, char **argv) {
    (void)argv;
#if VARIANT
    return big[argc & 0xFFFF];       /* 배열을 실제로 써야 최적화로 사라지지 않는다 */
#else
    return argc;
#endif
}
