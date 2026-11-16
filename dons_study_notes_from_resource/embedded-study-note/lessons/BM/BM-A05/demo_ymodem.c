/* BM-A05 데모 — 같은 폴더의 solution.c 를 포함해 두 가지 스윕을 찍는다 (테스트 아님, 관찰용).
 *  (1) 블록 크기(1K / 128) × 선 비트 오류율 × 송신기 응답 지연 → 전송 시간, 재전송, 선 속도 대비 효율
 *  (2) 지우기 정책(lazy / upfront-needed / upfront-slot) × 섹터 erase 시간 → 총 시간, 최대 ACK 지연,
 *      송신기 타임아웃, 재전송, 결과
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_ymodem.c -o demo && ./demo
 */
#define YM_NO_MAIN 1
#include "solution.c"

static uint8_t img[SLOT_SIZE];

int main(void) {
    uint32_t n = make_image(img, 65536u - 64u, 1, 7, 0);       /* 64 KB 파일 */
    printf("(1) 64 KB image, lazy erase 400 ms/16 KB, 5 seeds each (1 tick = 86.8 us)\n");
    printf("  blk   BER(1/bit)  turn(ms)  time(s)  retx/xfer  eff%%   ok\n");
    const uint32_t ppm[] = {0, 20, 100, 300, 1000};
    const uint32_t turn[] = {0, 115};                            /* 0 / 10 ms 응답 지연 */
    for (unsigned ti = 0; ti < 2; ti++)
        for (unsigned pi = 0; pi < sizeof ppm / sizeof ppm[0]; pi++)
            for (int m = 0; m < 2; m++) {
                uint64_t ticks = 0; uint32_t retx = 0, ok = 0;
                for (uint32_t seed = 1; seed <= 5; seed++) {
                    fault_t fx = fault_none(); fx.seed = seed * 2654435761u; fx.p_flip_s2r = ppm[pi];
                    fx.only_128 = m; fx.turnaround = turn[ti];
                    flash_init(&g_fl, seed);
                    ym_rc_t rc = xfer(img, n, &fx, ERASE_LAZY);
                    ok += rc == YM_OK && slot_equals(&g_fl, img, n);
                    ticks += sim_now(); retx += S.retx;
                }
                double t = (double)ticks / 5.0 / TICKS_PER_S;
                printf("  %-4s  %9.1e  %8.1f  %7.2f  %9.1f  %5.1f  %u/5\n", m ? "128" : "1K", ppm[pi] / 8e6,
                       turn[ti] * 1000.0 / TICKS_PER_S, t, retx / 5.0, 100.0 * n / TICKS_PER_S / t, (unsigned)ok);
            }

    printf("\n(2) erase policy x sector erase time, 40 KB image (3 of 6 slot sectors needed), sender timeout 10 s\n");
    printf("  policy          erase(ms)  erases  time(s)  max ACK delay(s)  snd timeouts  retx  overruns  rc\n");
    n = make_image(img, 40000, 1, 8, 0);
    const uint32_t ems[] = {400, 800, 2000, 4000};
    const char *pn[] = {"lazy", "upfront-needed", "upfront-slot"};
    fl_timing_t keep = g_fl_time;
    for (unsigned e = 0; e < 4; e++)
        for (int pol = 0; pol < 3; pol++) {
            g_fl_time.erase_ticks = ems[e] * TICKS_PER_S / 1000u;
            fault_t fx = fault_none();
            flash_init(&g_fl, 1);
            ym_rc_t rc = xfer(img, n, &fx, (erase_policy_t)pol);
            printf("  %-15s %9u  %6u  %7.2f  %16.2f  %12u  %4u  %8u  %s\n", pn[pol], (unsigned)ems[e],
                   (unsigned)g_fl.n_erase, (double)sim_now() / TICKS_PER_S, (double)S.max_stall / TICKS_PER_S,
                   (unsigned)S.s_timeouts, (unsigned)S.retx, (unsigned)S.overruns, ym_rc_name(rc));
        }
    g_fl_time = keep;
    (void)slot_check; (void)slot_hdr_blank; (void)boot_region_intact; (void)n_data_blocks; (void)sim_reset_raw;
    return 0;
}
