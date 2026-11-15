/* COMM-A07 추가 연습 — 한 번에 검증
 *  연습 1: 프레임 수 닫힌 식 vs 직접 세는 분할기 (L 1..4095 × BS 0..32 전수)
 *  연습 2: extended addressing (첫 바이트 = N_TA) 에서 프레임 수와 시간
 *  연습 3: 처리 시간 t µs 를 보장하는 가장 작은 STmin 바이트 (전수 대조)
 *  연습 4: Linux can-isotp 실제 캡처(candump -L) 를 독립 재조립기로 디코드 + 타임스탬프로 간격 확인
 *  연습 5: UDS 플래시 다운로드 256 KiB 의 ISO-TP 전송 시간 (응답 SF 포함)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------- 공통: 본문 모델 축약판 ---------- */
static uint32_t stmin_us(uint8_t raw)
{
    if (raw <= 0x7F) return raw * 1000u;
    if (raw >= 0xF1 && raw <= 0xF9) return (uint32_t)(raw - 0xF0) * 100u;
    return 127000u;
}
static int frame_bits(int dlc, bool worst)          /* 표준 데이터 프레임 + IFS 3 (COMM-M04) */
{
    int n = dlc > 8 ? 8 : dlc, b = 47 + 8 * n;
    return worst ? b + (34 + 8 * n - 1) / 4 : b;
}
/* 일반화: 프레임당 데이터 (SF 최대 sf, FF ff, CF cf) — normal 7/6/7, extended 6/5/6. 패딩 DLC 8 */
static double xfer_us(int len, int sf, int ff, int cf, int bs, uint8_t st, uint32_t br, bool worst)
{
    double fb = frame_bits(8, worst) * 1e6 / br;
    if (len <= sf) return fb;
    int ncf = (len - ff + cf - 1) / cf, nfc = bs ? (ncf + bs - 1) / bs : 1;
    return (1 + ncf + nfc) * fb + (double)(ncf - nfc) * stmin_us(st);
}

/* ---------- 연습 1 ---------- */
static int frames_formula(int len, int bs)
{
    if (len <= 7) return 1;
    int ncf = (len - 6 + 6) / 7;
    return 1 + ncf + (bs ? (ncf + bs - 1) / bs : 1);
}
static int frames_count(int len, int bs)            /* 송신자처럼 한 프레임씩 센다 */
{
    if (len <= 7) return 1;
    int frames = 2, off = 6, inblock = 0;            /* FF + 첫 FC */
    while (off < len) {
        off += 7; frames++;
        if (bs && ++inblock == bs && off < len) { frames++; inblock = 0; }   /* 블록 끝, 남았으면 FC */
    }
    return frames;
}

/* ---------- 연습 3 ---------- */
static uint8_t stmin_for_us(uint32_t us)            /* us 이상을 보장하는 가장 짧은 STmin. 127 ms 초과는 표현 불가 → 0x7F */
{
    if (us == 0) return 0x00;
    if (us <= 900) return (uint8_t)(0xF0 + (us + 99) / 100);
    if (us <= 127000) return (uint8_t)((us + 999) / 1000);
    return 0x7F;
}

/* ---------- 연습 4: demo_linux.sh 의 실제 출력 (K2, 40 B, BS=2, STmin=0xF5) ---------- */
static const char *const CAP_K2[] = {
    "(1794456424.791984) vcan0 7E0#102800070E151C23", "(1794456424.792009) vcan0 7E8#3002F5CCCCCCCCCC",
    "(1794456424.792011) vcan0 7E0#212A31383F464D54", "(1794456424.792568) vcan0 7E0#225B626970777E85",
    "(1794456424.792581) vcan0 7E8#3002F5CCCCCCCCCC", "(1794456424.792582) vcan0 7E0#238C939AA1A8AFB6",
    "(1794456424.793212) vcan0 7E0#24BDC4CBD2D9E0E7", "(1794456424.793222) vcan0 7E8#3002F5CCCCCCCCCC",
    "(1794456424.793224) vcan0 7E0#25EEF5FC030A11CC",
};
typedef struct { long long t_us; unsigned id; int dlc; uint8_t d[8]; } capline;
static bool parse_cap(const char *s, capline *c)
{
    long long sec, usec; char frame[40];
    if (sscanf(s, "(%lld.%lld) %*s %39s", &sec, &usec, frame) != 3) return false;
    c->t_us = sec * 1000000LL + usec;
    char *p; c->id = (unsigned)strtoul(frame, &p, 16);
    if (*p++ != '#') return false;
    c->dlc = 0;
    while (c->dlc < 8 && p[0] && p[1]) { char h[3] = { p[0], p[1], 0 }; c->d[c->dlc++] = (uint8_t)strtoul(h, NULL, 16); p += 2; }
    return true;
}

int main(void)
{
    /* 연습 1 */
    int bad = 0;
    for (int len = 1; len <= 4095; len++)
        for (int bs = 0; bs <= 32; bs++) if (frames_formula(len, bs) != frames_count(len, bs)) bad++;
    printf("ex1 frames(4095): BS=0 %d, BS=8 %d, BS=32 %d, BS=1 %d; formula vs counter mismatches %d (135135 cases)\n",
           frames_formula(4095, 0), frames_formula(4095, 8), frames_formula(4095, 32), frames_formula(4095, 1), bad);
    CHECK(bad == 0);
    CHECK(frames_formula(4095, 0) == 587 && frames_formula(4095, 8) == 660 && frames_formula(4095, 1) == 1171);

    /* 연습 2 */
    double tn = xfer_us(4095, 7, 6, 7, 0, 0, 500000, false), te = xfer_us(4095, 6, 5, 6, 0, 0, 500000, false);
    int fe = 1 + (4095 - 5 + 5) / 6 + 1;
    printf("ex2 4095 B @500k BS=0: normal %.2f ms (587 frames), extended %.2f ms (%d frames), +%.1f%%\n",
           tn / 1000, te / 1000, fe, (te / tn - 1) * 100);
    CHECK(fe == 684 && te > tn && (int)(te + 0.5) == 684 * 222);

    /* 연습 3 */
    int wrong = 0;
    for (uint32_t us = 0; us <= 127000; us++) {
        uint32_t best = 0xFFFFFFFFu; int braw = -1;
        for (int r = 0; r < 256; r++) {
            if (!(r <= 0x7F || (r >= 0xF1 && r <= 0xF9))) continue;      /* 예약값은 후보에서 뺀다 */
            uint32_t v = stmin_us((uint8_t)r);
            if (v >= us && v < best) { best = v; braw = r; }
        }
        if (stmin_for_us(us) != braw) wrong++;
    }
    printf("ex3 stmin_for_us: 350us->0x%02X 901us->0x%02X 950us->0x%02X 1001us->0x%02X 200ms->0x%02X; brute-force mismatches %d\n",
           stmin_for_us(350), stmin_for_us(901), stmin_for_us(950), stmin_for_us(1001), stmin_for_us(200000), wrong);
    CHECK(wrong == 0);
    CHECK(stmin_for_us(350) == 0xF4 && stmin_for_us(901) == 0x01 && stmin_for_us(1001) == 0x02);

    /* 연습 4 */
    uint8_t buf[64]; int len = -1, off = 0, sn = 1, ok = 1;
    long long last_cf_end = -1, min_cf_gap = 1LL << 40, max_fc_to_cf = -1, fc_t = -1;
    for (size_t i = 0; i < sizeof CAP_K2 / sizeof CAP_K2[0]; i++) {
        capline c;
        if (!parse_cap(CAP_K2[i], &c)) { ok = 0; continue; }
        int t = c.d[0] >> 4;
        if (c.id == 0x7E8 && t == 3) { fc_t = c.t_us; last_cf_end = -1; continue; }
        if (t == 1) { len = (c.d[0] & 15) << 8 | c.d[1]; memcpy(buf, c.d + 2, 6); off = 6; sn = 1; continue; }
        if (t == 2) {
            if ((c.d[0] & 15) != sn || len < 0) { ok = 0; break; }
            int n = len - off < 7 ? len - off : 7;
            memcpy(buf + off, c.d + 1, (size_t)n); off += n; sn = (sn + 1) & 15;
            if (last_cf_end >= 0 && c.t_us - last_cf_end < min_cf_gap) min_cf_gap = c.t_us - last_cf_end;
            if (last_cf_end < 0 && fc_t >= 0 && c.t_us - fc_t > max_fc_to_cf) max_fc_to_cf = c.t_us - fc_t;
            last_cf_end = c.t_us;
        }
    }
    for (int i = 0; i < len && ok; i++) if (buf[i] != (uint8_t)(i * 7)) ok = 0;
    printf("ex4 Linux capture K2: reassembled %d/%d B ok=%d, min CF->CF gap in block %lld us (STmin 500), max FC->first CF %lld us\n",
           off, len, ok, min_cf_gap, max_fc_to_cf);
    CHECK(ok && len == 40 && off == 40);
    CHECK(min_cf_gap >= 500 && max_fc_to_cf < 100);

    /* 연습 5: 256 KiB, TransferData 0x36 + BSC (2 B) + 4093 B 데이터 = ISO-TP 4095 B, 응답 0x76 BSC = SF 1개 */
    const int img = 256 * 1024, per = 4093;
    int chunks = (img + per - 1) / per, lastd = img - per * (chunks - 1);
    static const struct { uint8_t bs, st; bool worst; } CF5[3] = { { 0, 0x00, false }, { 8, 0x01, false }, { 0, 0x00, true } };
    double tot[3];
    for (int k = 0; k < 3; k++) {
        double t = 0;
        for (int c = 0; c < chunks; c++) {
            int l = 2 + (c == chunks - 1 ? lastd : per);
            t += xfer_us(l, 7, 6, 7, CF5[k].bs, CF5[k].st, 500000, CF5[k].worst) + frame_bits(8, CF5[k].worst) * 2.0;
        }
        tot[k] = t;
    }
    printf("ex5 256 KiB in %d TransferData (last %d B data) @500k: BS=0/STmin=0 %.2f s, BS=8/STmin=1ms %.2f s, "
           "BS=0 worst-stuff %.2f s (ECU flash time not included)\n", chunks, lastd, tot[0] / 1e6, tot[1] / 1e6, tot[2] / 1e6);
    CHECK(chunks == 65 && lastd == 192 && tot[0] < tot[2] && tot[1] > 4 * tot[0]);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
