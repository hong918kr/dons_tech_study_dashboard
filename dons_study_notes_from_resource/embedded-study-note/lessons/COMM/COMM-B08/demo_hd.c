/* COMM-B08 demo_hd: CRC 다항식의 해밍 거리(HD)가 코드워드 길이에 따라 어떻게 떨어지는지 직접 찾는다.
 *
 * CRC 는 선형이라 "비트 i 하나만 뒤집은 오류의 신드롬" S_i = x^i mod g 만 알면 된다 (i = 코드워드 끝에서의 거리).
 * 가중치 k 오류가 미검출 ⇔ S_i 들 k 개의 XOR == 0.  길이 N 을 1비트씩 늘리며 처음으로 생기는 N 을 찾는다.
 *   k=2: S_n == S_a                      (해시: 단일 값)        → 다항식의 주기(period) + 1
 *   k=3: S_n ^ S_a == S_b                 (g(1)=0 이면 홀수 가중치는 영원히 없음)
 *   k=4: S_n ^ S_a == S_b ^ S_c          (쌍 XOR 해시, O(N^2))
 *   k=5: S_n ^ S_a ^ S_b == S_c ^ S_d    (쌍 해시 + 삼중 루프, O(N^3), N ~ 300 이면 충분)
 * 결과는 "데이터 비트 수 = N - r" 로도 출력해 Koopman 의 CRC Zoo 표기와 맞춘다.
 *
 * 2부: 비트 순서 불일치. CRC 의 다항식 순서(MSB-first 또는 반사형 LSB-first)와 와이어 순서가 다르면
 *   다항식 위치 p 는 와이어 위치 8·(p/8) + 7 − p%8 로 간다 (바이트 안에서 뒤집힘). 와이어 길이 Lw 버스트는
 *   다항식 길이 ≤ Lw + 14 이므로, 미검출 오류 = g 의 배수 e = q·g (q(0)=1, deg q ≤ 13) 를 8가지 바이트 정렬로
 *   전부 놓아 보고 와이어 길이의 최솟값을 찾으면 "가장 짧은 미검출 와이어 버스트"가 정확히 나온다 (Lw ≤ r 이면 완전: 다항식 길이 ≤ r+14 → deg q ≤ 13).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_hd.c -o demo_hd && ./demo_hd
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXN   40000          /* 신드롬 표 최대 길이 (CRC-16 주기 32767 을 덮음) */
#define HBITS  23             /* 쌍 해시: 2^23 칸 */
#define HSIZE  (1u << HBITS)

static uint32_t S[MAXN];
static uint32_t hkey[HSIZE];
static uint16_t ha[HSIZE], hb[HSIZE];
static uint8_t  hused[HSIZE];

/* S_i = x^i mod g  (g 는 x^r 항을 뺀 정규형 poly) */
static void build(uint32_t poly, int r, int n) {
    uint32_t top = 1u << (r - 1), mask = r == 32 ? 0xFFFFFFFFu : (1u << r) - 1u, s = 1;
    for (int i = 0; i < n; i++) { S[i] = s; s = (s & top) ? ((s << 1) ^ poly) & mask : (s << 1) & mask; }
}

static uint32_t hidx(uint32_t k) { return (k * 2654435761u) >> (32 - HBITS); }
static void hclear(void) { memset(hused, 0, sizeof hused); }
/* 반환: 이미 있던 칸이면 true (a,b 를 돌려줌), 없으면 넣고 false */
static bool hfind(uint32_t k, int *a, int *b) {
    for (uint32_t i = hidx(k);; i = (i + 1) & (HSIZE - 1)) {
        if (!hused[i]) return false;
        if (hkey[i] == k) { *a = ha[i]; *b = hb[i]; return true; }
    }
}
static void hput(uint32_t k, int a, int b) {
    uint32_t i = hidx(k);
    while (hused[i] && hkey[i] != k) i = (i + 1) & (HSIZE - 1);
    hused[i] = 1; hkey[i] = k; ha[i] = (uint16_t)a; hb[i] = (uint16_t)b;
}

static int first_w2(int n) {                       /* 처음 S_n == S_0 (= x^n ≡ 1) 이 되는 길이 n+1 */
    for (int i = 1; i < n; i++) if (S[i] == 1u) return i + 1;
    return -1;
}
static int first_w3(int n) {                       /* 단일 값 해시 */
    hclear();
    for (int m = 0; m < n; m++) {
        for (int a = 0; a < m; a++) { int b, d; if (hfind(S[m] ^ S[a], &b, &d) && b != a) return m + 1; }
        hput(S[m], m, m);
    }
    return -1;
}
static int first_w4(int n) {
    hclear();
    long pairs = 0;
    for (int m = 1; m < n; m++) {
        for (int a = 0; a < m; a++) {
            int b, c;
            if (hfind(S[m] ^ S[a], &b, &c) && b != a && c != a && b != m && c != m) return m + 1;
        }
        for (int a = 0; a < m; a++) { hput(S[m] ^ S[a], a, m); pairs++; }
        if (pairs > (long)(HSIZE * 0.6)) return -2;   /* 해시가 꽉 참: 더 길게는 못 본다 */
    }
    return -1;
}
static int first_w5(int n) {
    hclear();
    for (int m = 2; m < n; m++) {
        for (int a = 0; a < m; a++)
            for (int b = a + 1; b < m; b++) {
                int c, d;
                if (hfind(S[m] ^ S[a] ^ S[b], &c, &d) && c != a && c != b && d != a && d != b) return m + 1;
            }
        for (int a = 0; a < m; a++) hput(S[m] ^ S[a], a, m);
    }
    return -1;
}

static void show(const char *what, int r, int N, int limit) {
    if (N > 0) printf("  %-18s first at codeword %6d bits (data %6d bits)\n", what, N, N - r);
    else if (N == -1) printf("  %-18s none up to codeword %6d bits\n", what, limit);
    else printf("  %-18s search table full (not computed)\n", what);
}

static int wire_pos(int p) { return 8 * (p / 8) + 7 - p % 8; }

/* 와이어 순서가 다를 때 가장 짧은 미검출 버스트 길이 (Lw <= r 인 미검출이 있으면 반드시 찾음; 없으면 r+1 이상) */
static int min_mismatched_burst(uint32_t poly, int r, uint64_t *example, int *ex_off) {
    uint64_t g = (1ull << r) | poly;
    int best = 1 << 30;
    for (uint32_t q = 1; q < (1u << 14); q += 2) {
        uint64_t e = 0;
        for (int j = 0; j < 14; j++) if (q >> j & 1u) e ^= g << j;
        int d = 63;
        while (!(e >> d & 1u)) d--;
        for (int o = 0; o < 8; o++) {
            int mn = 1 << 30, mx = -1;
            for (int j = 0; j <= d; j++)
                if (e >> j & 1u) { int w = wire_pos(o + d - j); if (w < mn) mn = w; if (w > mx) mx = w; }
            if (mx - mn + 1 < best) { best = mx - mn + 1; *example = e; *ex_off = o; }
        }
    }
    return best;
}

int main(void) {
    struct { const char *name; uint32_t poly; int r; int n2, n3, n4, n5; } P[3] = {
        { "CRC-8   poly 0x07       ", 0x07u,       8,  MAXN, 4000, 4000, 0 },
        { "CRC-16  poly 0x1021     ", 0x1021u,     16, MAXN, 4000, 4000, 0 },
        { "CRC-32  poly 0x04C11DB7 ", 0x04C11DB7u, 32, MAXN, 4000, 4000, 400 },
    };
    for (int t = 0; t < 3; t++) {
        int r = P[t].r;
        build(P[t].poly, r, MAXN);
        int par = __builtin_popcount(P[t].poly) + 1;   /* g(1) = 항 개수 mod 2 */
        printf("%s g(1) = %d (%s)\n", P[t].name, par & 1, (par & 1) ? "odd weights possible" : "x+1 divides g: every odd-weight error caught");
        show("2-bit undetected", r, first_w2(P[t].n2), P[t].n2);
        if (par & 1) show("3-bit undetected", r, first_w3(P[t].n3), P[t].n3);
        show("4-bit undetected", r, first_w4(P[t].n4), P[t].n4);
        if ((par & 1) && P[t].n5) show("5-bit undetected", r, first_w5(P[t].n5), P[t].n5);
    }
    printf("\nbit order mismatch (CRC order != wire order): shortest undetected wire burst\n");
    for (int t = 0; t < 3; t++) {
        uint64_t ex = 0; int off = 0;
        int L = min_mismatched_burst(P[t].poly, P[t].r, &ex, &off);
        printf("  %s matched order: %2d   mismatched: %2d  %s  (e = 0x%llx, first bit at byte offset %d)\n",
               P[t].name, P[t].r + 1, L, L <= P[t].r ? "<= r: burst guarantee BROKEN" : "> r: guarantee survives",
               (unsigned long long)ex, off);
    }
    return 0;
}
