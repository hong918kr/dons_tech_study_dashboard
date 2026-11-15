/* TOOL-B13 demo_fmt.c — 포맷 검사의 함정 3가지 (컴파일만 한다: -c)
 *   PART=1  format 속성을 함수 포인터에 붙이는 위치 (typedef vs 변수 선언)
 *   PART=2  uint32_t 를 %u 로 찍기: 호스트(macOS)는 조용, arm-none-eabi 는 경고 → PRIu32
 *   PART=3  LOG_I(msg) — 리터럴이 아닌 포맷 (-Wformat-security / -Wformat-nonliteral)
 */
#include <stdint.h>
void log_emit(int lvl, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#if PART == 1
typedef void (*sink_t)(const char *fmt, ...) __attribute__((format(printf, 1, 2)));    /* (a) typedef 에 */
static void (*sink_var)(const char *fmt, ...) __attribute__((format(printf, 1, 2)));   /* (b) 변수 선언에 */
void use_sinks(sink_t a) {
    a("%s\n", 42);          /* (a) 로 부름 */
    sink_var("%s\n", 43);   /* (b) 로 부름 */
}
#elif PART == 2
void report(uint32_t sector, uint32_t ms) {
    log_emit(3, "sector %u erased in %u ms", sector, ms);
}
#ifdef HAVE_INTTYPES
#include <inttypes.h>
void report_fixed(uint32_t sector, uint32_t ms) {
    log_emit(3, "sector %" PRIu32 " erased in %" PRIu32 " ms", sector, ms);
}
#endif
#elif PART == 3
void show(const char *msg) {
    log_emit(3, msg);               /* 인자 없음 + 리터럴 아님 */
    log_emit(3, msg, 1);            /* 인자 있음 + 리터럴 아님 */
    log_emit(3, "%s", msg);         /* 올바른 방법 */
}
#endif
