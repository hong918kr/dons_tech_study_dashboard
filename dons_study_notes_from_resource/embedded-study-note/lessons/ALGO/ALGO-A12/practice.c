/* ALGO-A12 추가 연습 — 한 번에 검증 (파일 I/O 없음: 런 생성·병합을 RAM 배열 위에서 흉내낸다)
 *   연습 1  교체 선택이 런 1개를 만드는 정확한 조건: "앞에 있는 더 큰 key 의 수 ≤ c-1" — 전수 검사
 *   연습 2  안정한 교체 선택: 도착 번호(stamp)를 붙여 (key, stamp) 로 비교 → 전체 외부 정렬이 안정
 *   연습 3  첫 패스 부분 병합(더미 런): 같은 길이 런 R 개를 k-way 로 합치는 최소 읽기량 = k-진 허프만
 *   연습 4  multiset 검사는 해시의 합: XOR 은 "쌍으로 바뀐" 손상을 못 본다
 *   연습 5  블록 크기 B 와 fan-in k: 4 KB 에서 B 를 바꾸면 패스 수 · 바이트 · I/O 횟수가 어떻게 변하나
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 2463534242u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

/* ---------- 연습 1·2 공용: 레코드와 교체 선택 (본문 gen_runs_rs 와 같은 힙 나눠 쓰기) ----------
 * stable = 0: key 만 비교 (본문과 같음),  stable = 1: (key, stamp) 비교, stamp = 도착 순번 */
typedef struct { uint32_t key, seq, stamp; } prec_t;
static int pless(const prec_t *a, const prec_t *b, int stable) {
    return a->key < b->key || (stable && a->key == b->key && a->stamp < b->stamp);
}
static void pdown(prec_t *h, int n, int i, int stable) {
    prec_t x = h[i];
    for (;;) {
        int c = 2 * i + 1;
        if (c >= n) break;
        if (c + 1 < n && pless(&h[c + 1], &h[c], stable)) c++;
        if (!pless(&h[c], &x, stable)) break;
        h[i] = h[c];
        i = c;
    }
    h[i] = x;
}
static void pbuild(prec_t *h, int n, int stable) { for (int i = n / 2; i-- > 0;) pdown(h, n, i, stable); }

#define MAXN 4096
static prec_t g_heap[512];
/* in[0..n) 를 힙 용량 c 로 런 생성. out 에 런들을 차례로, runlen[] 에 길이. 반환 = 런 수 */
static int rs_runs(const prec_t *in, int n, int c, int stable, prec_t *out, int *runlen) {
    prec_t *h = g_heap;
    int pos = 0, m = 0, hn, R = 0, len = 0, o = 0;
    uint32_t arrival = 0;
    while (m < c && pos < n) { h[m] = in[pos++]; h[m].stamp = arrival++; m++; }
    pbuild(h, m, stable);
    hn = m;
    while (m > 0) {
        if (hn == 0) { runlen[R++] = len; len = 0; hn = m; pbuild(h, hn, stable); }
        prec_t top = h[0];
        out[o++] = top; len++;
        if (pos < n && in[pos].key >= top.key) {
            h[0] = in[pos++]; h[0].stamp = arrival++;
            pdown(h, hn, 0, stable);
        } else {
            h[0] = h[hn - 1]; hn--;
            pdown(h, hn, 0, stable);
            if (pos < n) { h[hn] = in[pos++]; h[hn].stamp = arrival++; }
            else { h[hn] = h[m - 1]; m--; }
        }
    }
    if (len > 0) runlen[R++] = len;
    return R;
}

/* ---------- 연습 1: 전수 검사 ---------- */
static int max_left_inv(const prec_t *a, int n) {
    int best = 0;
    for (int t = 0; t < n; t++) {
        int cnt = 0;
        for (int j = 0; j < t; j++) cnt += a[j].key > a[t].key;
        if (cnt > best) best = cnt;
    }
    return best;
}
static void ex1(void) {
    static prec_t in[8], out[8];
    int runlen[8], bad_iff = 0, bad_sorted = 0, bad_min = 0, total = 0;
    for (int n = 0; n <= 7; n++) {
        int lim = 1;
        for (int i = 0; i < n; i++) lim *= 4;                 /* 알파벳 {0,1,2,3} 의 길이 n 단어 전부 (중복 key 포함) */
        for (int w = 0; w < lim; w++) {
            int v = w;
            for (int i = 0; i < n; i++) { in[i].key = (uint32_t)(v % 4); in[i].seq = (uint32_t)i; v /= 4; }
            int mli = max_left_inv(in, n);
            for (int c = 1; c <= 4; c++) {
                int R = rs_runs(in, n, c, 0, out, runlen), o = 0;
                total++;
                if ((n > 0 && R == 1) != (n > 0 && mli <= c - 1)) bad_iff++;
                for (int r = 0; r < R; r++) {
                    for (int i = 1; i < runlen[r]; i++) if (out[o + i].key < out[o + i - 1].key) bad_sorted++;
                    if (r + 1 < R && runlen[r] < c) bad_min++;   /* 마지막 런 빼고 길이 ≥ c */
                    o += runlen[r];
                }
                if (o != n) bad_sorted++;
            }
        }
    }
    printf("ex1: %d (input, c) pairs: iff violations=%d, unsorted runs=%d, short non-last runs=%d\n",
           total, bad_iff, bad_sorted, bad_min);
    CHECK(bad_iff == 0);
    CHECK(bad_sorted == 0 && bad_min == 0);
}

/* ---------- 연습 2: 안정한 교체 선택 + 안정 병합 ---------- */
static int cmp_ks(const void *a, const void *b) {
    const prec_t *p = a, *q = b;
    if (p->key != q->key) return p->key < q->key ? -1 : 1;
    return p->seq < q->seq ? -1 : p->seq > q->seq;
}
/* 런들을 (key, 런 번호) 로 병합 — 같은 key 면 앞 런 먼저 (본문 mless 와 같은 규칙, k 가 작아 선형 탐색) */
static void merge_runs(const prec_t *runs, const int *runlen, int R, prec_t *out) {
    static int start[MAXN], at[MAXN];
    int s = 0, n = 0;
    for (int r = 0; r < R; r++) { start[r] = s; at[r] = 0; s += runlen[r]; }
    for (;;) {
        int best = -1;
        for (int r = 0; r < R; r++)
            if (at[r] < runlen[r] && (best < 0 || runs[start[r] + at[r]].key < runs[start[best] + at[best]].key)) best = r;
        if (best < 0) break;
        out[n++] = runs[start[best] + at[best]++];
    }
}
static void ex2(void) {
    static prec_t in[MAXN], runs[MAXN], out[MAXN], ref[MAXN];
    static int runlen[MAXN];
    int unstable_plain = 0, bad_stable = 0;
    for (int t = 0; t < 300; t++) {
        int n = 1 + (int)(xs32() % 2000), c = 1 + (int)(xs32() % 100), mod = 1 + (int)(xs32() % 30);
        for (int i = 0; i < n; i++) { in[i].key = xs32() % (uint32_t)mod; in[i].seq = (uint32_t)i; in[i].stamp = 0; }
        memcpy(ref, in, (size_t)n * sizeof *in);
        qsort(ref, (size_t)n, sizeof *ref, cmp_ks);
        for (int st = 0; st < 2; st++) {
            int R = rs_runs(in, n, c, st, runs, runlen);
            merge_runs(runs, runlen, R, out);
            int same = 1;
            for (int i = 0; i < n; i++) if (out[i].key != ref[i].key || out[i].seq != ref[i].seq) same = 0;
            if (st == 0 && !same) unstable_plain++;
            if (st == 1 && !same) bad_stable++;
        }
    }
    printf("ex2: 300 dup-heavy cases: key-only RS unstable in %d, (key,stamp) RS unstable in %d;"
           " 4 KB heap capacity 16 B -> %u records, 20 B -> %u records\n",
           unstable_plain, bad_stable, (4096u - 3 * 256u) / 16u, (4096u - 3 * 256u) / 20u);
    CHECK(bad_stable == 0);
    CHECK(unstable_plain > 0);                                /* key 만 비교하면 실제로 순서가 깨진다 */
}

/* ---------- 연습 3: 같은 길이 런 R 개, k-way — 첫 패스 부분 병합의 비용 ----------
 * 비용 = 병합 단계에서 읽는 "런 단위" 합 = Σ (원래 런의 깊이). 균일 패스는 R·P. */
static uint32_t ptheory(uint64_t R, uint32_t k) { uint32_t p = 0; uint64_t cap = 1; while (cap < R) { cap *= k; p++; } return p; }
static uint64_t plan_cost(uint64_t R, uint32_t k) {
    uint32_t P = ptheory(R, k);
    if (P == 0) return 0;
    uint64_t top = 1;
    for (uint32_t i = 1; i < P; i++) top *= k;                /* k^(P-1): 나머지 P-1 패스가 한 번에 다룰 수 있는 런 수 */
    uint64_t r = R - top;                                     /* 첫 패스에서 줄여야 할 런 수 */
    uint64_t q = r / (k - 1), rem = r % (k - 1);
    uint64_t deep = q * k + (rem ? rem + 1 : 0);              /* 첫 패스에 참여하는 런 = 깊이 P */
    return deep * P + (R - deep) * (P - 1);
}
/* 기준: k-진 허프만 (무게 1 인 잎 R 개 + 무게 0 더미, (R+d-1) % (k-1) == 0) — 작은 최소 힙 */
static uint64_t hh[8192];
static int hn_;
static void hpush(uint64_t v) { int i = hn_++; while (i > 0 && hh[(i - 1) / 2] > v) { hh[i] = hh[(i - 1) / 2]; i = (i - 1) / 2; } hh[i] = v; }
static uint64_t hpop(void) {
    uint64_t r = hh[0], x = hh[--hn_];
    int i = 0;
    for (;;) { int c = 2 * i + 1; if (c >= hn_) break; if (c + 1 < hn_ && hh[c + 1] < hh[c]) c++; if (hh[c] >= x) break; hh[i] = hh[c]; i = c; }
    hh[i] = x;
    return r;
}
static uint64_t huffman_cost(uint32_t R, uint32_t k) {
    if (R <= 1) return 0;
    hn_ = 0;
    for (uint32_t i = 0; i < R; i++) hpush(1);
    while ((hn_ - 1) % (int)(k - 1) != 0) hpush(0);
    uint64_t cost = 0;
    while (hn_ > 1) { uint64_t s = 0; for (uint32_t j = 0; j < k; j++) s += hpop(); cost += s; hpush(s); }
    return cost;
}
static void ex3(void) {
    int bad = 0;
    for (uint32_t k = 2; k <= 13; k++)
        for (uint32_t R = 1; R <= 3000; R++) if (plan_cost(R, k) != huffman_cost(R, k)) bad++;
    uint64_t naive = 2520ull * 4, plan = plan_cost(2520, 13);
    printf("ex3: plan == k-ary Huffman for R<=3000, k=2..13: mismatches=%d;  R=2520 k=13: uniform %llu run-reads"
           " (4 passes), partial first pass %llu (= %.3f passes)\n", bad, (unsigned long long)naive,
           (unsigned long long)plan, plan / 2520.0);
    CHECK(bad == 0);
    CHECK(plan == 7910);
}

/* ---------- 연습 4: XOR 대 합 ---------- */
static uint64_t mix64(uint64_t z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static void ex4(void) {
    uint32_t in[3] = { 10, 20, 20 }, bad[3] = { 10, 30, 30 };  /* 20 두 개가 30 두 개로 바뀐 출력 */
    uint64_t xi = 0, xb = 0, si = 0, sb = 0;
    for (int i = 0; i < 3; i++) { xi ^= mix64(in[i]); xb ^= mix64(bad[i]); si += mix64(in[i]); sb += mix64(bad[i]); }
    printf("ex4: xor in=%016llx bad=%016llx | sum in=%016llx bad=%016llx\n", (unsigned long long)xi,
           (unsigned long long)xb, (unsigned long long)si, (unsigned long long)sb);
    CHECK(xi == xb && si != sb);
    /* 합은 순서와 무관: 무작위 셔플 1000번 모두 같은 값 */
    uint32_t a[64];
    uint64_t s0 = 0;
    for (int i = 0; i < 64; i++) { a[i] = xs32() % 8; s0 += mix64(a[i]); }
    int diff = 0;
    for (int t = 0; t < 1000; t++) {
        for (int i = 63; i > 0; i--) { int j = (int)(xs32() % (uint32_t)(i + 1)); uint32_t tmp = a[i]; a[i] = a[j]; a[j] = tmp; }
        uint64_t s = 0;
        for (int i = 0; i < 64; i++) s += mix64(a[i]);
        if (s != s0) diff++;
    }
    CHECK(diff == 0);
}

/* ---------- 연습 5: 블록 크기 스윕 (본문 merge_ram_need 와 같은 계산, 블록 크기만 바꿈) ---------- */
static size_t r8(size_t n) { return (n + 7u) & ~(size_t)7u; }
static size_t need(uint32_t k, uint32_t B) { return r8((size_t)k * B) + 2u * B + r8((size_t)k * 12u) + r8((size_t)k * 2u); }
static uint32_t fanin(uint32_t B) { uint32_t k = 1; while (need(k + 1, B) <= 4096) k++; return k; }
static void ex5(void) {
    const uint64_t D = (1ull << 20) * 16;                    /* 1M x 16 B */
    const uint32_t R = 4096;                                 /* LSW 런 수 (B 와 무관: RAM 4 KB = 256 레코드) */
    static const uint32_t Bs[] = { 64, 128, 256, 512, 1024 };
    uint32_t kk[5], pp[5];
    printf("ex5:   B   k   P   bytes read+write   I/O ops\n");
    for (int i = 0; i < 5; i++) {
        kk[i] = fanin(Bs[i]);
        pp[i] = kk[i] >= 2 ? ptheory(R, kk[i]) : 0;
        uint64_t bytes = 2 * D * (1 + pp[i]);
        if (kk[i] < 2) { printf("     %4u  %2u   -   (merge impossible: k < 2)\n", Bs[i], kk[i]); continue; }
        printf("     %4u  %2u  %2u   %10llu MiB   %9llu\n", Bs[i], kk[i], pp[i], (unsigned long long)(bytes >> 20),
               (unsigned long long)(bytes / Bs[i]));
    }
    CHECK(kk[2] == 13 && pp[2] == 4);                         /* 본문 설정 */
    CHECK(kk[3] == 5 && pp[3] == 6 && kk[4] == 1);           /* 1 KB 블록이면 병합 자체가 불가능 (k=1) */
    CHECK(pp[0] == 3 && pp[1] == 3);                          /* 작은 블록 = 큰 k = 패스 감소, 대신 I/O 횟수 증가 */
}

int main(void) {
    ex1();
    ex2();
    ex3();
    ex4();
    ex5();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
