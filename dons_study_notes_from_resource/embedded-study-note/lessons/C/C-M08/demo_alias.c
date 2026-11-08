/* C-M08 데모: 워드 채우기를 plain uint32_t* 로 쓰면 strict aliasing 으로 "틀린" 코드가 나올 수 있다
 *   cc -std=c11 -O0 demo_alias.c -o alias0 && ./alias0
 *   cc -std=c11 -O2 demo_alias.c -o alias2 && ./alias2
 *   cc -std=c11 -O2 -fno-strict-aliasing demo_alias.c -o alias2n && ./alias2n
 * fill_plain 은 uint16_t 객체(RGB565 픽셀)를 uint32_t lvalue 로 쓴다 → C11 6.5p7 위반(UB).
 * fill_alias 는 may_alias 타입이라 GNU C 에서 정의된 동작.
 */
#include <stdint.h>
#include <stdio.h>

typedef uint32_t __attribute__((may_alias)) word_t;

static inline void fill_plain(void *p, uint32_t v, int nwords) {
    uint32_t *w = p;
    for (int i = 0; i < nwords; i++) w[i] = v;
}
static inline void fill_alias(void *p, uint32_t v, int nwords) {
    word_t *w = p;
    for (int i = 0; i < nwords; i++) w[i] = v;
}

/* fb 는 같은 프레임버퍼를 바이트 단위로 본 포인터 (드라이버가 흔히 void* / uint8_t* 로 넘긴다) */
__attribute__((noinline)) uint16_t clear_then_read_plain(uint16_t *px, void *fb) {
    px[0] = 0xF800;                 /* 빨강 픽셀 */
    fill_plain(fb, 0, 1);           /* 픽셀 2개를 0 으로 (fb == px) */
    return px[0];                   /* 0 이어야 한다 */
}
__attribute__((noinline)) uint16_t clear_then_read_alias(uint16_t *px, void *fb) {
    px[0] = 0xF800;
    fill_alias(fb, 0, 1);
    return px[0];
}

int main(void) {
    _Alignas(4) uint16_t a[2], b[2];
    printf("plain uint32_t* : px[0] = 0x%04X\n", clear_then_read_plain(a, a));
    printf("may_alias word_t: px[0] = 0x%04X\n", clear_then_read_alias(b, b));
    return 0;
}
