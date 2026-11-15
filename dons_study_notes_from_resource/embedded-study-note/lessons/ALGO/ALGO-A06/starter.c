/* ALGO-A06 센서 로그 압축: 델타·RLE·허프만
 *
 * 무손실 파이프라인:  int16 샘플 → 델타(mod 2^16) → zigzag → LEB128 varint → 바이트 RLE → 정적 정규 허프만
 *   Part A  델타 · zigzag (전단사) · varint (엄격한 디코더) · RLE (리터럴/런 제어 바이트)
 *   Part B  허프만 부호 길이(두 큐) · 길이 제한 ≤ 15 · 정규(canonical) 부호 · MSB-first 비트 I/O · 디코더
 *           Kraft 부등식 · 엔트로피 하한 · 작은 n 전수 탐색으로 최적성 확인
 *   Part C  온도(1 Hz, 0.01°C) · 3축 가속도(100 Hz, 4096 LSB/g) 합성 로그 → 블록 압축
 *           (블록마다 가장 작은 모드 선택 + stored 폴백), 단계별 바이트/엔트로피 표, 왕복 검증, 손상 입력 퍼징
 *
 * malloc 없음: 버퍼는 전부 MAX_* 크기의 정적 배열. 모든 테스트는 결정적 (xorshift32 고정 시드).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so -lm && ./so      (starter.c 도 같은 명령)
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NSYM 256
#define HUF_MAXBITS 15
#define MAX_SAMPLES 16384                          /* 한 블록의 최대 샘플 수 (nframes × nch) */
#define MAX_VARINT (3 * MAX_SAMPLES)               /* u16 varint 는 최대 3바이트 */
#define MAX_RLE (MAX_VARINT + MAX_VARINT / 128 + 1) /* RLE 최악 팽창 n + ⌈n/128⌉ */
#define HUF_HDR (4 + NSYM / 2)                     /* u32 기호 수 + 길이 니블 128바이트 */
#define MAX_HUF (HUF_HDR + 2 * MAX_RLE + 2)        /* 기호당 ≤ 15비트 < 2바이트 */
#define BLK_HDR 4                                  /* mode, nch, nframes(u16 LE) */
#define MAX_BLOCK (BLK_HDR + 2 * MAX_SAMPLES)      /* stored 폴백 덕분에 블록은 이보다 커지지 않는다 */

/* int16 ↔ 비트 패턴. int16 → uint16 변환은 2^16 나머지로 정의돼 있다 (C11 6.3.1.3p2).
 * 반대 방향 (uint16 > 32767 → int16) 은 구현 정의(6.3.1.3p3) 라서 int 산술로 직접 만든다. */
static uint16_t i16_bits(int16_t x) { return (uint16_t)x; }
static int16_t bits_i16(uint16_t u) { return u < 0x8000u ? (int16_t)u : (int16_t)((int32_t)u - 65536); }

/* ======================= Part A — 델타 · zigzag · varint · RLE ======================= */

/* zigzag: 16비트 2의 보수 패턴 d 를 0, −1, 1, −2, 2, … → 0, 1, 2, 3, 4, … 로 보낸다.
 *   d ≥ 0 → 2d,  d < 0 → −2d − 1.  부호 비트를 맨 아래로 옮기는 전단사 (5. 풀이에 증명). */
static uint16_t zz16_enc(uint16_t d) {
    (void)d;
    return 0; /* TODO 1: (d << 1) ^ (부호 비트를 16비트 전체로 퍼뜨린 마스크) — 전부 unsigned 산술로 */
}
static uint16_t zz16_dec(uint16_t z) {
    (void)z;
    return 0; /* TODO 1: (z >> 1) ^ (0 − (z & 1)) */
}

/* 델타: 채널 nch 개가 프레임 단위로 섞여(interleaved: x y z x y z …) 들어온다.
 *   d[i] = x[i] − x[i−nch]  (mod 2^16),  처음 nch 개는 기준값 0.
 * int16 의 차는 17비트가 필요하지만 2^16 나머지로 계산하면 16비트에 들어가고 디코더가 같은 나머지 덧셈으로 복원한다. */
static void delta16_enc(const int16_t *x, size_t n, int nch, uint16_t *d) {
    (void)x; (void)nch;
    memset(d, 0, n * sizeof *d); /* TODO 2: d[i] = (uint16_t)(비트패턴(x[i]) − 비트패턴(x[i−nch])), 앞 nch 개는 0 기준 */
}
static void delta16_dec(const uint16_t *d, size_t n, int nch, int16_t *x) {
    (void)d; (void)nch;
    memset(x, 0, n * sizeof *x); /* TODO 2: x[i] = bits_i16(이전 같은 채널 + d[i]) (mod 2^16) */
}

/* LEB128 (unsigned) varint, 16비트 값 전용: 7비트씩 아래부터, 바이트 최상위 비트 = "뒤에 더 있음".
 * vu16_put: p 에 1..3 바이트를 쓰고 개수 반환.
 * vu16_get: 엄격한 디코더 — 성공하면 소비한 바이트 수(1..3), 아니면 0.
 *   거부: 잘림(avail 부족) · 3바이트째에도 연속 비트 · 값 > 0xFFFF · 비최소 인코딩(마지막 바이트가 0x00, 첫 바이트 제외)
 *   → 디코드에 성공한 바이트열은 다시 인코드하면 똑같이 나온다 (인코딩이 유일). */
static int vu16_put(uint8_t *p, uint16_t v) {
    (void)p; (void)v;
    return 0; /* TODO 3: 7비트씩 아래부터, 뒤에 더 있으면 0x80 */
}
static int vu16_get(const uint8_t *p, size_t avail, uint16_t *v) {
    (void)p; (void)avail; (void)v;
    return 0; /* TODO 3: 엄격한 디코드 (잘림 · 3바이트 초과 · > 0xFFFF · 비최소 거부) */
}

/* 값 n 개 → varint 바이트열. 반환 = 바이트 수 (≤ 3n) */
static size_t varint_enc(const uint16_t *z, size_t n, uint8_t *out) {
    size_t o = 0;
    for (size_t i = 0; i < n; i++) o += (size_t)vu16_put(out + o, z[i]);
    return o;
}
/* 바이트열 → 정확히 n 개 값, 바이트를 남김없이 소비해야 성공(0). 실패 -1 */
static int varint_dec(const uint8_t *in, size_t len, uint16_t *z, size_t n) {
    size_t i = 0;
    for (size_t k = 0; k < n; k++) {
        int c = vu16_get(in + i, len - i, &z[k]);
        if (c == 0) return -1;
        i += (size_t)c;
    }
    return i == len ? 0 : -1;
}

/* 바이트 RLE — 형식 (이 레슨이 정한 것, PackBits 와 비슷하지만 같지 않다):
 *   c = 0x00..0x7F : 리터럴 — 뒤따르는 c+1 바이트(1..128)를 그대로 복사
 *   c = 0x80..0xFF : 런     — 뒤따르는 1바이트를 (c−0x80)+3 번(3..130) 반복
 * 길이 2 런은 리터럴로 둔다 (런 토큰 2바이트 = 리터럴 2바이트인데 리터럴 덩어리를 끊어 제어 바이트가 늘 수 있다).
 * 최악 크기 ≤ n + ⌈n/128⌉ (5. 풀이에 증명). rle_enc 반환 = 출력 바이트 수.
 * rle_dec 반환 = 출력 바이트 수, 형식 오류/잘림/cap 초과면 -1 (범위 밖 접근 없음). */
#define RLE_MIN_RUN 3
#define RLE_MAX_RUN 130
#define RLE_MAX_LIT 128
static size_t rle_enc(const uint8_t *in, size_t n, uint8_t *out) {
    (void)in; (void)n; (void)out;
    return 0; /* TODO 4: 런 ≥ 3 이면 [0x80+(r−3)][b], 아니면 리터럴 덩어리 (≤ 128) 에 모았다가 [len−1][bytes] */
}
static long rle_dec(const uint8_t *in, size_t n, uint8_t *out, size_t cap) {
    (void)in; (void)n; (void)out; (void)cap;
    return -1; /* TODO 4: 제어 바이트 해석, 입력/출력 경계 검사 후 복사 */
}

/* ======================= Part B — 정적 정규 허프만 ======================= */

/* 빈도 → 부호 길이 (제한 없음). 두 큐(two-queue) 방법: 잎을 빈도 오름차순으로 정렬해 두면
 * 새로 만드는 내부 노드의 무게는 비감소 → 내부 노드도 FIFO 하나로 충분하다 (힙 불필요, DS-B12 힙과 같은 결과).
 * 같은 무게면 잎을 먼저 뽑는다 (결정적 + 최대 깊이를 덜 키운다).
 * len[s] = 0 은 안 쓰는 기호. 기호가 하나뿐이면 길이 1. 반환 = 최대 길이 (기호가 없으면 0). */
static int huff_lengths(const uint32_t *freq, uint8_t *len) {
    (void)freq;
    memset(len, 0, NSYM);
    return 0; /* TODO 5: 잎 정렬 → 두 큐로 병합 → 부모 배열로 깊이 */
}

/* 길이 제한: 모든 길이 ≤ maxbits 이고 Kraft 합 Σ 2^-len ≤ 1 이 되게 고친다 (휴리스틱, 최적은 연습 1 package-merge).
 *   K = Σ 2^(maxbits−len) 를 정수로 들고 다닌다 (Kraft ≤ 1 ⇔ K ≤ 2^maxbits).
 *   1) maxbits 보다 긴 길이를 maxbits 로 자른다 → K 가 넘칠 수 있다.
 *   2) K > 2^maxbits 인 동안: len < maxbits 인 기호 중 가장 긴 것(같으면 빈도 작은 것, 그다음 기호 번호 큰 것)을 1 늘린다.
 *      한 번에 K 가 2^(maxbits−len−1) ≥ 1 줄어드니 반드시 끝난다.
 *   3) 남은 여유(K < 2^maxbits)는 빈도 큰 기호부터 길이를 1씩 줄여 채운다 (Kraft 를 넘지 않는 한).
 * 전제: 쓰는 기호 수 ≤ 2^maxbits (그래야 전부 maxbits 로 둘 수 있다). 이미 ≤ maxbits 면 아무것도 안 바뀐다. */
static void huff_limit(const uint32_t *freq, uint8_t *len, int maxbits) {
    (void)freq; (void)len; (void)maxbits; /* TODO 6: 자르기 → Kraft 넘침 갚기 → 여유 채우기 */
}

/* 정규 부호 (RFC 1951 3.2.2 의 방법): 길이별 개수 → 길이별 첫 부호 → 같은 길이 안에서는 기호 번호 순으로 1씩 증가.
 * code[s] 는 len[s] 비트짜리 값 (MSB 부터 내보낸다). 길이 > 15 이거나 과다 할당(Kraft > 1)이면 -1. */
static int huff_canon(const uint8_t *len, uint16_t *code) {
    (void)len;
    memset(code, 0, NSYM * sizeof *code);
    return 0; /* TODO 7: bl_count → next_code[b] → 기호 순으로 code[s] = next_code[len[s]]++ */
}

/* MSB-first 비트 쓰기/읽기. 쓰기: acc 의 아래 nacc(0..7) 비트가 아직 못 낸 비트. */
typedef struct { uint8_t *buf; size_t cap, pos; uint32_t acc; int nacc; int ovf; } bw_t;
typedef struct { const uint8_t *buf; size_t len, pos; int nbit; } br_t;   /* nbit = 현재 바이트에서 읽은 비트 수 */

/* bits 의 아래 n 비트(0 ≤ n ≤ 16)를 최상위 비트부터 쓴다. cap 을 넘는 바이트는 버리고 ovf = 1 (pos 는 계속 센다). */
static void bw_put(bw_t *w, uint32_t bits, int n) {
    (void)w; (void)bits; (void)n; /* TODO 8: acc 에 붙이고 8비트 모일 때마다 위에서부터 1바이트 */
}
static void bw_flush(bw_t *w) { if (w->nacc) bw_put(w, 0, 8 - w->nacc); }   /* 남은 비트를 0 으로 채워 1바이트 */

/* 다음 비트 1개 (0/1), 입력 끝이면 -1 */
static int br_bit(br_t *r) {
    (void)r;
    return -1; /* TODO 8: buf[pos] 의 (7 − nbit) 번 비트, 8개 읽으면 다음 바이트 */
}

/* 디코더 표: 길이별 부호 개수 + (길이, 기호 번호) 순으로 정렬한 기호. 정규 부호라 이것만으로 충분하다. */
typedef struct { uint16_t count[HUF_MAXBITS + 1]; uint8_t sym[NSYM]; int nsym; } hdec_t;

/* 반환: 남은 부호 공간(0 = 완전, > 0 = 불완전 — 기호 1개일 때), 길이 > 15 또는 과다 할당이면 -1 */
static int hdec_build(hdec_t *h, const uint8_t *len) {
    uint16_t offs[HUF_MAXBITS + 2];
    memset(h, 0, sizeof *h);
    for (int s = 0; s < NSYM; s++) {
        if (len[s] > HUF_MAXBITS) return -1;
        if (len[s]) { h->count[len[s]]++; h->nsym++; }
    }
    int left = 1;
    for (int b = 1; b <= HUF_MAXBITS; b++) {
        left = (left << 1) - h->count[b];
        if (left < 0) return -1;
    }
    offs[1] = 0;
    for (int b = 1; b <= HUF_MAXBITS; b++) offs[b + 1] = (uint16_t)(offs[b] + h->count[b]);
    for (int s = 0; s < NSYM; s++) if (len[s]) h->sym[offs[len[s]]++] = (uint8_t)s;
    return left;
}

/* 비트를 하나씩 읽으며 정규 부호를 푼다. 길이 L 의 첫 부호 first, 그 길이 부호 개수 count 를 유지:
 *   code − first < count 이면 sym[index + code − first], 아니면 다음 길이로 (first = (first+count)<<1).
 * 반환: 기호(0..255), 입력 끝이거나 15비트 안에 안 풀리면(불완전 부호의 빈 자리) -1. */
static int huff_dec_sym(const hdec_t *h, br_t *r) {
    (void)h; (void)r;
    return -1; /* TODO 9: 길이 1..15 로 늘려 가며 code − first < count[len] 인지 */
}

/* 허프만 블록: [u32 LE 기호 수 m] — m > 0 이면 [길이 니블 128B: 기호 2k 상위 4비트, 2k+1 하위 4비트][MSB-first 부호, 0 패딩]
 * 반환 = 바이트 수, cap 부족이면 0. len_out != NULL 이면 사용한 길이표를 복사. */
static size_t huff_block_enc(const uint8_t *in, size_t m, uint8_t *out, size_t cap, uint8_t *len_out) {
    uint32_t freq[NSYM] = {0};
    uint8_t len[NSYM];
    uint16_t code[NSYM];
    if (cap < 4 || m > 0xFFFFFFFFu) return 0;
    for (int k = 0; k < 4; k++) out[k] = (uint8_t)(m >> (8 * k));
    if (m == 0) { if (len_out) memset(len_out, 0, NSYM); return 4; }
    if (cap < HUF_HDR) return 0;
    for (size_t i = 0; i < m; i++) freq[in[i]]++;
    huff_lengths(freq, len);
    huff_limit(freq, len, HUF_MAXBITS);
    if (huff_canon(len, code) != 0) return 0;
    for (int k = 0; k < NSYM / 2; k++) out[4 + k] = (uint8_t)((len[2 * k] << 4) | (len[2 * k + 1] & 0x0F));
    bw_t w = { out + HUF_HDR, cap - HUF_HDR, 0, 0, 0, 0 };
    for (size_t i = 0; i < m; i++) bw_put(&w, code[in[i]], len[in[i]]);
    bw_flush(&w);
    if (w.ovf) return 0;
    if (len_out) memcpy(len_out, len, NSYM);
    return HUF_HDR + w.pos;
}

/* 반환 = 기호 수 m, 오류(헤더 잘림 · 과다 할당 · m > cap · 부호 오류 · 잘림 · 0 아닌 패딩 · 남는 바이트)면 -1 */
static long huff_block_dec(const uint8_t *in, size_t n, uint8_t *out, size_t cap) {
    if (n < 4) return -1;
    uint32_t m = (uint32_t)in[0] | (uint32_t)in[1] << 8 | (uint32_t)in[2] << 16 | (uint32_t)in[3] << 24;
    if (m == 0) return n == 4 ? 0 : -1;
    if (n < HUF_HDR || m > cap) return -1;
    uint8_t len[NSYM];
    for (int k = 0; k < NSYM / 2; k++) { len[2 * k] = in[4 + k] >> 4; len[2 * k + 1] = in[4 + k] & 0x0F; }
    hdec_t h;
    if (hdec_build(&h, len) < 0 || h.nsym == 0) return -1;
    br_t r = { in + HUF_HDR, n - HUF_HDR, 0, 0 };
    for (uint32_t i = 0; i < m; i++) {
        int s = huff_dec_sym(&h, &r);
        if (s < 0) return -1;
        out[i] = (uint8_t)s;
    }
    if (r.nbit == 0) return r.pos == r.len ? (long)m : -1;        /* 바이트 경계에서 끝: 남는 바이트 없어야 */
    if (r.pos + 1 != r.len) return -1;                            /* 마지막 바이트 안에서 끝나야 */
    if (r.buf[r.pos] & ((1u << (8 - r.nbit)) - 1u)) return -1;    /* 패딩 비트는 0 */
    return (long)m;
}

/* ======================= Part C — 블록 파이프라인 ======================= */
/* 블록 = [mode][nch][nframes u16 LE][payload]. 인코더는 다섯 후보 중 가장 작은 것을 고른다 (같으면 단순한 쪽).
 *   STORED: int16 LE 그대로 · V: varint · VR: varint→RLE · VH: varint→허프만 · VRH: varint→RLE→허프만
 * 따라서 블록 크기 ≤ 4 + 2·n (압축이 안 되는 입력도 4바이트 이상 늘지 않는다). */
enum { MODE_STORED = 0, MODE_V, MODE_VR, MODE_VH, MODE_VRH, MODE_COUNT };
static const char *const mode_name[MODE_COUNT] = { "stored", "V", "VR", "VH", "VRH" };

typedef struct {
    size_t n, raw, varint, rle, huf_v, huf_vr, out;   /* 샘플 수와 단계별 바이트 (huf_* = 0 이면 실패) */
    int mode;
    const uint16_t *zz;                               /* 단계 출력 (다음 pipe_encode 호출 전까지 유효) */
    const uint8_t *vb, *rb;
    uint8_t len_vr[NSYM];                             /* VRH 후보가 쓴 허프만 길이 */
} pstat_t;

static uint16_t s_zz[MAX_SAMPLES];
static uint8_t s_vb[MAX_VARINT + 4], s_rb[MAX_RLE + 4], s_h1[MAX_HUF], s_h2[MAX_HUF];

static size_t pipe_encode(const int16_t *x, size_t nframes, int nch, uint8_t *out, size_t cap, pstat_t *st) {
    size_t n = nframes * (size_t)nch;
    if (nch < 1 || nch > 8 || nframes > 0xFFFF || n > MAX_SAMPLES) return 0;
    delta16_enc(x, n, nch, s_zz);
    for (size_t i = 0; i < n; i++) s_zz[i] = zz16_enc(s_zz[i]);
    size_t nv = varint_enc(s_zz, n, s_vb);
    size_t nr = rle_enc(s_vb, nv, s_rb);
    size_t h1 = huff_block_enc(s_vb, nv, s_h1, sizeof s_h1, NULL);
    size_t h2 = huff_block_enc(s_rb, nr, s_h2, sizeof s_h2, st ? st->len_vr : NULL);
    size_t size[MODE_COUNT] = { 2 * n, nv, nr, h1 ? h1 : (size_t)-1, h2 ? h2 : (size_t)-1 };
    int mode = MODE_STORED;
    for (int m = 1; m < MODE_COUNT; m++) if (size[m] < size[mode]) mode = m;
    if (cap < BLK_HDR + size[mode]) return 0;
    out[0] = (uint8_t)mode; out[1] = (uint8_t)nch;
    out[2] = (uint8_t)(nframes & 0xFF); out[3] = (uint8_t)(nframes >> 8);
    uint8_t *p = out + BLK_HDR;
    switch (mode) {
    case MODE_STORED: for (size_t i = 0; i < n; i++) { uint16_t u = i16_bits(x[i]); p[2 * i] = (uint8_t)u; p[2 * i + 1] = (uint8_t)(u >> 8); } break;
    case MODE_V:   memcpy(p, s_vb, nv); break;
    case MODE_VR:  memcpy(p, s_rb, nr); break;
    case MODE_VH:  memcpy(p, s_h1, h1); break;
    default:       memcpy(p, s_h2, h2); break;
    }
    if (st) {
        st->n = n; st->raw = 2 * n; st->varint = nv; st->rle = nr; st->huf_v = h1; st->huf_vr = h2;
        st->out = BLK_HDR + size[mode]; st->mode = mode; st->zz = s_zz; st->vb = s_vb; st->rb = s_rb;
    }
    return BLK_HDR + size[mode];
}

/* 반환 = nframes (nch 는 *nch_out), 오류면 -1. x 에는 최대 cap 개 샘플만 쓴다. */
static uint16_t d_zz[MAX_SAMPLES];
static uint8_t d_vb[MAX_VARINT + 4], d_rb[MAX_RLE + 4];
static long pipe_decode(const uint8_t *in, size_t len, int16_t *x, size_t cap, int *nch_out) {
    if (len < BLK_HDR) return -1;
    int mode = in[0], nch = in[1];
    size_t nframes = (size_t)in[2] | (size_t)in[3] << 8, n = nframes * (size_t)nch;
    if (mode >= MODE_COUNT || nch < 1 || nch > 8 || n > MAX_SAMPLES || n > cap) return -1;
    const uint8_t *p = in + BLK_HDR;
    size_t pl = len - BLK_HDR;
    long nv;
    if (mode == MODE_STORED) {
        if (pl != 2 * n) return -1;
        for (size_t i = 0; i < n; i++) x[i] = bits_i16((uint16_t)(p[2 * i] | p[2 * i + 1] << 8));
        *nch_out = nch;
        return (long)nframes;
    }
    switch (mode) {
    case MODE_V:  if (pl > MAX_VARINT) return -1; memcpy(d_vb, p, pl); nv = (long)pl; break;
    case MODE_VR: nv = rle_dec(p, pl, d_vb, MAX_VARINT); break;
    case MODE_VH: nv = huff_block_dec(p, pl, d_vb, MAX_VARINT); break;
    default: {
        long nr = huff_block_dec(p, pl, d_rb, MAX_RLE);
        nv = nr < 0 ? -1 : rle_dec(d_rb, (size_t)nr, d_vb, MAX_VARINT);
    } }
    if (nv < 0 || varint_dec(d_vb, (size_t)nv, d_zz, n) != 0) return -1;
    for (size_t i = 0; i < n; i++) d_zz[i] = zz16_dec(d_zz[i]);
    delta16_dec(d_zz, n, nch, x);
    *nch_out = nch;
    return (long)nframes;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 0차 경험적 엔트로피 (bits/symbol) */
static double entropy(const uint32_t *hist, size_t nbins, uint64_t total) {
    double h = 0;
    for (size_t i = 0; i < nbins; i++) if (hist[i]) { double p = (double)hist[i] / (double)total; h -= p * log2(p); }
    return h;
}
static uint64_t huff_cost(const uint32_t *freq, const uint8_t *len) {
    uint64_t c = 0;
    for (int s = 0; s < NSYM; s++) c += (uint64_t)freq[s] * len[s];
    return c;
}
static uint32_t kraft(const uint8_t *len, int L) {          /* Σ 2^(L−len), 쓰는 기호만 */
    uint32_t k = 0;
    for (int s = 0; s < NSYM; s++) if (len[s]) k += len[s] <= L ? 1u << (L - len[s]) : 0x40000000u;
    return k;
}
static int max_len(const uint8_t *len) { int m = 0; for (int s = 0; s < NSYM; s++) if (len[s] > m) m = len[s]; return m; }

/* 전수 탐색 기준: 기호 0..n-1, 길이 1..Lmax 의 모든 조합 중 Kraft ≤ 1 인 것의 최소 비용 (Kraft–McMillan: 그런 길이면 접두 부호가 존재) */
static uint64_t bf_best;
static void bf_rec(const uint32_t *f, int n, int Lmax, int i, uint32_t K, uint64_t cost) {
    if (K > (1u << Lmax) || cost >= bf_best) return;
    if (i == n) { bf_best = cost; return; }
    for (int l = 1; l <= Lmax; l++) bf_rec(f, n, Lmax, i + 1, K + (1u << (Lmax - l)), cost + (uint64_t)f[i] * (uint64_t)l);
}
static uint64_t brute_opt(const uint32_t *f, int n, int Lmax) { bf_best = UINT64_MAX; bf_rec(f, n, Lmax, 0, 0, 0); return bf_best; }

/* 합성 로그 생성기 (5. 풀이 Part C 에 설명) */
static void gen_temp(int16_t *x, size_t n, uint32_t seed) {
    uint32_t s = seed;
    int32_t v = 0, drift = 0;                             /* 드리프트: 속도 v [μ°C/s] 의 평균 회귀 랜덤 워크 */
    for (size_t i = 0; i < n; i++) {
        int32_t ph = (int32_t)(i % 86400), tri = ph < 43200 ? ph : 86400 - ph;
        int32_t daily = tri * 5000 / 43200 - 2500;        /* 하루 주기 삼각파 ±2.5 °C (m°C) */
        v += (int32_t)(xs32(&s) % 401) - 200 - v / 32;
        drift += v - drift / 4096;                        /* μ°C, 수 시간 시간상수로 0 쪽으로 */
        int32_t noise = 0;
        for (int k = 0; k < 4; k++) noise += (int32_t)(xs32(&s) % 11) - 5;   /* σ ≈ 6.3 m°C */
        int32_t t = 24000 + daily + drift / 1000 + noise;  /* 진짜 값 + 잡음, m°C */
        if (t < 1000) t = 1000;
        int32_t code = (t * 16 + 500) / 1000;             /* 12비트 센서: 1/16 °C 로 반올림 양자화 */
        x[i] = (int16_t)((code * 100 + 8) / 16);          /* 0.01 °C 로 저장 = round(code × 6.25) */
    }
}
static void gen_accel(int16_t *x, size_t nframes, uint32_t seed) {
    uint32_t s = seed;
    const int32_t base[3] = { 40, -25, 4096 };            /* 4096 LSB/g, 평평하게 놓임: z = 1 g */
    int32_t amp = 0, left = 0, gain[3] = { 0, 0, 0 };
    for (size_t f = 0; f < nframes; f++) {
        if (left == 0 && xs32(&s) % 1500 == 0) {          /* 평균 15 초에 한 번 충격 */
            amp = 6000 + (int32_t)(xs32(&s) % 42000);
            left = 40;
            for (int a = 0; a < 3; a++) gain[a] = (int32_t)(xs32(&s) % 9) - 4;
        }
        for (int a = 0; a < 3; a++) {
            int32_t v = base[a];
            for (int k = 0; k < 3; k++) v += (int32_t)(xs32(&s) % 17) - 8;   /* σ ≈ 8.5 LSB */
            if (left) v += ((left / 3) & 1 ? 1 : -1) * amp * gain[a] / 4;     /* 감쇠 진동 */
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;                   /* 센서 포화 */
            x[3 * f + (size_t)a] = (int16_t)v;
        }
        if (left) { amp = amp * 7 / 8; left--; }
    }
}

static int roundtrip(const int16_t *x, size_t nframes, int nch, size_t *out_len, int *mode) {
    static uint8_t blk[MAX_BLOCK];
    static int16_t y[MAX_SAMPLES];
    pstat_t st;
    size_t b = pipe_encode(x, nframes, nch, blk, sizeof blk, &st);
    if (out_len) *out_len = b;
    if (mode) *mode = b ? st.mode : -1;
    if (b == 0 || b > BLK_HDR + 2 * nframes * (size_t)nch) return 0;
    int c = 0;
    long r = pipe_decode(blk, b, y, MAX_SAMPLES, &c);
    return r == (long)nframes && c == nch && memcmp(x, y, nframes * (size_t)nch * sizeof *x) == 0;
}

/* Part C 보고서: 블록마다 인코딩하며 단계별 바이트와 전역 히스토그램을 모은다 */
static uint32_t h16a[65536], h16b[65536];
typedef struct { size_t raw, varint, rle, huf_vr, out, hdr; uint64_t huf_bits; int nblk, modes[MODE_COUNT], ok;
                 double H16raw, H16zz, H8raw, H8zz, H8v, H8r; } report_t;
static report_t log_report(const char *name, const int16_t *x, size_t nframes, int nch, size_t blkframes) {
    static uint8_t blk[MAX_BLOCK];
    static int16_t y[MAX_SAMPLES];
    uint32_t h8raw[256] = {0}, h8zz[256] = {0}, h8v[256] = {0}, h8r[256] = {0};
    report_t R; memset(&R, 0, sizeof R); R.ok = 1;
    memset(h16a, 0, sizeof h16a); memset(h16b, 0, sizeof h16b);
    for (size_t f0 = 0; f0 < nframes; f0 += blkframes) {
        size_t nf = nframes - f0 < blkframes ? nframes - f0 : blkframes, n = nf * (size_t)nch;
        const int16_t *xb = x + f0 * (size_t)nch;
        pstat_t st;
        size_t b = pipe_encode(xb, nf, nch, blk, sizeof blk, &st);
        int c = 0;
        if (b == 0 || pipe_decode(blk, b, y, MAX_SAMPLES, &c) != (long)nf || memcmp(xb, y, n * sizeof *y) != 0) { R.ok = 0; continue; }
        R.nblk++; R.modes[st.mode]++;
        R.raw += st.raw; R.varint += st.varint; R.rle += st.rle; R.huf_vr += st.huf_vr; R.out += st.out;
        for (size_t i = 0; i < n; i++) {
            uint16_t u = i16_bits(xb[i]);
            h16a[u]++; h16b[st.zz[i]]++;
            h8raw[u & 0xFF]++; h8raw[u >> 8]++; h8zz[st.zz[i] & 0xFF]++; h8zz[st.zz[i] >> 8]++;
        }
        uint32_t fr[NSYM] = {0};
        for (size_t i = 0; i < st.varint; i++) h8v[st.vb[i]]++;
        for (size_t i = 0; i < st.rle; i++) { h8r[st.rb[i]]++; fr[st.rb[i]]++; }
        R.huf_bits += huff_cost(fr, st.len_vr);
        R.hdr += st.rle ? HUF_HDR : 4;
    }
    uint64_t N = (uint64_t)nframes * (uint64_t)nch;
    R.H16raw = entropy(h16a, 65536, N); R.H16zz = entropy(h16b, 65536, N);
    R.H8raw = entropy(h8raw, 256, 2 * N); R.H8zz = entropy(h8zz, 256, 2 * N);
    R.H8v = entropy(h8v, 256, R.varint); R.H8r = entropy(h8r, 256, R.rle);
    printf("== %s: %zu frames x %d ch, block = %zu frames, %d blocks ==\n", name, nframes, nch, blkframes, R.nblk);
    printf("  stage           bytes   %%raw   H0 (bits/sym, sym)     H0 bound bytes\n");
    printf("  raw int16    %8zu  100.0   %6.3f /16b  %5.3f /B  %8.0f\n", R.raw, R.H16raw, R.H8raw, R.H16raw * (double)N / 8);
    printf("  delta+zz     %8zu  100.0   %6.3f /16b  %5.3f /B  %8.0f\n", R.raw, R.H16zz, R.H8zz, R.H16zz * (double)N / 8);
    printf("  varint       %8zu  %5.1f              %5.3f /B  %8.0f\n", R.varint, 100.0 * (double)R.varint / (double)R.raw, R.H8v, R.H8v * (double)R.varint / 8);
    printf("  RLE          %8zu  %5.1f              %5.3f /B  %8.0f\n", R.rle, 100.0 * (double)R.rle / (double)R.raw, R.H8r, R.H8r * (double)R.rle / 8);
    printf("  Huffman(RLE) %8zu  %5.1f   payload %.3f bits/B, headers %zu B\n", R.huf_vr, 100.0 * (double)R.huf_vr / (double)R.raw,
           R.rle ? (double)R.huf_bits / (double)R.rle : 0.0, R.hdr);
    printf("  chosen/block %8zu  %5.1f   (= %.2f bits/sample)  modes:", R.out, 100.0 * (double)R.out / (double)R.raw, 8.0 * (double)R.out / (double)N);
    for (int m = 0; m < MODE_COUNT; m++) if (R.modes[m]) printf(" %s=%d", mode_name[m], R.modes[m]);
    printf("\n");
    return R;
}

#define TEMP_N 86400        /* 24 h × 1 Hz */
#define ACC_F 60000         /* 10 min × 100 Hz */
static int16_t g_temp[TEMP_N], g_acc[3 * ACC_F];

#ifndef ALGO_A06_NO_MAIN
int main(void) {
    uint32_t seed = 0xA06u;

    /* ---------- Part A: zigzag ---------- */
    {
        static uint8_t seen[65536];
        int bij = 1, formula = 1;
        memset(seen, 0, sizeof seen);
        for (uint32_t u = 0; u < 65536; u++) {
            uint16_t z = zz16_enc((uint16_t)u);
            int32_t d = bits_i16((uint16_t)u);
            if (seen[z]++ || zz16_dec(z) != u) bij = 0;
            if ((int32_t)z != (d >= 0 ? 2 * d : -2 * d - 1)) formula = 0;
        }
        CHECK(bij);                                          /* 65536 개 전부: 순열 + 역함수 */
        CHECK(formula);                                      /* d≥0 → 2d, d<0 → −2d−1 */
        CHECK(zz16_enc(i16_bits(-1)) == 1 && zz16_enc(1) == 2 && zz16_enc(i16_bits(INT16_MIN)) == 65535 && zz16_enc(INT16_MAX) == 65534);
    }
    /* ---------- Part A: 델타 (mod 2^16) ---------- */
    {
        int16_t x[] = { INT16_MAX, INT16_MIN, INT16_MAX, 0, -1, -1, 100 };
        uint16_t d[7]; int16_t y[7];
        delta16_enc(x, 7, 1, d);
        CHECK(d[0] == 32767 && d[1] == 1 && d[2] == 65535 && d[3] == 32769 && d[4] == 65535 && d[5] == 0 && d[6] == 101);
        delta16_dec(d, 7, 1, y);
        CHECK(memcmp(x, y, sizeof x) == 0);
        int ok = 1;
        static int16_t a[4096], b[4096]; static uint16_t dd[4096];
        for (int t = 0; t < 300; t++) {
            int nch = 1 + (int)(xs32(&seed) % 4); size_t n = xs32(&seed) % 4096;
            for (size_t i = 0; i < n; i++) a[i] = (int16_t)(xs32(&seed) & 0xFFFF);
            delta16_enc(a, n, nch, dd); delta16_dec(dd, n, nch, b);
            if (memcmp(a, b, n * sizeof *a) != 0) ok = 0;
        }
        CHECK(ok);                                           /* 무작위 int16, 채널 1..4: 왕복 일치 */
        int16_t xyz[] = { 10, 20, 30, 11, 18, 30 };          /* 채널별 델타 */
        uint16_t e[6];
        delta16_enc(xyz, 6, 3, e);
        CHECK(e[0] == 10 && e[1] == 20 && e[2] == 30 && e[3] == 1 && e[4] == i16_bits(-2) && e[5] == 0);
    }
    /* ---------- Part A: varint ---------- */
    {
        uint8_t p[4]; uint16_t v = 0;
        CHECK(vu16_put(p, 0) == 1 && p[0] == 0x00);
        CHECK(vu16_put(p, 127) == 1 && p[0] == 0x7F);
        CHECK(vu16_put(p, 128) == 2 && p[0] == 0x80 && p[1] == 0x01);
        CHECK(vu16_put(p, 300) == 2 && p[0] == 0xAC && p[1] == 0x02);      /* protobuf 문서의 예 */
        CHECK(vu16_put(p, 16383) == 2 && p[0] == 0xFF && p[1] == 0x7F);
        CHECK(vu16_put(p, 16384) == 3 && p[0] == 0x80 && p[1] == 0x80 && p[2] == 0x01);
        CHECK(vu16_put(p, 65535) == 3 && p[0] == 0xFF && p[1] == 0xFF && p[2] == 0x03);
        int ok = 1;
        for (uint32_t u = 0; u < 65536; u++) {
            int k = vu16_put(p, (uint16_t)u);
            if (vu16_get(p, (size_t)k, &v) != k || v != u || (k > 1 && vu16_get(p, (size_t)k - 1, &v) != 0)) ok = 0;
        }
        CHECK(ok);                                           /* 65536 개 왕복 + 한 바이트 모자라면 거부 */
        const uint8_t bad1[] = { 0x80, 0x00 }, bad2[] = { 0xFF, 0xFF, 0x04 }, bad3[] = { 0x80, 0x80, 0x80, 0x01 }, bad4[] = { 0xFF, 0x80, 0x00 };
        CHECK(vu16_get(bad1, 2, &v) == 0 && vu16_get(bad2, 3, &v) == 0 && vu16_get(bad3, 4, &v) == 0 && vu16_get(bad4, 3, &v) == 0);
        int canon = 1, accepted = 0;                         /* 무작위 바이트: 디코드되면 재인코딩이 같아야 (유일성) */
        for (int t = 0; t < 200000; t++) {
            uint8_t q[3] = { (uint8_t)xs32(&seed), (uint8_t)xs32(&seed), (uint8_t)xs32(&seed) }, r[4];
            int k = vu16_get(q, 3, &v);
            if (k) { accepted++; if (vu16_put(r, v) != k || memcmp(q, r, (size_t)k) != 0) canon = 0; }
        }
        CHECK(canon && accepted > 0);
    }
    /* ---------- Part A: RLE ---------- */
    {
        uint8_t out[600], back[600];
        const uint8_t s1[] = { 'A', 'A', 'A', 'B' };
        CHECK(rle_enc(s1, 4, out) == 4 && out[0] == 0x80 && out[1] == 'A' && out[2] == 0x00 && out[3] == 'B');
        const uint8_t s2[] = { 7, 7, 9 };                    /* 길이 2 런은 리터럴 */
        CHECK(rle_enc(s2, 3, out) == 4 && out[0] == 0x02 && out[1] == 7 && out[2] == 7 && out[3] == 9);
        uint8_t z[300]; memset(z, 0, sizeof z);
        CHECK(rle_enc(z, 130, out) == 2 && out[0] == 0xFF);  /* 최대 런 */
        CHECK(rle_enc(z, 131, out) == 4 && out[2] == 0x00);  /* 130 런 + 1 리터럴 */
        CHECK(rle_enc(z, 133, out) == 4 && out[2] == 0x80);  /* 130 런 + 3 런 */
        CHECK(rle_enc(z, 0, out) == 0 && rle_dec(out, 0, back, sizeof back) == 0);
        int ok = 1, bound = 1, tight = 1;
        static uint8_t a[5000], e[5100], d[5000];
        for (int t = 0; t < 600; t++) {
            size_t n = xs32(&seed) % 5000;
            int kind = t % 4;
            for (size_t i = 0; i < n; i++) {
                if (kind == 0) a[i] = (uint8_t)xs32(&seed);                         /* 랜덤 */
                else if (kind == 1) a[i] = (uint8_t)(xs32(&seed) % 3);              /* 작은 알파벳: 짧은 런 많음 */
                else if (kind == 2) a[i] = (uint8_t)(i % 2 ? 0x55 : 0xAA);          /* 주기 2: 런 없음 */
                else a[i] = (uint8_t)((i / (1 + xs32(&seed) % 200)) & 1);           /* 긴 런 섞임 */
            }
            size_t m = rle_enc(a, n, e);
            long r = rle_dec(e, m, d, sizeof d);
            if (r != (long)n || memcmp(a, d, n) != 0) ok = 0;
            if (m > n + (n + 127) / 128) bound = 0;
            if (kind == 2 && m != n + (n + 127) / 128) tight = 0;
        }
        CHECK(ok);                                           /* 600 개 입력 왕복 */
        CHECK(bound);                                        /* ≤ n + ⌈n/128⌉ */
        CHECK(tight);                                        /* 런 없는 입력은 정확히 그 한계 */
        const uint8_t t1[] = { 0x05, 1, 2 }, t2[] = { 0x85 }, t3[] = { 0xFF, 1 };
        CHECK(rle_dec(t1, 3, back, sizeof back) == -1 && rle_dec(t2, 1, back, sizeof back) == -1 && rle_dec(t3, 2, back, 129) == -1);
    }

    /* ---------- Part B: 허프만 길이 ---------- */
    {
        uint32_t f[NSYM] = {0}; uint8_t len[NSYM];
        f['a'] = 45; f['b'] = 13; f['c'] = 12; f['d'] = 16; f['e'] = 9; f['f'] = 5;   /* CLRS 의 예 */
        CHECK(huff_lengths(f, len) == 4 && len['a'] == 1 && len['b'] == 3 && len['c'] == 3 && len['d'] == 3 && len['e'] == 4 && len['f'] == 4);
        CHECK(huff_cost(f, len) == 224 && kraft(len, 15) == 1u << 15);
        memset(f, 0, sizeof f); f[42] = 7;
        CHECK(huff_lengths(f, len) == 1 && len[42] == 1 && max_len(len) == 1);   /* 기호 하나 → 길이 1 */
        memset(f, 0, sizeof f);
        CHECK(huff_lengths(f, len) == 0 && max_len(len) == 0);
        int opt = 1, kr = 1;
        for (int t = 0; t < 400; t++) {
            int n = 2 + t % 6;                                /* 2..7 기호 */
            memset(f, 0, sizeof f);
            for (int i = 0; i < n; i++) f[i] = 1 + xs32(&seed) % (t & 1 ? 1000 : 8);
            huff_lengths(f, len);
            if (huff_cost(f, len) != brute_opt(f, n, n - 1)) opt = 0;
            if (kraft(len, 15) != 1u << 15) kr = 0;          /* 허프만 트리는 꽉 찬 이진 트리 → Kraft 합 = 1 */
        }
        CHECK(opt);                                          /* 전수 탐색 최적값과 같은 비용 (400 개) */
        CHECK(kr);
    }
    /* ---------- Part B: 길이 제한 ---------- */
    {
        uint32_t f[NSYM] = {0}; uint8_t len[NSYM], len0[NSYM];
        uint32_t a = 1, b = 1;
        for (int i = 0; i < 20; i++) { f[i] = a; uint32_t c = a + b; a = b; b = c; }   /* 피보나치 빈도 */
        int d0 = huff_lengths(f, len0);
        memcpy(len, len0, sizeof len);
        huff_limit(f, len, HUF_MAXBITS);
        printf("fib20: unlimited max=%d cost=%llu | limited(15) max=%d cost=%llu\n", d0, (unsigned long long)huff_cost(f, len0),
               max_len(len), (unsigned long long)huff_cost(f, len));
        CHECK(d0 == 19 && max_len(len) == 15 && kraft(len, 15) <= 1u << 15);
        CHECK(huff_cost(f, len) >= huff_cost(f, len0));
        int same = 1, ok = 1, worse = 0, cases = 0;
        uint64_t extra = 0, base = 0;
        for (int t = 0; t < 300; t++) {                       /* 이미 ≤ 15 이면 그대로 */
            memset(f, 0, sizeof f);
            int n = 2 + (int)(xs32(&seed) % 255);
            for (int i = 0; i < n; i++) f[xs32(&seed) % NSYM] += 1 + (xs32(&seed) % 4 == 0 ? xs32(&seed) % 5000 : 0);
            if (huff_lengths(f, len0) > HUF_MAXBITS) continue;
            memcpy(len, len0, sizeof len);
            huff_limit(f, len, HUF_MAXBITS);
            if (memcmp(len, len0, sizeof len) != 0) same = 0;
        }
        CHECK(same);
        for (int t = 0; t < 120; t++) {                       /* 작은 한계 L 로 휴리스틱 vs 전수 탐색 최적 */
            int n = 4 + t % 5, L = 3 + t % 2;                 /* n ≤ 8 ≤ 2^L */
            memset(f, 0, sizeof f);
            for (int i = 0; i < n; i++) f[i] = (t % 3 == 0) ? (1u << i) : 1 + xs32(&seed) % 200;   /* 일부는 2의 거듭제곱(아주 치우침) */
            huff_lengths(f, len); huff_limit(f, len, L);
            uint64_t best = brute_opt(f, n, L), c = huff_cost(f, len);
            if (max_len(len) > L || kraft(len, L) > 1u << L || c < best) ok = 0;
            cases++; if (c > best) { worse++; extra += c - best; base += best; }
        }
        printf("limit heuristic vs brute-force optimum: %d/%d cases worse, total extra %llu bits over %llu\n",
               worse, cases, (unsigned long long)extra, (unsigned long long)base);
        CHECK(ok);                                           /* 길이 ≤ L, Kraft ≤ 1, 최적보다 좋을 수는 없음 */
    }
    /* ---------- Part B: 정규 부호 · 비트 I/O ---------- */
    {
        uint8_t len[NSYM] = {0}; uint16_t code[NSYM];
        const uint8_t L8[8] = { 3, 3, 3, 3, 3, 2, 4, 4 };                   /* RFC 1951 3.2.2 의 예 (A..H) */
        const uint16_t want[8] = { 2, 3, 4, 5, 6, 0, 14, 15 };              /* 010 011 100 101 110 00 1110 1111 */
        memcpy(&len['A'], L8, 8);
        CHECK(huff_canon(len, code) == 0 && memcmp(&code['A'], want, sizeof want) == 0);
        uint8_t over[NSYM] = {0}; over[0] = over[1] = over[2] = 1;
        CHECK(huff_canon(over, code) == -1);                 /* Kraft 3/2 > 1 */
        uint32_t seed2 = 77; int pf = 1;
        for (int t = 0; t < 200; t++) {                      /* 무작위 길이표 → 정규 부호는 접두 부호 */
            uint32_t f[NSYM] = {0};
            for (int i = 0; i < 200; i++) f[xs32(&seed2) % (8 + t)] += 1 + xs32(&seed2) % 50;
            huff_lengths(f, len); huff_limit(f, len, HUF_MAXBITS);
            if (huff_canon(len, code) != 0) { pf = 0; continue; }
            for (int s = 0; s < NSYM && pf; s++) for (int u = 0; u < NSYM && pf; u++) {
                if (s == u || !len[s] || !len[u] || len[s] > len[u]) continue;
                if ((code[u] >> (len[u] - len[s])) == code[s]) pf = 0;          /* s 가 u 의 접두사 */
            }
        }
        CHECK(pf);
        uint8_t buf[4] = {0};
        bw_t w = { buf, sizeof buf, 0, 0, 0, 0 };
        bw_put(&w, 0x5, 3); bw_put(&w, 0x1, 1); bw_put(&w, 0xF0, 8); bw_put(&w, 0x3, 2); bw_flush(&w);
        CHECK(w.pos == 2 && buf[0] == 0xBF && buf[1] == 0x0C && !w.ovf);   /* 101 1 11110000 11 00 */
        br_t r = { buf, 2, 0, 0 };
        const int bits[16] = { 1,0,1,1, 1,1,1,1, 0,0,0,0, 1,1,0,0 };
        int okb = 1;
        for (int i = 0; i < 16; i++) if (br_bit(&r) != bits[i]) okb = 0;
        CHECK(okb && br_bit(&r) == -1);
    }
    /* ---------- Part B: 허프만 블록 왕복 · 엔트로피 · 손상 ---------- */
    {
        static uint8_t a[6000], e[MAX_HUF], d[6000];
        int ok = 1, lower = 1, upper = 1;
        for (int t = 0; t < 400; t++) {
            size_t m = t < 4 ? (size_t)t : xs32(&seed) % 6000;
            int kind = t % 5;
            for (size_t i = 0; i < m; i++) {
                uint32_t r = xs32(&seed);
                if (kind == 0) a[i] = (uint8_t)r;                                  /* 균등 */
                else if (kind == 1) { int g = 0; while (g < 40 && (r & 1)) { g++; r >>= 1; } a[i] = (uint8_t)g; }  /* 기하분포 */
                else if (kind == 2) a[i] = 9;                                      /* 기호 하나 */
                else if (kind == 3) a[i] = (uint8_t)(r % 2 ? 0 : 255);             /* 두 기호 */
                else a[i] = (uint8_t)(r % 100 < 90 ? 0 : r >> 24);                /* 0 이 90 % */
            }
            uint8_t len[NSYM];
            size_t b = huff_block_enc(a, m, e, sizeof e, len);
            long r = huff_block_dec(e, b, d, sizeof d);
            if (b == 0 || r != (long)m || memcmp(a, d, m) != 0) ok = 0;
            if (m > 0) {
                uint32_t f[NSYM] = {0};
                for (size_t i = 0; i < m; i++) f[a[i]]++;
                double H = entropy(f, NSYM, m), avg = (double)huff_cost(f, len) / (double)m;
                if (avg < H - 1e-9) lower = 0;                                     /* 어떤 접두 부호도 H 아래로 못 간다 */
                uint8_t l0[NSYM];
                int distinct = 0;
                for (int s = 0; s < NSYM; s++) distinct += f[s] != 0;
                if (distinct >= 2 && huff_lengths(f, l0) <= HUF_MAXBITS && avg >= H + 1) upper = 0;  /* 허프만: H ≤ L̄ < H+1 (기호 ≥ 2) */
            }
        }
        CHECK(ok);                                           /* 400 개 (빈 입력 · 1바이트 · 기호 하나 · 균등 · 치우침) */
        CHECK(lower);
        CHECK(upper);
        a[0] = 1; a[1] = 2; a[2] = 3;                         /* 손상된 헤더/스트림 */
        size_t b = huff_block_enc(a, 3, e, sizeof e, NULL);
        uint8_t c[MAX_HUF];
        memcpy(c, e, b); c[4] = 0x11; c[5] = 0x11;           /* 기호 0..3 모두 길이 1 → 과다 할당 */
        long r1 = huff_block_dec(c, b, d, sizeof d);
        long r2 = huff_block_dec(e, b - 1, d, sizeof d);     /* 잘림 */
        memcpy(c, e, b); c[b] = 0;                           /* 남는 바이트 */
        long r3 = huff_block_dec(c, b + 1, d, sizeof d);
        long r4 = huff_block_dec(e, b, d, 2);                /* m > cap */
        CHECK(r1 == -1 && r2 == -1 && r3 == -1 && r4 == -1);
    }

    /* ---------- Part C: 합성 로그 ---------- */
    gen_temp(g_temp, TEMP_N, 2026);
    gen_accel(g_acc, ACC_F, 1111);
    {
        int16_t tmin = INT16_MAX, tmax = INT16_MIN; int amin = INT16_MAX, amax = INT16_MIN, sat = 0;
        for (size_t i = 0; i < TEMP_N; i++) { if (g_temp[i] < tmin) tmin = g_temp[i]; if (g_temp[i] > tmax) tmax = g_temp[i]; }
        for (size_t i = 0; i < 3 * ACC_F; i++) { if (g_acc[i] < amin) amin = g_acc[i]; if (g_acc[i] > amax) amax = g_acc[i];
                                                 if (g_acc[i] == INT16_MIN || g_acc[i] == INT16_MAX) sat++; }
        printf("temp: %d..%d (0.01 C)   accel: %d..%d LSB, %d saturated samples\n", tmin, tmax, amin, amax, sat);
    }
    report_t RT = log_report("temperature 1 Hz", g_temp, TEMP_N, 1, 4096);
    report_t RA = log_report("accel 100 Hz xyz", g_acc, ACC_F, 3, 1024);
    CHECK(RT.ok && RT.nblk == 22);                           /* 모든 블록 왕복 일치 */
    CHECK(RA.ok && RA.nblk == 59);
    CHECK(RT.out * 8 < RT.raw);                              /* 온도: 8배 넘게 줄어든다 */
    CHECK(RA.out * 2 < RA.raw);                              /* 가속도: 절반 이하 */
    CHECK(RT.rle * 2 < RT.varint && RA.rle >= RA.varint);    /* RLE 는 온도에만 효과 (가속도는 오히려 늘어남) */
    {
        size_t L; int mode, ok = 1, bound = 1;
        int16_t one = -5, ext[64];
        CHECK(roundtrip(&one, 0, 1, &L, &mode) && L == BLK_HDR);            /* 빈 블록 */
        CHECK(roundtrip(&one, 1, 1, &L, &mode));
        for (int i = 0; i < 64; i++) ext[i] = i % 2 ? INT16_MIN : INT16_MAX;   /* 최악처럼 보이는 진동 */
        CHECK(roundtrip(ext, 64, 1, &L, &mode) && mode == MODE_V && L == BLK_HDR + 3 + 63);  /* mod 2^16 델타 = ±1 → 1바이트씩 */
        static int16_t a[MAX_SAMPLES];
        for (size_t i = 0; i < MAX_SAMPLES; i++) a[i] = (int16_t)(xs32(&seed) & 0xFFFF);
        CHECK(roundtrip(a, MAX_SAMPLES / 4, 4, &L, &mode) && mode == MODE_STORED && L == BLK_HDR + 2 * MAX_SAMPLES);
        CHECK(pipe_encode(a, MAX_SAMPLES + 1, 1, NULL, 0, NULL) == 0);       /* 블록 한도 초과는 거부 */
        for (int t = 0; t < 300; t++) {                      /* 무작위 신호 종류 · 채널 수 · 길이 */
            int nch = 1 + (int)(xs32(&seed) % 4), kind = t % 4;
            size_t nf = xs32(&seed) % (MAX_SAMPLES / (size_t)nch + 1);
            int32_t v = 0, step = 1 + (int32_t)(xs32(&seed) % 3000);
            for (size_t i = 0; i < nf * (size_t)nch; i++) {
                if (kind == 0) v += (int32_t)(xs32(&seed) % (uint32_t)(2 * step + 1)) - step;
                else if (kind == 1) v = (int32_t)(xs32(&seed) & 0xFFFF) - 32768;
                else if (kind == 2) v = (int32_t)(i / 50 % 7) * 1000;
                else v = (xs32(&seed) % 100 == 0) ? (int32_t)(xs32(&seed) & 0xFFFF) - 32768 : v;
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                a[i] = (int16_t)v;
            }
            if (!roundtrip(a, nf, nch, &L, &mode)) ok = 0;
            if (L > BLK_HDR + 2 * nf * (size_t)nch) bound = 0;
        }
        CHECK(ok);
        CHECK(bound);                                        /* 어떤 입력도 4 + 2n 을 넘지 않는다 */
    }
    /* ---------- Part C: 손상 입력 — 범위 밖 쓰기 없음, 잘림/덧붙임 거부 ---------- */
    {
        static uint8_t blk[MAX_BLOCK + 16], bad[MAX_BLOCK + 16];
        static int16_t y[MAX_SAMPLES + 16];
        int canary = 1, trunc = 1;
        int res[2][3] = { { 0 } };                           /* [블록][오류 / 다른 데이터 / 같은 데이터] */
        for (int which = 0; which < 2; which++) {
            const int16_t *x = which ? g_acc : g_temp;
            size_t nf = which ? 1024 : 4096; int nch = which ? 3 : 1;
            size_t n = nf * (size_t)nch;
            size_t b = pipe_encode(x, nf, nch, blk, sizeof blk, NULL);
            int c;
            if (b <= BLK_HDR) { trunc = 0; continue; }       /* 인코더가 아무것도 못 만들었다 */
            for (size_t k = BLK_HDR; k < b; k++) if (pipe_decode(blk, k, y, MAX_SAMPLES, &c) != -1) trunc = 0;
            memcpy(bad, blk, b); bad[b] = 0;
            if (pipe_decode(bad, b + 1, y, MAX_SAMPLES, &c) != -1) trunc = 0;
            for (int t = 0; t < 3000; t++) {
                memcpy(bad, blk, b);
                size_t bit = BLK_HDR * 8 + xs32(&seed) % ((b - BLK_HDR) * 8);   /* 헤더 뒤 한 비트 뒤집기 */
                bad[bit / 8] ^= (uint8_t)(1u << (bit % 8));
                for (int i = 0; i < 16; i++) y[n + (size_t)i] = 0x5A5A;
                long r = pipe_decode(bad, b, y, n, &c);
                for (int i = 0; i < 16; i++) if (y[n + (size_t)i] != 0x5A5A) canary = 0;
                res[which][r < 0 ? 0 : memcmp(x, y, n * sizeof *y) ? 1 : 2]++;
            }
        }
        for (int t = 0; t < 3000; t++) {                     /* 아무 바이트열 (그럴듯한 헤더) */
            size_t len = BLK_HDR + xs32(&seed) % 3000;
            for (size_t i = 0; i < len; i++) bad[i] = (uint8_t)xs32(&seed);
            bad[0] = (uint8_t)(xs32(&seed) % MODE_COUNT); bad[1] = (uint8_t)(1 + xs32(&seed) % 3);
            int c;
            size_t cap = xs32(&seed) % 2000;
            for (int i = 0; i < 16; i++) y[cap + (size_t)i] = 0x5A5A;
            pipe_decode(bad, len, y, cap, &c);
            for (int i = 0; i < 16; i++) if (y[cap + (size_t)i] != 0x5A5A) canary = 0;
        }
        printf("1-bit flips (3000 each): temp block err=%d wrong=%d same=%d | accel block err=%d wrong=%d same=%d\n",
               res[0][0], res[0][1], res[0][2], res[1][0], res[1][1], res[1][2]);
        CHECK(canary);                                       /* 출력 cap 뒤를 한 번도 안 건드림 */
        CHECK(trunc);                                        /* 잘린 블록 · 1바이트 덧붙인 블록은 전부 거부 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* ALGO_A06_NO_MAIN */
