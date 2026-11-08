/* demo_m4.c — X-macro 가 만든 switch 를 컴파일러가 무엇으로 바꾸나 (freestanding, libc 없음)
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -Os -S demo_m4.c -o -
 *   cc -std=c11 -O2 -S demo_m4.c -o -                                     # arm64 호스트
 */
#include <stdint.h>
#include <stddef.h>

#define FW_ERR_LIST(X)                       \
    X(OK, 0) X(TIMEOUT, -1) X(BUSY, -2) X(INVAL, -3) X(NOMEM, -4) \
    X(CRC, -10) X(NACK, -11) X(OVERRUN, -12) X(HW_FAULT, -100)

/* 값 → 이름: 희소한 음수 값의 switch */
const char *err_to_str(int code);
const char *err_to_str(int code) {
#define X_CASE(n, v) case (v): return #n;
    switch (code) { FW_ERR_LIST(X_CASE) default: return "UNKNOWN"; }
#undef X_CASE
}

/* 값 → 촘촘한 인덱스 (Part A 의 err_index) */
#define X_IDX(n, v) IDX_##n,
enum { FW_ERR_LIST(X_IDX) ERR_COUNT };
int err_index(int code);
int err_index(int code) {
#define X_CASE(n, v) case (v): return IDX_##n;
    switch (code) { FW_ERR_LIST(X_CASE) default: return -1; }
#undef X_CASE
}
