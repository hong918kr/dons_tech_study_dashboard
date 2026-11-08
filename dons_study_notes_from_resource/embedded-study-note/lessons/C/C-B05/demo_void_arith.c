/* C-B05 데모: void* 산술은 GNU 확장 — 경고 플래그별로 컴파일해 본다 */
#include <stdio.h>

int main(void) {
    unsigned buf[2] = { 0, 0 };
    void *v = buf;
    void *w = v + 1;                 /* 표준 C: 제약 위반 (void 는 불완전 타입) */
    printf("sizeof(void) = %zu, (char*)w - (char*)v = %td\n",
           sizeof(void), (char *)w - (char *)v);
    return 0;
}
