/* C-A08 데모: 전처리기는 타입을 모른다 — #if 에선 sizeof 를 못 쓴다. 매크로 상수(CHAR_BIT, __BYTE_ORDER__)만.
 *   cc -std=c11 -fsyntax-only demo_if_sizeof.c
 *   cc -std=c11 -fsyntax-only -DFIXED demo_if_sizeof.c
 */
#include <limits.h>
#if defined(FIXED)
#if CHAR_BIT != 8 || !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "need 8-bit bytes and a little-endian target"
#endif
_Static_assert(sizeof(int) == 4, "int must be 32-bit");   /* 타입 크기는 컴파일러 단계에서 */
#else
#if sizeof(int) == 4
#endif
#endif
