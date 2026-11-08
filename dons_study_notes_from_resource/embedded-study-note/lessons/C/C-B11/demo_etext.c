/* C-B11 데모: 옛 UNIX 방식 get_etext()/get_edata()/get_end() 는 ASLR 아래에서 왜 못 쓰나
 *   cc -std=c11 -O2 -Wno-deprecated-declarations demo_etext.c -o demo_etext && ./demo_etext && ./demo_etext
 * (macOS 13부터 deprecated — -Werror 빌드에선 에러가 나서 본 문제 코드에는 쓰지 않는다) */
#include <stdint.h>
#include <stdio.h>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>

int g_init = 42;

int main(void) {
    unsigned long sz;
    printf("get_etext() = %#lx   get_edata() = %#lx   get_end() = %#lx   (링크 시점 주소)\n",
           get_etext(), get_edata(), get_end());
    printf("&_mh_execute_header = %p   main = %p   &g_init = %p   (실제 주소)\n",
           (void *)&_mh_execute_header, (void *)(uintptr_t)main, (void *)&g_init);
    printf("getsectiondata(__DATA,__data) = %p   (슬라이드 반영됨)\n",
           (void *)getsectiondata(&_mh_execute_header, "__DATA", "__data", &sz));
    return 0;
}
