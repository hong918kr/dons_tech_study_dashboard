/* demo_vendor_macro.c — 항목 이름이 다른 헤더의 매크로와 같을 때
 *   cc -std=c11 -Wall -Wextra demo_vendor_macro.c -o demo_vendor_macro && ./demo_vendor_macro
 *   cc -std=c11 -E -P demo_vendor_macro.c | tail -n 12      # 확장 결과 직접 보기
 */
#include <stdio.h>

#define TIMEOUT 1000u            /* 드라이버 헤더의 "타임아웃 ms" */
#define BUSY    (1u << 7)        /* 벤더 헤더의 상태 비트 */

#define ERR_LIST(X) X(OK, 0) X(TIMEOUT, -1) X(BUSY, -2)

#define STR(x)  #x
#define XSTR(x) STR(x)           /* "인자를 먼저 펼친 뒤 문자열화" 하는 흔한 도우미 */

/* ① 직접 #n : 매개변수가 # 의 피연산자이면 인자는 펼쳐지지 않는다 (C11 6.10.3.1p1) */
#define X_NAME_DIRECT(n, v) #n,
/* ② 한 단계 거쳐 문자열화 : 중간 매크로에 넘길 때 인자가 먼저 완전히 펼쳐진다 */
#define X_NAME_FWD(n, v) XSTR(n),
/* ③ ## 로 붙인 이름은 안전, 하지만 ## 없이 그대로 쓰면 펼쳐진다 */
#define X_ENUM_PASTE(n, v) E_##n = (v),

static const char *const direct[] = { ERR_LIST(X_NAME_DIRECT) };
static const char *const fwd[]    = { ERR_LIST(X_NAME_FWD) };
enum { ERR_LIST(X_ENUM_PASTE) };

int main(void) {
    for (int i = 0; i < 3; i++) printf("direct=%-8s forwarded=%s\n", direct[i], fwd[i]);
    printf("E_TIMEOUT=%d E_BUSY=%d\n", E_TIMEOUT, E_BUSY);
    return 0;
}
