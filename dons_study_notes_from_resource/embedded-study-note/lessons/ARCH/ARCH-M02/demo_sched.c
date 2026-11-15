/* ARCH-M02 demo: 컴파일러가 load-use 를 어떻게 숨기나 (CHECK 없음, asm 관찰용)
 * freestanding 헤더만 → thumbv7em 타깃에서도 빌드된다. */
#include <stdint.h>

/* 배열 합: 본문 prog_sum 과 같은 루프. */
uint32_t sum_u16(const uint16_t *a, uint32_t n) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += a[i];
    return s;
}

/* 포인터 따라가기: 다음 주소가 방금 읽은 값 → 어떤 스케줄링으로도 load-use 를 못 숨긴다 */
struct node { struct node *next; uint32_t v; };
uint32_t chase(const struct node *p, uint32_t n) {
    uint32_t s = 0;
    while (n--) { s += p->v; p = p->next; }
    return s;
}
