/* COMM-B13 Modbus RTU 프레임: 함수 코드, CRC-16, 3.5문자 침묵
 *
 * Part A: crc16_modbus, rtu_timing(t1.5/t3.5), 시간 기반 프레이머(rtu_rx_byte / rtu_rx_poll)
 * Part B: 슬레이브 — CRC·주소·broadcast 검사, FC 0x03 / 0x06, 예외 0x01/0x02/0x03, 프레이밍 결함 주입
 * Part C: 마스터 드라이버(요청 생성·응답 검증·재시도) + RS-485 버스(슬레이브 2개) 종단 시험
 *
 * 시간 단위는 µs (uint32_t, 약 71분마다 wrap — 뺄셈으로만 비교). 바이트 타임스탬프 = 그 문자의
 * stop bit 가 끝난 시각(= 수신 ISR 이 불리는 시각). 문자 하나 = 11비트(8E1 또는 8N2).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o sol && ./sol
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- 바이트 헬퍼 (C-M05): 레지스터·주소·개수는 big-endian, CRC 만 little-endian ---- */
static uint16_t get_be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

/* now - then (mod 2^32). 앞뒤가 뒤집힌(음수) 값은 0 으로 본다 */
static uint32_t since(uint32_t now, uint32_t then) {
    uint32_t d = now - then;
    return d > 0x7FFFFFFFu ? 0u : d;
}

/* ======================= Part A: CRC · 타이밍 · 프레이머 ======================= */

/* A1. CRC-16/MODBUS: poly 0x8005 반사형(0xA001), init 0xFFFF, refin=refout=true, xorout 0.
 *     check("123456789") = 0x4B37. 와이어에는 하위 바이트 먼저. */
uint16_t crc16_modbus(const uint8_t *p, size_t n) {
    (void)p; (void)n;
    return 0; /* TODO: init 0xFFFF, 바이트 XOR 후 8번 '오른쪽 시프트 + 0xA001' */
}

/* A2. 문자 시간과 t1.5 / t3.5 (µs).
 *     tc = round(11e6 / baud). baud <= 19200: t1.5 = ceil(1.5·tc_exact), t3.5 = ceil(3.5·tc_exact).
 *     baud > 19200: 고정 750 / 1750 µs (Modbus over Serial Line V1.02 권장값). */
typedef struct { uint32_t tc_us, t15_us, t35_us; } rtu_timing_t;
#define RTU_CHAR_BITS 11u

void rtu_timing(uint32_t baud, rtu_timing_t *t) {
    (void)baud;
    t->tc_us = t->t15_us = t->t35_us = 0; /* TODO: 11비트 문자 시간, 19200 이하는 1.5/3.5 배(올림), 초과는 750/1750 */
}

/* A3. 프레이머: INIT --(t3.5 침묵)--> IDLE --(바이트)--> RX --(t3.5 침묵)--> 프레임 판정 --> IDLE */
#define RTU_MAX_ADU 256u
enum { RTU_INIT, RTU_IDLE, RTU_RX };
enum { RTU_BAD_T15 = 1, RTU_BAD_OVF = 2 };
typedef void (*rtu_frame_cb)(void *ctx, const uint8_t *adu, size_t n, uint32_t t_detect_us);

typedef struct {
    rtu_timing_t tm;
    bool strict_t15;              /* t1.5 초과 문자 간 침묵 → 프레임 폐기 (스펙대로) */
    int state;
    uint32_t last_us;             /* 마지막 문자의 끝 시각 (INIT 에선 init 시각) */
    unsigned why;                 /* 이번 프레임의 결함 비트 RTU_BAD_* */
    size_t len;
    uint8_t buf[RTU_MAX_ADU];
    rtu_frame_cb cb;
    void *ctx;
    uint32_t n_frames, n_t15, n_overflow, n_init_drop;
} rtu_rx_t;

void rtu_rx_init(rtu_rx_t *r, uint32_t baud, uint32_t now_us, bool strict_t15,
                 rtu_frame_cb cb, void *ctx) {
    memset(r, 0, sizeof *r);
    rtu_timing(baud, &r->tm);
    r->strict_t15 = strict_t15;
    r->state = RTU_INIT;          /* 켜진 직후 선 상태를 모른다: t3.5 침묵을 봐야 IDLE */
    r->last_us = now_us;
    r->cb = cb;
    r->ctx = ctx;
}

/* 타이머 틱(또는 다음 바이트 직전)에 부른다: 마지막 문자 뒤 t3.5 이상 조용하면 프레임을 닫는다.
 * 콜백의 t_detect_us = last_us + t3.5 (실제 타이머가 만료되는 순간). */
void rtu_rx_poll(rtu_rx_t *r, uint32_t now_us) {
    (void)r; (void)now_us; (void)since;
    /* TODO: RX 이고 마지막 문자 끝에서 t3.5 이상 지났으면 프레임 판정(콜백 또는 폐기 카운트) → IDLE.
     *       INIT 에서 t3.5 가 지나면 IDLE. */
}

/* 수신 ISR 이 문자 하나마다 부른다. t_end_us = 그 문자의 끝 시각. */
void rtu_rx_byte(rtu_rx_t *r, uint8_t b, uint32_t t_end_us) {
    (void)b;
    r->last_us = t_end_us;
    /* TODO: start 시각·침묵 계산 → poll 로 앞 프레임 닫기 → INIT 이면 버림 / IDLE 이면 새 프레임 /
     *       RX 이면 t1.5 검사 → 저장(넘침 표시) → last_us 갱신 */
}

/* ======================= Part B: 슬레이브 ======================= */
#define MB_NREGS 16u
enum { REG_ID = 0, REG_FWVER = 1, REG_TEMP = 2, REG_SETPOINT = 3, REG_MODE = 4 };  /* 5..15 = 범용 RW */
enum { MB_EX_FUNC = 0x01, MB_EX_ADDR = 0x02, MB_EX_VALUE = 0x03 };

typedef struct {
    uint8_t addr;                 /* 1..247 */
    uint16_t reg[MB_NREGS];
    uint32_t n_ok, n_exc, n_crc, n_short, n_other, n_bcast;
} mb_slave_t;

static void slave_init(mb_slave_t *s, uint8_t addr) {
    memset(s, 0, sizeof *s);
    s->addr = addr;
    s->reg[REG_ID] = 0x5A01; s->reg[REG_FWVER] = 0x0102; s->reg[REG_TEMP] = 253;   /* 25.3 °C ×10 */
    s->reg[REG_SETPOINT] = 300; s->reg[REG_MODE] = 1;
    for (unsigned i = 5; i < MB_NREGS; i++) s->reg[i] = (uint16_t)(0x1000u + i);
}

/* B1. buf[0..n) 뒤에 CRC 를 하위 바이트 먼저 붙인다. 넘치면 0, 아니면 새 길이 */
size_t adu_add_crc(uint8_t *buf, size_t n, size_t cap) {
    (void)buf; (void)n; (void)cap;
    return 0; /* TODO: 넘침 검사 → CRC 하위 바이트, 상위 바이트 순서로 붙이기 */
}

/* B2. FC 0x03 Read Holding Registers. pdu = [03][start hi lo][qty hi lo].
 *     성공: out = [03][byte count][값 BE ...], 반환 = PDU 길이. 실패: -예외코드 */
static int pdu_fc03(const mb_slave_t *s, const uint8_t *pdu, size_t pn, uint8_t *out) {
    (void)s; (void)pdu; (void)pn; (void)out;
    return -MB_EX_FUNC; /* TODO: 길이 → 개수 1..125 (03) → 주소 범위 (02) → [03][byte count][BE 값...] */
}

/* B3. FC 0x06 Write Single Register. pdu = [06][reg hi lo][val hi lo]. 성공 응답 = 요청 PDU 에코.
 *     이 mock 의 정책: 0..2 는 읽기 전용 → 02, SETPOINT > 1000 · MODE > 2 → 03 */
static int pdu_fc06(mb_slave_t *s, const uint8_t *pdu, size_t pn, uint8_t *out) {
    (void)s; (void)pdu; (void)pn; (void)out;
    return -MB_EX_FUNC; /* TODO: 길이 → 주소/읽기 전용 (02) → 값 범위 (03) → 쓰기 → 에코 */
}

/* B4. ADU 하나 처리. 반환 = 응답 ADU 길이 (0 = 응답 없음).
 *     순서: 길이 >= 4 → CRC → 주소(내 것 / 0 = broadcast / 남의 것) → FC 디스패치 → 응답(또는 예외) */
size_t mb_slave_handle(mb_slave_t *s, const uint8_t *adu, size_t n, uint8_t *rsp, size_t cap) {
    (void)s; (void)adu; (void)n; (void)rsp; (void)cap; (void)pdu_fc03; (void)pdu_fc06;
    return 0; /* TODO: 길이 → CRC → 주소/broadcast → FC 디스패치 → 응답 또는 예외 + CRC */
}

/* ======================= Part C: 마스터 드라이버 + RS-485 버스 ======================= */
enum { MB_OK = 0, MB_E_TIMEOUT = -1, MB_E_CRC = -2, MB_E_ADDR = -3, MB_E_FC = -4,
       MB_E_LEN = -5, MB_E_ARG = -6 };     /* > 0 이면 슬레이브가 보낸 예외 코드 */
#define MB_RETRIES 3
#define RESP_TIMEOUT_US 100000u            /* 응답 타임아웃 — 설정값(대표값), 스펙이 정하지 않음 */

/* C1. 요청 ADU 8바이트: [addr][fc][a hi lo][b hi lo][crc lo hi]. 넘치면 0 */
size_t mb_build_req(uint8_t addr, uint8_t fc, uint16_t a, uint16_t b, uint8_t *out, size_t cap) {
    (void)addr; (void)fc; (void)a; (void)b; (void)out; (void)cap;
    return 0; /* TODO: [addr][fc][a BE][b BE] + CRC */
}

/* C2. 응답 검증. FC03 이면 값을 regs[0..qty) 에 (qty <= regs_cap). 반환: MB_OK / MB_E_* / 예외 코드(>0) */
int mb_check_resp(const uint8_t *req, const uint8_t *rsp, size_t n, uint16_t *regs, size_t regs_cap) {
    (void)req; (void)rsp; (void)n; (void)regs; (void)regs_cap;
    return MB_E_TIMEOUT; /* TODO: 길이 → CRC → 주소 → 예외? → FC → byte count / 에코 → 값 꺼내기 */
}

/* ---- RS-485 반이중 버스 모델 (주어짐): 마스터 1 + 슬레이브 2. 송신자는 자기 바이트를 못 듣는다 ---- */
#define NSLV 2
#define MASTER (-1)
typedef struct bus bus_t;
typedef struct { bus_t *bus; int idx; } slv_port_t;
struct bus {
    rtu_timing_t tm;
    uint32_t now;                       /* 선의 마지막 활동(마지막 바이트 끝) 시각 */
    mb_slave_t slv[NSLV];
    rtu_rx_t srx[NSLV], mrx;
    slv_port_t port[NSLV];
    uint8_t pend[RTU_MAX_ADU]; size_t pend_len; uint32_t pend_t; int pend_from;
    uint8_t mgot[RTU_MAX_ADU]; size_t mgot_len;
    /* 결함 주입 */
    bool de_bug;                        /* 마스터가 TXE 에서 DE 를 내림 → 마지막 바이트가 선에 안 나감 */
    int corrupt_resp;                   /* 다음 N 개 응답의 비트 하나를 뒤집는다 (잡음) */
    size_t gap_at; uint32_t gap_us;     /* 마스터 요청의 gap_at 번째 바이트 앞에 침묵 (USB-RS485 어댑터) */
    uint32_t turnaround_us;             /* 슬레이브가 프레임 판정 후 답하기까지 */
    bool verbose;
    uint32_t transactions;
};

static void slave_rx_cb(void *ctx, const uint8_t *adu, size_t n, uint32_t t) {
    slv_port_t *p = ctx;
    bus_t *b = p->bus;
    size_t m = mb_slave_handle(&b->slv[p->idx], adu, n, b->pend, sizeof b->pend);
    if (b->verbose)
        printf("  t=%7u us  S%u  frame %zu B detected (t3.5) -> %s\n", (unsigned)t,
               (unsigned)b->slv[p->idx].addr, n, m ? "reply" : "no reply");
    if (m) { b->pend_len = m; b->pend_t = t; b->pend_from = p->idx; }
}
static void master_rx_cb(void *ctx, const uint8_t *adu, size_t n, uint32_t t) {
    bus_t *b = ctx;
    memcpy(b->mgot, adu, n);
    b->mgot_len = n;
    if (b->verbose) printf("  t=%7u us  M   frame %zu B detected (t3.5)\n", (unsigned)t, n);
}

static void bus_init(bus_t *b, uint32_t baud, bool strict_t15) {
    memset(b, 0, sizeof *b);
    rtu_timing(baud, &b->tm);
    for (int k = 0; k < NSLV; k++) {
        slave_init(&b->slv[k], (uint8_t)(k + 1));
        b->port[k] = (slv_port_t){ b, k };
        rtu_rx_init(&b->srx[k], baud, 0, strict_t15, slave_rx_cb, &b->port[k]);
    }
    rtu_rx_init(&b->mrx, baud, 0, strict_t15, master_rx_cb, b);
}

/* p[0..n) 를 t0 부터 back-to-back 송신, 송신자 외 모두에게 전달. 마지막 바이트의 끝 시각 반환 */
static uint32_t line_tx(bus_t *b, const uint8_t *p, size_t n, uint32_t t0, int from) {
    uint32_t t = t0;
    if (b->verbose) {
        printf("  t=%7u us  %s%c ->", (unsigned)t0, from == MASTER ? "M " : "S", from == MASTER ? ' ' : (char)('1' + from));
        for (size_t i = 0; i < n; i++) printf(" %02X", p[i]);
        printf("  (%zu B)\n", n);
    }
    for (size_t i = 0; i < n; i++) {
        if (from == MASTER && i == b->gap_at && b->gap_us) t += b->gap_us;
        t += b->tm.tc_us;
        for (int k = 0; k < NSLV; k++) if (k != from) rtu_rx_byte(&b->srx[k], p[i], t);
        if (from != MASTER) rtu_rx_byte(&b->mrx, p[i], t);
    }
    return t;
}

/* 요청 하나 → (있으면) 응답 하나. 반환 = 마스터가 받은 ADU 길이, 0 = 타임아웃 */
static size_t bus_transact(bus_t *b, const uint8_t *req, size_t n, uint8_t *rsp, size_t cap) {
    b->transactions++;
    uint32_t t0 = b->now + b->tm.t35_us;                  /* 마스터도 t3.5 침묵 후에 보낸다 */
    size_t nsend = (b->de_bug && n) ? n - 1 : n;
    uint32_t tend = line_tx(b, req, nsend, t0, MASTER);
    b->pend_len = 0;
    for (int k = 0; k < NSLV; k++) rtu_rx_poll(&b->srx[k], tend + b->tm.t35_us);
    b->now = tend;                                        /* 선의 마지막 활동 시각 */
    if (b->pend_len == 0) { b->now += RESP_TIMEOUT_US; return 0; }
    if (b->corrupt_resp > 0) { b->corrupt_resp--; b->pend[2] ^= 0x10; }
    b->mgot_len = 0;
    tend = line_tx(b, b->pend, b->pend_len, b->pend_t + b->turnaround_us, b->pend_from);
    rtu_rx_poll(&b->mrx, tend + b->tm.t35_us);
    for (int k = 0; k < NSLV; k++) rtu_rx_poll(&b->srx[k], tend + b->tm.t35_us);
    b->now = tend;                                        /* 선의 마지막 활동 시각 */
    if (b->mgot_len == 0 || b->mgot_len > cap) { b->now += RESP_TIMEOUT_US; return 0; }
    memcpy(rsp, b->mgot, b->mgot_len);
    return b->mgot_len;
}

/* C3. 드라이버 공통부: 요청 → 응답 검증. 타임아웃·CRC 오류만 최대 MB_RETRIES 번까지 재시도
 *     (예외·프로토콜 오류는 재시도해도 같으니 바로 반환). addr 0 (broadcast) 은 응답을 기다리지 않는다. */
static int drv_xfer(bus_t *b, uint8_t addr, uint8_t fc, uint16_t a, uint16_t v,
                    uint16_t *regs, size_t regs_cap) {
    (void)b; (void)addr; (void)fc; (void)a; (void)v; (void)regs; (void)regs_cap; (void)bus_transact;
    return MB_E_TIMEOUT; /* TODO: 요청 생성 → 최대 MB_RETRIES 번: bus_transact → 검증, 타임아웃·CRC 만 재시도. broadcast 는 바로 OK */
}
static int drv_read_regs(bus_t *b, uint8_t addr, uint16_t start, uint16_t qty, uint16_t *out, size_t cap) {
    return drv_xfer(b, addr, 0x03, start, qty, out, cap);
}
static int drv_write_reg(bus_t *b, uint8_t addr, uint16_t reg, uint16_t val) {
    return drv_xfer(b, addr, 0x06, reg, val, NULL, 0);
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 기준 CRC: 다른 길로 — 비반사 MSB-first poly 0x8005 + 입력 바이트·출력 비트 뒤집기 */
static uint8_t rev8(uint8_t x) { uint8_t r = 0; for (int i = 0; i < 8; i++) if (x & (1u << i)) r |= (uint8_t)(0x80u >> i); return r; }
static uint16_t ref_crc(const uint8_t *p, size_t n) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(rev8(p[i]) << 8);
        for (int k = 0; k < 8; k++) c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x8005u) : (uint16_t)(c << 1);
    }
    return (uint16_t)(rev8((uint8_t)c) << 8 | rev8((uint8_t)(c >> 8)));
}
static size_t ref_frame(uint8_t *f, const uint8_t *pdu_with_addr, size_t n) {
    memcpy(f, pdu_with_addr, n);
    uint16_t c = ref_crc(f, n);
    f[n] = (uint8_t)c; f[n + 1] = (uint8_t)(c >> 8);
    return n + 2;
}

/* 프레임 수집 콜백 */
typedef struct { int n; size_t len[8]; uint8_t f[8][RTU_MAX_ADU]; uint32_t t[8]; } frames_t;
static void collect(void *ctx, const uint8_t *adu, size_t n, uint32_t t) {
    frames_t *fr = ctx;
    if (fr->n < 8) { memcpy(fr->f[fr->n], adu, n); fr->len[fr->n] = n; fr->t[fr->n] = t; }
    fr->n++;
}
/* 테스트용 송신기: t0 = 첫 문자의 start 시각. gap_at 번째 바이트 앞에 gap_us 침묵. 마지막 끝 시각 반환 */
static uint32_t send_bytes(rtu_rx_t *r, const uint8_t *p, size_t n, uint32_t t0, size_t gap_at, uint32_t gap_us) {
    uint32_t t = t0;
    for (size_t i = 0; i < n; i++) {
        if (i == gap_at) t += gap_us;
        t += r->tm.tc_us;
        rtu_rx_byte(r, p[i], t);
    }
    return t;
}

static const uint8_t REQ[8] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD };
static const uint8_t RSP10[25] = { 0x01, 0x03, 0x14, 0x5A, 0x01, 0x01, 0x02, 0x00, 0xFD, 0x01, 0x2C,
    0x00, 0x01, 0x10, 0x05, 0x10, 0x06, 0x10, 0x07, 0x10, 0x08, 0x10, 0x09, 0xCB, 0x7D };

/* 슬레이브 정책의 기준 모델 (랜덤 시험용): 기대 예외 코드, 0 = 정상 */
static int expect_fc03(uint16_t start, uint16_t qty) {
    if (qty < 1 || qty > 125) return 3;
    return (uint32_t)start + qty > MB_NREGS ? 2 : 0;
}
static int expect_fc06(uint16_t a, uint16_t v) {
    if (a >= MB_NREGS || a <= 2) return 2;
    if ((a == 3 && v > 1000) || (a == 4 && v > 2)) return 3;
    return 0;
}

int main(void) {
    uint32_t seed = 0x13579BDFu;
    uint8_t buf[RTU_MAX_ADU + 8], rsp[RTU_MAX_ADU];

    /* ===== Part A: CRC ===== */
    CHECK(crc16_modbus((const uint8_t *)"123456789", 9) == 0x4B37);
    CHECK(crc16_modbus(REQ, 6) == 0xCDC5);                        /* 와이어: C5 CD */
    { const uint8_t q[6] = { 0x11, 0x03, 0x00, 0x6B, 0x00, 0x03 };
      CHECK(crc16_modbus(q, 6) == 0x8776); }                      /* 와이어: 76 87 */
    CHECK(crc16_modbus(REQ, 0) == 0xFFFF);
    CHECK(crc16_modbus(RSP10, 23) == (uint16_t)(RSP10[23] | RSP10[24] << 8));
    {
        int bad = 0;
        for (int t = 0; t < 2000; t++) {
            size_t n = xs32(&seed) % 257u;
            for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)xs32(&seed);
            if (crc16_modbus(buf, n) != ref_crc(buf, n)) bad++;
        }
        printf("crc16_modbus vs reference (reflect + MSB-first 0x8005): 2000 msgs, mismatches=%d\n", bad);
        CHECK(bad == 0);
    }

    /* ===== Part A: 타이밍 ===== */
    {
        rtu_timing_t t;
        rtu_timing(9600, &t);   CHECK(t.tc_us == 1146 && t.t15_us == 1719 && t.t35_us == 4011);
        rtu_timing(19200, &t);  CHECK(t.tc_us == 573 && t.t15_us == 860 && t.t35_us == 2006);
        rtu_timing(1200, &t);   CHECK(t.tc_us == 9167 && t.t15_us == 13750 && t.t35_us == 32084);
        rtu_timing(38400, &t);  CHECK(t.tc_us == 286 && t.t15_us == 750 && t.t35_us == 1750);
        rtu_timing(115200, &t); CHECK(t.tc_us == 95 && t.t15_us == 750 && t.t35_us == 1750);
    }

    /* ===== Part A: 프레이머 — t3.5 로 자르기 ===== */
    static frames_t fr;
    static rtu_rx_t rx;
    {
        memset(&fr, 0, sizeof fr);
        rtu_rx_init(&rx, 9600, 0, true, collect, &fr);
        uint32_t e1 = send_bytes(&rx, REQ, 8, 5000, 99, 0);              /* init 후 5000 us 조용 → IDLE */
        uint32_t e2 = send_bytes(&rx, REQ, 8, e1 + rx.tm.t35_us, 99, 0); /* 침묵 = 정확히 t3.5 */
        rtu_rx_poll(&rx, e2 + rx.tm.t35_us - 1);
        CHECK(fr.n == 1 && fr.len[0] == 8 && memcmp(fr.f[0], REQ, 8) == 0 && fr.t[0] == e1 + 4011);
        rtu_rx_poll(&rx, e2 + rx.tm.t35_us);
        CHECK(fr.n == 2 && fr.len[1] == 8 && fr.t[1] == e2 + 4011);
        CHECK(e1 == 5000 + 8 * 1146 && rx.n_frames == 2 && rx.n_t15 == 0 && rx.n_init_drop == 0);
    }
    {   /* 타임스탬프 wrap: 2^32 us 를 넘어가도 같은 결과 */
        memset(&fr, 0, sizeof fr);
        rtu_rx_init(&rx, 19200, 0xFFFFE000u, true, collect, &fr);
        uint32_t e1 = send_bytes(&rx, REQ, 8, 0xFFFFE000u + 2100u, 99, 0);
        uint32_t e2 = send_bytes(&rx, REQ, 8, e1 + 2006u, 99, 0);
        rtu_rx_poll(&rx, e2 + 2006u);
        CHECK(fr.n == 2 && fr.len[0] == 8 && fr.len[1] == 8 && e2 < 6000u);
    }

    /* ===== Part B: 프레이밍 결함 ===== */
    {   /* 두 프레임 사이 침묵 t3.5 - 1: 경계가 아님 → t1.5 초과 → 합쳐진 프레임 전체 폐기 */
        memset(&fr, 0, sizeof fr);
        rtu_rx_init(&rx, 9600, 0, true, collect, &fr);
        uint32_t e1 = send_bytes(&rx, REQ, 8, 5000, 99, 0);
        uint32_t e2 = send_bytes(&rx, REQ, 8, e1 + rx.tm.t35_us - 1, 99, 0);
        rtu_rx_poll(&rx, e2 + rx.tm.t35_us);
        CHECK(fr.n == 0 && rx.n_t15 == 1);
    }
    {   /* 프레임 안 침묵 = t1.5 (경계 포함 OK) vs t1.5 + 1 (폐기) */
        memset(&fr, 0, sizeof fr);
        rtu_rx_init(&rx, 9600, 0, true, collect, &fr);
        uint32_t e = send_bytes(&rx, REQ, 8, 5000, 4, 1719);
        rtu_rx_poll(&rx, e + 4011);
        CHECK(fr.n == 1 && fr.len[0] == 8);
        e = send_bytes(&rx, REQ, 8, e + 5000, 4, 1720);
        rtu_rx_poll(&rx, e + 4011);
        CHECK(fr.n == 1 && rx.n_t15 == 1);
        rx.strict_t15 = false;                                         /* 관대한 슬레이브 */
        e = send_bytes(&rx, REQ, 8, e + 5000, 4, 1720);
        rtu_rx_poll(&rx, e + 4011);
        CHECK(fr.n == 2 && fr.len[1] == 8);
    }
    {   /* 침묵 >= t3.5 가 프레임 한가운데 → 두 조각 (4 B + 4 B), 둘 다 슬레이브가 CRC 로 거른다 */
        memset(&fr, 0, sizeof fr);
        rtu_rx_init(&rx, 9600, 0, true, collect, &fr);
        uint32_t e = send_bytes(&rx, REQ, 8, 5000, 4, 4011);
        rtu_rx_poll(&rx, e + 4011);
        static mb_slave_t s;
        slave_init(&s, 1);
        CHECK(fr.n == 2 && fr.len[0] == 4 && fr.len[1] == 4);
        CHECK(mb_slave_handle(&s, fr.f[0], fr.len[0], rsp, sizeof rsp) == 0 &&
              mb_slave_handle(&s, fr.f[1], fr.len[1], rsp, sizeof rsp) == 0 && s.n_crc == 2);
    }
    {   /* 스트림 중간에 켜짐: t3.5 침묵 전 바이트는 버리고, 그다음 프레임부터 받는다 */
        memset(&fr, 0, sizeof fr);
        rtu_rx_init(&rx, 9600, 0, true, collect, &fr);
        uint32_t e = send_bytes(&rx, REQ + 3, 5, 1000, 99, 0);          /* 앞 프레임의 꼬리 5 B */
        e = send_bytes(&rx, REQ, 8, e + 2000, 99, 0);                   /* 침묵 2000 < t3.5: 여전히 INIT */
        e = send_bytes(&rx, REQ, 8, e + 4011, 99, 0);
        rtu_rx_poll(&rx, e + 4011);
        CHECK(rx.n_init_drop == 13 && fr.n == 1 && memcmp(fr.f[0], REQ, 8) == 0);
    }
    {   /* 256 B 초과 → 넘침 폐기, 그다음 프레임은 정상 */
        memset(&fr, 0, sizeof fr);
        rtu_rx_init(&rx, 115200, 0, true, collect, &fr);
        uint8_t big[300];
        for (int i = 0; i < 300; i++) big[i] = (uint8_t)i;
        uint32_t e = send_bytes(&rx, big, 300, 2000, 999, 0);
        e = send_bytes(&rx, REQ, 8, e + 1750, 999, 0);
        rtu_rx_poll(&rx, e + 1750);
        CHECK(rx.n_overflow == 1 && fr.n == 1 && fr.len[0] == 8);
    }

    /* ===== Part B: 슬레이브 ===== */
    static mb_slave_t s;
    slave_init(&s, 1);
    {
        size_t m = mb_slave_handle(&s, REQ, 8, rsp, sizeof rsp);
        CHECK(m == 25 && memcmp(rsp, RSP10, 25) == 0);
        if (m) { printf("01 03 00 00 00 0A C5 CD ->"); for (size_t i = 0; i < m; i++) printf(" %02X", rsp[i]); printf("\n"); }
    }
    {   /* 예외 응답: 바이트 단위로 정확히 */
        static const uint8_t ex02[5] = { 0x01, 0x83, 0x02, 0xC0, 0xF1 };
        static const uint8_t ex03[5] = { 0x01, 0x83, 0x03, 0x01, 0x31 };
        static const uint8_t ex01[5] = { 0x01, 0x90, 0x01, 0x8D, 0xC0 };
        uint8_t q[8];
        mb_build_req(1, 0x03, 15, 2, q, 8);   CHECK(mb_slave_handle(&s, q, 8, rsp, sizeof rsp) == 5 && memcmp(rsp, ex02, 5) == 0);
        mb_build_req(1, 0x03, 0, 126, q, 8);  CHECK(mb_slave_handle(&s, q, 8, rsp, sizeof rsp) == 5 && memcmp(rsp, ex03, 5) == 0);
        mb_build_req(1, 0x03, 0x100, 0, q, 8); CHECK(mb_slave_handle(&s, q, 8, rsp, sizeof rsp) == 5 && memcmp(rsp, ex03, 5) == 0); /* 개수 먼저 */
        mb_build_req(1, 0x10, 0, 1, q, 8);    CHECK(mb_slave_handle(&s, q, 8, rsp, sizeof rsp) == 5 && memcmp(rsp, ex01, 5) == 0);
        mb_build_req(1, 0x03, 15, 1, q, 8);   CHECK(mb_slave_handle(&s, q, 8, rsp, sizeof rsp) == 7 && get_be16(rsp + 3) == 0x100F);
    }
    {   /* FC06: 에코 + 반영, 읽기 전용(02), 범위 밖 값(03), 주소 밖(02) */
        static const uint8_t w[8] = { 0x01, 0x06, 0x00, 0x03, 0x00, 0x64, 0x78, 0x21 };
        CHECK(mb_slave_handle(&s, w, 8, rsp, sizeof rsp) == 8 && memcmp(rsp, w, 8) == 0 && s.reg[REG_SETPOINT] == 100);
        uint8_t q[8];
        mb_build_req(1, 0x06, REG_ID, 7, q, 8);        CHECK(mb_slave_handle(&s, q, 8, rsp, sizeof rsp) == 5 && rsp[1] == 0x86 && rsp[2] == 2 && s.reg[REG_ID] == 0x5A01);
        mb_build_req(1, 0x06, REG_SETPOINT, 1001, q, 8); CHECK(mb_slave_handle(&s, q, 8, rsp, sizeof rsp) == 5 && rsp[2] == 3 && s.reg[REG_SETPOINT] == 100);
        mb_build_req(1, 0x06, 16, 0, q, 8);            CHECK(mb_slave_handle(&s, q, 8, rsp, sizeof rsp) == 5 && rsp[2] == 2);
        mb_build_req(1, 0x06, REG_MODE, 2, q, 8);      CHECK(mb_slave_handle(&s, q, 8, rsp, sizeof rsp) == 8 && s.reg[REG_MODE] == 2);
    }
    {   /* PDU 길이 틀림 → 03, 너무 짧음 → 무응답 */
        uint8_t q[16] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00 };
        size_t n = ref_frame(q, q, 7);
        CHECK(mb_slave_handle(&s, q, n, rsp, sizeof rsp) == 5 && rsp[2] == 3);
        uint32_t sh = s.n_short;
        CHECK(mb_slave_handle(&s, q, 3, rsp, sizeof rsp) == 0 && s.n_short == sh + 1);
    }
    {   /* 1비트 오류 64가지 모두 → 무응답 + n_crc, 마지막 바이트 잘림 → 무응답 */
        uint32_t c0 = s.n_crc;
        int replied = 0;
        for (int bit = 0; bit < 64; bit++) {
            uint8_t q[8]; memcpy(q, REQ, 8);
            q[bit / 8] ^= (uint8_t)(1u << (bit % 8));
            if (mb_slave_handle(&s, q, 8, rsp, sizeof rsp) != 0) replied++;
        }
        CHECK(replied == 0 && s.n_crc == c0 + 64);
        CHECK(mb_slave_handle(&s, REQ, 7, rsp, sizeof rsp) == 0 && s.n_crc == c0 + 65);
    }
    {   /* 남의 주소 → 무응답. broadcast 06 → 실행 + 무응답. broadcast 03 → 실행 안 함 + 무응답 */
        uint8_t q[8];
        mb_build_req(2, 0x03, 0, 1, q, 8);  CHECK(mb_slave_handle(&s, q, 8, rsp, sizeof rsp) == 0 && s.n_other == 1);
        mb_build_req(0, 0x06, 7, 0xBEEF, q, 8); CHECK(mb_slave_handle(&s, q, 8, rsp, sizeof rsp) == 0 && s.reg[7] == 0xBEEF);
        mb_build_req(0, 0x03, 0, 1, q, 8);  CHECK(mb_slave_handle(&s, q, 8, rsp, sizeof rsp) == 0 && s.n_bcast == 2);
        mb_build_req(1, 0x03, 0, 125, q, 8);
        CHECK(mb_slave_handle(&s, q, 8, rsp, 4) == 0);                 /* 응답 버퍼가 작으면 쓰지 않는다 */
    }
    {   /* FC 256가지: 03·06 말고는 전부 예외 01 (FC | 0x80) */
        int bad = 0;
        for (int fc = 0; fc < 256; fc++) {
            if (fc == 3 || fc == 6) continue;
            uint8_t q[8], ex[8] = { 0x01, (uint8_t)(fc | 0x80), 0x01 };
            mb_build_req(1, (uint8_t)fc, 0, 1, q, 8);
            ref_frame(ex, ex, 3);
            if (mb_slave_handle(&s, q, 8, rsp, sizeof rsp) != 5 || memcmp(rsp, ex, 5) != 0) bad++;
        }
        CHECK(bad == 0);
    }
    {   /* 랜덤 3000 요청 (프레이머 경유): 기준 모델과 응답 바이트 비교 */
        static mb_slave_t s2;
        static uint16_t shadow[MB_NREGS];
        slave_init(&s2, 7);
        memcpy(shadow, s2.reg, sizeof shadow);
        memset(&fr, 0, sizeof fr);
        rtu_rx_init(&rx, 19200, 0, true, collect, &fr);
        uint32_t t = 10000;
        int bad = 0;
        for (int k = 0; k < 3000; k++) {
            uint8_t fc = (xs32(&seed) & 1) ? 0x03 : 0x06, q[8], exp[RTU_MAX_ADU];
            uint16_t a = (uint16_t)(xs32(&seed) % 20u), v = (uint16_t)(xs32(&seed) % 1200u);
            if (fc == 0x03) v = (uint16_t)(v % 20u);                 /* qty 0..19 */
            mb_build_req(7, fc, a, v, q, 8);
            fr.n = 0;
            t = send_bytes(&rx, q, 8, t, 99, 0);
            rtu_rx_poll(&rx, t + rx.tm.t35_us);
            t += rx.tm.t35_us + 100;
            size_t m = fr.n == 1 ? mb_slave_handle(&s2, fr.f[0], fr.len[0], rsp, sizeof rsp) : 0;
            int ex = fc == 0x03 ? expect_fc03(a, v) : expect_fc06(a, v);
            size_t en;
            exp[0] = 7;
            if (ex) { exp[1] = (uint8_t)(fc | 0x80); exp[2] = (uint8_t)ex; en = ref_frame(exp, exp, 3); }
            else if (fc == 0x03) {
                exp[1] = 3; exp[2] = (uint8_t)(2 * v);
                for (unsigned i = 0; i < v; i++) put_be16(exp + 3 + 2 * i, shadow[a + i]);
                en = ref_frame(exp, exp, 3u + 2u * v);
            } else { shadow[a] = v; memcpy(exp, q, 8); en = 8; }
            if (m != en || memcmp(rsp, exp, en) != 0) bad++;
        }
        printf("slave via framer: 3000 random requests (ok=%u exc=%u), mismatches=%d\n",
               (unsigned)s2.n_ok, (unsigned)s2.n_exc, bad);
        CHECK(bad == 0 && rx.n_frames == 3000);
    }

    /* ===== Part C: 마스터 응답 검증 ===== */
    {
        uint16_t regs[16];
        CHECK(mb_check_resp(REQ, RSP10, 25, regs, 16) == MB_OK && regs[0] == 0x5A01 && regs[9] == 0x1009);
        CHECK(mb_check_resp(REQ, RSP10, 0, regs, 16) == MB_E_TIMEOUT);
        uint8_t r2[32]; memcpy(r2, RSP10, 25); r2[5] ^= 1;
        CHECK(mb_check_resp(REQ, r2, 25, regs, 16) == MB_E_CRC);
        memcpy(r2, RSP10, 25); r2[0] = 2; ref_frame(r2, r2, 23);
        CHECK(mb_check_resp(REQ, r2, 25, regs, 16) == MB_E_ADDR);       /* 다른 슬레이브가 답함 */
        memcpy(r2, RSP10, 25); r2[1] = 4; ref_frame(r2, r2, 23);
        CHECK(mb_check_resp(REQ, r2, 25, regs, 16) == MB_E_FC);
        memcpy(r2, RSP10, 21); r2[2] = 0x12; ref_frame(r2, r2, 21);
        CHECK(mb_check_resp(REQ, r2, 23, regs, 16) == MB_E_LEN);       /* 9개만 옴 */
        static const uint8_t ex02[5] = { 0x01, 0x83, 0x02, 0xC0, 0xF1 };
        CHECK(mb_check_resp(REQ, ex02, 5, regs, 16) == 2);
        CHECK(mb_check_resp(REQ, RSP10, 25, regs, 4) == MB_E_ARG);
        uint8_t w[8], e[8];
        mb_build_req(1, 0x06, 3, 100, w, 8);
        CHECK(w[6] == 0x78 && w[7] == 0x21 && mb_check_resp(w, w, 8, NULL, 0) == MB_OK);
        memcpy(e, w, 8); e[5] = 0x65; ref_frame(e, e, 6);
        CHECK(mb_check_resp(w, e, 8, NULL, 0) == MB_E_LEN);            /* 에코가 다름 */
        CHECK(mb_build_req(1, 3, 0, 1, w, 7) == 0);
    }

    /* ===== Part C: RS-485 버스 종단 시험 ===== */
    static bus_t bus;
    {
        bus_init(&bus, 9600, true);
        bus.slv[1].reg[REG_TEMP] = 187;
        uint16_t v[10] = { 0 };
        printf("bus log @9600 (tc=%u t1.5=%u t3.5=%u us):\n", (unsigned)bus.tm.tc_us,
               (unsigned)bus.tm.t15_us, (unsigned)bus.tm.t35_us);
        bus.verbose = true;
        int r = drv_read_regs(&bus, 1, 0, 10, v, 10);
        bus.verbose = false;
        CHECK(r == MB_OK && v[0] == 0x5A01 && v[2] == 253 && v[9] == 0x1009 && bus.transactions == 1);
        CHECK(bus.slv[1].n_other == 2);                     /* S2 는 요청과 S1 의 응답을 듣고 무시 */
        CHECK(bus.now == 45840);                            /* 응답 끝 = 4011 + 33·1146 + 4011 (demo_timing 의 45.84 ms 사이클) */
        r = drv_read_regs(&bus, 2, REG_TEMP, 1, v, 10);
        CHECK(r == MB_OK && v[0] == 187);
        CHECK(drv_write_reg(&bus, 2, REG_SETPOINT, 450) == MB_OK && bus.slv[1].reg[REG_SETPOINT] == 450);
        uint32_t tr = bus.transactions;
        CHECK(drv_read_regs(&bus, 1, 20, 2, v, 10) == MB_EX_ADDR && bus.transactions == tr + 1);   /* 예외는 재시도 안 함 */
        tr = bus.transactions;
        CHECK(drv_read_regs(&bus, 5, 0, 1, v, 10) == MB_E_TIMEOUT && bus.transactions == tr + 3); /* 없는 장치 */
        CHECK(drv_write_reg(&bus, 0, 9, 0x0909) == MB_OK && bus.slv[0].reg[9] == 0x0909 && bus.slv[1].reg[9] == 0x0909);
        bus.corrupt_resp = 1; tr = bus.transactions;
        CHECK(drv_read_regs(&bus, 1, 0, 2, v, 10) == MB_OK && bus.transactions == tr + 2);        /* 잡음 → 1번 재시도 */
        bus.de_bug = true; tr = bus.transactions;
        uint32_t c0 = bus.slv[0].n_crc;
        CHECK(drv_read_regs(&bus, 1, 0, 1, v, 10) == MB_E_TIMEOUT && bus.slv[0].n_crc == c0 + 3); /* CRC 상위 바이트 유실 */
        bus.de_bug = false;
    }
    {   /* USB-RS485 어댑터가 요청 중간에 1 ms 쉼 @115200: t1.5 = 750 < 1000 < t3.5 = 1750 */
        uint16_t v[2];
        bus_init(&bus, 115200, true);
        bus.gap_at = 4; bus.gap_us = 1000;
        CHECK(drv_read_regs(&bus, 1, 0, 2, v, 2) == MB_E_TIMEOUT && bus.srx[0].n_t15 == 3);
        bus_init(&bus, 115200, false);
        bus.gap_at = 4; bus.gap_us = 1000;
        CHECK(drv_read_regs(&bus, 1, 0, 2, v, 2) == MB_OK && v[0] == 0x5A01);
        bus.gap_us = 1750;                                    /* t3.5 이상이면 관대해도 두 조각 */
        CHECK(drv_read_regs(&bus, 1, 0, 2, v, 2) == MB_E_TIMEOUT && bus.slv[0].n_crc == 6);
    }
    {   /* 랜덤 400 트랜잭션: 슬레이브 1/2/없는 3, 읽기/쓰기, 10% 응답 잡음 → 그림자 모델과 비교 */
        static uint16_t shadow[NSLV][MB_NREGS];
        bus_init(&bus, 19200, true);
        bus.turnaround_us = 300;
        for (int k = 0; k < NSLV; k++) memcpy(shadow[k], bus.slv[k].reg, sizeof shadow[k]);
        int bad = 0, retried = 0;
        for (int it = 0; it < 400; it++) {
            uint8_t addr = (uint8_t)(1 + xs32(&seed) % 3u);
            uint16_t a = (uint16_t)(xs32(&seed) % 18u), val = (uint16_t)(xs32(&seed) % 1100u), got[20];
            uint32_t tr = bus.transactions;
            if (xs32(&seed) % 10u == 0) { bus.corrupt_resp = 1; }
            int want, r;
            if (xs32(&seed) & 1) {
                uint16_t qty = (uint16_t)(1 + xs32(&seed) % 6u);
                r = drv_read_regs(&bus, addr, a, qty, got, 20);
                want = addr == 3 ? MB_E_TIMEOUT : expect_fc03(a, qty);
                if (r == MB_OK && want == 0)
                    for (unsigned i = 0; i < qty; i++) if (got[i] != shadow[addr - 1][a + i]) r = 99;
            } else {
                r = drv_write_reg(&bus, addr, a, val);
                want = addr == 3 ? MB_E_TIMEOUT : expect_fc06(a, val);
                if (want == 0) shadow[addr - 1][a] = val;
            }
            bus.corrupt_resp = 0;
            if (r != want) bad++;
            if (addr != 3 && bus.transactions - tr > 1) retried++;
        }
        for (int k = 0; k < NSLV; k++) if (memcmp(shadow[k], bus.slv[k].reg, sizeof shadow[k]) != 0) bad++;
        printf("bus: 400 random transactions (%u frames on wire incl. retries, %d recovered by retry), bad=%d\n",
               (unsigned)bus.transactions, retried, bad);
        CHECK(bad == 0 && retried > 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
