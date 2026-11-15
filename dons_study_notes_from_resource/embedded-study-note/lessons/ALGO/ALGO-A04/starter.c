/* ALGO-A04 스트리밍 패턴 매칭: KMP와 Rabin-Karp
 *
 * UART 바이트를 "하나씩" 받아 동기 패턴(프리앰블)을 찾는다. 텍스트를 버퍼에 모으지 않는다.
 *   Part A  KMP: 실패 함수(prefix function) + push(byte) — 가변 상태는 matched(q) 1바이트뿐
 *           겹치는 매칭(끝 위치 전부), 덩어리(chunk) 경계에 걸친 매칭, 상각 O(1)/바이트 측정
 *   Part B  KMP DFA ((m+1)×256 표, 바이트당 표 1번) / Rabin-Karp 롤링 해시 (m 바이트 링 필요)
 *           mod 2^32 vs mod (2^31-1), 해시 일치 → 반드시 원문 비교(verify), 일부러 만든 충돌 2종
 *   Part C  잡음 섞인 UART 에서 0x55 0x55 0x7E 프레임 동기: 깨진 프레임의 바이트를 다시 헌팅(replay)
 *
 * 모든 검사는 naive 검색 / 정의대로 만든 brute force / 오프라인 참조 파서와 대조. xorshift32 고정 시드.
 * 알고리즘 부분은 malloc 없음 (정적 배열 + 호출자 버퍼).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(c) do { g_total++; if (c) g_pass++; else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } while (0)

static uint32_t g_rng = 2463534242u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

#define KMP_MAXM 255                     /* 상태·실패값이 uint8_t 에 들어가는 최대 패턴 길이 */

/* ============================ Part A — KMP ============================ */
/* fail[i] = p[0..i] 의 가장 긴 "진 경계"(proper border: 접두사이면서 접미사, 자기 자신 제외) 길이.
 * 예) "aabaaab" → 0 1 0 1 2 2 3.   m >= 1. O(m). */
static void kmp_build(const uint8_t *p, int m, uint8_t *fail) {
    (void)p;
    memset(fail, 0, (size_t)m);          /* TODO: k = 지금 경계 길이. 불일치면 k = fail[k-1] 로 물러나기 */
}

typedef struct {
    const uint8_t *p, *fail;             /* 패턴과 실패 함수 — 상수, 플래시에 둘 수 있다 */
    uint8_t m;
    uint8_t q;                           /* 유일한 가변 상태: 지금 맞은 길이 (0..m-1) */
    uint32_t steps;                      /* 통계: q = fail[q-1] 를 실행한 총 횟수 */
    uint32_t max_steps;                  /* 통계: 바이트 하나에서의 최대 횟수 */
} kmp_t;

static void kmp_init(kmp_t *k, const uint8_t *p, const uint8_t *fail, int m) {
    memset(k, 0, sizeof *k);
    k->p = p; k->fail = fail; k->m = (uint8_t)m;
}

/* 바이트 하나를 넣는다. 이 바이트에서 패턴이 끝나면 true (겹치는 매칭도 전부 보고). */
static bool kmp_push(kmp_t *k, uint8_t b) {
    (void)k; (void)b;
    return false;   /* TODO: 불일치면 q = fail[q-1] 반복(steps 세기) → 일치면 q++ → q==m 이면 보고 + q = fail[m-1] */
}

/* DMA/FIFO 덩어리 단위 입력. base = buf[0] 의 스트림 위치. 끝 위치를 ends[] 에 기록, 개수 반환. */
static int kmp_feed(kmp_t *k, const uint8_t *buf, int n, uint32_t base, uint32_t *ends, int cap) {
    int ne = 0;
    for (int i = 0; i < n; i++)
        if (kmp_push(k, buf[i]) && ne < cap) ends[ne++] = base + (uint32_t)i;
    return ne;
}

/* ============================ Part B — KMP DFA ============================ */
/* dfa[q*256 + c] = 상태 q(맞은 길이 0..m)에서 바이트 c 를 읽은 뒤의 상태.  표 크기 (m+1)*256 B.
 * 행 m = "방금 매칭됨" 상태 (겹치는 다음 매칭을 위해 필요). */
static void dfa_build(const uint8_t *p, int m, const uint8_t *fail, uint8_t *dfa) {
    (void)p; (void)fail;
    memset(dfa, 0, (size_t)(m + 1) * 256);   /* TODO: 행 q = 행 fail[q-1] 복사 + dfa[q][p[q]] = q+1 */
}

typedef struct { const uint8_t *t; uint8_t m, q; } dfa_t;
static bool dfa_push(dfa_t *d, uint8_t b) {
    d->q = d->t[256u * d->q + b];
    return d->q == d->m;
}

/* ============================ Part B — Rabin-Karp ============================ */
/* h(s[0..m)) = Σ s[i]·B^(m-1-i)  (mod 2^32: uint32 자연 wrap) 또는 (mod P = 2^31-1, 메르센 소수) */
#define RK_P 2147483647u

static uint32_t mod_p(uint64_t x) {      /* x < 2^63 → x mod (2^31-1). 나눗셈 없이 접기 두 번 */
    x = (x & RK_P) + (x >> 31);
    x = (x & RK_P) + (x >> 31);
    return (uint32_t)(x >= RK_P ? x - RK_P : x);
}

static uint32_t rk_hash(const uint8_t *s, int m, bool modp, uint32_t base) {
    uint32_t h = 0;
    for (int i = 0; i < m; i++)
        h = modp ? mod_p((uint64_t)h * base + s[i]) : h * base + s[i];
    return h;
}

typedef struct {
    const uint8_t *p; uint8_t m;
    bool modp, verify;                   /* verify=false 는 충돌 시연용. 실전은 항상 true */
    uint32_t base, hp, bm1, h;           /* hp = h(pattern), bm1 = B^(m-1), h = 창의 해시 */
    uint8_t ring[KMP_MAXM]; uint8_t head;/* 마지막 m 바이트. ring[head] = 가장 오래된 것 */
    uint32_t seen;                       /* 지금까지 넣은 바이트 수 (창이 찼는지) */
    uint32_t hash_hits, false_hits;      /* 통계: 해시 일치 횟수, 그중 원문이 다른 것 */
} rk_t;

static void rk_init(rk_t *r, const uint8_t *p, int m, bool modp, uint32_t base, bool verify) {
    memset(r, 0, sizeof *r);
    r->p = p; r->m = (uint8_t)m; r->modp = modp; r->base = base; r->verify = verify;
    /* TODO: r->hp = 패턴 해시, r->bm1 = base^(m-1)  (modp 면 mod P, 아니면 mod 2^32) */
}

static bool rk_ring_equals(const rk_t *r) {   /* 창(링, head 부터 순서대로) == 패턴 ? */
    unsigned j = r->head;
    for (unsigned i = 0; i < r->m; i++) {
        if (r->ring[j] != r->p[i]) return false;
        j = (j + 1 == r->m) ? 0 : j + 1;
    }
    return true;
}

static bool rk_push(rk_t *r, uint8_t b) {
    (void)r; (void)b; (void)rk_ring_equals;
    return false;   /* TODO: 링에서 out 빼고 b 넣기 → h 굴리기 → 창이 찼고 h==hp 면 hash_hits++, verify */
}

static int rk_feed(rk_t *r, const uint8_t *buf, int n, uint32_t base, uint32_t *ends, int cap) {
    int ne = 0;
    for (int i = 0; i < n; i++)
        if (rk_push(r, buf[i]) && ne < cap) ends[ne++] = base + (uint32_t)i;
    return ne;
}

/* ============================ Part C — UART 프레임 동기 ============================ */
/* 프레임 = 55 55 7E | len (1..32) | payload[len] | ck,  ck = ~(len + Σpayload) & 0xFF
 * 바이트 스터핑 없음 → 페이로드 안에 프리앰블이 있어도 된다. 깨진 프레임(len 범위 밖, ck 틀림)은
 * 프리앰블 "뒤" 바이트들을 다시 헌팅한다 — 가짜 프리앰블이 진짜 프레임을 삼키지 않게. */
#define PRE_M 3
static const uint8_t PREAMBLE[PRE_M] = {0x55, 0x55, 0x7E};
#define FR_MAXP 32
#define FR_MAXB (FR_MAXP + 2)            /* 프리앰블 뒤 최대 바이트: len + payload + ck */
#define SY_QCAP 64                       /* 재생 FIFO. 내용 + fb ≤ FR_MAXB + 1 (5. 풀이) */

typedef struct { uint32_t start; uint8_t len; uint8_t pay[FR_MAXP]; } frame_t;

typedef struct {
    kmp_t k; uint8_t fail[PRE_M];
    bool body;                           /* false = 프리앰블 헌팅, true = 프레임 수집 */
    uint8_t fb[FR_MAXB]; uint8_t fn;     /* 프리앰블 뒤에 모은 바이트 */
    uint32_t fstart, fpos;               /* 프레임 시작(프리앰블 첫 바이트) 위치, fb[0] 위치 */
    uint8_t q[SY_QCAP]; uint8_t qh, qn;  /* 처리 대기 FIFO (앞에 다시 끼워 넣기 가능) */
    uint32_t qpos;                       /* q 맨 앞 바이트의 스트림 위치 */
    uint32_t steps, max_steps;           /* 통계: 처리한 바이트(재생 포함), push 한 번의 최대 */
    uint32_t false_syncs, overflow;      /* 프리앰블은 맞았는데 깨진 프레임 수, FIFO 넘침(0 이어야) */
} sync_t;

static uint8_t frame_ck(const uint8_t *lenpay, int n) {
    uint8_t s = 0;
    for (int i = 0; i < n; i++) s = (uint8_t)(s + lenpay[i]);
    return (uint8_t)~s;
}

static void sync_init(sync_t *s) {
    memset(s, 0, sizeof *s);
    kmp_build(PREAMBLE, PRE_M, s->fail);
    kmp_init(&s->k, PREAMBLE, s->fail, PRE_M);
}

static void q_push_back(sync_t *s, uint8_t b) {
    if (s->qn == SY_QCAP) { s->overflow++; return; }
    s->q[(s->qh + s->qn++) & (SY_QCAP - 1)] = b;
}
static void q_push_front(sync_t *s, const uint8_t *buf, int n, uint32_t pos) {
    if (s->qn + n > SY_QCAP) { s->overflow++; return; }
    for (int i = n - 1; i >= 0; i--) { s->qh = (uint8_t)((s->qh - 1) & (SY_QCAP - 1)); s->q[s->qh] = buf[i]; s->qn++; }
    s->qpos = pos;
}
static uint8_t q_pop_front(sync_t *s) {
    uint8_t b = s->q[s->qh];
    s->qh = (uint8_t)((s->qh + 1) & (SY_QCAP - 1)); s->qn--; s->qpos++;
    return b;
}

/* live 바이트 하나. 완성된 프레임을 out[] 에 넣고 개수 반환 (재생 때문에 2개 이상일 수 있다). */
static int sync_push(sync_t *s, uint8_t b, frame_t *out, int maxout) {
    (void)out; (void)maxout; (void)q_push_front; (void)frame_ck;
    q_push_back(s, b);
    while (s->qn > 0) (void)q_pop_front(s);   /* TODO: 헌팅(kmp_push) ↔ 수집(fb), 깨지면 fb 를 q 앞에 재생 */
    return 0;
}

/* ============================ 테스트 쪽 참조 구현 ============================ */
static int naive_find(const uint8_t *t, int n, const uint8_t *p, int m, uint32_t *ends, int cap) {
    int ne = 0;
    for (int i = 0; i + m <= n; i++)
        if (memcmp(t + i, p, (size_t)m) == 0 && ne < cap) ends[ne++] = (uint32_t)(i + m - 1);
    return ne;
}

static int brute_border(const uint8_t *s, int len) {   /* 정의 그대로: 가장 긴 진 경계 */
    for (int k = len - 1; k > 0; k--)
        if (memcmp(s, s + len - k, (size_t)k) == 0) return k;
    return 0;
}

static int brute_delta(const uint8_t *p, int m, int q, uint8_t c) {  /* p[0..q)+c 의 접미사인 가장 긴 접두사 */
    uint8_t s[KMP_MAXM + 1];
    memcpy(s, p, (size_t)q); s[q] = c;
    for (int k = (q + 1 < m ? q + 1 : m); k > 0; k--)
        if (memcmp(p, s + q + 1 - k, (size_t)k) == 0) return k;
    return 0;
}

static bool same_ends(const uint32_t *a, int na, const uint32_t *b, int nb) {
    return na == nb && (na == 0 || memcmp(a, b, (size_t)na * sizeof *a) == 0);
}

/* 알파벳 크기 A: A==256 이면 아무 바이트, 아니면 'a'.. 'a'+A-1 */
static uint8_t rnd_sym(int A) { return A == 256 ? (uint8_t)xs32() : (uint8_t)('a' + xs32() % (uint32_t)A); }

/* 오프라인 참조 파서: 왼쪽부터, 프리앰블 + 유효 프레임이면 내보내고 건너뛰고, 아니면 한 칸.
 * 스트림 끝에서 프레임이 덜 끝났으면 거기서 멈춘다 (스트리밍 쪽도 그 프레임을 기다리는 중). */
static int ref_frames(const uint8_t *t, int n, frame_t *out, int cap) {
    int i = 0, nf = 0;
    while (i + PRE_M <= n) {
        if (memcmp(t + i, PREAMBLE, PRE_M) == 0) {
            if (i + PRE_M >= n) break;
            int len = t[i + PRE_M];
            if (len >= 1 && len <= FR_MAXP) {
                if (i + PRE_M + len + 2 > n) break;
                if (t[i + PRE_M + len + 1] == frame_ck(t + i + PRE_M, len + 1)) {
                    if (nf < cap) { out[nf].start = (uint32_t)i; out[nf].len = (uint8_t)len; memcpy(out[nf].pay, t + i + PRE_M + 1, (size_t)len); }
                    nf++; i += PRE_M + len + 2; continue;
                }
            }
        }
        i++;
    }
    return nf;
}

static bool same_frames(const frame_t *a, int na, const frame_t *b, int nb) {
    if (na != nb) return false;
    for (int i = 0; i < na; i++)
        if (a[i].start != b[i].start || a[i].len != b[i].len || memcmp(a[i].pay, b[i].pay, a[i].len) != 0) return false;
    return true;
}

/* 잡음 섞인 UART 스트림: 0x55·0x7E 가 많은 잡음 + (1/3 확률) 가짜 프리앰블 + 진짜 프레임 */
static int gen_uart(uint8_t *t, int nframes, uint32_t *starts) {
    int n = 0;
    for (int f = 0; f < nframes; f++) {
        int noise = (int)(xs32() % 41);
        for (int i = 0; i < noise; i++) {
            uint32_t r = xs32() % 100;
            t[n++] = r < 35 ? 0x55 : r < 45 ? 0x7E : (uint8_t)xs32();
        }
        if (xs32() % 3 == 0) { t[n++] = 0x55; t[n++] = 0x55; t[n++] = 0x7E; t[n++] = (uint8_t)(1 + xs32() % FR_MAXP); }
        int len = 1 + (int)(xs32() % FR_MAXP);
        starts[f] = (uint32_t)n;
        t[n++] = 0x55; t[n++] = 0x55; t[n++] = 0x7E; t[n++] = (uint8_t)len;
        for (int i = 0; i < len; i++) { uint32_t r = xs32() % 4; t[n++] = r == 0 ? 0x55 : r == 1 ? 0x7E : (uint8_t)xs32(); }
        t[n] = frame_ck(t + starts[f] + PRE_M, len + 1); n++;
    }
    memset(t + n, 0xFF, 40); n += 40;                  /* 끝: 유휴 라인(0xFF) — 덜 끝난 가짜 프레임을 마무리 */
    return n;
}

static int sync_run(sync_t *s, const uint8_t *t, int n, bool chunked, frame_t *out, int cap) {
    int nf = 0, i = 0;
    frame_t tmp[8];
    while (i < n) {
        int c = chunked ? 1 + (int)(xs32() % 64) : n - i;       /* DMA 반쪽 전송 크기 흉내 */
        if (c > n - i) c = n - i;
        for (int j = 0; j < c; j++) {
            int k = sync_push(s, t[i + j], tmp, 8);
            for (int z = 0; z < k && z < 8; z++) if (nf < cap) out[nf++] = tmp[z];
        }
        i += c;
    }
    return nf;
}

/* Thue–Morse: T_{k+1} = T_k · ~T_k.  여기선 'a'/'b' 두 글자 */
static void thue_morse(uint8_t *t, int n, bool complement) {
    for (int i = 0; i < n; i++) {
        int bit = __builtin_popcount((unsigned)i) & 1;        /* 테스트 쪽: builtin 허용 */
        t[i] = (uint8_t)((bit ^ complement) ? 'b' : 'a');
    }
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}
static void bday_str(uint32_t idx, uint8_t *s) {               /* 인덱스 → 결정적 8바이트 문자열 */
    uint32_t x = idx * 2654435761u + 0x9E3779B9u;
    for (int i = 0; i < 8; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; s[i] = (uint8_t)x; }
}

#define BIGN 200000
static uint8_t g_text[BIGN];
static uint32_t g_e1[BIGN], g_e2[BIGN], g_e3[BIGN];
static uint8_t g_dfa[(KMP_MAXM + 1) * 256];
static frame_t g_f1[4096], g_f2[4096];
#define BDAY_N (1u << 18)
static uint64_t g_bday[BDAY_N];

int main(void) {
    /* ---------- Part A: 실패 함수 ---------- */
    {
        uint8_t f[16];
        const uint8_t p1[] = "aabaaab"; const uint8_t e1[] = {0, 1, 0, 1, 2, 2, 3};
        kmp_build(p1, 7, f); CHECK(memcmp(f, e1, 7) == 0);
        const uint8_t p2[] = "ABCDABD"; const uint8_t e2[] = {0, 0, 0, 0, 1, 2, 0};
        kmp_build(p2, 7, f); CHECK(memcmp(f, e2, 7) == 0);
        const uint8_t p3[] = {0x55, 0x55, 0x55, 0x7E}; const uint8_t e3[] = {0, 1, 2, 0};
        kmp_build(p3, 4, f); CHECK(memcmp(f, e3, 4) == 0);
        kmp_build(PREAMBLE, 3, f); CHECK(f[0] == 0 && f[1] == 1 && f[2] == 0);
    }
    {   /* 정의(brute)와 대조: 길이 1..12 이진 문자열 전부 + 무작위 */
        int bad = 0, cnt = 0;
        uint8_t s[KMP_MAXM], f[KMP_MAXM];
        for (int len = 1; len <= 12; len++)
            for (uint32_t bits = 0; bits < (1u << len); bits++) {
                for (int i = 0; i < len; i++) s[i] = (uint8_t)('a' + ((bits >> i) & 1));
                kmp_build(s, len, f); cnt++;
                for (int i = 0; i < len; i++) if (f[i] != brute_border(s, i + 1)) { bad++; break; }
            }
        for (int c = 0; c < 2000; c++) {
            int len = 1 + (int)(xs32() % 60), A = 1 + (int)(xs32() % 4);
            for (int i = 0; i < len; i++) s[i] = rnd_sym(A);
            kmp_build(s, len, f); cnt++;
            for (int i = 0; i < len; i++) if (f[i] != brute_border(s, i + 1)) { bad++; break; }
        }
        printf("prefix function: %d patterns vs brute border, %d wrong\n", cnt, bad);
        CHECK(bad == 0);
    }
    /* ---------- Part A: 스트리밍 매칭, 겹침 ---------- */
    {
        uint8_t f[8]; kmp_t k; uint32_t ends[16];
        const uint8_t pa[] = "aa", ta[] = "aaaa"; const uint32_t ea[] = {1, 2, 3};
        kmp_build(pa, 2, f); kmp_init(&k, pa, f, 2);
        CHECK(same_ends(ends, kmp_feed(&k, ta, 4, 0, ends, 16), ea, 3));
        const uint8_t pb[] = "aba", tb[] = "abababa"; const uint32_t eb[] = {2, 4, 6};
        kmp_build(pb, 3, f); kmp_init(&k, pb, f, 3);
        CHECK(same_ends(ends, kmp_feed(&k, tb, 7, 0, ends, 16), eb, 3));
        const uint8_t pc[] = {0x55, 0x55, 0x7E}, tc[] = {0x55, 0x55, 0x55, 0x55, 0x7E, 0x00}; const uint32_t ec[] = {4};
        kmp_build(pc, 3, f); kmp_init(&k, pc, f, 3);
        CHECK(same_ends(ends, kmp_feed(&k, tc, 6, 0, ends, 16), ec, 1));   /* 55 55 55 55 7E: 끝 4 */
        const uint8_t pd[] = "x", td[] = "xyxx"; const uint32_t ed[] = {0, 2, 3};
        kmp_build(pd, 1, f); kmp_init(&k, pd, f, 1);
        CHECK(same_ends(ends, kmp_feed(&k, td, 4, 0, ends, 16), ed, 3));    /* m = 1 */
        kmp_build(pb, 3, f); kmp_init(&k, pb, f, 3);
        CHECK(kmp_feed(&k, (const uint8_t *)"ab", 2, 0, ends, 16) == 0 && kmp_feed(&k, NULL, 0, 2, ends, 16) == 0);
    }
    {   /* 전수: 이진 패턴 길이 1..5 × 이진 텍스트 길이 0..10, KMP·DFA·RK(2종) 모두 naive 와 같아야 */
        int bad[4] = {0}, runs = 0;
        uint8_t p[8], t[16], f[8];
        for (int m = 1; m <= 5; m++)
            for (uint32_t pb = 0; pb < (1u << m); pb++) {
                for (int i = 0; i < m; i++) p[i] = (uint8_t)('a' + ((pb >> i) & 1));
                kmp_build(p, m, f); dfa_build(p, m, f, g_dfa);
                for (int n = 0; n <= 10; n++)
                    for (uint32_t tb = 0; tb < (1u << n); tb++) {
                        for (int i = 0; i < n; i++) t[i] = (uint8_t)('a' + ((tb >> i) & 1));
                        uint32_t e0[16], e[16]; int n0 = naive_find(t, n, p, m, e0, 16), ne = 0;
                        kmp_t k; kmp_init(&k, p, f, m);
                        if (!same_ends(e, kmp_feed(&k, t, n, 0, e, 16), e0, n0)) bad[0]++;
                        dfa_t d = {g_dfa, (uint8_t)m, 0};
                        ne = 0; for (int i = 0; i < n; i++) if (dfa_push(&d, t[i])) e[ne++] = (uint32_t)i;
                        if (!same_ends(e, ne, e0, n0)) bad[1]++;
                        rk_t r; rk_init(&r, p, m, false, 31, true);
                        if (!same_ends(e, rk_feed(&r, t, n, 0, e, 16), e0, n0)) bad[2]++;
                        rk_init(&r, p, m, true, 257, true);
                        if (!same_ends(e, rk_feed(&r, t, n, 0, e, 16), e0, n0)) bad[3]++;
                        runs++;
                    }
            }
        printf("exhaustive binary: %d (pattern,text) pairs, mismatches kmp=%d dfa=%d rk32=%d rkp=%d\n",
               runs, bad[0], bad[1], bad[2], bad[3]);
        CHECK(bad[0] == 0); CHECK(bad[1] == 0); CHECK(bad[2] == 0 && bad[3] == 0);
    }
    {   /* 무작위: 알파벳 2/3/4/256, m 1..16 (+ 가끔 긴 m), 한 번에 vs 무작위 덩어리 */
        int bad_one = 0, bad_chunk = 0, bad_amort = 0, bad_dfa = 0, bad_rk = 0, cases = 0;
        long total_matches = 0, total_bytes = 0, total_steps = 0;
        uint8_t p[KMP_MAXM], f[KMP_MAXM];
        for (int c = 0; c < 1200; c++) {
            const int As[4] = {2, 3, 4, 256};
            int A = As[c % 4];
            int m = (c % 10 == 9) ? 17 + (int)(xs32() % 200) : 1 + (int)(xs32() % 16);
            int n = (int)(xs32() % 3000);
            for (int i = 0; i < m; i++) p[i] = rnd_sym(A);
            for (int i = 0; i < n; i++) g_text[i] = rnd_sym(A);
            if (c % 5 == 0)                               /* 패턴을 일부러 심는다 (겹치게도) */
                for (int r = 0; r < 20 && n > m; r++) { int at = (int)(xs32() % (uint32_t)(n - m + 1)); memcpy(g_text + at, p, (size_t)m); }
            kmp_build(p, m, f);
            int n0 = naive_find(g_text, n, p, m, g_e1, BIGN);
            kmp_t k; kmp_init(&k, p, f, m);
            int n1 = kmp_feed(&k, g_text, n, 0, g_e2, BIGN);
            if (!same_ends(g_e2, n1, g_e1, n0)) bad_one++;
            if (k.steps > (uint32_t)n) bad_amort++;       /* 퍼텐셜 논증: 총 후퇴 ≤ 총 전진 ≤ n */
            total_steps += k.steps;
            kmp_init(&k, p, f, m);
            int n2 = 0, i = 0;
            while (i < n) {                               /* 1..37 바이트 덩어리, 상태는 k 에만 */
                int len = 1 + (int)(xs32() % 37); if (len > n - i) len = n - i;
                n2 += kmp_feed(&k, g_text + i, len, (uint32_t)i, g_e3 + n2, BIGN - n2);
                i += len;
            }
            if (!same_ends(g_e3, n2, g_e1, n0)) bad_chunk++;
            dfa_build(p, m, f, g_dfa);
            dfa_t d = {g_dfa, (uint8_t)m, 0}; int nd = 0;
            for (int j = 0; j < n; j++) if (dfa_push(&d, g_text[j])) g_e2[nd++] = (uint32_t)j;
            if (!same_ends(g_e2, nd, g_e1, n0)) bad_dfa++;
            rk_t r; rk_init(&r, p, m, c & 1, (c & 1) ? 1000003u : 0x01000193u, true);
            int nr = 0; i = 0;
            while (i < n) {
                int len = 1 + (int)(xs32() % 37); if (len > n - i) len = n - i;
                nr += rk_feed(&r, g_text + i, len, (uint32_t)i, g_e2 + nr, BIGN - nr);
                i += len;
            }
            if (!same_ends(g_e2, nr, g_e1, n0)) bad_rk++;
            total_matches += n0; total_bytes += n; cases++;
        }
        printf("random: %d cases, %ld bytes, %ld matches | mismatches one-shot=%d chunked=%d dfa=%d rk=%d | "
               "fallback steps total=%ld (%.3f/byte), cases over n: %d\n",
               cases, total_bytes, total_matches, bad_one, bad_chunk, bad_dfa, bad_rk,
               total_steps, total_bytes ? (double)total_steps / (double)total_bytes : 0.0, bad_amort);
        CHECK(bad_one == 0); CHECK(bad_chunk == 0); CHECK(bad_amort == 0 && total_steps > 0);
        CHECK(bad_dfa == 0); CHECK(bad_rk == 0);
    }
    {   /* 최악 바이트: p = a^m, 상태 m-1 에서 'b' → fail 링크 m-1 번 */
        uint8_t p[KMP_MAXM], f[KMP_MAXM];
        const int ms[3] = {8, 32, 255};
        int ok = 1;
        for (int z = 0; z < 3; z++) {
            int m = ms[z];
            memset(p, 'a', (size_t)m); kmp_build(p, m, f);
            kmp_t k; kmp_init(&k, p, f, m);
            int n = 0;
            for (int r = 0; r < 50; r++) { memset(g_text + n, 'a', (size_t)(m - 1)); n += m - 1; g_text[n++] = 'b'; }
            int nm = kmp_feed(&k, g_text, n, 0, g_e1, BIGN);
            printf("worst byte: p=a^%d, text=(a^%d b)^50: max fallback/byte=%u, total=%u over %d bytes, matches=%d\n",
                   m, m - 1, k.max_steps, k.steps, n, nm);
            ok &= (k.max_steps == (uint32_t)(m - 1) && k.steps <= (uint32_t)n && nm == 0);
        }
        CHECK(ok);
    }
    /* ---------- Part B: DFA 는 정의(δ) 그대로인가 ---------- */
    {
        int bad = 0, cells = 0;
        uint8_t p[10], f[10];
        const uint8_t cs[3] = {'a', 'b', 'z'};
        for (int m = 1; m <= 8; m++)
            for (uint32_t pb = 0; pb < (1u << m); pb++) {
                for (int i = 0; i < m; i++) p[i] = (uint8_t)('a' + ((pb >> i) & 1));
                kmp_build(p, m, f); dfa_build(p, m, f, g_dfa);
                for (int q = 0; q <= m; q++)
                    for (int ci = 0; ci < 3; ci++, cells++)
                        if (g_dfa[256 * q + cs[ci]] != brute_delta(p, m, q, cs[ci])) bad++;
                for (int q = 0; q <= m; q++)       /* 패턴에 없는 바이트는 전부 같은 값이어야 (= 'z' 칸) */
                    for (int c = 0; c < 256; c++)
                        if (c != 'a' && c != 'b' && g_dfa[256 * q + c] != g_dfa[256 * q + 'z']) bad++;
            }
        printf("dfa vs definition: %d cells checked, %d wrong\n", cells, bad);
        CHECK(bad == 0 && cells > 0);
    }
    /* ---------- Part B: Rabin-Karp ---------- */
    {   /* mod_p 접기 == % */
        int bad = 0;
        for (int i = 0; i < 200000; i++) {
            uint64_t x = ((uint64_t)xs32() << 31) ^ xs32();
            if (i < 4) x = (uint64_t[]){0, RK_P, 2ull * RK_P, (1ull << 62) - 1}[i];
            if (mod_p(x) != x % RK_P) bad++;
        }
        CHECK(bad == 0);
    }
    {   /* 창이 차기 전 보고 금지: 패턴 00 00 7E, 스트림이 7E 로 시작 */
        const uint8_t p[] = {0x00, 0x00, 0x7E}, t[] = {0x7E, 0x00, 0x00, 0x7E};
        uint32_t e[4]; rk_t r;
        rk_init(&r, p, 3, false, 0x01000193u, true);
        int n1 = rk_feed(&r, t, 4, 0, e, 4);
        CHECK(n1 == 1 && e[0] == 3);
        rk_init(&r, p, 3, true, 1000003u, true);
        int n2 = rk_feed(&r, t, 4, 0, e, 4);
        CHECK(n2 == 1 && e[0] == 3);
    }
    {   /* 충돌 1 — Thue–Morse: 길이 128, 홀수 base 라면 무엇이든 mod 2^32 해시가 같다 */
        uint8_t tm[128], tmc[128];
        thue_morse(tm, 128, false); thue_morse(tmc, 128, true);
        const uint32_t bases[5] = {31, 257, 0x01000193u, 0x9E3779B1u, 0x5BD1E995u};
        int coll = 0;
        for (int i = 0; i < 5; i++) coll += rk_hash(tm, 128, false, bases[i]) == rk_hash(tmc, 128, false, bases[i]);
        int coll64 = 0;                                  /* 길이 64 (k=6) 는 보장 안 됨 → 몇 개나? */
        for (int i = 0; i < 5; i++) {
            uint8_t a[64], b[64]; thue_morse(a, 64, false); thue_morse(b, 64, true);
            coll64 += rk_hash(a, 64, false, bases[i]) == rk_hash(b, 64, false, bases[i]);
        }
        bool pdiff = rk_hash(tm, 128, true, 1000003u) != rk_hash(tmc, 128, true, 1000003u);
        rk_t r; uint32_t e[4];
        rk_init(&r, tm, 128, false, 0x01000193u, false);
        int nv = rk_feed(&r, tmc, 128, 0, e, 4);                 /* verify 끔: 가짜 매칭 */
        rk_init(&r, tm, 128, false, 0x01000193u, true);
        int nv2 = rk_feed(&r, tmc, 128, 0, e, 4);                /* verify 켬: 걸러냄 */
        printf("thue-morse 128: mod 2^32 collisions %d/5 bases (len 64: %d/5), mod p differs=%d | "
               "no-verify matches=%d, verify matches=%d (false_hits=%u)\n", coll, coll64, pdiff, nv, nv2, r.false_hits);
        CHECK(coll == 5); CHECK(pdiff); CHECK(nv == 1 && nv2 == 0 && r.false_hits == 1);
    }
    {   /* 충돌 2 — 생일 공격: mod (2^31-1), m=8. 2^18 개 문자열의 해시를 정렬해 같은 쌍 찾기 */
        const uint32_t B = 1000003u;
        uint8_t s[8], a[8], b[8];
        for (uint32_t i = 0; i < BDAY_N; i++) { bday_str(i, s); g_bday[i] = ((uint64_t)rk_hash(s, 8, true, B) << 32) | i; }
        qsort(g_bday, BDAY_N, sizeof g_bday[0], cmp_u64);
        int pairs = 0; uint32_t ia = 0, ib = 0;
        for (uint32_t i = 1; i < BDAY_N; i++)
            if ((g_bday[i] >> 32) == (g_bday[i - 1] >> 32)) {
                uint32_t x = (uint32_t)g_bday[i - 1], y = (uint32_t)g_bday[i];
                bday_str(x, a); bday_str(y, b);
                if (memcmp(a, b, 8) != 0) { if (!pairs) { ia = x; ib = y; } pairs++; }
            }
        bday_str(ia, a); bday_str(ib, b);
        rk_t r; uint32_t e[4];
        rk_init(&r, a, 8, true, B, false); int nv = rk_feed(&r, b, 8, 0, e, 4);
        rk_init(&r, a, 8, true, B, true);  int nv2 = rk_feed(&r, b, 8, 0, e, 4);
        printf("birthday mod 2^31-1: %u strings, %d colliding pairs (expected ~%.1f); first pair #%u/#%u hash=%08x | "
               "no-verify matches=%d, verify matches=%d\n", BDAY_N, pairs,
               (double)BDAY_N * (BDAY_N - 1) / 2.0 / (double)RK_P, ia, ib, rk_hash(a, 8, true, B), nv, nv2);
        CHECK(pairs >= 1 && rk_hash(a, 8, true, B) == rk_hash(b, 8, true, B) && memcmp(a, b, 8) != 0);
        CHECK(nv == 1 && nv2 == 0 && r.false_hits == 1);
    }
    {   /* 무작위 바이트 텍스트에서 해시 일치가 얼마나 헛되나 (verify 비용) */
        uint8_t p[8]; for (int i = 0; i < 8; i++) p[i] = (uint8_t)xs32();
        for (int i = 0; i < BIGN; i++) g_text[i] = (uint8_t)xs32();
        rk_t r32, rp; uint32_t e[8];
        rk_init(&r32, p, 8, false, 0x01000193u, true); rk_init(&rp, p, 8, true, 1000003u, true);
        int a = rk_feed(&r32, g_text, BIGN, 0, e, 8), b = rk_feed(&rp, g_text, BIGN, 0, e, 8);
        printf("random %d bytes, m=8: rk32 hash hits=%u false=%u | rkp hash hits=%u false=%u\n",
               BIGN, r32.hash_hits, r32.false_hits, rp.hash_hits, rp.false_hits);
        CHECK(a == 0 && b == 0 && r32.hash_hits == r32.false_hits && rp.hash_hits == rp.false_hits);
    }
    /* ---------- Part C: UART 프레임 동기 ---------- */
    {
        sync_t s; frame_t fr[8];
        uint8_t t[64]; int n = 0;
        /* C1 깨끗한 프레임 하나 */
        const uint8_t f1[] = {0x55, 0x55, 0x7E, 0x02, 0xAA, 0xBB};
        memcpy(t, f1, 6); t[6] = frame_ck(t + 3, 3); n = 7;
        sync_init(&s); int nf = sync_run(&s, t, n, false, fr, 8);
        CHECK(nf == 1 && fr[0].start == 0 && fr[0].len == 2 && fr[0].pay[0] == 0xAA && fr[0].pay[1] == 0xBB);
        /* C2 앞에 0x55 가 하나 더 (55 55 55 7E): 시작 1 */
        memmove(t + 1, t, 7); t[0] = 0x55; n = 8;
        sync_init(&s); nf = sync_run(&s, t, n, false, fr, 8);
        CHECK(nf == 1 && fr[0].start == 1);
        /* C3 가짜 프리앰블(len=5)이 진짜 프레임을 삼키려 한다: 55 55 7E 05 | 55 55 7E 01 42 ck */
        const uint8_t f3[] = {0x55, 0x55, 0x7E, 0x05, 0x55, 0x55, 0x7E, 0x01, 0x42};
        memcpy(t, f3, 9); t[9] = frame_ck(t + 7, 2); n = 10;
        CHECK(t[9] != frame_ck(t + 3, 6));                 /* 가짜 프레임은 정말 깨져 있어야 */
        sync_init(&s); nf = sync_run(&s, t, n, false, fr, 8);
        CHECK(nf == 1 && fr[0].start == 4 && fr[0].len == 1 && fr[0].pay[0] == 0x42 && s.false_syncs == 1);
        /* C4 len 범위 밖(0x00, 0x21) 바로 뒤에 진짜 프레임 */
        const uint8_t f4[] = {0x55, 0x55, 0x7E, 0x00, 0x55, 0x55, 0x7E, 0x21, 0x55, 0x55, 0x7E, 0x01, 0x07};
        memcpy(t, f4, 13); t[13] = frame_ck(t + 11, 2); n = 14;
        sync_init(&s); nf = sync_run(&s, t, n, false, fr, 8);
        CHECK(nf == 1 && fr[0].start == 8 && s.false_syncs == 2);
        /* C5 페이로드 안의 프리앰블은 무시 (유효 프레임 안에서는 헌팅 안 함) */
        const uint8_t f5[] = {0x55, 0x55, 0x7E, 0x05, 0x55, 0x55, 0x7E, 0x01, 0x00};
        memcpy(t, f5, 9); t[9] = frame_ck(t + 3, 6); n = 10;
        sync_init(&s); nf = sync_run(&s, t, n, false, fr, 8);
        CHECK(nf == 1 && fr[0].start == 0 && fr[0].len == 5);
    }
    {   /* C6 무작위 UART 스트림: 스트리밍(바이트·덩어리) == 오프라인 참조, 심은 프레임 회수 */
        static uint32_t starts[400];
        int bad = 0, bad_chunk = 0, ins = 0, rec = 0, explained = 0, unexplained = 0;
        long bytes = 0, steps = 0; uint32_t maxw = 0, fs = 0, ovf = 0;
        for (int c = 0; c < 120; c++) {
            int nfr = 50 + (int)(xs32() % 300);
            int n = gen_uart(g_text, nfr, starts);
            int nr = ref_frames(g_text, n, g_f1, 4096);
            sync_t s; sync_init(&s);
            int ns = sync_run(&s, g_text, n, false, g_f2, 4096);
            if (!same_frames(g_f1, nr, g_f2, ns)) bad++;
            steps += s.steps; bytes += n; fs += s.false_syncs; ovf += s.overflow;
            if (s.max_steps > maxw) maxw = s.max_steps;
            sync_init(&s);
            ns = sync_run(&s, g_text, n, true, g_f2, 4096);
            if (!same_frames(g_f1, nr, g_f2, ns)) bad_chunk++;
            for (int i = 0, j = 0; i < nfr; i++) {           /* 심은 프레임 → 회수됐나, 아니면 왜 */
                while (j < nr && g_f1[j].start < starts[i]) j++;
                ins++;
                if (j < nr && g_f1[j].start == starts[i]) { rec++; continue; }
                bool inside = j > 0 && starts[i] < g_f1[j - 1].start + PRE_M + g_f1[j - 1].len + 2u;
                if (inside) explained++; else unexplained++;  /* 우연히 유효한 가짜 프레임이 삼킴 */
            }
        }
        printf("uart streams: 120, %ld bytes | frames inserted=%d recovered=%d swallowed-by-valid-false-frame=%d other=%d | "
               "false syncs=%u | stream!=ref: byte=%d chunked=%d | work %.3f bytes/byte, max per push=%u, fifo overflow=%u\n",
               bytes, ins, rec, explained, unexplained, fs, bad, bad_chunk, (double)steps / (double)bytes, maxw, ovf);
        CHECK(bad == 0); CHECK(bad_chunk == 0);
        CHECK(rec + explained == ins && unexplained == 0 && rec > ins * 9 / 10);
        CHECK(ovf == 0 && steps <= 13L * bytes);           /* 바이트당 처리 ≤ 1 + ⌈34/3⌉ = 13 (5. 풀이) */
    }
    {   /* C7 최악의 push 하나: 가짜 프리앰블을 겹겹이 — 마지막 바이트가 연쇄 재생을 일으킨다 */
        /* 오프셋 4z 에 프리앰블 + len = 32-4z  → 8개 프레임의 ck 가 모두 같은 바이트(36)에 온다 */
        uint8_t t[64]; int lens[8];
        const int n = PRE_M + 1 + FR_MAXP + 1;             /* 37 바이트, ck 위치 = 36 */
        memset(t, 0x00, sizeof t);
        for (int z = 0; z < 8; z++) {
            lens[z] = FR_MAXP - 4 * z;
            t[4 * z] = 0x55; t[4 * z + 1] = 0x55; t[4 * z + 2] = 0x7E; t[4 * z + 3] = (uint8_t)lens[z];
        }
        t[n - 1] = 0xA5;                                    /* ck 위치: 모든 가짜 프레임이 틀리게 */
        int okck = 1;
        for (int z = 0; z < 8; z++) okck &= t[n - 1] != frame_ck(t + 4 * z + PRE_M, lens[z] + 1);
        sync_t s; sync_init(&s); frame_t fr[8];
        sync_run(&s, t, n, false, fr, 8);
        printf("nested false preambles (lens %d..%d): %d bytes, total work=%u, max work in one push=%u, false syncs=%u\n",
               lens[7], lens[0], n, s.steps, s.max_steps, s.false_syncs);
        CHECK(okck && s.false_syncs >= 8 && s.max_steps > 8 && s.overflow == 0);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
