/* C-B13 demo: 하지 말 것 — 바이트 버퍼를 uint32_t* 로 캐스트해 읽기 (정렬 UB + aliasing 위반) */
#include <stdint.h>
#include <stdio.h>

int main(void) {
    _Alignas(4) uint8_t buf[8] = { 0xAA, 0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0 };
    uint32_t v = *(const uint32_t *)(buf + 1);   /* 주소가 4의 배수가 아님 */
    printf("v = 0x%08x (host order, not BE!)\n", v);
    return 0;
}
