/* BM-A04 펌웨어 이미지 헤더 + CRC/SHA-256 검증
 *
 * Part A — SHA-256 을 FIPS 180-4 대로 직접 구현 (압축 함수 + 패딩). CRC-32(zlib) 는 주어진다.
 *          FIPS 벡터 + python hashlib 이 만든 교차검증 상수(길이 0..300 전부, 큰 버퍼 6 개)로 확인.
 * Part B — img_verify(): 64 B 헤더(BM-A04 소유 포맷)를 싼 검사 → 비싼 검사 순서로, 필드를 믿기 전에 CRC 로
 *          확인하고, payload_size 가 UINT32_MAX 근처여도 넘치지 않게. 모든 단일 비트 플립 / 모든 길이 잘림 /
 *          롤백 / 3000 개 무작위 결함 / CRC 를 보존한 악의적 변조(→ SHA 가 잡음) / python 이 만든 이미지.
 * Part C — boot_select(): 두 슬롯(A/B) 중 무엇으로 부팅할지 + 현장 엔지니어가 읽을 부트 로그.
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 * 교차검증 모드 (레슨 폴더에서): ./so emit /tmp/BM-A04_c  |  ./so verify file.img [min]  |  ./so emitmut file
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================= 이미지 헤더 (BM-A04 소유 — A05/A06/A11 은 축약판) ======================= */
#define IMG_MAGIC        0x474D4942u      /* 메모리에 'B','I','M','G' */
#define IMG_HDR_VERSION  1u
#define IMG_HDR_SIZE     64u
#define IMG_HDR_CRC_OFF  60u              /* hdr_crc32 = CRC-32(bytes[0, 60)) */
#define IMG_FLAG_SHA256  (1u << 0)
#define IMG_FLAGS_KNOWN  IMG_FLAG_SHA256
#define IMG_MIN_PAYLOAD  8u               /* 최소한 초기 SP + 리셋 벡터 */

typedef struct {
    uint32_t magic;          /* 0x474D4942 */
    uint16_t hdr_version;    /* 1 */
    uint16_t hdr_size;       /* 64 */
    uint32_t img_version;    /* major<<24 | minor<<16 | patch */
    uint32_t payload_size;   /* 헤더 뒤 바이트 수 */
    uint32_t load_addr;      /* payload(벡터 테이블)가 놓여야 할 주소 */
    uint32_t flags;          /* bit0 = SHA-256 있음 */
    uint32_t payload_crc32;  /* CRC-32/ISO-HDLC (zlib) */
    uint8_t  sha256[32];     /* SHA-256(payload) */
    uint32_t hdr_crc32;      /* CRC-32(bytes[0, 60)) */
} img_hdr_t;
_Static_assert(sizeof(img_hdr_t) == 64, "img_hdr_t must be 64 bytes");
_Static_assert(offsetof(img_hdr_t, sha256) == 28 && offsetof(img_hdr_t, hdr_crc32) == 60, "layout");

/* 이 보드의 정책 (STM32F4 내부 플래시 섹터 5–7 = 0x08020000, 3 × 128 KB — RM0090 기준 대표값, 확인 권장) */
#define EXEC_ADDR  0x08020000u
#define EXEC_MAX   0x00060000u
#define VER(ma, mi, pa) (((uint32_t)(ma) << 24) | ((uint32_t)(mi) << 16) | (uint32_t)(pa))

typedef enum {
    IMG_OK = 0, IMG_ERR_ARG, IMG_EMPTY, IMG_ERR_BAD_MAGIC, IMG_ERR_BAD_HDR_VERSION, IMG_ERR_BAD_HDR_CRC,
    IMG_ERR_BAD_FLAGS, IMG_ERR_BAD_SIZE, IMG_ERR_TOO_BIG, IMG_ERR_BAD_LOAD_ADDR, IMG_ERR_ROLLBACK, IMG_ERR_BAD_CRC,
    IMG_ERR_BAD_SHA, IMG_RC_COUNT
} img_rc_t;
static const char *const RC_NAME[IMG_RC_COUNT] = {
    "OK", "ERR_ARG", "EMPTY", "BAD_MAGIC", "BAD_HDR_VERSION", "BAD_HDR_CRC", "BAD_FLAGS",
    "BAD_SIZE", "TOO_BIG", "BAD_LOAD_ADDR", "ROLLBACK", "BAD_CRC", "BAD_SHA"};
static const char *img_rc_name(img_rc_t rc) { return (unsigned)rc < IMG_RC_COUNT ? RC_NAME[rc] : "?"; }

typedef struct {
    bool     hdr_ok;                     /* 헤더 CRC 까지 통과 → 아래 필드는 믿을 만함 (진단용) */
    uint32_t version, payload_size, load_addr, payload_crc32;
    uint8_t  sha256[32];
    uint32_t init_sp, reset_vec;         /* payload word0/word1 — OK 일 때만 */
} img_info_t;

/* ======================= 바이트 순서 도우미 ======================= */
static uint32_t ld_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t ld_le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void st_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void st_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint32_t ld_be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}
static void st_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/* ======================= CRC-32/ISO-HDLC (zlib) — 주어짐 (ALGO-M02 표 방식 축약판) ======================= */
static uint32_t crc_tab[256];
static bool crc_ready;
static void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : c >> 1;   /* 반사형 0x04C11DB7 */
        crc_tab[i] = c;
    }
    crc_ready = true;
}
/* zlib 의미: crc32_update(crc32_update(0, a), b) == crc32_update(0, a‖b) */
static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n) {
    if (!crc_ready) crc32_init();
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = crc_tab[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}
static uint32_t crc32(const uint8_t *p, size_t n) { return crc32_update(0, p, n); }

/* ======================= Part A: SHA-256 (FIPS 180-4) ======================= */
typedef struct { uint32_t h[8]; uint64_t nbytes; uint8_t buf[64]; uint32_t nbuf; } sha256_ctx;

static const uint32_t SHA_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
static const uint32_t SHA_H0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

/* TODO A1: 64 B 블록 하나를 상태 h[8] 에 섞는다.
 *   W[0..15] = 블록의 big-endian 워드, W[t] = σ1(W[t-2]) + W[t-7] + σ0(W[t-15]) + W[t-16]  (t = 16..63)
 *   σ0 = ROR7 ^ ROR18 ^ SHR3,  σ1 = ROR17 ^ ROR19 ^ SHR10,  Σ0 = ROR2 ^ ROR13 ^ ROR22,  Σ1 = ROR6 ^ ROR11 ^ ROR25
 *   라운드 64 번: T1 = h + Σ1(e) + Ch(e,f,g) + K[t] + W[t],  T2 = Σ0(a) + Maj(a,b,c), 레지스터 한 칸씩 밀기
 *   마지막에 h[i] += a..h  (모두 mod 2^32 — uint32_t 가 알아서) */
static void sha256_compress(uint32_t h[8], const uint8_t blk[64]) {
    uint32_t w[64];
    for (int t = 0; t < 16; t++) w[t] = ld_be32(blk + 4 * t);
    for (int t = 16; t < 64; t++) {
        uint32_t s0 = ROR32(w[t - 15], 7) ^ ROR32(w[t - 15], 18) ^ (w[t - 15] >> 3);
        uint32_t s1 = ROR32(w[t - 2], 17) ^ ROR32(w[t - 2], 19) ^ (w[t - 2] >> 10);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int t = 0; t < 64; t++) {
        uint32_t t1 = hh + (ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25)) + ((e & f) ^ (~e & g)) + SHA_K[t] + w[t];
        uint32_t t2 = (ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

static void sha256_init(sha256_ctx *c) {
    memcpy(c->h, SHA_H0, sizeof c->h);
    c->nbytes = 0;
    c->nbuf = 0;
}
static void sha256_update(sha256_ctx *c, const uint8_t *p, size_t n) {
    if (n == 0) return;
    c->nbytes += n;
    if (c->nbuf) {                                   /* 지난번에 남은 조각부터 채운다 */
        size_t k = 64u - c->nbuf;
        if (k > n) k = n;
        memcpy(c->buf + c->nbuf, p, k);
        c->nbuf += (uint32_t)k; p += k; n -= k;
        if (c->nbuf == 64u) { sha256_compress(c->h, c->buf); c->nbuf = 0; }
    }
    for (; n >= 64u; p += 64, n -= 64u) sha256_compress(c->h, p);
    if (n) { memcpy(c->buf, p, n); c->nbuf = (uint32_t)n; }
}

/* TODO A2: 패딩 + 출력. 0x80 한 바이트 → 0 으로 채워 블록 안 56 바이트 위치까지 → 메시지 길이(비트, uint64)
 *   big-endian 8 바이트 → 마지막 compress. 0x80 을 넣은 뒤 남은 자리가 8 바이트보다 적으면(nbuf > 56) 블록이
 *   하나 더 필요하다. 출력 = h[0..7] big-endian. */
static void sha256_final(sha256_ctx *c, uint8_t out[32]) {
    uint64_t bits = c->nbytes * 8u;
    c->buf[c->nbuf++] = 0x80;
    if (c->nbuf > 56u) {
        memset(c->buf + c->nbuf, 0, 64u - c->nbuf);
        sha256_compress(c->h, c->buf);
        c->nbuf = 0;
    }
    memset(c->buf + c->nbuf, 0, 56u - c->nbuf);
    for (int i = 0; i < 8; i++) c->buf[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    sha256_compress(c->h, c->buf);
    for (int i = 0; i < 8; i++) st_be32(out + 4 * i, c->h[i]);
}

static void sha256(const uint8_t *p, size_t n, uint8_t out[32]) {
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, p, n);
    sha256_final(&c, out);
}

/* ======================= 헤더 직렬화 / 이미지 만들기 (주어짐) ======================= */
/* 구조체를 memcpy 하지 않고 필드별 little-endian — 호스트 엔디언·패딩과 무관 (타깃에선 memcpy 와 같다) */
static void img_hdr_load(const uint8_t *b, img_hdr_t *h) {
    h->magic = ld_le32(b + 0);         h->hdr_version = ld_le16(b + 4);  h->hdr_size = ld_le16(b + 6);
    h->img_version = ld_le32(b + 8);   h->payload_size = ld_le32(b + 12); h->load_addr = ld_le32(b + 16);
    h->flags = ld_le32(b + 20);        h->payload_crc32 = ld_le32(b + 24);
    memcpy(h->sha256, b + 28, 32);     h->hdr_crc32 = ld_le32(b + 60);
}
/* 헤더 버퍼 b[0..60) 의 CRC 를 다시 계산해 b[60..64) 에 넣는다 (도구·테스트가 필드를 바꾼 뒤 '봉인') */
static void img_hdr_seal(uint8_t *b) { st_le32(b + IMG_HDR_CRC_OFF, crc32(b, IMG_HDR_CRC_OFF)); }
static void img_hdr_store(const img_hdr_t *h, uint8_t *b) {
    st_le32(b + 0, h->magic);          st_le16(b + 4, h->hdr_version);   st_le16(b + 6, h->hdr_size);
    st_le32(b + 8, h->img_version);    st_le32(b + 12, h->payload_size); st_le32(b + 16, h->load_addr);
    st_le32(b + 20, h->flags);         st_le32(b + 24, h->payload_crc32);
    memcpy(b + 28, h->sha256, 32);
    img_hdr_seal(b);
}
/* 헤더 + payload 를 out 에 쓴다. 반환 = 총 길이, 실패 0 */
static uint32_t img_build(uint8_t *out, uint32_t cap, const uint8_t *payload, uint32_t n,
                          uint32_t version, uint32_t load_addr, uint32_t flags) {
    if (!out || (n && !payload) || cap < IMG_HDR_SIZE || n > cap - IMG_HDR_SIZE) return 0;
    img_hdr_t h;
    memset(&h, 0, sizeof h);
    h.magic = IMG_MAGIC; h.hdr_version = IMG_HDR_VERSION; h.hdr_size = IMG_HDR_SIZE;
    h.img_version = version; h.payload_size = n; h.load_addr = load_addr; h.flags = flags;
    h.payload_crc32 = crc32(payload, n);
    if (flags & IMG_FLAG_SHA256) sha256(payload, n, h.sha256);
    img_hdr_store(&h, out);
    if (n) memcpy(out + IMG_HDR_SIZE, payload, n);
    return IMG_HDR_SIZE + n;
}

static bool all_ff(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) if (p[i] != 0xFFu) return false;
    return true;
}
/* 다이제스트 비교는 '처음 다른 바이트에서 멈추지 않게' — 서명/MAC 이면 필수, 해시도 습관으로 */
static bool ct_equal(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) d |= (uint8_t)(a[i] ^ b[i]);
    return d == 0;
}

/* ======================= Part B: img_verify ======================= */
/* TODO B1: 슬롯 바이트를 검사해 첫 번째로 걸린 오류 코드를 돌려준다. 순서(규격):
 *   slot NULL → ERR_ARG / slot_len < 64 → BAD_SIZE / 헤더 64 B 가 전부 0xFF → EMPTY
 *   magic → BAD_MAGIC / hdr_version != 1 또는 hdr_size != 64 → BAD_HDR_VERSION / CRC-32([0,60)) → BAD_HDR_CRC
 *   (여기부터 헤더 필드를 믿는다: out->hdr_ok = true, version/payload_size/load_addr/payload_crc32/sha256 채움)
 *   모르는 flags 비트 또는 SHA 비트 없음 → BAD_FLAGS
 *   payload_size < 8 또는 > slot_len − 64 (64 + size 덧셈 금지! 넘친다) → BAD_SIZE
 *   payload_size > EXEC_MAX (슬롯엔 맞아도 이 보드 실행 영역 초과) → TOO_BIG / load_addr != EXEC_ADDR → BAD_LOAD_ADDR
 *   img_version < min_version → ROLLBACK / payload CRC → BAD_CRC / SHA-256 (ct_equal) → BAD_SHA
 *   OK 면 out->init_sp / reset_vec = payload word0 / word1.  out 은 NULL 일 수 있다. */
static img_rc_t img_verify(const uint8_t *slot, uint32_t slot_len, uint32_t min_version, img_info_t *out) {
    img_info_t tmp;
    if (!out) out = &tmp;
    memset(out, 0, sizeof *out);
    if (!slot) return IMG_ERR_ARG;
    if (slot_len < IMG_HDR_SIZE) return IMG_ERR_BAD_SIZE;
    if (all_ff(slot, IMG_HDR_SIZE)) return IMG_EMPTY;
    img_hdr_t h;
    img_hdr_load(slot, &h);
    if (h.magic != IMG_MAGIC) return IMG_ERR_BAD_MAGIC;
    if (h.hdr_version != IMG_HDR_VERSION || h.hdr_size != IMG_HDR_SIZE) return IMG_ERR_BAD_HDR_VERSION;
    if (crc32(slot, IMG_HDR_CRC_OFF) != h.hdr_crc32) return IMG_ERR_BAD_HDR_CRC;
    out->hdr_ok = true;
    out->version = h.img_version; out->payload_size = h.payload_size;
    out->load_addr = h.load_addr; out->payload_crc32 = h.payload_crc32;
    memcpy(out->sha256, h.sha256, 32);
    if ((h.flags & ~IMG_FLAGS_KNOWN) || !(h.flags & IMG_FLAG_SHA256)) return IMG_ERR_BAD_FLAGS;
    if (h.payload_size < IMG_MIN_PAYLOAD || h.payload_size > slot_len - IMG_HDR_SIZE) return IMG_ERR_BAD_SIZE;
    if (h.payload_size > EXEC_MAX) return IMG_ERR_TOO_BIG;
    if (h.load_addr != EXEC_ADDR) return IMG_ERR_BAD_LOAD_ADDR;
    if (h.img_version < min_version) return IMG_ERR_ROLLBACK;
    const uint8_t *pl = slot + IMG_HDR_SIZE;
    if (crc32(pl, h.payload_size) != h.payload_crc32) return IMG_ERR_BAD_CRC;
    uint8_t d[32];
    sha256(pl, h.payload_size, d);
    if (!ct_equal(d, h.sha256, 32)) return IMG_ERR_BAD_SHA;
    out->init_sp = ld_le32(pl);
    out->reset_vec = ld_le32(pl + 4);
    return IMG_OK;
}

/* ======================= Part C: 두 슬롯 부트 결정 + 로그 ======================= */
typedef struct { char name; uint32_t addr; const uint8_t *data; uint32_t len; } boot_slot_t;
typedef struct { char *buf; size_t cap, len; bool truncated; } logbuf_t;

static void log_printf(logbuf_t *lb, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void log_printf(logbuf_t *lb, const char *fmt, ...) {
    if (!lb->buf || lb->cap == 0) { lb->truncated = true; return; }
    size_t room = lb->cap - lb->len;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(lb->buf + lb->len, room, fmt, ap);
    va_end(ap);
    if (n < 0) { lb->truncated = true; return; }
    if ((size_t)n >= room) { lb->len = lb->cap - 1; lb->truncated = true; }   /* 잘려도 NUL 로 끝난다 */
    else lb->len += (size_t)n;
}
#define VER_FMT "v%u.%u.%u"
#define VER_ARG(v) (unsigned)((v) >> 24), (unsigned)(((v) >> 16) & 0xFFu), (unsigned)((v) & 0xFFFFu)

/* 슬롯 한 줄 (주어짐) */
static void log_slot(logbuf_t *lb, const boot_slot_t *s, img_rc_t rc, const img_info_t *in) {
    log_printf(lb, "[boot] slot %c @0x%08X len=%u: %s", s->name, (unsigned)s->addr, (unsigned)s->len,
               img_rc_name(rc));
    if (rc == IMG_OK)
        log_printf(lb, " " VER_FMT " size=%u crc=0x%08X sha=%02x%02x%02x%02x.. sp=0x%08X pc=0x%08X",
                   VER_ARG(in->version), (unsigned)in->payload_size, (unsigned)in->payload_crc32,
                   in->sha256[0], in->sha256[1], in->sha256[2], in->sha256[3],
                   (unsigned)in->init_sp, (unsigned)in->reset_vec);
    else if (in->hdr_ok)                           /* 헤더는 믿을 만함 → 무엇이 망가졌는지 현장 진단에 남긴다 */
        log_printf(lb, " (hdr ok: " VER_FMT " size=%u)", VER_ARG(in->version), (unsigned)in->payload_size);
    log_printf(lb, "\n");
}

/* TODO C1: 두 슬롯을 img_verify 로 검사해 부팅할 슬롯 번호(0 = slots[0], 1 = slots[1])를 고른다.
 *   규칙: OK 인 슬롯 중 img_version 이 큰 쪽, 같으면 slots[0]. 둘 다 아니면 −1 (부트로더에 남아 DFU 대기).
 *   로그 (cap 이 작아도 넘치지 않게 — log_printf 가 처리):
 *     "[boot] anti-rollback min=v1.2.0\n"
 *     슬롯마다 log_slot(...)
 *     선택: "[boot] select %c v1.3.0 -> copy %u B to 0x%08X, jump reset=0x%08X\n"
 *     없음: "[boot] no bootable image -> stay in bootloader (DFU)\n"
 *   chosen 이 NULL 이 아니면 고른 슬롯의 info 를 복사. */
static int boot_select(const boot_slot_t slots[2], uint32_t min_version, char *log, size_t cap,
                       img_info_t *chosen) {
    logbuf_t lb = {log, cap, 0, false};
    if (log && cap) log[0] = '\0';
    log_printf(&lb, "[boot] anti-rollback min=" VER_FMT "\n", VER_ARG(min_version));
    img_info_t info[2];
    img_rc_t rc[2];
    for (int i = 0; i < 2; i++) {
        rc[i] = img_verify(slots[i].data, slots[i].len, min_version, &info[i]);
        log_slot(&lb, &slots[i], rc[i], &info[i]);
    }
    int pick = -1;
    if (rc[0] == IMG_OK) pick = 0;
    if (rc[1] == IMG_OK && (pick < 0 || info[1].version > info[0].version)) pick = 1;
    if (pick < 0) {
        log_printf(&lb, "[boot] no bootable image -> stay in bootloader (DFU)\n");
        return -1;
    }
    log_printf(&lb, "[boot] select %c " VER_FMT " -> copy %u B to 0x%08X, jump reset=0x%08X\n",
               slots[pick].name, VER_ARG(info[pick].version), (unsigned)info[pick].payload_size,
               (unsigned)info[pick].load_addr, (unsigned)info[pick].reset_vec);
    if (chosen) *chosen = info[pick];
    return pick;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xs32_state(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static uint32_t xs32(void) { return xs32_state(&g_rng); }

static void hex32(char out[65], const uint8_t d[32]) {
    for (int i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", d[i]);
}
static bool sha_hex_is(const uint8_t *p, size_t n, const char *hex) {
    uint8_t d[32]; char s[65];
    sha256(p, n, d); hex32(s, d);
    return strcmp(s, hex) == 0;
}
/* python mkimg.py make_payload 와 같은 '가짜 앱' */
static void make_payload(uint8_t *buf, uint32_t n, uint32_t seed) {
    st_le32(buf, 0x20020000u);
    st_le32(buf + 4, EXEC_ADDR + 0x1C1u);
    for (uint32_t i = 8; i < n; i++) buf[i] = (uint8_t)xs32_state(&seed);
}

/* python3 -I mkimg.py fixture 출력 그대로: make_payload(200, 0xF1C7A04), v1.2.3, 264 B */
static const uint8_t PY_FIXTURE[264] = {
    0x42, 0x49, 0x4D, 0x47, 0x01, 0x00, 0x40, 0x00, 0x03, 0x00, 0x02, 0x01, 0xC8, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x02, 0x08, 0x01, 0x00, 0x00, 0x00, 0xCB, 0x41, 0xEA, 0x0A, 0x3B, 0x6E, 0x89, 0xB6,
    0xFF, 0x08, 0x02, 0x7C, 0xFD, 0x39, 0xF1, 0xD9, 0x71, 0xAC, 0x73, 0xCB, 0x12, 0xBA, 0xDE, 0x5A,
    0xFF, 0x0E, 0x33, 0x4E, 0x85, 0xC0, 0x57, 0x4B, 0x05, 0xF4, 0x00, 0x1B, 0x28, 0x94, 0x01, 0x9E,
    0x00, 0x00, 0x02, 0x20, 0xC1, 0x01, 0x02, 0x08, 0x6A, 0x59, 0xE3, 0xE6, 0xC4, 0xB9, 0x11, 0x6E,
    0xDA, 0xEB, 0x1B, 0x9B, 0x24, 0x9A, 0x6A, 0xF9, 0x1E, 0xB8, 0x21, 0x7C, 0xB8, 0x4D, 0x78, 0xC6,
    0xAA, 0x6B, 0xB5, 0xE7, 0xC7, 0xA8, 0x8E, 0xD4, 0x13, 0x1B, 0x33, 0x51, 0x2A, 0x46, 0x6C, 0x8B,
    0xE0, 0xEC, 0x6A, 0x63, 0xE0, 0xDF, 0xC5, 0x78, 0x42, 0x6E, 0xBE, 0x93, 0x6A, 0xDE, 0x89, 0xB2,
    0x02, 0x14, 0x1A, 0x90, 0xF5, 0x39, 0x69, 0x03, 0xD9, 0xD0, 0x80, 0x00, 0x41, 0x2E, 0x6E, 0x28,
    0x22, 0xB5, 0x9B, 0x16, 0x50, 0x43, 0x5B, 0x3C, 0x8A, 0x9D, 0x0C, 0x29, 0x36, 0x2C, 0x22, 0x58,
    0xD6, 0x58, 0x36, 0xD1, 0xE5, 0x6D, 0x1D, 0x13, 0xC7, 0xC8, 0xEA, 0xC2, 0x64, 0xBE, 0x29, 0x63,
    0xA9, 0x79, 0xE1, 0x92, 0xFB, 0x97, 0x89, 0xF5, 0xC4, 0xFC, 0x56, 0x8F, 0x61, 0x3D, 0xCE, 0x3D,
    0x2F, 0xFD, 0x61, 0x31, 0x11, 0xFD, 0x62, 0x9A, 0x92, 0x30, 0xC9, 0x6A, 0xD7, 0x0B, 0xC4, 0x50,
    0x6E, 0x16, 0x5D, 0x15, 0x67, 0x65, 0xA2, 0xAE, 0x40, 0x13, 0xC8, 0xBC, 0xB9, 0x62, 0xF9, 0xCA,
    0xB5, 0x78, 0xFA, 0xD3, 0xA1, 0xB6, 0x6E, 0xA6, 0xF0, 0x85, 0xB5, 0x9D, 0x07, 0xBD, 0xF8, 0xAB,
    0xAA, 0x5F, 0x60, 0x12, 0xEF, 0x5A, 0x3D, 0xFB, 0x1E, 0x1F, 0xAF, 0x16, 0xC6, 0x83, 0x68, 0xA1,
    0x95, 0x90, 0x03, 0x03, 0x46, 0x46, 0x34, 0xA8,
};
/* python3 -I mkimg.py vectors 출력 (hashlib / zlib) */
#define PY_SMALL_SHA_CHAIN "68344c4f9d6a7272e5631831c8f24b4873f847f0a9d5f2627c81bf0254d8ab2d"
#define PY_SMALL_CRC_CHAIN 0x625B381Au
#define PY_BIG_SHA_CHAIN   "e30c418ca28781076ba3c6ec882e98bc2de9ab827644848701844500e7c400b9"

/* CRC-32 는 메시지에 대해 아핀(affine) → 4 바이트 창 pos..pos+3 을 GF(2) 연립방정식으로 풀어
 * 전체 CRC 를 target 으로 맞춘다 (공격자 흉내, 하네스 도우미). 성공 true */
static bool crc_forge4(uint8_t *buf, size_t n, size_t pos, uint32_t target) {
    uint32_t basis[32] = {0}, comb[32] = {0};
    memset(buf + pos, 0, 4);
    uint32_t base = crc32(buf, n);
    for (int j = 0; j < 32; j++) {
        buf[pos + (size_t)j / 8] = (uint8_t)(1u << (j % 8));
        uint32_t v = crc32(buf, n) ^ base, m = 1u << j;
        buf[pos + (size_t)j / 8] = 0;
        for (int b = 31; b >= 0 && v; b--) {
            if (!((v >> b) & 1u)) continue;
            if (!basis[b]) { basis[b] = v; comb[b] = m; break; }
            v ^= basis[b]; m ^= comb[b];
        }
    }
    uint32_t t = target ^ base, x = 0;
    for (int b = 31; b >= 0; b--) {
        if (!((t >> b) & 1u)) continue;
        if (!basis[b]) return false;
        t ^= basis[b]; x ^= comb[b];
    }
    st_le32(buf + pos, x);                           /* 창의 비트 j = 바이트 pos+j/8 의 비트 j%8 → LE 저장과 같다 */
    return crc32(buf, n) == target;
}

#define SLOT_LEN   512u
#define PL_LEN     256u
/* 기준 이미지 = 64 + 256 B, 슬롯 512 B 의 나머지는 0xFF (지워진 플래시) */
static uint32_t g_img_len;
static void fresh_image(uint8_t *slot, uint32_t version) {
    uint8_t pl[PL_LEN];
    memset(slot, 0xFF, SLOT_LEN);
    make_payload(pl, PL_LEN, 0xB00710ADu);
    g_img_len = img_build(slot, SLOT_LEN, pl, PL_LEN, version, EXEC_ADDR, IMG_FLAG_SHA256);
}

static void test_part_a(void) {
    printf("-- Part A: CRC-32 (주어짐) + SHA-256 (FIPS 180-4)\n");
    CHECK(crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);          /* CRC-32/ISO-HDLC check 값 */
    CHECK(crc32(NULL, 0) == 0);
    CHECK(sha_hex_is((const uint8_t *)"abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK(sha_hex_is(NULL, 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK(sha_hex_is((const uint8_t *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
                     "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));   /* 448 비트 */
    {   /* 백만 개의 'a' — 4096 B 조각으로 흘려 넣기 */
        static uint8_t a4k[4096];
        memset(a4k, 'a', sizeof a4k);
        sha256_ctx c; uint8_t d[32]; char s[65];
        sha256_init(&c);
        for (uint32_t left = 1000000u; left; ) { uint32_t k = left < 4096u ? left : 4096u; sha256_update(&c, a4k, k); left -= k; }
        sha256_final(&c, d); hex32(s, d);
        CHECK(strcmp(s, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0);
    }
    {   /* python hashlib / zlib 교차검증: 길이 0..300 전부 (패딩 경계 55/56/63/64/119/120 포함) */
        static uint8_t digests[301 * 32], crcs[301 * 4], buf[300];
        uint32_t s = 0xA04C0DE5u;
        for (uint32_t n = 0; n <= 300; n++) {
            for (uint32_t i = 0; i < n; i++) buf[i] = (uint8_t)xs32_state(&s);
            sha256(buf, n, digests + 32 * n);
            st_le32(crcs + 4 * n, crc32(buf, n));
        }
        CHECK(sha_hex_is(digests, sizeof digests, PY_SMALL_SHA_CHAIN));
        CHECK(crc32(crcs, sizeof crcs) == PY_SMALL_CRC_CHAIN);
    }
    {   /* 큰 버퍼 6 개 (블록 경계 ±1 포함) */
        static uint8_t big[100003], dig[6 * 32];
        static const uint32_t sizes[6] = {1000, 4095, 4096, 4097, 65536, 100003};
        uint32_t s = 0x5EED0A04u;
        for (int k = 0; k < 6; k++) {
            for (uint32_t i = 0; i < sizes[k]; i++) big[i] = (uint8_t)xs32_state(&s);
            sha256(big, sizes[k], dig + 32 * k);
        }
        CHECK(sha_hex_is(dig, sizeof dig, PY_BIG_SHA_CHAIN));
    }
    {   /* 아무렇게나 쪼개 흘려 넣어도 한 번에 넣은 것과 같아야 (update 의 잔여 버퍼 경로) */
        static uint8_t m[5000];
        for (int i = 0; i < 5000; i++) m[i] = (uint8_t)xs32();
        uint8_t ref[32], d[32];
        sha256(m, sizeof m, ref);
        uint32_t ref_crc = crc32(m, sizeof m);
        int bad = 0;
        for (int trial = 0; trial < 200; trial++) {
            sha256_ctx c; uint32_t cr = 0; size_t off = 0;
            sha256_init(&c);
            while (off < sizeof m) {
                size_t k = xs32() % 150u;
                if (k > sizeof m - off) k = sizeof m - off;
                sha256_update(&c, m + off, k); cr = crc32_update(cr, m + off, k); off += k;
            }
            sha256_final(&c, d);
            if (memcmp(d, ref, 32) != 0 || cr != ref_crc) bad++;
        }
        CHECK(bad == 0);
    }
}

static void test_part_b(void) {
    printf("-- Part B: img_verify — 비트 플립 / 잘림 / 롤백 / 넘침 / 악의적 변조 / python 이미지\n");
    static uint8_t slot[SLOT_LEN], ref[SLOT_LEN];
    img_info_t in;
    fresh_image(ref, VER(1, 3, 0));
    CHECK(g_img_len == 64u + PL_LEN);
    CHECK(img_verify(ref, SLOT_LEN, VER(1, 2, 0), &in) == IMG_OK);
    CHECK(in.version == VER(1, 3, 0) && in.payload_size == PL_LEN && in.load_addr == EXEC_ADDR);
    CHECK(in.init_sp == 0x20020000u && in.reset_vec == EXEC_ADDR + 0x1C1u);
    CHECK(img_verify(ref, g_img_len, 0, NULL) == IMG_OK);                 /* 슬롯 = 이미지 딱 맞게 */
    CHECK(img_verify(NULL, SLOT_LEN, 0, &in) == IMG_ERR_ARG);

    {   /* 모든 단일 비트 플립 (헤더 512 + payload 2048 비트): 영역마다 정해진 코드 */
        int bad = 0;
        for (uint32_t bit = 0; bit < g_img_len * 8u; bit++) {
            memcpy(slot, ref, SLOT_LEN);
            slot[bit / 8] ^= (uint8_t)(1u << (bit % 8));
            uint32_t byte = bit / 8;
            img_rc_t want = byte < 4 ? IMG_ERR_BAD_MAGIC : byte < 8 ? IMG_ERR_BAD_HDR_VERSION
                          : byte < 64 ? IMG_ERR_BAD_HDR_CRC : IMG_ERR_BAD_CRC;
            img_rc_t got = img_verify(slot, SLOT_LEN, 0, NULL);
            if (got != want && bad++ < 3) printf("  flip bit %u: got %s want %s\n", bit, img_rc_name(got), img_rc_name(want));
        }
        CHECK(bad == 0);
    }
    {   /* 잘림 1: 다운로드가 cut 바이트에서 끊김, 슬롯 나머지는 지워진 0xFF */
        int bad = 0;
        for (uint32_t cut = 0; cut < g_img_len; cut++) {
            memset(slot, 0xFF, SLOT_LEN);
            memcpy(slot, ref, cut);
            img_rc_t got = img_verify(slot, SLOT_LEN, 0, NULL);
            img_rc_t want = all_ff(ref + cut, g_img_len - cut) ? IMG_OK
                          : cut == 0 ? IMG_EMPTY : cut < 4 ? IMG_ERR_BAD_MAGIC : cut < 8 ? IMG_ERR_BAD_HDR_VERSION
                          : cut < 64 ? IMG_ERR_BAD_HDR_CRC : IMG_ERR_BAD_CRC;
            if (got != want && bad++ < 3) printf("  cut %u: got %s want %s\n", cut, img_rc_name(got), img_rc_name(want));
        }
        CHECK(bad == 0);
    }
    {   /* 잘림 2: 슬롯(버퍼) 자체가 짧다 — slot_len 0..len−1 전부, 범위 밖을 읽으면 ASan 이 잡는다 */
        int bad = 0;
        for (uint32_t len = 0; len < g_img_len; len++) {
            uint8_t *heap = malloc(len ? len : 1);       /* 정확한 크기의 힙 버퍼 → 한 바이트만 넘어도 ASan */
            if (!heap) { bad++; continue; }
            memcpy(heap, ref, len);
            if (img_verify(heap, len, 0, NULL) != IMG_ERR_BAD_SIZE) bad++;
            free(heap);
        }
        CHECK(bad == 0);
    }
    {   /* 롤백: min_version 은 OTP/eFuse/보호된 플래시에 따로 있는 단조 카운터 (여기선 인자) */
        CHECK(img_verify(ref, SLOT_LEN, VER(1, 3, 0), NULL) == IMG_OK);          /* 같으면 OK */
        CHECK(img_verify(ref, SLOT_LEN, VER(1, 3, 1), NULL) == IMG_ERR_ROLLBACK);
        CHECK(img_verify(ref, SLOT_LEN, VER(2, 0, 0), NULL) == IMG_ERR_ROLLBACK);
        fresh_image(slot, VER(1, 10, 0));                                          /* 문자열 비교면 "1.10" < "1.9" */
        CHECK(img_verify(slot, SLOT_LEN, VER(1, 9, 0), NULL) == IMG_OK);
        fresh_image(slot, VER(1, 1, 0));
        slot[100] ^= 0x40;                                                         /* 옛 버전 + 손상: 싼 검사 먼저 */
        CHECK(img_verify(slot, SLOT_LEN, VER(1, 2, 0), &in) == IMG_ERR_ROLLBACK && in.hdr_ok);
    }
    {   /* 크기 넘침: 헤더 CRC 는 정상(공격자/버그 도구가 봉인), payload_size 가 거대 */
        static const uint32_t evil[] = {0xFFFFFFFFu, 0xFFFFFFC0u /* 64+x = 0 */, 0xFFFFFFC1u /* = 1 */,
                                        0x80000000u, SLOT_LEN - 63u, 0u, 7u};
        int bad = 0;
        for (size_t k = 0; k < sizeof evil / sizeof evil[0]; k++) {
            memcpy(slot, ref, SLOT_LEN);
            st_le32(slot + 12, evil[k]); img_hdr_seal(slot);
            if (img_verify(slot, SLOT_LEN, 0, NULL) != IMG_ERR_BAD_SIZE) bad++;
        }
        CHECK(bad == 0);
        memcpy(slot, ref, SLOT_LEN);
        st_le32(slot + 12, SLOT_LEN - 64u); img_hdr_seal(slot);                    /* 경계 안: 크기는 통과, CRC 에서 */
        CHECK(img_verify(slot, SLOT_LEN, 0, NULL) == IMG_ERR_BAD_CRC);
        static uint8_t bigslot[IMG_HDR_SIZE + EXEC_MAX + 16];                       /* 실행 영역 384 KB 경계 */
        memset(bigslot, 0xFF, sizeof bigslot);
        static uint8_t pl[EXEC_MAX + 1];
        make_payload(pl, sizeof pl, 7);
        img_build(bigslot, sizeof bigslot, pl, EXEC_MAX, VER(1, 0, 0), EXEC_ADDR, IMG_FLAG_SHA256);
        CHECK(img_verify(bigslot, sizeof bigslot, 0, NULL) == IMG_OK);              /* 딱 384 KB 는 OK */
        img_build(bigslot, sizeof bigslot, pl, EXEC_MAX + 1, VER(1, 0, 0), EXEC_ADDR, IMG_FLAG_SHA256);
        CHECK(img_verify(bigslot, sizeof bigslot, 0, NULL) == IMG_ERR_TOO_BIG);     /* 슬롯엔 들어가도 실행 영역 초과 */
    }
    {   /* 필드별 이름 있는 결함 */
        memcpy(slot, ref, SLOT_LEN); st_le32(slot + 16, 0x08000000u); img_hdr_seal(slot);
        CHECK(img_verify(slot, SLOT_LEN, 0, NULL) == IMG_ERR_BAD_LOAD_ADDR);
        memcpy(slot, ref, SLOT_LEN); st_le32(slot + 20, IMG_FLAG_SHA256 | 0x100u); img_hdr_seal(slot);
        CHECK(img_verify(slot, SLOT_LEN, 0, NULL) == IMG_ERR_BAD_FLAGS);            /* 모르는 플래그 = 거부 */
        memcpy(slot, ref, SLOT_LEN); st_le32(slot + 20, 0); img_hdr_seal(slot);
        CHECK(img_verify(slot, SLOT_LEN, 0, NULL) == IMG_ERR_BAD_FLAGS);            /* SHA 빼기(다운그레이드) 거부 */
        memcpy(slot, ref, SLOT_LEN); st_le16(slot + 4, 2); img_hdr_seal(slot);
        CHECK(img_verify(slot, SLOT_LEN, 0, NULL) == IMG_ERR_BAD_HDR_VERSION);      /* 미래 포맷 */
        memcpy(slot, ref, SLOT_LEN); st_le16(slot + 6, 128); img_hdr_seal(slot);
        CHECK(img_verify(slot, SLOT_LEN, 0, NULL) == IMG_ERR_BAD_HDR_VERSION);
        memset(slot, 0xFF, SLOT_LEN);
        CHECK(img_verify(slot, SLOT_LEN, 0, NULL) == IMG_EMPTY);
        memset(slot, 0x00, SLOT_LEN);
        CHECK(img_verify(slot, SLOT_LEN, 0, NULL) == IMG_ERR_BAD_MAGIC);
        int bad = 0;                                                                /* 저장된 SHA 256 비트 각각 + 재봉인 */
        for (int k = 0; k < 256; k++) {                                             /* → 다이제스트 '일부만' 비교하면 걸린다 */
            memcpy(slot, ref, SLOT_LEN); slot[28 + k / 8] ^= (uint8_t)(1u << (k % 8)); img_hdr_seal(slot);
            if (img_verify(slot, SLOT_LEN, 0, NULL) != IMG_ERR_BAD_SHA) bad++;
        }
        CHECK(bad == 0);
        memcpy(slot, ref, SLOT_LEN); slot[64 + 200] ^= 0xFF;                         /* hdr_ok 이면 진단 정보 */
        CHECK(img_verify(slot, SLOT_LEN, 0, &in) == IMG_ERR_BAD_CRC && in.hdr_ok && in.version == VER(1, 3, 0));
    }
    {   /* 악의적 변조 1: payload 를 바꾸고 4 바이트로 CRC 를 원래 값에 맞춤 → 헤더 그대로 → SHA 만 잡는다 */
        memcpy(slot, ref, SLOT_LEN);
        uint8_t *pl = slot + 64;
        st_le32(pl + 4, 0x08031337u);                                               /* 리셋 벡터를 '악성' 주소로 */
        CHECK(crc_forge4(pl, PL_LEN, 100, ld_le32(ref + 24)));
        CHECK(crc32(pl, PL_LEN) == ld_le32(ref + 24));
        CHECK(img_verify(slot, SLOT_LEN, 0, NULL) == IMG_ERR_BAD_SHA);
        /* 악의적 변조 2: 공격자가 SHA 까지 다시 계산해 헤더에 넣으면? → 통과한다. 무결성 ≠ 진위 (서명 = SEC) */
        img_build(slot, SLOT_LEN, pl, PL_LEN, VER(9, 9, 9), EXEC_ADDR, IMG_FLAG_SHA256);
        CHECK(img_verify(slot, SLOT_LEN, VER(1, 3, 0), &in) == IMG_OK && in.reset_vec == 0x08031337u);
    }
    {   /* python 이 만든 이미지를 C 가 검증 + 같은 payload 로 C 가 만든 이미지와 바이트 단위로 동일 */
        uint8_t pl[200], mine[264];
        CHECK(img_verify(PY_FIXTURE, sizeof PY_FIXTURE, VER(1, 2, 3), &in) == IMG_OK && in.version == VER(1, 2, 3));
        make_payload(pl, 200, 0xF1C7A04u);
        CHECK(img_build(mine, sizeof mine, pl, 200, VER(1, 2, 3), EXEC_ADDR, IMG_FLAG_SHA256) == 264u);
        CHECK(memcmp(mine, PY_FIXTURE, 264) == 0);
    }
}

/* 무작위 결함 하나를 넣은 이미지 + 기대 코드 (Part B 무작위 시험과 emitmut 이 같이 쓴다) */
static img_rc_t random_case(uint8_t *slot, uint32_t *min_v) {
    uint32_t n = IMG_MIN_PAYLOAD + xs32() % (SLOT_LEN - 64u - IMG_MIN_PAYLOAD + 1u);
    uint32_t ver = VER(1 + xs32() % 3, xs32() % 20, xs32() % 300);
    uint8_t pl[SLOT_LEN];
    memset(slot, 0xFF, SLOT_LEN);
    make_payload(pl, n, xs32() | 1u);
    img_build(slot, SLOT_LEN, pl, n, ver, EXEC_ADDR, IMG_FLAG_SHA256);
    *min_v = xs32() % 2 ? 0 : ver - (xs32() % 4);                                  /* 대부분 통과하는 min */
    uint32_t pos = 64 + xs32() % n;
    switch (xs32() % 10) {
    case 0: slot[pos] ^= (uint8_t)(1 + xs32() % 255); return IMG_ERR_BAD_CRC;       /* 1 바이트 오류 */
    case 1: {                                                                       /* 32 비트 이하 버스트 */
        uint32_t k = 1 + xs32() % 4;
        if (pos + k > 64 + n) pos = 64 + n - k;
        slot[pos] ^= (uint8_t)(1 + xs32() % 255);                                   /* 버스트 첫 바이트는 반드시 변경 */
        for (uint32_t i = 1; i < k; i++) slot[pos + i] ^= (uint8_t)xs32();
        return IMG_ERR_BAD_CRC; }
    case 2: slot[pos] ^= (uint8_t)(1 + xs32() % 255);                               /* CRC 다시 봉인, SHA 는 낡음 */
        st_le32(slot + 24, crc32(slot + 64, n)); img_hdr_seal(slot); return IMG_ERR_BAD_SHA;
    case 3: slot[8 + xs32() % 52] ^= (uint8_t)(1 + xs32() % 255); return IMG_ERR_BAD_HDR_CRC;
    case 4: *min_v = ver + 1 + xs32() % 1000; return IMG_ERR_ROLLBACK;
    case 5: { uint32_t a = xs32(); if (a == EXEC_ADDR) a ^= 4; st_le32(slot + 16, a); img_hdr_seal(slot);
        return IMG_ERR_BAD_LOAD_ADDR; }
    case 6: st_le32(slot + 12, (SLOT_LEN - 63u) + xs32() % (0xFFFFFFFFu - (SLOT_LEN - 63u))); img_hdr_seal(slot);
        return IMG_ERR_BAD_SIZE;
    case 7: st_le32(slot + 20, IMG_FLAG_SHA256 | (xs32() & ~1u) | 2u); img_hdr_seal(slot); return IMG_ERR_BAD_FLAGS;
    case 8: { uint32_t m = xs32(); if (m == IMG_MAGIC) m ^= 1; if (m == 0xFFFFFFFFu) m = 0; st_le32(slot, m);
        return IMG_ERR_BAD_MAGIC; }
    default: return IMG_OK;
    }
}

static void test_random(void) {
    printf("-- Part B: 무작위 결함 3000 개 (고정 시드 xorshift32)\n");
    static uint8_t slot[SLOT_LEN];
    int bad = 0, hist[IMG_RC_COUNT] = {0};
    g_rng = 0xC0FFEE01u;
    for (int i = 0; i < 3000; i++) {
        uint32_t min_v;
        img_rc_t want = random_case(slot, &min_v);
        img_rc_t got = img_verify(slot, SLOT_LEN, min_v, NULL);
        hist[want]++;
        if (got != want && bad++ < 3) printf("  case %d: got %s want %s\n", i, img_rc_name(got), img_rc_name(want));
    }
    CHECK(bad == 0);
    int kinds = 0;
    for (int k = 0; k < IMG_RC_COUNT; k++) kinds += hist[k] > 100;
    CHECK(kinds == 9);                                                              /* 9 가지 결과가 고루 나왔나 */
}

static void test_part_c(void) {
    printf("-- Part C: 두 슬롯 부트 결정 + 로그\n");
    static uint8_t img[6][SLOT_LEN];
    enum { S_V13, S_V14, S_EMPTY, S_BADCRC, S_OLD, S_BADMAGIC, S_N };
    fresh_image(img[S_V13], VER(1, 3, 0));
    fresh_image(img[S_V14], VER(1, 4, 0));
    memset(img[S_EMPTY], 0xFF, SLOT_LEN);
    fresh_image(img[S_BADCRC], VER(1, 5, 0)); img[S_BADCRC][64 + 77] ^= 0x10;      /* 다운로드 중 손상된 최신판 */
    fresh_image(img[S_OLD], VER(1, 1, 0));                                          /* 롤백 대상 */
    memset(img[S_BADMAGIC], 0x00, SLOT_LEN);
    static const uint32_t ver_of[S_N] = {VER(1, 3, 0), VER(1, 4, 0), 0, 0, 0, 0};
    const uint32_t minv = VER(1, 2, 0);
    char log[1024];
    int bad = 0;
    for (int a = 0; a < S_N; a++)
        for (int b = 0; b < S_N; b++) {                                             /* 36 조합 전수 */
            boot_slot_t s[2] = {{'A', 0x000000u, img[a], SLOT_LEN}, {'B', 0x080000u, img[b], SLOT_LEN}};
            bool oka = a <= S_V14, okb = b <= S_V14;
            int want = !oka && !okb ? -1 : !okb ? 0 : !oka ? 1 : (ver_of[b] > ver_of[a] ? 1 : 0);
            img_info_t ch;
            int got = boot_select(s, minv, log, sizeof log, &ch);
            if (got != want) bad++;
            else if (got >= 0 && ch.version != ver_of[got == 0 ? a : b]) bad++;
        }
    CHECK(bad == 0);
    {   /* 현장 시나리오: A = v1.3.0 정상, B = 손상된 v1.5.0 → A 로 부팅, 로그에 B 의 헤더 진단 */
        boot_slot_t s[2] = {{'A', 0x000000u, img[S_V13], SLOT_LEN}, {'B', 0x080000u, img[S_BADCRC], SLOT_LEN}};
        CHECK(boot_select(s, minv, log, sizeof log, NULL) == 0);
        printf("%s", log);
        CHECK(strstr(log, "[boot] anti-rollback min=v1.2.0\n") == log);
        CHECK(strstr(log, "slot A @0x00000000 len=512: OK v1.3.0 size=256") != NULL);
        CHECK(strstr(log, "sp=0x20020000 pc=0x080201C1") != NULL);
        CHECK(strstr(log, "slot B @0x00080000 len=512: BAD_CRC (hdr ok: v1.5.0 size=256)") != NULL);
        CHECK(strstr(log, "[boot] select A v1.3.0 -> copy 256 B to 0x08020000, jump reset=0x080201C1\n") != NULL);
        boot_slot_t t[2] = {{'A', 0x000000u, img[S_OLD], SLOT_LEN}, {'B', 0x080000u, img[S_EMPTY], SLOT_LEN}};
        CHECK(boot_select(t, minv, log, sizeof log, NULL) == -1);
        printf("%s", log);
        CHECK(strstr(log, "slot A @0x00000000 len=512: ROLLBACK (hdr ok: v1.1.0 size=256)") != NULL);
        CHECK(strstr(log, "slot B @0x00080000 len=512: EMPTY\n") != NULL);
        CHECK(strstr(log, "no bootable image -> stay in bootloader (DFU)") != NULL);
        char tiny[24];                                                               /* 로그 버퍼가 작아도 결정은 같다 */
        memset(tiny, 'X', sizeof tiny);
        CHECK(boot_select(s, minv, tiny, sizeof tiny, NULL) == 0);
        CHECK(memchr(tiny, '\0', sizeof tiny) != NULL && strlen(tiny) == sizeof tiny - 1);
        CHECK(boot_select(s, minv, NULL, 0, NULL) == 0);
    }
}

/* ---------- 교차검증 모드 (python mkimg.py 와 주고받기) ---------- */
static int write_file(const char *path, const uint8_t *p, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    size_t w = fwrite(p, 1, n, f);
    fclose(f);
    return w == n ? 0 : 1;
}
static int mode_emit(const char *prefix) {
    char path[512];
    uint8_t pl[PL_LEN];
    static uint8_t img[64 + PL_LEN];
    make_payload(pl, PL_LEN, 0xB00710ADu);
    uint32_t n = img_build(img, sizeof img, pl, PL_LEN, VER(1, 3, 0), EXEC_ADDR, IMG_FLAG_SHA256);
    snprintf(path, sizeof path, "%s.payload", prefix);
    if (write_file(path, pl, PL_LEN)) return 1;
    snprintf(path, sizeof path, "%s.img", prefix);
    if (write_file(path, img, n)) return 1;
    printf("C emit %s.img: %u B, v1.3.0, crc32=0x%08X\n", prefix, (unsigned)n, (unsigned)ld_le32(img + 24));
    return 0;
}
static int mode_verify(const char *path, uint32_t min_v) {
    static uint8_t buf[IMG_HDR_SIZE + EXEC_MAX + 1];
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 2; }
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    img_info_t in;
    img_rc_t rc = img_verify(buf, (uint32_t)n, min_v, &in);
    printf("C verify %s: %s", path, img_rc_name(rc));
    if (in.hdr_ok) printf(" (" VER_FMT " size=%u crc=0x%08X)", VER_ARG(in.version), (unsigned)in.payload_size,
                          (unsigned)in.payload_crc32);
    printf("\n");
    return rc == IMG_OK ? 0 : 1;
}
static int mode_emitmut(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    static uint8_t slot[SLOT_LEN];
    uint8_t h[12];
    uint32_t count = 2000;
    fwrite("A4MR", 1, 4, f); st_le32(h, count); fwrite(h, 1, 4, f);
    g_rng = 0x0DDBA11u;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t min_v;
        if (i % 10 == 9) {                                                          /* 10 % 는 통째로 쓰레기 */
            for (uint32_t k = 0; k < SLOT_LEN; k++) slot[k] = (uint8_t)xs32();
            if (i % 20 == 9) { st_le32(slot, IMG_MAGIC); st_le16(slot + 4, 1); st_le16(slot + 6, 64); img_hdr_seal(slot); }
            min_v = xs32();
        } else {
            (void)random_case(slot, &min_v);
        }
        uint32_t len = SLOT_LEN - (i % 7 == 0 ? xs32() % SLOT_LEN : 0);             /* 가끔 슬롯도 짧게 */
        st_le32(h, min_v); st_le32(h + 4, len); st_le32(h + 8, (uint32_t)img_verify(slot, len, min_v, NULL));
        fwrite(h, 1, 12, f); fwrite(slot, 1, len, f);
    }
    fclose(f);
    printf("C emitmut %s: %u records\n", path, (unsigned)count);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[1], "emit") == 0) return mode_emit(argv[2]);
    if (argc >= 3 && strcmp(argv[1], "verify") == 0)
        return mode_verify(argv[2], argc >= 4 ? (uint32_t)strtoul(argv[3], NULL, 0) : 0);
    if (argc >= 3 && strcmp(argv[1], "emitmut") == 0) return mode_emitmut(argv[2]);
    test_part_a();
    test_part_b();
    test_random();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
