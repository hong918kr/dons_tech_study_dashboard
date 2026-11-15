/* COMM-A04 추가 연습 — 한 번에 검증
 *
 * 연습 1: 1의 보수 합이 못 잡는 2비트 오류 — 예측식 vs 전수 탐색 (IPv4 헤더 160비트, 모든 쌍)
 * 연습 2: 빠른 체크섬 — 32비트 little-endian 워드로 더하고 마지막에 바이트 교환 (RFC 1071 의 바이트 순서 독립성)
 * 연습 3: IPv4 조각 재조립 (고정 버퍼, 8바이트 블록 비트맵, 겹침 거절) 후 UDP 체크섬 검증
 * 연습 4: 하드웨어 RX 체크섬(L4 시작~프레임 끝의 1의 보수 합)으로 UDP 검증 — Ethernet 패딩을 빼야 한다
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint16_t rd16be(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void wr16be(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void wr32be(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t rd32le(const uint8_t *p) { return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0]; }

/* 본문과 같은 RFC 1071 합 (COMM-A04 solution.c 축약판) */
static uint32_t csum_add(uint32_t sum, const uint8_t *p, size_t n) {
    size_t i;
    for (i = 0; i + 1 < n; i += 2) sum += rd16be(p + i);
    if (n & 1u) sum += (uint32_t)p[n - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return sum;
}
static uint16_t csum_fold(uint32_t sum) { while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16); return (uint16_t)~sum; }
static uint32_t pseudo_sum(uint32_t src, uint32_t dst, uint16_t ulen) {
    uint8_t ph[12]; wr32be(ph, src); wr32be(ph + 4, dst); ph[8] = 0; ph[9] = 17; wr16be(ph + 10, ulen);
    return csum_add(0, ph, 12);
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)
static uint32_t g_rng = 0xA04A04u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

/* 랜덤 IPv4 헤더 (체크섬 채움) */
static void rand_ip_hdr(uint8_t *h) {
    for (int i = 0; i < 20; i++) h[i] = (uint8_t)xs32();
    h[0] = 0x45; h[10] = h[11] = 0;
    wr16be(h + 10, csum_fold(csum_add(0, h, 20)));
}

/* ---------- 연습 1 ---------- */
/* 비트 a, b (0..159, 헤더 안 비트 번호 = 바이트*8 + (7 - 비트)) 를 동시에 뒤집어도 합이 그대로인 쌍의 수 — 예측식 */
static unsigned predict_2bit(const uint8_t *h) {
    unsigned cnt = 0;
    for (int col = 0; col < 16; col++) {             /* 16비트 워드 안의 같은 자리(열) */
        unsigned ones = 0, zeros = 0;
        for (int w = 0; w < 10; w++) { if ((rd16be(h + 2 * w) >> col) & 1u) ones++; else zeros++; }
        cnt += ones * zeros;                          /* 한쪽은 1→0 (−2^k), 다른 쪽은 0→1 (+2^k) */
    }
    return cnt;
}
static unsigned brute_2bit(const uint8_t *h) {
    uint8_t t[20]; unsigned cnt = 0;
    for (int a = 0; a < 160; a++)
        for (int b = a + 1; b < 160; b++) {
            memcpy(t, h, 20);
            t[a / 8] ^= (uint8_t)(0x80u >> (a % 8)); t[b / 8] ^= (uint8_t)(0x80u >> (b % 8));
            if (csum_fold(csum_add(0, t, 20)) == 0) cnt++;
        }
    return cnt;
}

/* ---------- 연습 2 ---------- */
/* 32비트 LE 워드로 더해 64비트에 모은 뒤 16비트로 접고 바이트 교환 → csum_add(0, p, n) 과 같은 값 */
static uint16_t csum_fast32(const uint8_t *p, size_t n) {
    uint64_t s = 0; size_t i = 0;
    for (; i + 4 <= n; i += 4) s += rd32le(p + i);
    uint8_t tail[4] = { 0, 0, 0, 0 };
    if (i < n) { memcpy(tail, p + i, n - i); s += rd32le(tail); }
    while (s >> 16) s = (s & 0xFFFFu) + (s >> 16);
    return (uint16_t)((s >> 8) | ((s & 0xFFu) << 8));
}

/* ---------- 연습 3: 재조립 ---------- */
#define RA_MAX 2048u
enum { RA_PENDING = 0, RA_DONE = 1, RA_E_MISMATCH = -1, RA_E_OVERLAP = -2, RA_E_TOOBIG = -3, RA_E_BADLEN = -4 };
typedef struct {
    bool     used;
    uint32_t src, dst; uint16_t id; uint8_t proto;
    uint8_t  data[RA_MAX];
    uint8_t  have[RA_MAX / 8 / 8];        /* 8바이트 블록 비트맵 (256 블록) */
    size_t   total;                       /* 마지막 조각을 보면 정해짐, 0 = 아직 모름 */
    unsigned dups;
} reasm;
static bool blk_get(const reasm *r, size_t b) { return (r->have[b / 8] >> (b % 8)) & 1u; }
static void blk_set(reasm *r, size_t b) { r->have[b / 8] = (uint8_t)(r->have[b / 8] | (1u << (b % 8))); }
/* ip = IPv4 헤더 시작 (IHL 5 가정 아님), n = 이 조각의 IP total length 만큼 들어 있는 바이트 */
static int reasm_add(reasm *r, const uint8_t *ip, size_t n) {
    size_t hl = (size_t)(ip[0] & 15u) * 4u, tot = rd16be(ip + 2);
    if (hl < 20 || tot < hl || tot > n) return RA_E_BADLEN;
    uint16_t fw = rd16be(ip + 6);
    size_t off = (size_t)(fw & 0x1FFFu) * 8u, len = tot - hl;
    bool mf = (fw & 0x2000u) != 0;
    uint32_t src = ((uint32_t)rd16be(ip + 12) << 16) | rd16be(ip + 14), dst = ((uint32_t)rd16be(ip + 16) << 16) | rd16be(ip + 18);
    if (!r->used) { memset(r, 0, sizeof *r); r->used = true; r->src = src; r->dst = dst; r->id = rd16be(ip + 4); r->proto = ip[9]; }
    else if (r->src != src || r->dst != dst || r->id != rd16be(ip + 4) || r->proto != ip[9]) return RA_E_MISMATCH;
    if (mf && (len % 8u) != 0) return RA_E_BADLEN;           /* 마지막이 아닌 조각은 8의 배수 */
    if (off + len > RA_MAX) return RA_E_TOOBIG;
    if (!mf) { if (r->total && r->total != off + len) return RA_E_OVERLAP; r->total = off + len; }
    size_t b0 = off / 8u, b1 = (off + len + 7u) / 8u;
    bool all = true, any = false;
    for (size_t b = b0; b < b1; b++) { if (blk_get(r, b)) any = true; else all = false; }
    if (any) {
        if (all && memcmp(r->data + off, ip + hl, len) == 0) { r->dups++; return RA_PENDING; }   /* 똑같은 중복은 무시 */
        return RA_E_OVERLAP;                                  /* 부분 겹침·다른 내용 → 거절 (정책) */
    }
    memcpy(r->data + off, ip + hl, len);
    for (size_t b = b0; b < b1; b++) blk_set(r, b);
    if (!r->total) return RA_PENDING;
    for (size_t b = 0; b < (r->total + 7u) / 8u; b++) if (!blk_get(r, b)) return RA_PENDING;
    return RA_DONE;
}
static bool reasm_udp_ok(const reasm *r) {
    if (r->total < 8) return false;
    uint16_t ulen = rd16be(r->data + 4);
    if (ulen != r->total) return false;
    if (rd16be(r->data + 6) == 0) return true;
    return csum_fold(csum_add(pseudo_sum(r->src, r->dst, ulen), r->data, ulen)) == 0;
}

/* UDP 데이터그램 + IPv4 조각 만들기 */
static size_t mk_udp(uint8_t *u, uint32_t src, uint32_t dst, const uint8_t *pl, size_t n) {
    wr16be(u, 5000); wr16be(u + 2, 6000); wr16be(u + 4, (uint16_t)(8 + n)); wr16be(u + 6, 0);
    memcpy(u + 8, pl, n);
    uint16_t c = csum_fold(csum_add(pseudo_sum(src, dst, (uint16_t)(8 + n)), u, 8 + n));
    wr16be(u + 6, c ? c : 0xFFFF);
    return 8 + n;
}
static size_t mk_frag(uint8_t *ip, uint32_t src, uint32_t dst, uint16_t id, size_t off, bool mf, const uint8_t *d, size_t len) {
    memset(ip, 0, 20);
    ip[0] = 0x45; wr16be(ip + 2, (uint16_t)(20 + len)); wr16be(ip + 4, id);
    wr16be(ip + 6, (uint16_t)((mf ? 0x2000u : 0) | (off / 8u))); ip[8] = 64; ip[9] = 17;
    wr32be(ip + 12, src); wr32be(ip + 16, dst);
    wr16be(ip + 10, csum_fold(csum_add(0, ip, 20)));
    memcpy(ip + 20, d, len);
    return 20 + len;
}

/* ---------- 연습 4: 하드웨어 RX 합 ---------- */
/* MAC 이 "L4 시작부터 프레임 끝까지" 의 1의 보수 합을 디스크립터에 준다고 가정 (IP 길이를 모름 → 패딩 포함).
 * 패딩은 L4 시작 기준 offset ulen 에서 시작 — ulen 이 홀수면 패딩의 바이트 자리가 바뀌어 합이 바이트 교환된다. */
static uint16_t swap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static bool hw_verify(uint32_t hw_sum, const uint8_t *frame, size_t l4_off, size_t frame_len, uint32_t src, uint32_t dst,
                      uint16_t ulen, bool subtract_pad) {
    uint32_t s = hw_sum;
    if (subtract_pad) {
        size_t pad_off = l4_off + ulen, pad_n = frame_len - pad_off;
        uint16_t ps = (uint16_t)csum_add(0, frame + pad_off, pad_n);
        if (ulen & 1u) ps = swap16(ps);                       /* 홀수 자리에서 시작한 영역 */
        s += (uint16_t)~ps;                                   /* 1의 보수 뺄셈 = 보수를 더함 */
        while (s >> 16) s = (s & 0xFFFFu) + (s >> 16);
    }
    s += pseudo_sum(src, dst, ulen);
    return csum_fold(s) == 0;
}

int main(void) {
    /* 연습 1 */
    {
        uint8_t h[20]; unsigned mism = 0, tot_und = 0;
        for (int t = 0; t < 120; t++) {
            rand_ip_hdr(h);
            unsigned p = predict_2bit(h), b = brute_2bit(h);
            if (p != b) mism++;
            tot_und += b;
        }
        printf("ex1 120 random headers: undetected 2-bit pairs %u of %u (%.2f%%), predictor mismatches %u\n",
               tot_und, 120u * 12720u, 100.0 * tot_und / (120.0 * 12720.0), mism);
        CHECK(mism == 0 && tot_und > 0);
        static const uint8_t ref[20] = { 0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11, 0xb8, 0x61,
                                         0xc0, 0xa8, 0x00, 0x01, 0xc0, 0xa8, 0x00, 0xc7 };
        printf("ex1 example header 4500 0073 ... (b861): undetected pairs %u\n", brute_2bit(ref));
        CHECK(predict_2bit(ref) == brute_2bit(ref));
    }
    /* 연습 2 */
    {
        static uint8_t buf[1600]; unsigned mism = 0;
        for (int t = 0; t < 20000; t++) {
            size_t n = xs32() % 1501u, off = xs32() % 4u;
            for (size_t i = 0; i < n; i++) buf[off + i] = (uint8_t)xs32();
            if (csum_fast32(buf + off, n) != (uint16_t)csum_add(0, buf + off, n)) mism++;
        }
        memset(buf, 0xFF, 64);
        CHECK(csum_fast32(buf, 64) == 0xFFFFu);
        printf("ex2 fast32 vs bytewise: 20000 random buffers (len 0..1500, offset 0..3), mismatches %u\n", mism);
        CHECK(mism == 0);
    }
    /* 연습 3 */
    {
        static uint8_t pl[1600], dg[1608], f1[1600], f2[1600], f3[1600];
        for (int i = 0; i < 1600; i++) pl[i] = (uint8_t)(i * 7 + 3);   /* 본문 벡터 #5/#6 과 같은 데이터그램 */
        uint32_t A = 0xC0A8010Au, B = 0xC0A80114u;
        size_t dl = mk_udp(dg, A, B, pl, 1600);
        size_t n1 = mk_frag(f1, A, B, 0x2222, 0, true, dg, 1480), n2 = mk_frag(f2, A, B, 0x2222, 1480, false, dg + 1480, dl - 1480);
        reasm r; int rc;
        r.used = false;
        CHECK(reasm_add(&r, f1, n1) == RA_PENDING && reasm_add(&r, f2, n2) == RA_DONE && r.total == 1608 && reasm_udp_ok(&r));
        printf("ex3 vector #5+#6: total %zu, udp cksum 0x%04x %s\n", r.total, rd16be(r.data + 6), reasm_udp_ok(&r) ? "ok" : "BAD");
        r.used = false;
        CHECK(reasm_add(&r, f2, n2) == RA_PENDING && reasm_add(&r, f1, n1) == RA_DONE && reasm_udp_ok(&r));     /* 역순 */
        r.used = false;
        CHECK(reasm_add(&r, f1, n1) == RA_PENDING && reasm_add(&r, f1, n1) == RA_PENDING && r.dups == 1);        /* 중복 */
        size_t n3 = mk_frag(f3, A, B, 0x2222, 1472, false, dg + 1472, dl - 1472);                                 /* 8 B 겹침 */
        CHECK(reasm_add(&r, f3, n3) == RA_E_OVERLAP);
        n3 = mk_frag(f3, A, B, 0x2223, 1480, false, dg + 1480, dl - 1480);
        CHECK(reasm_add(&r, f3, n3) == RA_E_MISMATCH);
        r.used = false;
        memcpy(f3, f2, n2); f3[20 + 50] ^= 0x01;                                                                 /* 조각 2 payload 손상 */
        CHECK(reasm_add(&r, f1, n1) == RA_PENDING && reasm_add(&r, f3, n2) == RA_DONE && !reasm_udp_ok(&r));
        /* 랜덤: 3~5 조각 (8의 배수 경계), 무작위 순서 + 가끔 중복 → 항상 완성 + 체크섬 ok */
        unsigned good = 0, runs = 600;
        for (unsigned t = 0; t < runs; t++) {
            size_t plen = 64 + xs32() % 1400u;
            for (size_t i = 0; i < plen; i++) pl[i] = (uint8_t)xs32();
            uint32_t s = xs32(), d = xs32();
            dl = mk_udp(dg, s, d, pl, plen);
            size_t cuts[6]; unsigned nf = 3 + xs32() % 3u; cuts[0] = 0; cuts[nf] = dl;
            for (unsigned k = 1; k < nf; k++) {
                size_t lo = cuts[k - 1] + 8, hi = dl - 8 * (nf - k);
                cuts[k] = lo + 8u * ((xs32() % ((hi - lo) / 8u + 1u)));
                if (cuts[k] > hi) cuts[k] = hi;
                cuts[k] -= cuts[k] % 8u;
                if (cuts[k] <= cuts[k - 1]) cuts[k] = cuts[k - 1] + 8;
            }
            unsigned ord[6]; for (unsigned k = 0; k < nf; k++) ord[k] = k;
            for (unsigned k = nf - 1; k > 0; k--) { unsigned j = xs32() % (k + 1); unsigned x = ord[k]; ord[k] = ord[j]; ord[j] = x; }
            r.used = false; rc = RA_PENDING;
            for (unsigned k = 0; k < nf && rc == RA_PENDING; k++) {
                unsigned q = ord[k];
                size_t fl = mk_frag(f1, s, d, (uint16_t)t, cuts[q], q + 1 < nf, dg + cuts[q], cuts[q + 1] - cuts[q]);
                rc = reasm_add(&r, f1, fl);
                if (rc == RA_PENDING && (xs32() & 3u) == 0) rc = reasm_add(&r, f1, fl);
            }
            if (rc == RA_DONE && r.total == dl && memcmp(r.data, dg, dl) == 0 && reasm_udp_ok(&r)) good++;
        }
        printf("ex3 random 3..5 fragments in random order (+dups): %u/%u reassembled with udp ok\n", good, runs);
        CHECK(good == runs);
    }
    /* 연습 4 */
    {
        static uint8_t fr[128]; unsigned n = 0, sw_ok = 0, naive_ok = 0, fixed_ok = 0, zero_pad_naive_ok = 0, zero_pad_n = 0;
        for (int t = 0; t < 5000; t++) {
            size_t plen = xs32() % 19u;                       /* 짧은 payload → 60 B 까지 패딩 */
            uint32_t s = xs32(), d = xs32();
            memset(fr, 0, sizeof fr);
            uint8_t pl[32]; for (size_t i = 0; i < plen; i++) pl[i] = (uint8_t)xs32();
            size_t ul = mk_udp(fr + 34, s, d, pl, plen);
            fr[12] = 0x08; fr[13] = 0x00;
            size_t flen = 34 + ul < 60 ? 60 : 34 + ul;
            bool zero_pad = (t % 2) == 0;
            if (!zero_pad) for (size_t i = 34 + ul; i < flen; i++) fr[i] = (uint8_t)(xs32() | 1u);   /* 쓰레기 패딩 */
            if (t % 5 == 0 && plen) fr[42 + xs32() % plen] ^= 0x10;                                     /* 일부는 payload 손상 */
            uint32_t hw = csum_add(0, fr + 34, flen - 34);   /* "MAC 이 준 합" */
            bool sw = csum_fold(csum_add(pseudo_sum(s, d, (uint16_t)ul), fr + 34, ul)) == 0;
            bool nv = hw_verify(hw, fr, 34, flen, s, d, (uint16_t)ul, false);
            bool fx = hw_verify(hw, fr, 34, flen, s, d, (uint16_t)ul, true);
            n++; sw_ok += sw; naive_ok += (nv == sw); fixed_ok += (fx == sw);
            if (zero_pad) { zero_pad_n++; zero_pad_naive_ok += (nv == sw); }
        }
        printf("ex4 hw-sum verify vs software (5000 frames, half with garbage padding, 20%% corrupted): "
               "software ok %u, fixed agrees %u/%u, naive agrees %u/%u (zero-pad only: %u/%u)\n",
               sw_ok, fixed_ok, n, naive_ok, n, zero_pad_naive_ok, zero_pad_n);
        CHECK(fixed_ok == n && naive_ok < n && zero_pad_naive_ok == zero_pad_n);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
