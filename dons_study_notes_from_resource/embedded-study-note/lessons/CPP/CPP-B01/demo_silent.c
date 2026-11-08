/* CPP-B01 demo — C 로도 C++ 로도 "에러 없이" 빌드되지만 결과가 다른 코드
 *   cc  -std=c11 -Wall -Wextra -Werror -O2 demo_silent.c -o /tmp/cppb01_sc && /tmp/cppb01_sc
 *   c++ -x c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_silent.c -o /tmp/cppb01_sx && /tmp/cppb01_sx
 */
#include <stdbool.h>
#include <stdio.h>

static char frame[64];                     /* UART 프레임 버퍼 */

static unsigned frame_size(void) {
    struct frame { char hdr, len; };       /* 지역 struct 태그가 같은 이름 */
    return (unsigned)sizeof(frame);        /* C: 배열 frame(64)  C++: struct frame(2) */
}

int main(void) {
    printf("sizeof('A')   = %zu\n", sizeof('A'));   /* C: int(4)   C++: char(1) */
    printf("sizeof(true)  = %zu\n", sizeof(true));  /* C11: 매크로 1 → int(4)   C++: bool(1) */
    printf("frame_size()  = %u\n", frame_size());
    (void)frame;
    return 0;
}
