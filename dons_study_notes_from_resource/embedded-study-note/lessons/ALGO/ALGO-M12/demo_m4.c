/* ALGO-M12 demo_m4 — 파서를 Cortex-M4 / Cortex-M0 용으로 컴파일해 코드 크기·스택·64비트 나눗셈을 본다 (컴파일만).
 * solution.c 의 함수는 static 이라 쓰이지 않으면 사라지므로 바깥으로 노출하는 얇은 래퍼를 둔다.
 *
 *   SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os -fstack-usage \
 *         -isystem $SDK/usr/include -c demo_m4.c -o /tmp/ALGO-M12_m4.o
 *   cat /tmp/ALGO-M12_m4.su ; llvm-nm --size-sort -S /tmp/ALGO-M12_m4.o ; llvm-objdump -d /tmp/ALGO-M12_m4.o | grep 'bl\b'
 *   (Cortex-M0: --target=thumbv6m-none-eabi -mcpu=cortex-m0)
 * 맥에서 그냥 실행해도 된다: cc -std=c11 -Wall -Wextra -Werror -O2 demo_m4.c -o demo_m4 && ./demo_m4
 */
#define ALGO_M12_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"
#include "solution.c"

nmea_ev_t m4_feed(nmea_parser_t *p, uint8_t c);
bool      m4_split(char *body, char *f[], int max, int *nf);
gga_err_t m4_gga(char *const f[], int n, gga_t *out);
bool      m4_latlon(const char *v, const char *h, int dd, int32_t *out);
void      m4_gps_feed(gps_t *g, const uint8_t *buf, size_t n);

nmea_ev_t m4_feed(nmea_parser_t *p, uint8_t c) { return nmea_feed(p, c); }
bool      m4_split(char *body, char *f[], int max, int *nf) { return nmea_split(body, f, max, nf); }
gga_err_t m4_gga(char *const f[], int n, gga_t *out) { return nmea_parse_gga(f, n, out); }
bool      m4_latlon(const char *v, const char *h, int dd, int32_t *out) { return parse_latlon_e7(v, h, dd, out); }
void      m4_gps_feed(gps_t *g, const uint8_t *buf, size_t n) { gps_feed(g, buf, n); }

#if !defined(__arm__)
int main(void) {
    printf("sizeof(nmea_parser_t)=%zu  sizeof(gga_t)=%zu  sizeof(gps_t)=%zu\n", sizeof(nmea_parser_t),
           sizeof(gga_t), sizeof(gps_t));
    int32_t v = 0;
    bool ok = m4_latlon("4807.038", "N", 2, &v);
    printf("latlon(4807.038,N) -> %d (%s)\n", v, ok ? "ok" : "fail");
    return ok ? 0 : 1;
}
#endif
