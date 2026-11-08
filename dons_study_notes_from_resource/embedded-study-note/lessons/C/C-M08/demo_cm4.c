/* C-M08 데모: 워드 루프가 Cortex-M 에서 어떤 명령어가 되나 (타깃 전용, main 없음)
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os -ffreestanding -S demo_cm4.c -o -
 *   clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -Os -ffreestanding -S demo_cm4.c -o -
 * M4(ARMv7-M) 는 LDR/STR 비정렬 접근을 하드웨어가 처리한다 (CCR.UNALIGN_TRP=0 일 때, LDM/STM/LDRD 는 제외).
 * M0(ARMv6-M) 는 비정렬 LDR 이 HardFault → 컴파일러가 aligned(1) 로드를 바이트 4개로 쪼갠다.
 */
#include <stddef.h>
#include <stdint.h>

typedef uint32_t __attribute__((may_alias)) word_t;
typedef uint32_t __attribute__((may_alias, aligned(1))) uword_t;

/* dst, src 모두 4바이트 정렬 */
void copy_words(unsigned char *d, const unsigned char *s, size_t nwords) {
    for (; nwords; nwords--, d += 4, s += 4)
        *(word_t *)d = *(const word_t *)s;
}

/* dst 는 정렬, src 는 비정렬일 수 있음 */
void copy_words_u(unsigned char *d, const unsigned char *s, size_t nwords) {
    for (; nwords; nwords--, d += 4, s += 4)
        *(word_t *)d = *(const uword_t *)s;
}
