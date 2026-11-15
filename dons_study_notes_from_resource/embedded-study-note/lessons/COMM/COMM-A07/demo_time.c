/* COMM-A07 demo_time.c — 4095 바이트 ISO-TP 전송 시간: 공식(isotp_transfer_us) vs 시뮬레이션, 버스 부하의 영향
 * solution.c 를 그대로 include 해서 같은 모델을 쓴다 (main 만 이름을 바꿈). tick = 1 µs, 프레임 시간 = 비트 수 × 비트 시간.
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_time.c -o demo_time && ./demo_time      (레슨 폴더 안에서)
 */
#define main solution_main
#include "solution.c"
#undef main

/* 시뮬레이션: S → R 한 번, tick 1 µs. load_k > 0 이면 ID 0x100 노드가 1 ms 마다 DLC 8 프레임 load_k 개를 넣는다 */
static double sim_once(uint16_t len, uint8_t bs, uint8_t st, uint32_t br, bool pad, bool worst, int load_k)
{
    static isotp_link motor;
    duo_init(1, br, pad, bs, st, ISOTP_MAX);
    D.b.worst_stuff = worst;
    if (load_k) {
        link_init(&motor, cfg_make(0x100, 0x7FF, 1, true, 0, 0), NULL, 0);
        bus_add(&D.b, &motor);
    }
    for (int i = 0; i < len; i++) SRC[i] = (uint8_t)(i * 31 + 7);
    tx_start(&D.s, SRC, len, 0);
    while (D.r.rx_ind[N_OK] + D.r.rx_ind[N_TIMEOUT_CR] == 0 && D.b.now < 400000000u) {
        if (load_k && D.b.now % 1000 == 0)
            for (int k = 0; k < load_k; k++) raw_send(&motor, "100#0102030405060708");
        sim_step(&D.b);
    }
    return D.r.rx_ind[N_OK] == 1 && memcmp(D.rbuf, SRC, len) == 0 ? (double)D.r.rx_last_at : -1.0;
}

int main(void)
{
    static const uint8_t BS[3] = { 0, 8, 32 };
    static const uint8_t ST[6] = { 0x00, 0xF5, 0x01, 0x05, 0x14, 0x7F };
    int mism = 0;
    printf("4095 B @ 500 kbit/s, std ID, DLC 8 + 0xCC padding (CF 585, frame 111 bits = 222 us no-stuff / 135 bits = 270 us worst)\n");
    printf(" BS  STmin   STmin_us  frames | no-stuff ms  sim ms  | worst ms | payload kB/s (no-stuff)\n");
    for (int b = 0; b < 3; b++)
        for (int s = 0; s < 6; s++) {
            int ncf = 585, nfc = BS[b] ? (ncf + BS[b] - 1) / BS[b] : 1;
            double f = isotp_transfer_us(4095, BS[b], ST[s], 500000, true, false);
            double w = isotp_transfer_us(4095, BS[b], ST[s], 500000, true, true);
            double sim = sim_once(4095, BS[b], ST[s], 500000, true, false, 0);
            if (sim != f) mism++;
            printf(" %2u   0x%02X  %8u  %6d | %10.2f  %7.2f | %8.2f | %8.2f\n", BS[b], ST[s], stmin_to_us(ST[s]),
                   1 + ncf + nfc, f / 1000, sim / 1000, w / 1000, 4095.0 / (f / 1e6) / 1000);
        }
    printf("\nBS=0 STmin=0, by bitrate (padding; unpadded in brackets)\n");
    static const uint32_t BR[4] = { 125000, 250000, 500000, 1000000 };
    for (int i = 0; i < 4; i++) {
        double f = isotp_transfer_us(4095, 0, 0, BR[i], true, false), u = isotp_transfer_us(4095, 0, 0, BR[i], false, false);
        double w = isotp_transfer_us(4095, 0, 0, BR[i], true, true);
        double sim = sim_once(4095, 0, 0, BR[i], true, false, 0);
        if (sim != f) mism++;
        printf("  %4u kbit/s: %8.2f ms [%8.2f]  worst %8.2f ms  -> %6.2f kB/s payload, %4.1f%% of raw bitrate\n",
               BR[i] / 1000, f / 1000, u / 1000, w / 1000, 4095.0 / (f / 1e6) / 1000, 4095.0 * 8 / (f / 1e6) / BR[i] * 100);
    }
    printf("\nsame transfer (BS=0 STmin=0, 500k) while ID 0x100 sends k DLC-8 frames every 1 ms (higher priority)\n");
    double base = 0;
    for (int k = 0; k <= 4; k++) {
        double sim = sim_once(4095, 0, 0, 500000, true, false, k);
        if (k == 0) base = sim;
        printf("  k=%d (bus load from 0x100 = %3.0f%%): %8.2f ms  (x%.2f)\n", k, k * 222.0 / 1000 * 100, sim / 1000, sim / base);
    }
    printf("\nformula/sim mismatches: %d\n", mism);
    return mism != 0;
}
