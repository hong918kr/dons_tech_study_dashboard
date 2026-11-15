/* COMM-M04 CAN 프레임 구조: 비트 스터핑과 CRC-15
 *
 * Part A: CRC-15/CAN (poly 0x4599, init 0) · 표준/확장 데이터/리모트 프레임의 언스터프 비트열(SOF..CRC)
 *         · 비트 스터핑 · 전체 프레임(CRC delim, ACK, ACK delim, EOF) 인코더 — 알려진 비트열(verify_frame.py)과 대조
 * Part B: 비트 하나씩 받는 수신 상태 기계: 디스터핑 + 필드 파싱 + CRC 검사 + 폼(form) 검사
 *         결함 주입: 스터프 비트 뒤집기(stuff error), 데이터/CRC 비트(CRC error), 고정 필드(form error),
 *         EOF 마지막 비트(overload, 오류 아님), ACK 없음, 잘린 입력, 단일 비트 전수 뒤집기
 * Part C: 로직 애널라이저처럼 필드별 와이어 비트 구간 · 시간, 최악 프레임 길이 상한(Davis/Burns)과
 *         작은 DLC 전수 탐색으로 확인한 실제 최대 스터프 수
 *
 * 비트 표현: uint8_t 배열 원소 하나 = 비트 하나 (0 = dominant, 1 = recessive). 버스는 wired-AND.
 * 무거운 탐색은 demo_worst.c (DP 로 정확한 최악값), demo_flip.c (비트 오류 검출 실험).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { DOM = 0, REC = 1 };

#define CAN_STD_MAX_ID 0x7FFu
#define CAN_EXT_MAX_ID 0x1FFFFFFFu
#define CAN_MAX_UNSTUFFED 118   /* 확장 + 8 B: 헤더 39 + 데이터 64 + CRC 15 */
#define CAN_MAX_WIRE 168        /* 118 + 스터프 최대 29 + 고정 10 = 157, 여유 포함 */
#define CAN_E_CAP (-1)          /* 출력 용량 부족 */
#define CAN_E_ARG (-2)          /* ID 범위 초과, DLC > 15 */

typedef struct {
    uint32_t id;      /* 표준 11비트 / 확장 29비트 */
    bool ide;         /* true = 확장 프레임 */
    bool rtr;         /* true = 리모트 프레임 (데이터 없음) */
    uint8_t dlc;      /* 0..15. Classic CAN 에서 9..15 는 8 바이트를 뜻한다 */
    uint8_t data[8];
} can_frame_t;

static int can_data_len(bool rtr, unsigned dlc) { return rtr ? 0 : (dlc > 8 ? 8 : (int)dlc); }

/* v 의 하위 width 비트를 MSB 먼저 bits[*k..] 에 쓴다 */
static void put_bits(uint8_t *bits, int *k, uint32_t v, int width) {
    for (int i = width - 1; i >= 0; i--) bits[(*k)++] = (uint8_t)((v >> i) & 1u);
}
static uint32_t get_bits(const uint8_t *bits, int start, int width) {
    uint32_t v = 0;
    for (int i = 0; i < width; i++) v = v << 1 | bits[start + i];
    return v;
}

/* ---------------- Part A: CRC-15 · 필드 · 스터핑 · 인코더 ---------------- */

/* CRC-15/CAN 한 비트: ISO 11898-1 의 CRC_RG 시프트 레지스터 (init 0, 반사 없음, xorout 없음) */
static uint16_t crc15_update(uint16_t crc, unsigned bit) {
    (void)crc; (void)bit;
    return 0; /* TODO: fb = bit ^ crc[14]; crc = (crc << 1) & 0x7FFF; fb 이면 ^= 0x4599 */
}
static uint16_t crc15_bits(const uint8_t *bits, size_t n) {
    uint16_t c = 0;
    for (size_t i = 0; i < n; i++) c = crc15_update(c, bits[i]);
    return c;
}
static uint16_t crc15_bytes(const uint8_t *d, size_t n) {          /* 바이트는 MSB 먼저 */
    uint16_t c = 0;
    for (size_t i = 0; i < n; i++)
        for (int k = 7; k >= 0; k--) c = crc15_update(c, (d[i] >> k) & 1u);
    return c;
}

/* SOF..CRC 끝까지 스터핑 전 비트열. 반환: 비트 수, CAN_E_ARG, CAN_E_CAP (용량 부족이면 아무것도 안 씀) */
static int can_build_fields(const can_frame_t *f, uint8_t *bits, size_t cap) {
    (void)f; (void)bits; (void)cap; (void)put_bits;
    return CAN_E_CAP; /* TODO: 인자 검사 → 길이(34/54 + 8n) 검사 → SOF, ID(-A), RTR/SRR, IDE, (ID-B, RTR, r1), r0, DLC, DATA, CRC-15 */
}

/* 같은 값 5개 연속(스터프 비트 포함) 뒤에 반대 비트를 넣는다. 마지막 입력 비트 뒤도 포함.
 * 반환: 출력 비트 수 또는 CAN_E_CAP (cap 밖에는 절대 안 씀). *nstuff = 넣은 스터프 수 */
static int can_stuff(const uint8_t *in, size_t n, uint8_t *out, size_t cap, int *nstuff) {
    (void)in; (void)n; (void)out; (void)cap; (void)nstuff;
    return CAN_E_CAP; /* TODO: 같은 값 5개(스터프 비트 포함) 뒤에 반대 비트, 마지막 비트 뒤도, cap 밖에 쓰지 않기 */
}

/* 전체 프레임 = 스터핑된 SOF..CRC + CRC delim(1) + ACK slot + ACK delim(1) + EOF(1111111).
 * acked = false: 송신기가 내보내는 값 (ACK slot recessive), true: 수신기가 덮어쓴 버스 값 (dominant) */
static int can_encode(const can_frame_t *f, bool acked, uint8_t *out, size_t cap, int *nstuff) {
    (void)f; (void)acked; (void)out; (void)cap; (void)nstuff;
    return CAN_E_CAP; /* TODO: can_build_fields → can_stuff → CRCdel(1) ACK(acked ? 0 : 1) ACKdel(1) EOF(1 x 7) */
}

/* ---------------- Part B: 비트 단위 수신 상태 기계 ---------------- */
typedef enum { CAN_BUSY = 0, CAN_OK, CAN_ERR_STUFF, CAN_ERR_CRC, CAN_ERR_FORM, CAN_ERR_TRUNC } can_status;
typedef enum { RX_IDLE, RX_STUFFED, RX_CRC_DEL, RX_ACK, RX_ACK_DEL, RX_EOF, RX_END } rx_state;
typedef enum { DS_MORE, DS_DONE, DS_STUFF_ERR } ds_result;

#define MAX_STUFF_LOG 32
typedef struct {
    rx_state st;
    can_status result;                 /* RX_END 일 때 최종 결과 */
    uint8_t last, run;                 /* 디스터핑: 마지막 와이어 비트, 같은 값 연속 길이 (스터프 비트 포함) */
    uint8_t ub[CAN_MAX_UNSTUFFED];     /* 디스터핑된 비트 SOF..CRC */
    uint16_t upos[CAN_MAX_UNSTUFFED];  /* ub[k] 의 와이어 위치 (SOF = 0) — LA 주석용 */
    int nu, need;                      /* 받은 / 받아야 할 언스터프 비트 수 (DLC 전에는 최댓값) */
    int wire;                          /* 마지막으로 받은 와이어 비트 위치 */
    int nstuff;
    uint16_t spos[MAX_STUFF_LOG];      /* 스터프 비트 와이어 위치 */
    int crc_end;                       /* 스터핑 구간 마지막 와이어 비트 (뒤따른 스터프 비트 포함) */
    int eof_n;
    bool acked, overload;
    int err_pos;
    can_frame_t f;
} can_rx_t;

static void can_rx_reset(can_rx_t *rx) { memset(rx, 0, sizeof *rx); rx->st = RX_IDLE; rx->err_pos = -1; }
static can_status rx_fail(can_rx_t *rx, can_status e, int pos) {
    rx->st = RX_END; rx->result = e; rx->err_pos = pos;
    return e;
}

/* STUFFED 상태의 와이어 비트 하나: 스터프 비트면 확인 후 버리고, 아니면 ub 에 저장.
 * DLC 를 받으면 need 확정. 마지막 CRC 비트(와 뒤따를 스터프 비트)까지 받으면 DS_DONE */
static ds_result rx_destuff(can_rx_t *rx, unsigned bit, int pos) {
    (void)rx; (void)bit; (void)pos;
    return DS_STUFF_ERR; /* TODO: run == 5 이면 스터프 비트 검사·버림, 아니면 ub[] 저장, nu==19/39 에서 need, 끝이면 DS_DONE */
}

/* 스터핑 구간이 끝났다: 필드를 꺼내 rx->f 에 넣고 CRC 를 확인한다 */
static can_status rx_end_stuffed(can_rx_t *rx, int pos) {
    (void)rx;
    return rx_fail(rx, CAN_ERR_CRC, pos); /* TODO: ub[] → rx->f (std/ext), crc_end = pos, CRC 비교, st = RX_CRC_DEL */
}

/* 고정 형식 구간: CRC delim(1) ACK slot(아무 값) ACK delim(1) EOF 7개(1, 마지막 비트 0 = overload) */
static can_status rx_tail(can_rx_t *rx, unsigned bit, int pos) {
    (void)bit;
    return rx_fail(rx, CAN_ERR_FORM, pos); /* TODO: CRCdel=1, ACK 기록, ACKdel=1, EOF 1 x 7 (7번째 0 = overload) */
}

/* 주어진 디스패처: idle 에서 첫 dominant = SOF (hard sync), 이후 상태별 처리 */
static can_status can_rx_bit(can_rx_t *rx, unsigned bit) {
    bit &= 1u;
    if (rx->st == RX_END) return rx->result;
    if (rx->st == RX_IDLE) {
        if (bit == REC) return CAN_BUSY;              /* 버스 idle */
        rx->st = RX_STUFFED;
        rx->wire = 0; rx->last = DOM; rx->run = 1;
        rx->ub[0] = DOM; rx->upos[0] = 0; rx->nu = 1;
        rx->need = CAN_MAX_UNSTUFFED;
        return CAN_BUSY;
    }
    int pos = ++rx->wire;
    if (rx->st == RX_STUFFED) {
        ds_result r = rx_destuff(rx, bit, pos);
        if (r == DS_STUFF_ERR) return rx_fail(rx, CAN_ERR_STUFF, pos);
        if (r == DS_DONE) return rx_end_stuffed(rx, pos);
        return CAN_BUSY;
    }
    return rx_tail(rx, bit, pos);
}

/* 비트 배열 전체를 먹인다. 프레임이 끝나기 전에 입력이 끝나면 CAN_ERR_TRUNC */
static can_status can_decode(const uint8_t *bus, size_t n, can_rx_t *rx) {
    can_rx_reset(rx);
    for (size_t i = 0; i < n; i++) {
        can_status s = can_rx_bit(rx, bus[i]);
        if (s != CAN_BUSY) return s;
    }
    return CAN_ERR_TRUNC;
}

/* ---------------- Part C: LA 주석 · 최악 길이 ---------------- */
typedef struct { const char *name; int first, last; uint32_t value; } can_span;

/* 성공적으로 받은 rx 에서 필드별 와이어 구간 [first,last] (스터프 비트 포함) 과 값을 채운다.
 * 표준: SOF ID RTR IDE r0 DLC [DATA] CRC CRCdel ACK ACKdel EOF
 * 확장: SOF ID-A SRR IDE ID-B RTR r1 r0 DLC [DATA] CRC CRCdel ACK ACKdel EOF
 * DATA 는 데이터 길이가 0 이면 생략, value = 바이트 수. 반환: 필드 수 또는 CAN_E_CAP */
static int can_fields(const can_rx_t *rx, can_span *sp, int cap) {
    (void)rx; (void)sp; (void)cap;
    return CAN_E_CAP; /* TODO: 헤더 필드는 upos[시작]..upos[끝], DATA, CRC(..crc_end), 고정 필드는 crc_end + 1.. */
}

/* Davis/Burns/Bril/Lukkien (2007) 최악 프레임 길이 [비트], 3비트 IFS 포함:
 *   표준 8n + 47 + floor((34 + 8n - 1) / 4),  확장 8n + 67 + floor((54 + 8n - 1) / 4) */
static int can_frame_bits_bound(int n, bool ext) {
    (void)n; (void)ext;
    return 0; /* TODO: g = (ext ? 54 : 34) + 8n; 고정 10 + IFS 3 + g + floor((g - 1) / 4) */
}

/* 표준 데이터 프레임, DLC = n (0 또는 1): 모든 ID(0..0x7FF) × 모든 데이터로 스터프 수 최댓값.
 * *count = 최댓값을 내는 프레임 수, *worst = 그중 첫 프레임 */
static int max_stuff_exhaustive(int n, long *count, can_frame_t *worst) {
    (void)n; (void)worst;
    *count = 0;
    return -1; /* TODO: 모든 ID x 모든 데이터(n = 0: 1개, 1: 256개) 인코드 → 스터프 수 최댓값과 개수 */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static can_frame_t rand_frame(uint32_t *s) {
    static const uint8_t HEAVY[] = { 0x00, 0xFF, 0x0F, 0xF0, 0x78, 0x3C, 0x87, 0xC3, 0x1E, 0xE1 };
    can_frame_t f;
    memset(&f, 0, sizeof f);
    f.ide = xs32(s) & 1u;
    f.rtr = xs32(s) % 5u == 0;
    f.id = xs32(s) & (f.ide ? CAN_EXT_MAX_ID : CAN_STD_MAX_ID);
    f.dlc = (uint8_t)(xs32(s) % 16u);
    bool heavy = xs32(s) % 3u == 0;                   /* 스터핑이 많이 생기는 바이트를 섞는다 */
    for (int i = 0; i < can_data_len(f.rtr, f.dlc); i++)
        f.data[i] = heavy ? HEAVY[xs32(s) % sizeof HEAVY] : (uint8_t)xs32(s);
    return f;
}
static bool frame_eq(const can_frame_t *a, const can_frame_t *b) {
    if (a->id != b->id || a->ide != b->ide || a->rtr != b->rtr || a->dlc != b->dlc) return false;
    return memcmp(a->data, b->data, (size_t)can_data_len(a->rtr, a->dlc)) == 0;
}
static bool bits_eq_str(const uint8_t *b, int n, const char *s) {
    if (n < 0 || (size_t)n != strlen(s)) return false;
    for (int i = 0; i < n; i++) if (b[i] != (uint8_t)(s[i] - '0')) return false;
    return true;
}
static int bits_from_str(const char *s, uint8_t *b) {
    int n = 0;
    for (; s[n]; n++) b[n] = (uint8_t)(s[n] - '0');
    return n;
}
static void print_bits(const uint8_t *b, int n) { for (int i = 0; i < n; i++) putchar('0' + b[i]); putchar('\n'); }

/* verify_frame.py (긴 나눗셈 CRC + 문자열 스터핑, 독립 구현) 가 출력한 송신기 관점 비트열 */
static const struct { can_frame_t f; const char *bits; int nstuff; } KNOWN[] = {
    { { 0x123, false, false, 2, { 0x11, 0x22 } },
      "00010010001100000110000100010010001000001100101101111111111111", 2 },
    { { 0x000, false, false, 8, { 0 } },
      "0000010000010000011000001000001000001000001000001000001000001000001000001000001000001000001000001000010100010110111111111111", 16 },
    { { 0x555, false, true, 8, { 0 } },
      "010101010101100100011000001100011101111111111", 1 },
    { { 0x18DAF110, true, false, 8, { 0x02, 0x10, 0x03, 0, 0, 0, 0, 0 } },
      "011000110110111011110001000100000100100000100001000010000010000011100000100000100000100000100000100000100000100000101010000011100111111111111", 13 },
    { { 0x1FFFFFFF, true, false, 0, { 0 } },
      "01111101111101111101111101111101111101000001000011100100010001111111111", 7 },
};
#define NKNOWN (int)(sizeof KNOWN / sizeof KNOWN[0])
/* KNOWN[i] 를 버스 관점으로: 다른 노드가 ACK slot 을 dominant 로 덮어쓴 모양 */
static int known_bus(int i, uint8_t *w) {
    int n = bits_from_str(KNOWN[i].bits, w);
    w[n - 9] = DOM;
    return n;
}

int main(void) {
    uint32_t seed = 0xC0FFEEu;
    uint8_t raw[CAN_MAX_UNSTUFFED + 8], wire[CAN_MAX_WIRE + 16], buf[CAN_MAX_WIRE + 16];
    can_rx_t rx;

    /* ===== Part A ===== */
    static const uint8_t s9[] = "123456789";
    printf("CRC-15/CAN(\"123456789\") = 0x%04X\n", crc15_bytes(s9, 9));
    CHECK(crc15_bytes(s9, 9) == 0x059Eu);            /* RevEng CRC-15/CAN check */
    CHECK(crc15_bytes(s9, 0) == 0);
    {
        can_frame_t f1 = { 0x123, false, false, 2, { 0x11, 0x22 } }, f2 = { 0x1ABCDEF, true, false, 8, { 0 } };
        can_frame_t f3 = { 0x7A, false, true, 8, { 0 } }, f4 = { 0x7A, true, true, 3, { 0 } };
        CHECK(can_build_fields(&f1, raw, sizeof raw) == 34 + 16);
        CHECK(can_build_fields(&f2, raw, sizeof raw) == 54 + 64);
        CHECK(can_build_fields(&f3, raw, sizeof raw) == 34);    /* 리모트: DLC 8 이어도 데이터 없음 */
        CHECK(can_build_fields(&f4, raw, sizeof raw) == 54);
        int n = can_build_fields(&f1, raw, sizeof raw);
        CHECK(n == 50 && raw[0] == DOM && get_bits(raw, 1, 11) == 0x123 && raw[12] == DOM && raw[13] == DOM
              && get_bits(raw, 15, 4) == 2 && get_bits(raw, 19, 8) == 0x11 && get_bits(raw, 35, 15) == 0x04B7);
    }
    {   /* 나머지(residue) = 0: 프레임 비트 + 자기 CRC 를 다시 CRC 하면 0 */
        int bad = 0;
        uint32_t s = seed;
        for (int t = 0; t < 3000; t++) {
            can_frame_t f = rand_frame(&s);
            int n = can_build_fields(&f, raw, sizeof raw);
            if (n < 0 || crc15_bits(raw, (size_t)n) != 0) bad++;
        }
        CHECK(bad == 0);
    }
    {   /* 스터핑 규칙: 스터프 비트도 다음 연속에 센다, 마지막 비트 뒤에도 넣는다 */
        static const struct { const char *in, *out; int ns; } V[] = {
            { "00000", "000001", 1 }, { "000000", "0000010", 1 }, { "0000011111", "000001111101", 2 },
            { "1111111111", "111110111110", 2 }, { "0101010101", "0101010101", 0 }, { "0000100001", "0000100001", 0 } };
        int bad = 0;
        for (size_t i = 0; i < sizeof V / sizeof V[0]; i++) {
            int n = bits_from_str(V[i].in, raw), ns = -1;
            int w = can_stuff(raw, (size_t)n, wire, sizeof wire, &ns);
            if (!bits_eq_str(wire, w, V[i].out) || ns != V[i].ns) bad++;
        }
        CHECK(bad == 0);
        int n = bits_from_str("00000", raw);
        memset(wire, 0xEE, sizeof wire);
        CHECK(can_stuff(raw, (size_t)n, wire, 5, NULL) == CAN_E_CAP && wire[5] == 0xEE);   /* 6비트 필요 */
    }
    {
        can_frame_t bad1 = { 0x800, false, false, 0, { 0 } }, bad2 = { 0x20000000, true, false, 0, { 0 } };
        can_frame_t bad3 = { 0x10, false, false, 16, { 0 } }, ok = { 0x10, false, false, 8, { 0 } };
        CHECK(can_build_fields(&bad1, raw, sizeof raw) == CAN_E_ARG);
        CHECK(can_build_fields(&bad2, raw, sizeof raw) == CAN_E_ARG);
        CHECK(can_build_fields(&bad3, raw, sizeof raw) == CAN_E_ARG);
        CHECK(can_build_fields(&ok, raw, 97) == CAN_E_CAP && can_build_fields(&ok, raw, 98) == 98);
        CHECK(can_encode(&ok, false, wire, 50, NULL) == CAN_E_CAP);
    }
    {   /* 알려진 비트열 대조 (verify_frame.py) */
        int bad = 0;
        for (int i = 0; i < NKNOWN; i++) {
            int ns = -1, w = can_encode(&KNOWN[i].f, false, wire, sizeof wire, &ns);
            if (!bits_eq_str(wire, w, KNOWN[i].bits) || ns != KNOWN[i].nstuff) { bad++; if (w > 0) print_bits(wire, w); }
        }
        CHECK(bad == 0);
        int ns = 0, w = can_encode(&KNOWN[0].f, false, wire, sizeof wire, &ns);
        printf("K1 std 0x123 DLC2 11 22 (%d bits, %d stuff): ", w, ns);
        if (w > 0) print_bits(wire, w); else printf("encode error %d\n", w);
    }

    /* ===== Part B ===== */
    {   /* 랜덤 왕복: 인코드(버스 관점 ACK=dominant) → 디코드 */
        int bad = 0;
        uint32_t s = seed ^ 0x1234u;
        for (int t = 0; t < 20000; t++) {
            can_frame_t f = rand_frame(&s);
            int ns = 0, w = can_encode(&f, true, wire, sizeof wire, &ns);
            if (w < 0) { bad++; continue; }
            can_status st = can_decode(wire, (size_t)w, &rx);
            if (st != CAN_OK || !frame_eq(&f, &rx.f) || rx.nstuff != ns || rx.wire != w - 1 || !rx.acked || rx.overload)
                bad++;
        }
        printf("random round trip: 20000 frames, bad=%d\n", bad);
        CHECK(bad == 0);
    }
    {   /* 표준 ID 2048개 전부 × DLC 0..15 (데이터/리모트) */
        int bad = 0;
        uint32_t s = seed ^ 0x55u;
        for (uint32_t id = 0; id <= CAN_STD_MAX_ID; id++)
            for (int dlc = 0; dlc < 16; dlc++) {
                can_frame_t f = { id, false, (dlc & 1) && id % 3 == 0, (uint8_t)dlc, { 0 } };
                for (int i = 0; i < 8; i++) f.data[i] = (uint8_t)xs32(&s);
                int w = can_encode(&f, true, wire, sizeof wire, NULL);
                if (w < 0 || can_decode(wire, (size_t)w, &rx) != CAN_OK || !frame_eq(&f, &rx.f)) bad++;
            }
        CHECK(bad == 0);
    }
    {   /* DLC 9..15: 데이터 8 바이트, DLC 값은 그대로 */
        can_frame_t f = { 0x321, false, false, 13, { 1, 2, 3, 4, 5, 6, 7, 8 } };
        int w = can_encode(&f, true, wire, sizeof wire, NULL);
        CHECK(w > 0 && can_decode(wire, (size_t)w, &rx) == CAN_OK && rx.f.dlc == 13 && rx.f.data[7] == 8);
    }
    {   /* 앞에 idle(recessive) 11비트: 첫 dominant 가 SOF, 위치는 SOF 기준 */
        memset(buf, REC, 11);
        int w = can_encode(&KNOWN[0].f, true, buf + 11, sizeof buf - 11, NULL);
        CHECK(w > 0 && can_decode(buf, (size_t)w + 11, &rx) == CAN_OK && frame_eq(&rx.f, &KNOWN[0].f) && rx.wire == w - 1);
    }
    {   /* 스터프 비트를 뒤집으면 → 앞의 5개와 같아져 6연속 → 정확히 그 위치에서 STUFF */
        int tested = 0, bad = 0;
        uint32_t s = seed ^ 0x777u;
        for (int t = 0; t < 2000; t++) {
            can_frame_t f = rand_frame(&s);
            int ns = 0, w = can_encode(&f, true, wire, sizeof wire, &ns);
            if (w < 0 || can_decode(wire, (size_t)w, &rx) != CAN_OK) { bad++; continue; }
            int nlog = rx.nstuff < MAX_STUFF_LOG ? rx.nstuff : MAX_STUFF_LOG;
            uint16_t sp[MAX_STUFF_LOG];
            memcpy(sp, rx.spos, sizeof sp);
            for (int k = 0; k < nlog; k++) {
                memcpy(buf, wire, (size_t)w);
                buf[sp[k]] ^= 1u;
                tested++;
                if (can_decode(buf, (size_t)w, &rx) != CAN_ERR_STUFF || rx.err_pos != sp[k]) bad++;
            }
        }
        printf("stuff-bit flips: %d tested, wrong=%d\n", tested, bad);
        CHECK(tested > 5000 && bad == 0);
    }
    {   /* CRC 오류: 데이터 비트 하나를 바꾸고 옛 CRC 를 유지한 채 다시 스터핑 (스터핑은 올바름) */
        int tested = 0, bad = 0;
        uint32_t s = seed ^ 0x999u;
        for (int t = 0; t < 2000; t++) {
            can_frame_t f = rand_frame(&s);
            int n = can_build_fields(&f, raw, sizeof raw);
            int nd = can_data_len(f.rtr, f.dlc);
            if (n < 0) { bad++; continue; }
            int at = 1 + (int)(xs32(&s) % (uint32_t)(n - 1));       /* SOF 빼고 아무 비트 (CRC 포함) */
            if (nd == 0 && at < n - 15 && xs32(&s) & 1u) at = n - 15 + (int)(xs32(&s) % 15u);
            raw[at] ^= 1u;
            int w = can_stuff(raw, (size_t)n, wire, sizeof wire, NULL);
            if (w < 0) { bad++; continue; }
            memcpy(wire + w, "\1\0\1\1\1\1\1\1\1\1", 10);
            w += 10;
            can_status st = can_decode(wire, (size_t)w, &rx);
            /* 헤더(IDE/DLC/RTR) 비트를 바꾸면 길이가 달라져 CRC 가 아닌 다른 오류도 가능 → 데이터·ID·CRC 만 엄격히 */
            bool len_field = (at == 12 || at == 13 || (!f.ide && at >= 15 && at <= 18) || (f.ide && at >= 32 && at <= 38));
            tested++;
            if (st == CAN_OK) bad++;
            else if (!len_field && st != CAN_ERR_CRC) bad++;
        }
        CHECK(tested == 2000 && bad == 0);
    }
    {   /* 고정 형식 필드 — 디코더만 시험하도록 인코더 대신 K1 비트열(버스 관점)을 쓴다 */
        int w = known_bus(0, wire);                       /* 62비트, 스터핑 구간 0..51 */
        int c = w - 10;                                   /* CRC delimiter 위치 */
        memcpy(buf, wire, (size_t)w); buf[c] = DOM;
        CHECK(can_decode(buf, (size_t)w, &rx) == CAN_ERR_FORM && rx.err_pos == c);
        memcpy(buf, wire, (size_t)w); buf[c + 2] = DOM;
        CHECK(can_decode(buf, (size_t)w, &rx) == CAN_ERR_FORM && rx.err_pos == c + 2);
        int bad = 0;
        for (int e = 0; e < 6; e++) {
            memcpy(buf, wire, (size_t)w); buf[c + 3 + e] = DOM;
            if (can_decode(buf, (size_t)w, &rx) != CAN_ERR_FORM || rx.err_pos != c + 3 + e) bad++;
        }
        CHECK(bad == 0);
        memcpy(buf, wire, (size_t)w); buf[w - 1] = DOM;   /* EOF 7번째 비트 */
        CHECK(can_decode(buf, (size_t)w, &rx) == CAN_OK && rx.overload && frame_eq(&rx.f, &KNOWN[0].f));
        memcpy(buf, wire, (size_t)w); buf[c + 1] = REC;   /* 아무도 ACK 안 함: 수신기는 OK (송신기는 ACK error = M07) */
        CHECK(can_decode(buf, (size_t)w, &rx) == CAN_OK && !rx.acked);
        CHECK(can_decode(wire, (size_t)w - 1, &rx) == CAN_ERR_TRUNC);
        memset(buf, REC, 40);
        CHECK(can_decode(buf, 40, &rx) == CAN_ERR_TRUNC);
        memcpy(buf, wire, (size_t)w);                     /* CRC 마지막 비트 */
        buf[c - 1] ^= 1u;
        can_status st = can_decode(buf, (size_t)w, &rx);
        CHECK(st == CAN_ERR_CRC || st == CAN_ERR_STUFF);
    }
    {   /* 단일 비트 전수 뒤집기 (버스 관점, 뒤에 idle 11비트): 오류로 잡히거나, 내용이 같아야 한다 */
        long flips = 0, detected = 0, same = 0, undetected = 0;
        long by[6] = { 0 };
        uint32_t s = seed ^ 0xABCu;
        for (int t = 0; t < 400; t++) {
            can_frame_t f = rand_frame(&s);
            int w = can_encode(&f, true, wire, sizeof wire, NULL);
            if (w < 0) { undetected++; continue; }
            memset(wire + w, REC, 11);
            for (int p = 0; p < w; p++) {
                memcpy(buf, wire, (size_t)w + 11);
                buf[p] ^= 1u;
                can_status st = can_decode(buf, (size_t)w + 11, &rx);
                flips++;
                by[st]++;
                if (st != CAN_OK) detected++;
                else if (frame_eq(&rx.f, &f)) same++;
                else undetected++;
            }
        }
        printf("single-bit flips: %ld, detected=%ld (stuff %ld, crc %ld, form %ld, trunc %ld), same-content=%ld, undetected=%ld\n",
               flips, detected, by[CAN_ERR_STUFF], by[CAN_ERR_CRC], by[CAN_ERR_FORM], by[CAN_ERR_TRUNC], same, undetected);
        CHECK(flips > 30000 && undetected == 0 && same == 800);   /* same = ACK slot + EOF 7번째, 프레임당 2개 */
    }

    /* ===== Part C ===== */
    {
        int w = known_bus(0, wire);
        can_span sp[16];
        int nf = can_decode(wire, (size_t)w, &rx) == CAN_OK ? can_fields(&rx, sp, 16) : -1;
        /* K1 (verify_frame.py 비트열에서 손으로 셈): 스터프 비트 와이어 17 (DLC 안), 40 (CRC 안) */
        static const can_span EXP[] = {
            { "SOF", 0, 0, 0 }, { "ID", 1, 11, 0x123 }, { "RTR", 12, 12, 0 }, { "IDE", 13, 13, 0 }, { "r0", 14, 14, 0 },
            { "DLC", 15, 19, 2 }, { "DATA", 20, 35, 2 }, { "CRC", 36, 51, 0x04B7 }, { "CRCdel", 52, 52, 1 },
            { "ACK", 53, 53, 0 }, { "ACKdel", 54, 54, 1 }, { "EOF", 55, 61, 0x7F } };
        bool ok = nf == 12;
        for (int i = 0; ok && i < 12; i++)
            ok = strcmp(sp[i].name, EXP[i].name) == 0 && sp[i].first == EXP[i].first && sp[i].last == EXP[i].last
                 && sp[i].value == EXP[i].value;
        CHECK(ok);
        CHECK(can_fields(&rx, sp, 11) == CAN_E_CAP);
        if (nf > 0) {
            printf("LA decode @500 kbit/s (2 us/bit), stuff bits at wire");
            for (int k = 0; k < rx.nstuff && k < MAX_STUFF_LOG; k++) printf(" %d", rx.spos[k]);
            printf("\n");
            for (int i = 0; i < nf; i++)
                printf("  [%3d..%3d] %6.1f us  %-6s 0x%X\n", sp[i].first, sp[i].last, sp[i].first * 2.0, sp[i].name,
                       (unsigned)sp[i].value);
        }
        int w4 = can_encode(&KNOWN[3].f, true, wire, sizeof wire, NULL);
        nf = (w4 > 0 && can_decode(wire, (size_t)w4, &rx) == CAN_OK) ? can_fields(&rx, sp, 16) : -1;
        CHECK(nf == 15 && strcmp(sp[4].name, "ID-B") == 0 && sp[1].value == (0x18DAF110u >> 18)
              && sp[4].value == (0x18DAF110u & 0x3FFFFu) && sp[2].value == 1 && sp[14].last == w4 - 1);
    }
    CHECK(can_frame_bits_bound(8, false) == 135 && can_frame_bits_bound(8, true) == 160);
    CHECK(can_frame_bits_bound(0, false) == 55 && can_frame_bits_bound(0, true) == 80);
    {   /* 인코더 출력은 언제나 상한 이내: wire + IFS 3 <= bound */
        int bad = 0;
        uint32_t s = seed ^ 0x3141u;
        for (int t = 0; t < 20000; t++) {
            can_frame_t f = rand_frame(&s);
            int w = can_encode(&f, true, wire, sizeof wire, NULL);
            if (w < 0 || w + 3 > can_frame_bits_bound(can_data_len(f.rtr, f.dlc), f.ide)) bad++;
        }
        CHECK(bad == 0);
    }
    {
        long c0 = 0, c1 = 0;
        can_frame_t w0, w1;
        memset(&w0, 0, sizeof w0); memset(&w1, 0, sizeof w1);
        int m0 = max_stuff_exhaustive(0, &c0, &w0), m1 = max_stuff_exhaustive(1, &c1, &w1);
        printf("exhaustive max stuff (std data): DLC0 %d (bound %d, %ld frames, e.g. ID 0x%03X)"
               " | DLC1 %d (bound %d, %ld frames, e.g. ID 0x%03X data %02X)\n",
               m0, (34 - 1) / 4, c0, (unsigned)w0.id, m1, (42 - 1) / 4, c1, (unsigned)w1.id, w1.data[0]);
        CHECK(m0 >= 0 && m0 <= (34 - 1) / 4 && m1 >= 0 && m1 <= (42 - 1) / 4);
        CHECK(m0 == 6 && m1 == 7);   /* demo_worst.c 의 DP(독립 방법)와 같은 값, 상한 8 · 10 보다 작다 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
