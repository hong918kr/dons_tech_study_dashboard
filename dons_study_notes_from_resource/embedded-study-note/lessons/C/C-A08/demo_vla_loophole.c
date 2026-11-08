/* C-A08 데모: C99 시절 BUILD_BUG_ON (음수 배열 크기) 의 구멍.
 * 조건이 상수식이 아니면 char[1 - 2*!!(c)] 는 VLA 가 되어 컴파일이 "통과" 하고, 검사는 런타임으로 밀린다.
 *
 *   cc -std=c11 -Wall -Wextra -O2 demo_vla_loophole.c -o dv && ./dv a b c          # 경고 0, 그냥 실행됨
 *   cc -std=c11 -Wall -Wextra -Wvla -fsyntax-only demo_vla_loophole.c               # -Wvla 만 알려 준다
 *   cc -std=c11 -g -O1 -fsanitize=undefined demo_vla_loophole.c -o dvu && ./dvu a b c   # 런타임에야 UB 보고
 *   cc -std=c11 -fsyntax-only -DOLD_FAIL demo_vla_loophole.c                        # 옛 판이 상수로 실패할 때의 메시지
 *   cc -std=c11 -fsyntax-only -DUSE_C11 demo_vla_loophole.c                         # _Static_assert 판: 빌드 실패
 *   cc -std=c11 -Wall -Wextra -pedantic -fsyntax-only -DKERNEL_STYLE demo_vla_loophole.c         # 커널식 비트필드
 *   cc -std=c11 -fsyntax-only -DKERNEL_STYLE -DKERNEL_FAIL demo_vla_loophole.c
 */
#include <stdio.h>

#if defined(USE_C11)
#define BUILD_BUG_ON(c) do { _Static_assert(!(c), "BUILD_BUG_ON(" #c ")"); } while (0)
#else
#define BUILD_BUG_ON(c) ((void)sizeof(char[1 - 2 * !!(c)]))          /* C99 시절 관용구 */
#endif
/* 리눅스 커널식: 폭이 음수인 비트필드. 조건이 거짓이면 이름 없는 0폭 비트필드만 남은 "빈 구조체" (GNU 확장) */
#define BUILD_BUG_ON_ZERO_K(e) ((int)sizeof(struct { int : (-!!(e)); }))

int main(int argc, char **argv) {
    (void)argv;
    BUILD_BUG_ON(sizeof(int) != 4);      /* 상수식: 어느 판이든 컴파일 타임 검사 */
    BUILD_BUG_ON(argc > 3);              /* 상수식 아님: 옛 판은 VLA 로 조용히 통과 */
#ifdef OLD_FAIL
    BUILD_BUG_ON(sizeof(int) == 4);      /* 상수식이고 참 → 실패해야 함 */
#endif
#ifdef KERNEL_STYLE
    printf("BUILD_BUG_ON_ZERO_K(0) = %d\n", BUILD_BUG_ON_ZERO_K(0));
#ifdef KERNEL_FAIL
    printf("%d\n", BUILD_BUG_ON_ZERO_K(sizeof(int) == 4));
#endif
#endif
    printf("argc=%d: built and ran\n", argc);
    return 0;
}
