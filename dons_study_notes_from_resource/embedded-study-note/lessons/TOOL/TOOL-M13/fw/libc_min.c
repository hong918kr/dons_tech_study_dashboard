/* ENV: cross — check.sh / demo_*.sh 가 arm-none-eabi-gcc 로 빌드 (호스트 실행 금지) */
/* TOOL-M13 libc_min.c — -nostdlib 빌드용 최소 libc: gcc 는 구조체 복사·배열 초기화에 memcpy/memset
 * 호출을 스스로 만들어 낼 수 있다 (-ffreestanding 이어도). newlib 단계(VM)에서는 링크하지 않는다.
 * optimize(...): -O2/-Os 의 "루프 → memcpy 호출" 변환이 memcpy 자신을 재귀 호출로 만들지 않게.
 */
#include <stddef.h>

__attribute__((optimize("no-tree-loop-distribute-patterns"))) void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

__attribute__((optimize("no-tree-loop-distribute-patterns"))) void *memset(void *dst, int c, size_t n) {
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
