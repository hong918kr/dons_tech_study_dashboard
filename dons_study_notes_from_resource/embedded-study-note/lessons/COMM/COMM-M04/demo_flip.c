/* COMM-M04 demo_flip.c — 비트 오류를 넣었을 때 수신기가 몇 개를 놓치나 (스터핑 + CRC-15 + 폼 검사)
 *
 * solution.c 의 인코더/디코더를 그대로 쓴다 (main 은 이름을 바꿔 숨김).
 * 버스 관점 프레임(ACK slot dominant) + 뒤 idle 11비트에 오류를 넣고 디코드:
 *   detected   = 오류 코드 (STUFF / CRC / FORM / TRUNC)
 *   same       = OK 이고 내용이 같음 (ACK slot · EOF 7번째 비트처럼 내용과 무관한 비트)
 *   undetected = OK 인데 내용이 다름  ← 진짜 사고
 *   crc-only   = 고정 필드(CRC delim / ACK delim / EOF) 검사를 빼면 놓쳤을 것 (폼 검사의 기여)
 * 1) 단일 비트: 프레임 3000개 × 모든 위치
 * 2) 두 비트: 프레임 1500개 × 모든 위치 쌍 (스터핑 때문에 CRC 의 HD 6 가 깨지는지)
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_flip.c -o demo_flip && ./demo_flip
 */
#define main solution_main
#include "solution.c"
#undef main

typedef struct { long total, detected, same, undetected, crc_only; long by[6]; } tally;

static void classify(tally *t, const uint8_t *buf, int n, const can_frame_t *f) {
    can_rx_t rx;
    can_status st = can_decode(buf, (size_t)n, &rx);
    t->total++;
    t->by[st]++;
    bool differs = !frame_eq(&rx.f, f);
    if (st == CAN_OK) {
        if (differs) t->undetected++; else t->same++;
    } else {
        t->detected++;
        if (st == CAN_ERR_FORM && rx.err_pos > rx.crc_end && rx.crc_end > 0 && differs) t->crc_only++;
    }
}

static void report(const char *name, const tally *t) {
    printf("%-22s %10ld  det %10ld (stuff %9ld crc %9ld form %8ld trunc %ld)  same %7ld  UNDET %ld  crc-only-would-miss %ld\n",
           name, t->total, t->detected, t->by[CAN_ERR_STUFF], t->by[CAN_ERR_CRC], t->by[CAN_ERR_FORM],
           t->by[CAN_ERR_TRUNC], t->same, t->undetected, t->crc_only);
}

int main(void) {
    uint8_t wire[CAN_MAX_WIRE + 16], buf[CAN_MAX_WIRE + 16];
    uint32_t s = 0x5EEDu;
    tally t1 = { 0 }, t2 = { 0 };
    int shown = 0;
    for (int fr = 0; fr < 3000; fr++) {
        can_frame_t f = rand_frame(&s);
        int w = can_encode(&f, true, wire, sizeof wire, NULL);
        if (w < 0) return 1;
        memset(wire + w, REC, 11);
        int n = w + 11;
        for (int p = 0; p < w; p++) {
            memcpy(buf, wire, (size_t)n);
            buf[p] ^= 1u;
            classify(&t1, buf, n, &f);
        }
        if (fr >= 1500) continue;
        for (int p = 0; p < w; p++)
            for (int q = p + 1; q < w; q++) {
                memcpy(buf, wire, (size_t)n);
                buf[p] ^= 1u; buf[q] ^= 1u;
                long before = t2.undetected;
                classify(&t2, buf, n, &f);
                if (t2.undetected > before && shown < 3) {
                    can_rx_t rx;
                    can_decode(buf, (size_t)n, &rx);
                    int ns_tx = 0;
                    can_encode(&f, true, wire, sizeof wire, &ns_tx);
                    printf("undetected 2-bit example: flips @%d,@%d (stuff bits sent %d, seen by rx %d)\n", p, q, ns_tx, rx.nstuff);
                    printf("  sent    %s 0x%08X DLC %2u data", f.ide ? "ext" : "std", (unsigned)f.id, f.dlc);
                    for (int i = 0; i < can_data_len(f.rtr, f.dlc); i++) printf(" %02X", f.data[i]);
                    printf("\n  decoded %s 0x%08X DLC %2u data", rx.f.ide ? "ext" : "std", (unsigned)rx.f.id, rx.f.dlc);
                    for (int i = 0; i < can_data_len(rx.f.rtr, rx.f.dlc); i++) printf(" %02X", rx.f.data[i]);
                    printf("  CRC 0x%04X\n", (unsigned)get_bits(rx.ub, rx.need - 15, 15));
                    printf("  sent: "); print_bits(wire, w);
                    printf("  recv: "); print_bits(buf, w);
                    shown++;
                }
            }
    }
    report("1-bit (3000 frames)", &t1);
    report("2-bit (1500 frames)", &t2);
    printf("2-bit undetected rate = %.3g per error pattern\n", (double)t2.undetected / (double)t2.total);
    return 0;
}
