/* COMM-B13 추가 연습 — 한 번에 검증
 *
 * 연습 1: CRC 잔여값(residue) 검사 — 프레임 전체(CRC 포함)의 CRC 가 0, CRC 바이트 순서 실수 잡기
 * 연습 2: FC 0x10 Write Multiple Registers (원자적 쓰기, 예외 02/03)
 * 연습 3: Modbus ASCII 프레임 — ':' + 16진 + LRC + CR LF (인코더 + 검증 디코더)
 * 연습 4: Modbus TCP ↔ RTU 게이트웨이 — MBAP 헤더 변환 + 게이트웨이 예외 0x0B
 * 연습 5: 32비트 값/float 을 레지스터 두 개로 — 워드 순서 ABCD vs CDAB
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint16_t get_be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

/* solution.c 의 CRC-16/MODBUS 그대로 */
static uint16_t crc16_modbus(const uint8_t *p, size_t n) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++)
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
    }
    return crc;
}
static size_t adu_add_crc(uint8_t *buf, size_t n, size_t cap) {
    if (n + 2 > cap) return 0;
    uint16_t c = crc16_modbus(buf, n);
    buf[n] = (uint8_t)c;
    buf[n + 1] = (uint8_t)(c >> 8);
    return n + 2;
}

/* ---- 연습 1: residue ---- */
static bool adu_crc_ok(const uint8_t *f, size_t n) { return n >= 4 && crc16_modbus(f, n) == 0; }

/* ---- 연습 2: FC 0x10 ---- */
#define NREGS 16u
static uint16_t regs[NREGS];
/* pdu = [10][start][qty][byte count][값 BE × qty]. 성공 응답 PDU = [10][start][qty]. 실패 = -예외 */
static int pdu_fc10(const uint8_t *pdu, size_t pn, uint8_t *out) {
    if (pn < 6) return -3;
    uint16_t start = get_be16(pdu + 1), qty = get_be16(pdu + 3);
    uint8_t bc = pdu[5];
    if (qty < 1 || qty > 123 || bc != 2u * qty || pn != 6u + bc) return -3;
    if ((uint32_t)start + qty > NREGS) return -2;
    for (unsigned i = 0; i < qty; i++) regs[start + i] = get_be16(pdu + 6 + 2 * i);  /* 검사 뒤에 한꺼번에 */
    memcpy(out, pdu, 5);
    return 5;
}

/* ---- 연습 3: Modbus ASCII ---- */
static uint8_t lrc8(const uint8_t *p, size_t n) {
    uint8_t s = 0;
    for (size_t i = 0; i < n; i++) s = (uint8_t)(s + p[i]);
    return (uint8_t)(0u - s);
}
static const char HEX[] = "0123456789ABCDEF";
/* adu = [addr][PDU] (CRC 없음). 출력 = ":" + 2n+2 hex + "\r\n" + NUL. 반환 = 글자 수 (NUL 제외), 넘치면 0 */
static size_t mb_ascii_encode(const uint8_t *adu, size_t n, char *out, size_t cap) {
    size_t need = 1 + 2 * (n + 1) + 2 + 1;
    if (cap < need) return 0;
    size_t k = 0;
    out[k++] = ':';
    uint8_t l = lrc8(adu, n);
    for (size_t i = 0; i <= n; i++) {
        uint8_t b = i < n ? adu[i] : l;
        out[k++] = HEX[b >> 4];
        out[k++] = HEX[b & 15];
    }
    out[k++] = '\r'; out[k++] = '\n'; out[k] = '\0';
    return k;
}
static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;                               /* 스펙상 대문자만 (이 연습의 정책) */
}
/* 반환 = adu 길이 (LRC 제외), 형식·LRC 오류면 -1 */
static int mb_ascii_decode(const char *s, uint8_t *adu, size_t cap) {
    size_t L = strlen(s);
    if (L < 1 + 2 * 3 + 2 || s[0] != ':' || s[L - 2] != '\r' || s[L - 1] != '\n' || (L - 3) % 2) return -1;
    size_t nb = (L - 3) / 2;                 /* LRC 포함 바이트 수 */
    if (nb - 1 > cap) return -1;
    uint8_t sum = 0;
    for (size_t i = 0; i < nb; i++) {
        int h = hexv(s[1 + 2 * i]), l = hexv(s[2 + 2 * i]);
        if (h < 0 || l < 0) return -1;
        uint8_t last = (uint8_t)(h << 4 | l);
        sum = (uint8_t)(sum + last);
        if (i < nb - 1) adu[i] = last;
    }
    return sum == 0 ? (int)(nb - 1) : -1;
}

/* ---- 연습 4: TCP ↔ RTU ---- */
/* tcp = [tid 2][pid 2 = 0][len 2 = 1 + PDU][uid][PDU]. 반환 = RTU 길이, 형식 오류/넘침이면 0 */
static size_t tcp_to_rtu(const uint8_t *tcp, size_t n, uint8_t *rtu, size_t cap) {
    if (n < 8 || get_be16(tcp + 2) != 0 || get_be16(tcp + 4) != n - 6 || n - 6 > 254) return 0;
    if (n - 6 + 2 > cap) return 0;
    memcpy(rtu, tcp + 6, n - 6);             /* uid 가 그대로 RTU 주소 */
    return adu_add_crc(rtu, n - 6, cap);
}
/* RTU 응답 → TCP 응답 (요청의 tid 를 돌려준다). CRC 틀리면 0 */
static size_t rtu_to_tcp(const uint8_t *rtu, size_t n, uint16_t tid, uint8_t *tcp, size_t cap) {
    if (!adu_crc_ok(rtu, n) || n - 2 + 6 > cap) return 0;
    put_be16(tcp, tid); put_be16(tcp + 2, 0); put_be16(tcp + 4, (uint16_t)(n - 2));
    memcpy(tcp + 6, rtu, n - 2);
    return n - 2 + 6;
}
/* 슬레이브가 답이 없을 때 게이트웨이가 만드는 예외 (0x0B = Gateway Target Device Failed to Respond) */
static size_t gw_exception(uint16_t tid, uint8_t uid, uint8_t fc, uint8_t code, uint8_t *tcp) {
    put_be16(tcp, tid); put_be16(tcp + 2, 0); put_be16(tcp + 4, 3);
    tcp[6] = uid; tcp[7] = (uint8_t)(fc | 0x80); tcp[8] = code;
    return 9;
}

/* ---- 연습 5: 32비트 값 ---- */
static uint32_t regs_to_u32(const uint16_t r[2], bool word_swap) {
    return word_swap ? ((uint32_t)r[1] << 16 | r[0]) : ((uint32_t)r[0] << 16 | r[1]);
}
static void u32_to_regs(uint32_t v, uint16_t r[2], bool word_swap) {
    uint16_t hi = (uint16_t)(v >> 16), lo = (uint16_t)v;
    r[0] = word_swap ? lo : hi;
    r[1] = word_swap ? hi : lo;
}
static float regs_to_float(const uint16_t r[2], bool word_swap) {
    uint32_t u = regs_to_u32(r, word_swap);
    float f;
    memcpy(&f, &u, sizeof f);                /* 포인터 캐스팅 대신 memcpy (C-M13) */
    return f;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

int main(void) {
    uint32_t seed = 0x2468ACE1u;
    static const uint8_t REQ[8] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD };

    /* 연습 1 */
    {
        CHECK(adu_crc_ok(REQ, 8));
        int ok = 0, swapped_ok = 0, same = 0;
        for (int t = 0; t < 2000; t++) {
            uint8_t f[260];
            size_t n = 2 + xs32(&seed) % 252u;
            for (size_t i = 0; i < n; i++) f[i] = (uint8_t)xs32(&seed);
            n = adu_add_crc(f, n, sizeof f);
            ok += adu_crc_ok(f, n);
            if (f[n - 2] == f[n - 1]) same++;
            uint8_t tmp = f[n - 2]; f[n - 2] = f[n - 1]; f[n - 1] = tmp;   /* 상위 바이트 먼저 보낸 실수 */
            swapped_ok += adu_crc_ok(f, n);
        }
        printf("ex1 residue: 2000 frames ok=%d, swapped-CRC frames accepted=%d (lo==hi cases=%d)\n", ok, swapped_ok, same);
        CHECK(ok == 2000 && swapped_ok == same);
    }

    /* 연습 2 */
    {
        uint8_t pdu[260], out[8];
        for (unsigned i = 0; i < NREGS; i++) regs[i] = 0;
        uint8_t w[] = { 0x10, 0x00, 0x02, 0x00, 0x03, 0x06, 0x00, 0x0A, 0x01, 0x02, 0xBE, 0xEF };
        CHECK(pdu_fc10(w, sizeof w, out) == 5 && memcmp(out, w, 5) == 0 && regs[2] == 0x000A && regs[3] == 0x0102 && regs[4] == 0xBEEF);
        memcpy(pdu, w, sizeof w); pdu[5] = 0x05;                         /* byte count ≠ 2·qty */
        CHECK(pdu_fc10(pdu, sizeof w, out) == -3);
        memcpy(pdu, w, sizeof w); put_be16(pdu + 1, 14);                  /* 14 + 3 > 16 */
        regs[14] = 0x1111;
        CHECK(pdu_fc10(pdu, sizeof w, out) == -2 && regs[14] == 0x1111);  /* 아무것도 안 씀 */
        CHECK(pdu_fc10(w, sizeof w - 1, out) == -3);                      /* 잘린 PDU */
        pdu[0] = 0x10; put_be16(pdu + 1, 0); put_be16(pdu + 3, 124); pdu[5] = 248;
        CHECK(pdu_fc10(pdu, 6 + 248, out) == -3);                         /* qty 124 > 123 */
        uint8_t adu[16] = { 0x11, 0x10, 0x00, 0x01, 0x00, 0x02 };
        CHECK(adu_add_crc(adu, 6, sizeof adu) == 8);                      /* 응답 ADU 예 */
        printf("ex2 FC10 response 11 10 00 01 00 02 -> CRC %02X %02X\n", adu[6], adu[7]);
    }

    /* 연습 3 */
    {
        char s[600];
        uint8_t a[260];
        CHECK(mb_ascii_encode(REQ, 6, s, sizeof s) == 17 && strcmp(s, ":01030000000AF2\r\n") == 0);
        CHECK(mb_ascii_decode(":01030000000AF2\r\n", a, sizeof a) == 6 && memcmp(a, REQ, 6) == 0);
        CHECK(mb_ascii_decode(":01030000000AF3\r\n", a, sizeof a) == -1);   /* LRC 틀림 */
        CHECK(mb_ascii_decode(":01030000000aF2\r\n", a, sizeof a) == -1);   /* 소문자: 이 연습은 거부 */
        CHECK(mb_ascii_decode(":01030000000AF2\n", a, sizeof a) == -1);
        CHECK(mb_ascii_encode(REQ, 6, s, 17) == 0);                         /* NUL 자리 없음 */
        int bad = 0;
        for (int t = 0; t < 1000; t++) {
            uint8_t m[253];
            size_t n = 2 + xs32(&seed) % 252u;          /* 주소 + FC 이상 */
            for (size_t i = 0; i < n; i++) m[i] = (uint8_t)xs32(&seed);
            if (mb_ascii_encode(m, n, s, sizeof s) != 2 * n + 5 || mb_ascii_decode(s, a, sizeof a) != (int)n || memcmp(a, m, n)) bad++;
        }
        CHECK(bad == 0);
    }

    /* 연습 4 */
    {
        static const uint8_t tcp_req[12] = { 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A };
        static const uint8_t rsp10[25] = { 0x01, 0x03, 0x14, 0x5A, 0x01, 0x01, 0x02, 0x00, 0xFD, 0x01, 0x2C,
            0x00, 0x01, 0x10, 0x05, 0x10, 0x06, 0x10, 0x07, 0x10, 0x08, 0x10, 0x09, 0xCB, 0x7D };
        uint8_t rtu[260], tcp[300], bad[12];
        CHECK(tcp_to_rtu(tcp_req, 12, rtu, sizeof rtu) == 8 && memcmp(rtu, REQ, 8) == 0);
        memcpy(bad, tcp_req, 12); bad[3] = 1;  CHECK(tcp_to_rtu(bad, 12, rtu, sizeof rtu) == 0);   /* pid ≠ 0 */
        memcpy(bad, tcp_req, 12); bad[5] = 7;  CHECK(tcp_to_rtu(bad, 12, rtu, sizeof rtu) == 0);   /* len 불일치 */
        CHECK(tcp_to_rtu(tcp_req, 12, rtu, 7) == 0);
        size_t m = rtu_to_tcp(rsp10, 25, 0x0001, tcp, sizeof tcp);
        CHECK(m == 29 && get_be16(tcp) == 1 && get_be16(tcp + 4) == 23 && memcmp(tcp + 6, rsp10, 23) == 0);
        uint8_t r2[25]; memcpy(r2, rsp10, 25); r2[10] ^= 4;
        CHECK(rtu_to_tcp(r2, 25, 1, tcp, sizeof tcp) == 0);
        static const uint8_t gw[9] = { 0x00, 0x07, 0x00, 0x00, 0x00, 0x03, 0x05, 0x83, 0x0B };
        CHECK(gw_exception(7, 5, 0x03, 0x0B, tcp) == 9 && memcmp(tcp, gw, 9) == 0);
    }

    /* 연습 5 */
    {
        uint16_t r[2];
        float f = 25.3f;
        uint32_t u;
        memcpy(&u, &f, 4);
        CHECK(u == 0x41CA6666u);
        u32_to_regs(u, r, false); CHECK(r[0] == 0x41CA && r[1] == 0x6666 && regs_to_float(r, false) == 25.3f);
        u32_to_regs(u, r, true);  CHECK(r[0] == 0x6666 && r[1] == 0x41CA && regs_to_float(r, true) == 25.3f);
        float wrong = regs_to_float(r, false);
        printf("ex5 25.3f as CDAB read with ABCD order = %g (0x%08X)\n", (double)wrong, (unsigned)regs_to_u32(r, false));
        CHECK(wrong != 25.3f);
        int bad = 0;
        for (int t = 0; t < 1000; t++) {
            uint32_t v = xs32(&seed);
            bool sw = (t & 1) != 0;
            u32_to_regs(v, r, sw);
            if (regs_to_u32(r, sw) != v) bad++;
        }
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
