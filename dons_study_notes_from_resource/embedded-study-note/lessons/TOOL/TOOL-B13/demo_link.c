/* TOOL-B13 demo_link.c — 링크까지 하면 "포맷하는 로그" 는 vsnprintf 를 끌고 온다 (VM, arm-none-eabi-gcc)
 *   기본       : log_emit = vsnprintf 로 한 줄을 만든다
 *   -DTOKENIZED: tlog_put = (포맷 주소, 인자 2개) 12 B 를 RAM 링에 저장만 한다
 * demo_size.sh 의 7번 절이 nano.specs/nosys.specs 로 링크해 arm-none-eabi-size 를 찍는다.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

volatile char g_tx[128];
#ifndef TOKENIZED
__attribute__((format(printf, 1, 2)))
static void log_emit(const char *fmt, ...) {
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    for (int i = 0; i < n && i < 64; i++) g_tx[i] = buf[i];
}
#define LOG(fmt, a, b) log_emit(fmt, (int)(a), (int)(b))
#else
typedef struct { const char *fmt; int32_t a, b; } rec_t;
static volatile rec_t g_ring[16];
static volatile unsigned g_n;
static void tlog_put(const char *fmt, int32_t a, int32_t b) {
    unsigned i = g_n++ % 16u;
    g_ring[i].fmt = fmt; g_ring[i].a = a; g_ring[i].b = b;
}
#define LOG(fmt, a, b) tlog_put(fmt, (int32_t)(a), (int32_t)(b))
#endif

int main(void) {
    for (int i = 0; i < 4; i++)
        LOG("adc ch%d = %d", i, 1000 + i);
    LOG("boot reason %d, wdt %d", 3, 0);
    return 0;
}
