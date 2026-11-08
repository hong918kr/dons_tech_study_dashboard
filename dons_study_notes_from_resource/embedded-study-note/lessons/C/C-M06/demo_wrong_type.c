/* C-M06 demo — 함수 포인터를 "틀린 타입" 으로 호출하기 (UB 관찰용, 일부러 -Werror 없이 빌드)
 *
 *   cc -std=c11 -Wall -Wextra -Wcast-function-type-strict -O2 demo_wrong_type.c -o /tmp/wt && /tmp/wt
 *   cc -std=c11 -O1 -g -fsanitize=function demo_wrong_type.c -o /tmp/wt_f && /tmp/wt_f
 *   cc -std=c11 -O1 -g -fsanitize=undefined demo_wrong_type.c -o /tmp/wt_u && /tmp/wt_u
 *   /tmp/wt null          # 컴파일러가 "널" 임을 아는 함수 포인터 호출
 */
#include <stdio.h>
#include <string.h>

struct uart { int id; };

typedef void (*cb_void_t)(void);
typedef int  (*rx_generic_t)(void *ctx);            /* "범용" 콜백 타입 */

static void handler_int(int x) { printf("  handler_int got x = %d\n", x); }
static int  uart_rx(struct uart *u) { return u->id * 10; }   /* 매개변수가 void * 가 아니다 */
static int  uart_rx_ok(void *ctx) { struct uart *u = ctx; return u->id * 10; }  /* 올바른 모양 */

static cb_void_t pick(int i) { return i ? (cb_void_t)handler_int : NULL; }

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "null") == 0) {
        cb_void_t f = pick(0);                       /* 인라인되면 컴파일러는 f == NULL 을 안다 */
        puts("calling a NULL function pointer...");
        f();                                         /* UB */
        return 0;
    }
    /* ① void(int) 를 void(void) 로 캐스트해서 호출 — 인자 레지스터에 남아 있던 쓰레기를 받는다 */
    cb_void_t volatile f = (cb_void_t)handler_int;   /* 캐스트 자체는 합법 (6.3.2.3p8). volatile = 탈가상화 방지 */
    puts("1) call void(int) through void(*)(void):");
    f();                                             /* UB: 호환되지 않는 타입으로 호출 */

    /* ② 되돌려 캐스트한 뒤 호출 — 합법 */
    void (*back)(int) = (void (*)(int))f;
    puts("2) cast back to void(*)(int) and call:");
    back(42);

    /* ③ int(struct uart *) 를 int(void *) 로 호출 — ABI 상 "우연히" 동작하지만 UB */
    struct uart u = { 7 };
    rx_generic_t volatile g = (rx_generic_t)uart_rx;
    printf("3) int(struct uart*) via int(*)(void*): %d\n", g(&u));
    /* ④ 올바른 방법: 콜백 자체를 void * 를 받도록 만들고 안에서 변환 */
    rx_generic_t volatile h = uart_rx_ok;
    printf("4) int(void*) callback, converts inside: %d\n", h(&u));
    return 0;
}
