/* ARCH-B12 demo — 정답 모델(solution.c)로 파형과 표를 찍는다. 체크 없음, 관찰용.
 *
 *   ./demo wave     텍스트 파형: Flash(3 WS) → SRAM(0 WS), 파이프라인 vs 비파이프라인
 *   ./demo bw       256 워드 읽기 대역폭 표 (120 MHz)
 *   ./demo clock    클럭 스윕: WS · 워드당 ns · 캐시 없는 MB/s · 프리페치 순차 페치율
 *   ./demo fetch    트레이스 × WS × 페치 모드 → cycle/fetch 표
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bus.c -o demo && ./demo wave
 */
#define main solution_main          /* solution.c 의 테스트 main 은 이름만 바꿔서 같이 링크 */
#include "solution.c"
#undef main
#include <string.h>

static const char *cell(int owner, const char *tag) {
    static char buf[8][16];
    static int k;
    k = (k + 1) & 7;
    if (owner < 0) snprintf(buf[k], sizeof buf[k], "  .   ");
    else snprintf(buf[k], sizeof buf[k], "  %s%d  ", tag, owner);
    return buf[k];
}

static void print_wave(const char *title, bus_mode_t m, const xfer_t *x, size_t n) {
    cycle_t w[32];
    long c = bus_run(m, x, n, w, 32);
    printf("%s  (총 %ld 사이클)\n", title, c);
    printf("  cycle  |"); for (long i = 0; i < c; i++) printf("  %2ld  |", i); printf("\n");
    printf("  HADDR  |"); for (long i = 0; i < c; i++) printf("%s|", cell(w[i].addr_owner, "A")); printf("\n");
    printf("  DATA   |"); for (long i = 0; i < c; i++) printf("%s|", cell(w[i].data_owner, "D")); printf("\n");
    printf("  HREADY |"); for (long i = 0; i < c; i++) printf("  %d   |", w[i].hready); printf("\n\n");
}

static void demo_wave(void) {
    xfer_t x[3] = { { FLASH_BASE, 0, 4 }, { SRAM_BASE, 0, 4 }, { SRAM_BASE + 4, 0, 4 } };
    printf("A0 = Flash(3 WS) 읽기, A1/A2 = SRAM(0 WS) 읽기.  An = 주소 단계, Dn = 데이터 단계\n\n");
    print_wave("[pipelined / AHB 스타일]", BUS_PIPE, x, 3);
    print_wave("[non-pipelined / APB 스타일]", BUS_NONPIPE, x, 3);
}

static void demo_bw(void) {
    static xfer_t x[256];
    printf("256 워드(1024 B) 연속 읽기, f = 120 MHz\n");
    printf("%-7s %-8s %7s %9s %9s\n", "region", "bus", "cycles", "B/cycle", "MB/s");
    const uint32_t bases[2] = { SRAM_BASE, FLASH_BASE };
    const char *names[2] = { "SRAM", "FLASH" };
    for (int r = 0; r < 2; r++) {
        fill_reads(x, 256, bases[r], 4);
        for (int m = 1; m >= 0; m--) {
            long c = bus_run((bus_mode_t)m, x, 256, NULL, 0);
            printf("%-7s %-8s %7ld %9.3f %9.1f\n", names[r], m ? "pipe" : "nonpipe", c,
                   bw_bytes_per_cycle(1024, c), bw_mb_per_s(1024, c, 120e6));
        }
    }
}

static void demo_clock(void) {
    static uint32_t tr[1024];
    gen_seq(tr, 1024, FLASH_BASE);
    printf("t_acc = 33.333 ns 플래시 모델 (STM32F4 2.7-3.6 V 표와 같은 계단)\n");
    printf("%5s %3s %9s %13s %16s\n", "MHz", "WS", "ns/word", "MB/s(no buf)", "Mfetch/s(prefetch)");
    static const unsigned mhz[] = { 24, 30, 31, 45, 60, 61, 90, 91, 100, 120, 121, 150, 151, 168, 180 };
    for (size_t i = 0; i < sizeof mhz / sizeof mhz[0]; i++) {
        uint64_t f = (uint64_t)mhz[i] * 1000000u;
        unsigned ws = ws_for_clock(33333, f);
        fetch_stats_t s = fetch_run(FETCH_PREFETCH, ws, tr, 1024);
        printf("%5u %3u %9.2f %13.1f %16.1f\n", mhz[i], ws, (ws + 1) * 1e3 / mhz[i],
               flash_word_mbps(33333, f), (double)s.fetches / (double)s.cycles * mhz[i]);
    }
}

static void demo_fetch(void) {
    static uint32_t tr[4096];
    const char *tn[4] = { "seq", "loop8", "far", "branchy" };
    const char *mn[3] = { "NOBUF", "LINEBUF", "PREFETCH" };
    printf("cycle/fetch (낮을수록 좋다, 1.00 = 0 WS 처럼 보임). n = 4096 페치\n");
    printf("%-8s %3s %9s %9s %9s   %s\n", "trace", "WS", mn[0], mn[1], mn[2], "PREFETCH 상세");
    for (int t = 0; t < 4; t++) {
        if (t == 0) gen_seq(tr, 4096, FLASH_BASE);
        if (t == 1) gen_loop(tr, 4096, FLASH_BASE, 8);
        if (t == 2) gen_far(tr, 4096, FLASH_BASE, 10);
        if (t == 3) gen_branchy(tr, 4096, FLASH_BASE, 0xB12u);
        static const unsigned wss[] = { 0, 1, 3, 5 };
        for (size_t k = 0; k < 4; k++) {
            unsigned ws = wss[k];
            fetch_stats_t s[3];
            for (int m = 0; m < 3; m++) s[m] = fetch_run((fetch_mode_t)m, ws, tr, 4096);
            printf("%-8s %3u %9.2f %9.2f %9.2f   hit %llu pf %llu miss %llu stall %llu\n", tn[t], ws,
                   (double)s[0].cycles / 4096, (double)s[1].cycles / 4096, (double)s[2].cycles / 4096,
                   (unsigned long long)s[2].hits, (unsigned long long)s[2].pf_hits,
                   (unsigned long long)s[2].misses, (unsigned long long)s[2].stall);
        }
    }
}

int main(int argc, char **argv) {
    const char *w = argc > 1 ? argv[1] : "all";
    bool all = strcmp(w, "all") == 0;
    if (all || !strcmp(w, "wave"))  demo_wave();
    if (all || !strcmp(w, "bw"))    demo_bw();
    if (all || !strcmp(w, "clock")) { printf("\n"); demo_clock(); }
    if (all || !strcmp(w, "fetch")) { printf("\n"); demo_fetch(); }
    return 0;
}
