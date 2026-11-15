/* COMM-M06 demo_resync.c — 조건 2 (df ≤ SJW / (20·NBT)) 를 비트 동기화 시뮬레이션으로 확인한다.
 *
 * solution.c 의 rx_sim / resync_on_edge 를 그대로 복사했다 (모델 정의는 solution.c 주석 — ISO 11898-1 의
 * 동기화 규칙을 단순화, 확인 권장). δ = TX bitrate / RX bitrate − 1 을 0 에서 바깥으로 0.01% 씩 키우며,
 * 위상 40개 중 하나라도 샘플 오류가 나는 첫 δ (onset) 를 이론 |δ| = 2·df2 = SJW / (10·NBT) 와 비교한다.
 * 패턴: "00000 11111" 반복 (스터핑이 허용하는 가장 드문 열성→우성 에지 = 10 비트마다) 과
 *       길이 1..5 의 랜덤 런 (스터핑을 지키는 랜덤 데이터 — 에지가 더 자주 온다).
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_resync.c -o demo_resync && ./demo_resync     (약 10 초)
 */
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint32_t f_can, bitrate;            /* 입력: CAN 커널 클럭 [Hz], 목표 bitrate [bit/s] */
    uint32_t brp, tseg1, tseg2, sjw;    /* 실제 길이 (레지스터 값 + 1) */
    uint32_t ntq;                       /* NBT = 1 + tseg1 + tseg2 */
    int64_t  err_ppm;                   /* (실제 − 목표) / 목표 × 1e6, 반올림 (표시용) */
    uint32_t sp_permille;               /* 1000·(1 + tseg1) / ntq, 반올림 (표시용) */
} can_bt;

typedef struct { int pos, sp, end, resynced; long bitno; } rx_bt;

/* 열성→우성 에지가 slot rx->pos (≥ 1) 에서 보였다 (rx->resynced == 0 일 때만 불린다). 규칙:
 *   pos ≤ sp (샘플 전): 위상 오차 e = +pos  → 샘플 포인트와 비트 끝을 min(e, SJW) 만큼 뒤로 (PS1 연장).
 *   pos > sp (샘플 후): e = pos − (end + 1) < 0 → |e| ≤ SJW 면 이 slot 이 다음 비트의 SYNC_SEG
 *                       (pos = 0, sp = TSEG1, end = NBT − 1, bitno++), 아니면 end 를 SJW 만큼 앞으로 (PS2 단축).
 *   어느 경우든 resynced = 1. */
static void resync_on_edge(rx_bt *rx, int tseg1, int ntq, int sjw) {
    if (rx->pos <= rx->sp) {
        int a = rx->pos < sjw ? rx->pos : sjw;
        rx->sp += a; rx->end += a;
    } else if (rx->end + 1 - rx->pos <= sjw) {
        rx->pos = 0; rx->sp = tseg1; rx->end = ntq - 1; rx->bitno++;
    } else {
        rx->end -= sjw;
    }
    rx->resynced = 1;
}

/* bits[0..nbits) 를 TX 가 보내고 (앞에 idle 3 비트), RX 가 b 의 설정으로 받는다. 샘플 오류 수를 돌려준다.
 * 오류 = n 번째 샘플이 TX 비트 n 밖에 떨어짐 (비트 슬립) 또는 값이 다름. 루프는 시간으로 끝난다.
 * 잘못된 설정 (NBT ≠ 1 + TSEG1 + TSEG2, SJW 범위 밖, |δ| ≥ 50%) → −1. */
static long rx_sim(const can_bt *b, int32_t delta_ppm, int64_t phase, const uint8_t *bits, int nbits) {
    const int64_t Qt = 1000;
    if (!b || !bits || nbits < 2 || b->ntq < 3 || b->ntq != 1 + b->tseg1 + b->tseg2 || b->sjw < 1 || b->sjw > b->tseg2 ||
        delta_ppm <= -500000 || delta_ppm >= 500000) return -1;
    int ntq = (int)b->ntq, tseg1 = (int)b->tseg1, sjw = (int)b->sjw;
    int64_t P = ((int64_t)ntq * Qt * 1000000 * 2 / (1000000 + delta_ppm) + 1) / 2;
    int64_t t0 = 3 * ntq * Qt + phase, t_end = t0 + (int64_t)(nbits - 1) * P;
    rx_bt rx = { 0, 0, 0, 0, 0 };
    int prev = 1, running = 0;
    long errors = 0;
    for (int64_t t = Qt; t < t_end; t += Qt) {
        int64_t ti = t < t0 ? -1 : (t - t0) / P;
        int lvl = ti < 0 || ti >= nbits ? 1 : bits[ti];
        int edge = prev == 1 && lvl == 0;
        prev = lvl;
        if (!running) {
            if (edge) { running = 1; rx.pos = 0; rx.sp = tseg1; rx.end = ntq - 1; rx.bitno = 0; rx.resynced = 0; }
            continue;
        }
        if (++rx.pos > rx.end) { rx.pos = 0; rx.sp = tseg1; rx.end = ntq - 1; rx.bitno++; }
        if (edge && rx.pos != 0 && !rx.resynced) resync_on_edge(&rx, tseg1, ntq, sjw);
        if (rx.pos == rx.sp) {
            if (ti != rx.bitno || ti < 0 || lvl != bits[ti]) errors++;
            rx.resynced = 0;
        }
    }
    return errors;
}

#define NBITS 2000
static uint8_t bits[NBITS];
static uint32_t g_rng = 0x1234567u;
static uint32_t xorshift32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static void make_pattern(int random_runs) {
    int i = 0, v = 0;
    while (i < NBITS) {
        int run = random_runs ? 1 + (int)(xorshift32() % 5) : 5;
        for (int k = 0; k < run && i < NBITS; k++) bits[i++] = (uint8_t)v;
        v ^= 1;
    }
}

static int32_t onset(const can_bt *b, int sign, int random_runs) {
    for (int k = 1; k <= 400; k++) {
        int32_t d = sign * k * 100;
        for (int ph = 0; ph < 40; ph++) {
            make_pattern(random_runs);
            if (rx_sim(b, d, (int64_t)(xorshift32() % 1000), bits, NBITS) > 0) return d;
        }
    }
    return 0;
}

int main(void) {
    struct { uint32_t ntq, tseg1, tseg2, sjw; } C[] = {
        { 16, 13, 2, 1 }, { 16, 13, 2, 2 }, { 16, 11, 4, 4 }, { 8, 6, 1, 1 }, { 10, 7, 2, 2 }, { 25, 16, 8, 4 }, { 25, 16, 8, 1 },
    };
    printf("NBT TSEG1 TSEG2 SJW | theory SJW/(10*NBT) | onset 5/5 (fast TX / slow TX) | onset random runs\n");
    for (unsigned i = 0; i < sizeof C / sizeof C[0]; i++) {
        can_bt b = { 0 };
        b.ntq = C[i].ntq; b.tseg1 = C[i].tseg1; b.tseg2 = C[i].tseg2; b.sjw = C[i].sjw;
        double th = 100.0 * C[i].sjw / (10.0 * C[i].ntq);
        int32_t p5 = onset(&b, +1, 0), m5 = onset(&b, -1, 0), pr = onset(&b, +1, 1), mr = onset(&b, -1, 1);
        printf("%3u %5u %5u %3u |       %5.2f%%        |     %+5.2f%% / %+5.2f%%         |  %+5.2f%% / %+5.2f%%\n",
               C[i].ntq, C[i].tseg1, C[i].tseg2, C[i].sjw, th, p5 / 1e4, m5 / 1e4, pr / 1e4, mr / 1e4);
    }
    printf("\n16 tq (1+13+2), SJW 2, 5/5 pattern, 40 phases x %d bits: sample errors per delta\n", NBITS);
    can_bt b = { 0 };
    b.ntq = 16; b.tseg1 = 13; b.tseg2 = 2; b.sjw = 2;
    int32_t ds[] = { 10000, 12000, 12600, 13000, 15000, 20000, -12000, -12600, -13000, -20000 };
    make_pattern(0);
    for (unsigned i = 0; i < sizeof ds / sizeof ds[0]; i++) {
        long tot = 0;
        for (int ph = 0; ph < 40; ph++) tot += rx_sim(&b, ds[i], ph * 25, bits, NBITS);
        printf("  delta %+6.2f%%: %ld errors\n", ds[i] / 1e4, tot);
    }
    return 0;
}
