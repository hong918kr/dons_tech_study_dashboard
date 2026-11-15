/* TOOL-B13 demo_variadic.c — "인자 없는 로그" LOG_X("boom") 를 받는 세 가지 방법 (-c 만)
 *   STYLE=1  GNU   : #define LOG(fmt, ...) emit(fmt, ##__VA_ARGS__)
 *   STYLE=2  C23   : #define LOG(fmt, ...) emit(fmt __VA_OPT__(,) __VA_ARGS__)
 *   STYLE=3  C11   : #define LOG(...)      emit(__VA_ARGS__)          ← 이 레슨의 선택
 *   STYLE=4  C11 순진: #define LOG(fmt, ...) emit(fmt, __VA_ARGS__)  ← 인자 없으면 깨짐
 */
void emit(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#if STYLE == 1
#define LOG(fmt, ...) emit(fmt, ##__VA_ARGS__)
#elif STYLE == 2
#define LOG(fmt, ...) emit(fmt __VA_OPT__(,) __VA_ARGS__)
#elif STYLE == 3
#define LOG(...) emit(__VA_ARGS__)
#else
#define LOG(fmt, ...) emit(fmt, __VA_ARGS__)
#endif

void f(int x) {
    LOG("x=%d", x);
    LOG("boom");
}
