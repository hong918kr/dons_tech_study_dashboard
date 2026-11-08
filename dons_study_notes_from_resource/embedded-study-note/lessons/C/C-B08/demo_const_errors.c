/* C-B08 demo: "컴파일이 실패해야 하는" const 위반 모음.
 * 그냥 빌드하면 깨끗하게 통과하고, -DE1 ... -DE8 을 하나씩 켜면 그 줄만 에러가 난다.
 *   for e in E1 E2 E3 E4 E5 E6 E7 E8; do
 *     echo "== $e"; cc -std=c11 -Wall -Wextra -Werror -fsyntax-only -D$e demo_const_errors.c 2>&1 | grep -E 'error|warning'
 *   done
 */
#include <stdint.h>

typedef struct {
    const volatile uint32_t SR;   /* 읽기 전용 상태 레지스터 */
    volatile uint32_t DR;
} UART_TypeDef;

void demo(UART_TypeDef *u);
void demo(UART_TypeDef *u) {
    int a = 1, b = 2;
    int *p1 = &a;                     /* ① T*             */
    const int *p2 = &a;               /* ② const T*       */
    int *const p3 = &a;               /* ③ T* const       */
    const int *const p4 = &a;         /* ④ const T* const */

    *p1 = 10; p1 = &b;                /* ① 둘 다 OK */
    p2 = &b;                          /* ② 포인터 바꾸기 OK */
    *p3 = 30;                         /* ③ 값 바꾸기 OK */
    (void)p4;

#ifdef E1
    *p2 = 20;                         /* ② 값은 못 바꾼다 */
#endif
#ifdef E2
    p3 = &b;                          /* ③ 포인터는 못 바꾼다 */
#endif
#ifdef E3
    *p4 = 40;                         /* ④ 값도 */
#endif
#ifdef E4
    p4 = &b;                          /* ④ 포인터도 */
#endif
#ifdef E5
    int *q = p2;                      /* const 를 몰래 떼기: const int * → int * */
    (void)q;
#endif
#ifdef E6
    char *s = "hi";
    char **pp = &s;
    const char **cpp = pp;            /* char ** → const char ** (중첩 포인터) */
    (void)cpp;
#endif
#ifdef E7
    char *s = "hi";
    const char *const *cpp = &s;      /* char ** → const char *const * : C 에서는 이것도 경고 */
    (void)cpp;
#endif
#ifdef E8
    u->SR = 0;                        /* const volatile 레지스터에 쓰기 */
#endif
    u->DR = (uint32_t)(*p1 + *p2 + *p3 + *p4) + u->SR;
}
