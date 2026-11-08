/* demo_manual.c — X-macro 없이 손으로 맞춘 enum + 문자열 표가 어떻게 어긋나는가
 *   cc -std=c11 -Wall -Wextra demo_manual.c -o demo_manual && ./demo_manual
 * 상황: 누군가 enum 중간에 ERR_NOMEM 을 추가했지만 이름 표는 안 고쳤다 (리뷰에서 흔히 놓친다).
 */
#include <stdio.h>

enum err { ERR_OK, ERR_TIMEOUT, ERR_BUSY, ERR_NOMEM /* ← 새로 추가 */, ERR_CRC, ERR_COUNT };

/* ① 위치(positional) 초기화: 컴파일러는 아무 말도 없다 */
static const char *const names_pos[] = { "OK", "TIMEOUT", "BUSY", "CRC" };
#ifdef POS_ASSERT   /* -DPOS_ASSERT: 길이 검사를 붙이면 이번 실수는 잡힌다 (순서 바뀜은 여전히 못 잡음) */
_Static_assert(sizeof names_pos / sizeof names_pos[0] == ERR_COUNT, "names_pos length != ERR_COUNT");
#endif

/* ② 지정 초기화(designated initializer) + 길이 검사: 길이는 맞아도 구멍(NULL)이 생긴다 */
static const char *const names_desig[ERR_COUNT] = {
    [ERR_OK] = "OK", [ERR_TIMEOUT] = "TIMEOUT", [ERR_BUSY] = "BUSY", [ERR_CRC] = "CRC",
};
_Static_assert(sizeof names_desig / sizeof names_desig[0] == ERR_COUNT, "length");

/* ③ enum switch, default 없음: -Wswitch(-Wall 에 포함)가 빠진 case 를 알려준다 */
static const char *name_sw(enum err e) {
    switch (e) {
    case ERR_OK: return "OK";
    case ERR_TIMEOUT: return "TIMEOUT";
    case ERR_BUSY: return "BUSY";
    case ERR_CRC: return "CRC";
    case ERR_COUNT: break;
    }
    return "?";
}

int main(void) {
    for (int e = 0; e < ERR_COUNT; e++) {
        const char *p = e < (int)(sizeof names_pos / sizeof names_pos[0]) ? names_pos[e] : "(out of table)";
        printf("code %d: positional=%-15s designated=%-8s switch=%s\n", e, p,
               names_desig[e] ? names_desig[e] : "(NULL)", name_sw((enum err)e));
    }
    return 0;
}
