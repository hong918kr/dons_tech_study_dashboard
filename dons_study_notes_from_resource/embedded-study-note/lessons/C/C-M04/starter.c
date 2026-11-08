/* C-M04 Packed 구조체와 비정렬 접근
 *
 * 와이어 포맷: 7바이트 헤더 [type:1][len:2][seq:4], 패딩 없음, 리틀 엔디안.
 * 이 헤더는 UART/DMA 바이트 스트림의 "아무 오프셋" 에나 온다 → 필드가 비정렬일 수 있다.
 *   Part A — 같은 헤더를 ① packed 구조체 캐스팅 ② memcpy 두 방식으로 파싱
 *   Part B — 경계 검사(오버플로 안전), memcpy 직렬화, packed 멤버를 포인터 API 에 넘기기
 *   Part C — 스트림 프레이머: [0xA5][hdr 7][payload len][xor] 를 DMA 버퍼에서 찾아 파싱
 * 엔디안: 호스트(arm64 맥)와 Cortex-M 모두 리틀 엔디안 → 여기서는 호스트 순서 그대로 읽는다.
 *         엔디안 독립 직렬화(get_le16/put_be32)는 C-M05.
 */
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "이 레슨은 리틀 엔디안 호스트 기준이다 (엔디안 독립 버전은 C-M05)"
#endif

/* 와이어 포맷의 필드 오프셋 — 데이터시트/프로토콜 문서의 표 그대로 */
enum { HDR_SIZE = 7, OFF_TYPE = 0, OFF_LEN = 1, OFF_SEQ = 3 };

/* 앱 내부용 헤더: 자연 정렬 (8 B, align 4). 파싱 결과는 항상 여기에 담는다. */
typedef struct { uint8_t type; uint16_t len; uint32_t seq; } hdr_t;

/* ======================= Part A: 두 가지 파싱 ======================= */

/* 와이어 헤더를 그대로 덮어쓸 구조체. sizeof 가 7, len@1, seq@3 이어야 한다. */
struct wire_hdr { uint8_t type; uint16_t len; uint32_t seq; };   /* TODO: 패딩을 없애라 */

static bool span_ok(size_t n, size_t off, size_t len);           /* Part B */

/* buf[off..off+6] 의 헤더를 packed 구조체 포인터로 캐스팅해서 읽는다. 바이트 부족이면 -1 */
static int parse_hdr_packed(const uint8_t *buf, size_t n, size_t off, hdr_t *out) {
    (void)buf; (void)n; (void)off; (void)out;
    return -1; /* TODO */
}

/* 같은 일을 memcpy 로: 필드마다 OFF_* 위치에서 지역 변수로 복사 */
static int parse_hdr_memcpy(const uint8_t *buf, size_t n, size_t off, hdr_t *out) {
    (void)buf; (void)n; (void)off; (void)out;
    return -1; /* TODO */
}

/* ======================= Part B: 경계 · 쓰기 · 포인터 API ======================= */

/* [off, off+len) 이 [0, n) 안에 들어가나? off + len 이 넘쳐도(wrap) 정답이어야 한다 */
static bool span_ok(size_t n, size_t off, size_t len) {
    (void)n; (void)off; (void)len;
    return false; /* TODO */
}

/* h 를 buf[off..off+6] 에 와이어 포맷으로 쓴다 (memcpy). 다른 바이트는 건드리지 않는다. */
static int write_hdr(uint8_t *buf, size_t n, size_t off, const hdr_t *h) {
    (void)buf; (void)n; (void)off; (void)h;
    return -1; /* TODO */
}

/* 주어진 "레거시" API — 정렬된 uint16_t* 를 가정한다 (고치지 않는다) */
static void u16_add_sat(uint16_t *p, uint16_t d) {
    uint32_t s = (uint32_t)*p + d;
    *p = (uint16_t)(s > 0xFFFFu ? 0xFFFFu : s);
}

/* w->len 에 d 를 포화 덧셈. 반드시 u16_add_sat 을 써라. 단, &w->len 을 넘기면 안 된다. */
static void wire_len_add(struct wire_hdr *w, uint16_t d) {
    (void)w; (void)d; (void)u16_add_sat;
    /* TODO */
}

/* ======================= Part C: 스트림 프레이머 =======================
 *  프레임: [0]=0xA5 sync | [1..7] 헤더 | [8 .. 8+len-1] payload | [8+len] chk
 *          chk = 헤더+payload 바이트([1 .. 8+len-1])의 XOR. len 은 0..MAX_PAYLOAD.
 *  규칙: sync 가 아니면 1바이트 건너뜀. len > MAX_PAYLOAD 또는 chk 불일치 → 가짜 sync, 1바이트 건너뜀.
 *        헤더/프레임이 버퍼 끝에서 잘렸으면 멈춘다 → *consumed = 그 sync 위치 (다음 DMA 조각과 합칠 자리).
 *        out 이 max 개 차면 멈춘다. 끝까지 다 봤으면 *consumed = n.
 */
#define SYNC        0xA5u
#define MAX_PAYLOAD 32u

typedef struct { hdr_t h; size_t payload_off; } frame_t;

static size_t parse_stream(const uint8_t *s, size_t n, frame_t *out, size_t max, size_t *consumed) {
    (void)s; (void)n; (void)out; (void)max;
    *consumed = 0;
    return 0; /* TODO: parse_hdr_memcpy 로 헤더를 읽어라 */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0xC0FFEE11u;               /* 고정 시드 → 재현 가능 */
static uint32_t xorshift32(void) {
    uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x;
}
/* 참조 구현: 바이트 조립 (LE) */
static uint16_t ref_le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t ref_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static bool hdr_eq(const hdr_t *a, uint8_t type, uint16_t len, uint32_t seq) {
    return a->type == type && a->len == len && a->seq == seq;
}
/* 테스트용 프레임 생성기 (정답 프레이머와 독립) */
static size_t put_frame(uint8_t *p, uint8_t type, uint8_t len, uint32_t seq, const uint8_t *pl) {
    uint8_t chk = 0;
    p[0] = SYNC; p[1] = type; p[2] = len; p[3] = 0;
    for (int i = 0; i < 4; i++) p[4 + i] = (uint8_t)(seq >> (8 * i));
    for (unsigned i = 0; i < len; i++) p[8 + i] = pl ? pl[i] : (uint8_t)(i * 7u + 1u);
    for (unsigned i = 1; i < 8u + len; i++) chk ^= p[i];
    p[8 + len] = chk;
    return 9u + len;
}

int main(void) {
    alignas(8) uint8_t buf[64];

    /* ---- Part A: 고정 벡터, 오프셋 0..7 전부 ---- */
    static const uint8_t H[HDR_SIZE] = { 0x02, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12 };
    int okA = 0, okB = 0;
    for (size_t off = 0; off < 8; off++) {
        hdr_t a = { 0 }, b = { 0 };
        memset(buf, 0xEE, sizeof buf);
        memcpy(buf + off, H, HDR_SIZE);
        okA += parse_hdr_packed(buf, sizeof buf, off, &a) == 0 && hdr_eq(&a, 0x02, 0x1234, 0x12345678u);
        okB += parse_hdr_memcpy(buf, sizeof buf, off, &b) == 0 && hdr_eq(&b, 0x02, 0x1234, 0x12345678u);
    }
    CHECK(okA == 8);                                   /* packed 캐스팅: 모든 오프셋 */
    CHECK(okB == 8);                                   /* memcpy: 모든 오프셋 */
    {   /* 퍼즈: 무작위 바이트·오프셋 300회, 두 방식 == 바이트 조립 참조 */
        int agree = 0;
        for (int it = 0; it < 300; it++) {
            for (size_t i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)xorshift32();
            size_t off = xorshift32() % (sizeof buf - HDR_SIZE + 1);
            hdr_t a = { 0 }, b = { 0 };
            int ra = parse_hdr_packed(buf, sizeof buf, off, &a);
            int rb = parse_hdr_memcpy(buf, sizeof buf, off, &b);
            agree += ra == 0 && rb == 0
                  && hdr_eq(&a, buf[off], ref_le16(buf + off + 1), ref_le32(buf + off + 3))
                  && hdr_eq(&b, buf[off], ref_le16(buf + off + 1), ref_le32(buf + off + 3));
        }
        CHECK(agree == 300);
    }
    {   /* 바이트가 모자라면 -1 이고 out 을 건드리지 않는다 */
        hdr_t a = { 9, 9, 9 };
        CHECK(parse_hdr_packed(buf, 10, 4, &a) == -1 && hdr_eq(&a, 9, 9, 9));
        CHECK(parse_hdr_memcpy(buf, 10, 4, &a) == -1 && hdr_eq(&a, 9, 9, 9));
        CHECK(parse_hdr_memcpy(buf, 10, 3, &a) == 0);  /* 정확히 끝까지 = OK */
    }

    /* ---- Part B: packed 레이아웃의 성질 ---- */
    CHECK(sizeof(struct wire_hdr) == 7);
    CHECK(alignof(struct wire_hdr) == 1);
    CHECK(offsetof(struct wire_hdr, len) == OFF_LEN && offsetof(struct wire_hdr, seq) == OFF_SEQ);
    CHECK(sizeof(struct wire_hdr[3]) == 21);           /* 배열에도 꼬리 패딩 없음 → arr[1].seq @10 */
    {
        struct rec { uint8_t tag; struct wire_hdr h; uint32_t crc; };
        CHECK(offsetof(struct rec, h) == 1);           /* 정렬 1 인 멤버 → 앞 패딩 없음 */
        CHECK(offsetof(struct rec, crc) == 8 && sizeof(struct rec) == 12);
    }
    /* span_ok: 경계와 오버플로 */
    CHECK(span_ok(64, 57, 7) && !span_ok(64, 58, 7));
    CHECK(span_ok(7, 0, 7) && !span_ok(6, 0, 7) && span_ok(0, 0, 0));
    CHECK(!span_ok(64, 65, 0));                        /* off 자체가 범위 밖 */
    CHECK(!span_ok(64, SIZE_MAX, 7) && !span_ok(64, SIZE_MAX - 3, 7));  /* off+len wrap */
    {   /* write_hdr: 홀수 오프셋, 주변 바이트 보존 */
        hdr_t h = { 0xA1, 0xBEEF, 0xDEADC0DEu }, back = { 0 };
        static const uint8_t exp[HDR_SIZE] = { 0xA1, 0xEF, 0xBE, 0xDE, 0xC0, 0xAD, 0xDE };
        memset(buf, 0x5A, sizeof buf);
        CHECK(write_hdr(buf, sizeof buf, 5, &h) == 0);
        CHECK(memcmp(buf + 5, exp, HDR_SIZE) == 0);
        CHECK(buf[4] == 0x5A && buf[12] == 0x5A);      /* 카나리 */
        CHECK(parse_hdr_packed(buf, sizeof buf, 5, &back) == 0 && hdr_eq(&back, 0xA1, 0xBEEF, 0xDEADC0DEu));
        CHECK(write_hdr(buf, sizeof buf, 58, &h) == -1 && buf[58] == 0x5A);  /* 넘치면 한 바이트도 안 씀 */
    }
    {   /* wire_len_add: 헤더가 오프셋 2 → len 은 홀수 주소 */
        static const uint8_t H2[HDR_SIZE] = { 0x02, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00 };
        memset(buf, 0, sizeof buf);
        memcpy(buf + 2, H2, HDR_SIZE);
        struct wire_hdr *w = (struct wire_hdr *)(buf + 2);
        wire_len_add(w, 5);
        CHECK(ref_le16(buf + 3) == 0x0015);
        wire_len_add(w, 0xFFF0);
        CHECK(ref_le16(buf + 3) == 0xFFFF);            /* 포화 */
        CHECK(buf[2] == 0x02 && ref_le32(buf + 5) == 1u);   /* 다른 필드 보존 */
    }

    /* ---- Part C: 프레이머 ---- */
    {
        uint8_t s[128];
        frame_t f[8];
        size_t c, n, k;

        n = put_frame(s, 1, 4, 0x11111111u, NULL);                       /* 프레임 1개 */
        CHECK(parse_stream(s, n, f, 8, &c) == 1 && c == n);
        CHECK(hdr_eq(&f[0].h, 1, 4, 0x11111111u) && f[0].payload_off == 8);

        s[0] = 0x00; s[1] = 0x13; s[2] = 0x37;                           /* 쓰레기 3B + 2개 */
        n = 3 + put_frame(s + 3, 2, 0, 0xAABBCCDDu, NULL);
        n += put_frame(s + n, 3, 5, 7u, NULL);
        k = parse_stream(s, n, f, 8, &c);
        CHECK(k == 2 && c == n);
        CHECK(hdr_eq(&f[0].h, 2, 0, 0xAABBCCDDu) && hdr_eq(&f[1].h, 3, 5, 7u));
        CHECK(f[1].payload_off == 3 + 9 + 8 && s[f[1].payload_off] == 1 && s[f[1].payload_off + 4] == 29);

        n = put_frame(s, 4, 3, 1u, NULL);                                /* chk 깨짐 → 건너뜀 */
        s[n - 1] ^= 0xFF;
        n += put_frame(s + n, 5, 1, 2u, NULL);
        k = parse_stream(s, n, f, 8, &c);
        CHECK(k == 1 && hdr_eq(&f[0].h, 5, 1, 2u) && c == n);

        s[0] = SYNC; s[1] = 0; s[2] = 200; s[3] = 0;                     /* len=200 > MAX → 가짜 sync */
        n = 4 + put_frame(s + 4, 6, 2, 3u, NULL);
        k = parse_stream(s, n, f, 8, &c);
        CHECK(k == 1 && hdr_eq(&f[0].h, 6, 2, 3u) && f[0].payload_off == 12);

        n = put_frame(s, 7, 6, 4u, NULL);                                /* 뒤 프레임이 잘림 */
        size_t cut = put_frame(s + n, 8, 10, 5u, NULL);
        k = parse_stream(s, n + cut - 3, f, 8, &c);
        CHECK(k == 1 && c == n);                                         /* 잘린 프레임의 sync 위치 */
        k = parse_stream(s, n + 5, f, 8, &c);                            /* 헤더조차 잘림 */
        CHECK(k == 1 && c == n);

        n = put_frame(s, 1, 0, 1u, NULL); n += put_frame(s + n, 1, 0, 2u, NULL);
        n += put_frame(s + n, 1, 0, 3u, NULL);
        k = parse_stream(s, n, f, 2, &c);                                /* max 에서 멈춤 */
        CHECK(k == 2 && c == 18 && f[1].h.seq == 2u);
        CHECK(parse_stream(s, 0, f, 8, &c) == 0 && c == 0);
    }
    {   /* 퍼즈: 무작위 프레임 + 쓰레기(0xA5 제외), 무작위 위치에서 두 조각으로 잘라 이어 붙이기 */
        static uint8_t s[1024];
        frame_t f[64];
        uint8_t ty[16], ln[16]; uint32_t sq[16];
        int good = 0;
        for (int it = 0; it < 200; it++) {
            size_t n = 0, K = 1 + xorshift32() % 16;
            for (size_t i = 0; i < K; i++) {
                for (uint32_t g = xorshift32() % 6; g > 0; g--) {
                    uint8_t b = (uint8_t)xorshift32();
                    s[n++] = b == SYNC ? 0 : b;
                }
                uint8_t pl[MAX_PAYLOAD];
                ty[i] = (uint8_t)xorshift32(); ln[i] = (uint8_t)(xorshift32() % (MAX_PAYLOAD + 1));
                sq[i] = xorshift32();
                for (unsigned j = 0; j < ln[i]; j++) pl[j] = (uint8_t)xorshift32();
                n += put_frame(s + n, ty[i], ln[i], sq[i], pl);
            }
            size_t split = xorshift32() % (n + 1), c1, c2;
            size_t k1 = parse_stream(s, split, f, 64, &c1);              /* 첫 DMA 조각 */
            size_t k2 = parse_stream(s + c1, n - c1, f + k1, 64 - k1, &c2);   /* 남은 것 + 나머지 */
            bool ok = k1 + k2 == K && c1 <= split && c1 + c2 == n;
            for (size_t i = 0; ok && i < K; i++)
                ok = hdr_eq(&f[i].h, ty[i], ln[i], sq[i]);
            good += ok;
        }
        CHECK(good == 200);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
