/* COMM-M08 추가 연습 — 한 번에 검증
 *
 * 연습 1: 길이 → DLC 를 65칸 표(LUT)로 (Linux can_fd_len2dlc 와 같은 방식), 반복문 버전과 전수 비교
 * 연습 2: 패딩 낭비 — 길이 0~64 의 패딩 바이트, 최대 낭비 길이, 총합
 * 연습 3: stuff count 필드 — Gray 코드 3비트 + 짝수 패리티, 왕복과 1비트 오류 검출 (전수)
 * 연습 4: 바이트당 시간 손익분기 — Classic 8 B @500k 보다 바이트당 싼 가장 짧은 FD 길이 (BRS 켜고 / 끄고)
 * 연습 5: Classic 전용 노드가 FD 헤더를 읽으면 — "유령 DLC"와 수신 정책 (Classic / FD-tolerant / FD)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static int near(double a, double b, double tol) { return a - b <= tol && b - a <= tol; }

static const uint8_t fd_dlc2len_tab[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 };

/* 기준 구현 (solution.c 와 같은 반복문) */
static int len2dlc_loop(unsigned len) {
    if (len > 64) return -1;
    if (len <= 8) return (int)len;
    int d = 9;
    while (fd_dlc2len_tab[d] < len) d++;
    return d;
}

/* ---- 연습 1: 65칸 LUT. 컴파일 시간 상수 표 → 분기 없는 O(1), ISR 에서 쓰기 좋다 ---- */
static const uint8_t len2dlc_lut[65] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8,                                   /* 0..8 */
    9, 9, 9, 9,                                                  /* 9..12 */
    10, 10, 10, 10,                                              /* 13..16 */
    11, 11, 11, 11,                                              /* 17..20 */
    12, 12, 12, 12,                                              /* 21..24 */
    13, 13, 13, 13, 13, 13, 13, 13,                              /* 25..32 */
    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,   /* 33..48 */
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,   /* 49..64 */
};
static int len2dlc_fast(unsigned len) { return len > 64 ? -1 : len2dlc_lut[len]; }

/* ---- 연습 2: 패딩 낭비 ---- */
static unsigned pad_waste(unsigned len) { return len > 64 ? 0 : fd_dlc2len_tab[len2dlc_fast(len)] - len; }

/* ---- 연습 3: stuff count (SBC) = Gray(동적 스터프 수 mod 8) 3비트 + 짝수 패리티 (ISO 11898-1 확인 권장) ---- */
static unsigned sbc_encode(unsigned stuff_count) {
    unsigned g = (stuff_count & 7u) ^ ((stuff_count & 7u) >> 1);
    unsigned par = ((g >> 2) ^ (g >> 1) ^ g) & 1u;
    return (g << 1) | par;
}
/* 반환: 0..7 (stuff count mod 8) 또는 -1 (패리티 오류) */
static int sbc_decode(unsigned sbc) {
    unsigned g = (sbc >> 1) & 7u, par = sbc & 1u;
    if ((((g >> 2) ^ (g >> 1) ^ g) & 1u) != par) return -1;
    unsigned b = g;                                  /* Gray → 이진: b = g ^ g>>1 ^ g>>2 */
    b ^= b >> 1; b ^= b >> 2;
    return (int)(b & 7u);
}

/* ---- 연습 4: 최악 스터핑 시간 (solution.c 의 계산기를 표준 ID 데이터 프레임만으로 축약) ---- */
static int stuff_max(int k) { return k > 1 ? (k - 1) / 4 : 0; }

static double worst_us(int fd, int brs, unsigned len, uint32_t nom, uint32_t dat) {
    int data = 8 * (int)len;
    if (!fd) return (47 + data + stuff_max(34 + data)) * (1e6 / nom);
    int fixed = (len <= 16 ? 62 : 67) + data, dyn = 22 + data;
    if (!brs) return (fixed + stuff_max(dyn)) * (1e6 / nom);
    int s_nom = stuff_max(16);
    return (29 + s_nom) * (1e6 / nom) + (fixed - 29 + stuff_max(dyn) - s_nom) * (1e6 / dat);
}

/* Classic 8 B @nom 보다 바이트당 시간이 짧은 가장 작은 FD 길이 (DLC 표의 길이 중). 없으면 -1. */
static int breakeven_len(int brs, uint32_t nom, uint32_t dat) {
    double ref = worst_us(0, 0, 8, nom, nom) / 8.0;
    for (int d = 1; d < 16; d++) {
        unsigned L = fd_dlc2len_tab[d];
        if (worst_us(1, brs, L, nom, dat) / L < ref) return (int)L;
    }
    return -1;
}

/* ---- 연습 5: Classic 전용 노드가 보는 FD 헤더 ----
 * FD 표준 헤더: SOF ID11 RRS IDE FDF res BRS ESI DLC4. Classic 디코더는 FDF 를 r0 로 받아들이고(예약 비트는
 * 값과 상관없이 수용) 다음 4비트(res BRS ESI DLC3)를 DLC 로 읽는다 → "유령 DLC". */
static int phantom_dlc(int brs, int esi, int dlc) { return (0 << 3) | (brs << 2) | (esi << 1) | ((dlc >> 3) & 1); }

enum { RX_RECEIVE = 0, RX_ERROR_FRAME = 1, RX_PROTOCOL_EXCEPTION = 2 };
enum { NODE_CLASSIC = 0, NODE_FD_TOLERANT = 1, NODE_FD = 2 };
/* fdf = 수신한 FDF 자리 비트, res = 그다음 비트 (FD 에서), pex = 프로토콜 예외 기능 켜짐 */
static int rx_policy(int node, int fdf, int res, int pex) {
    if (!fdf) return RX_RECEIVE;                         /* Classic 프레임은 모두 받는다 */
    if (node == NODE_CLASSIC) return RX_ERROR_FRAME;     /* 데이터 위상을 nominal 로 샘플 → 스터프/CRC/폼 오류 */
    if (node == NODE_FD_TOLERANT) return RX_PROTOCOL_EXCEPTION;
    if (res) return pex ? RX_PROTOCOL_EXCEPTION : RX_ERROR_FRAME;   /* res=1: 미래 형식 (CAN XL 의 XLF 자리) */
    return RX_RECEIVE;
}

int main(void) {
    /* 연습 1 */
    int ok = 1;
    for (unsigned l = 0; l <= 70; l++) ok &= len2dlc_fast(l) == len2dlc_loop(l);
    CHECK(ok);
    CHECK(sizeof len2dlc_lut == 65 && len2dlc_fast(64) == 15 && len2dlc_fast(65) == -1);

    /* 연습 2 */
    unsigned worst = 0, worst_len = 0, total = 0, exact = 0;
    for (unsigned l = 0; l <= 64; l++) {
        unsigned w = pad_waste(l);
        total += w;
        if (w == 0) exact++;
        if (w > worst) { worst = w; worst_len = l; }
    }
    printf("연습 2: 최대 낭비 %u B (len %u), 총 %u B, 패딩 없는 길이 %u개, 평균 %.2f B\n", worst, worst_len, total, exact, total / 65.0);
    CHECK(worst == 15 && worst_len == 33);   /* 33 → 48: 15 B (49 → 64 도 15 B, 처음 나오는 것은 33) */
    CHECK(total == 292 && exact == 16);
    CHECK(pad_waste(9) == 3 && pad_waste(12) == 0 && pad_waste(49) == 15 && pad_waste(25) == 7);

    /* 연습 3 */
    ok = 1;
    for (unsigned s = 0; s < 64; s++) ok &= sbc_decode(sbc_encode(s)) == (int)(s & 7u);
    CHECK(ok);
    ok = 1;
    for (unsigned s = 0; s < 8; s++)
        for (int b = 0; b < 4; b++) ok &= sbc_decode(sbc_encode(s) ^ (1u << b)) == -1;   /* 1비트 오류는 전부 검출 */
    CHECK(ok);
    ok = 1;
    for (unsigned s = 0; s < 8; s++) {                    /* Gray: 이웃 값은 1비트만 다르다 (mod 8 순환) */
        unsigned a = sbc_encode(s) >> 1, b = sbc_encode((s + 1) & 7u) >> 1, x = a ^ b;
        ok &= x != 0 && (x & (x - 1)) == 0;
    }
    CHECK(ok);
    CHECK(sbc_encode(0) == 0x0 && sbc_encode(1) == 0x3 && sbc_encode(2) == 0x6 && sbc_encode(7) == 0x9);

    /* 연습 4 */
    CHECK(near(worst_us(0, 0, 8, 500000, 500000), 270.0, 1e-9));
    CHECK(near(worst_us(1, 1, 64, 500000, 2000000), 404.0, 1e-9));
    CHECK(near(worst_us(1, 0, 8, 500000, 500000), 294.0, 1e-9));     /* BRS 없는 FD 8 B 는 Classic 보다 느리다 */
    int be1 = breakeven_len(1, 500000, 2000000), be0 = breakeven_len(0, 500000, 500000);
    printf("연습 4: Classic 8 B @500k 바이트당 %.2f us. FD 500k/2M 은 %d B 부터, BRS 없는 FD 는 %d B 부터 더 싸다\n",
           270.0 / 8, be1, be0);
    CHECK(be1 == 3 && be0 == 12);
    CHECK(near(worst_us(1, 1, 3, 500000, 2000000), 96.5, 1e-9) && near(worst_us(1, 0, 12, 500000, 500000), 374.0, 1e-9));
    /* 64 B 를 Classic 8 프레임 vs FD 한 프레임 (500k/2M, 최악) */
    double cc = 8 * worst_us(0, 0, 8, 500000, 500000), fd = worst_us(1, 1, 64, 500000, 2000000);
    printf("연습 4: 64 B — Classic 8 프레임 %.1f us vs FD 1 프레임 %.1f us (%.2f배)\n", cc, fd, cc / fd);
    CHECK(near(cc, 2160.0, 1e-9) && cc / fd > 5.3 && cc / fd < 5.4);

    /* 연습 5 */
    CHECK(phantom_dlc(1, 0, 15) == 5);          /* BRS=1 ESI=0 DLC=1111 → r0 다음 4비트 0101 */
    CHECK(phantom_dlc(0, 0, 8) == 1 && phantom_dlc(1, 1, 0) == 6);
    ok = 1;
    for (int brs = 0; brs < 2; brs++)
        for (int esi = 0; esi < 2; esi++)
            for (int d = 0; d < 16; d++) ok &= phantom_dlc(brs, esi, d) <= 7;   /* res=0 이라 항상 0..7 */
    CHECK(ok);
    CHECK(rx_policy(NODE_CLASSIC, 0, 0, 0) == RX_RECEIVE && rx_policy(NODE_CLASSIC, 1, 0, 0) == RX_ERROR_FRAME);
    CHECK(rx_policy(NODE_FD_TOLERANT, 1, 0, 0) == RX_PROTOCOL_EXCEPTION && rx_policy(NODE_FD, 1, 0, 0) == RX_RECEIVE);
    CHECK(rx_policy(NODE_FD, 1, 1, 1) == RX_PROTOCOL_EXCEPTION && rx_policy(NODE_FD, 1, 1, 0) == RX_ERROR_FRAME);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
