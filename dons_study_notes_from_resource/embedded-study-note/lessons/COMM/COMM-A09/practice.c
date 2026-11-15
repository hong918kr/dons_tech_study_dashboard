/* COMM-A09 추가 연습 — 본문 solution.c 의 모델·녹화기·재생기를 그대로 담고 (TODO 채운 판), 아래에 연습 1~4 를 더한다.
 *   연습 1: 블랙박스 링 레코더 (마지막 16개 트랜잭션만 보관, 사고 직전 기록 덤프)
 *   연습 2: 두 로그의 LCS diff 리포트 (ALGO-M08) — "무엇이 바뀌었나"를 한눈에
 *   연습 3: 드라이버 버그 두 개 (짧은 전송 무시, 모든 에러 재시도) 를 변형 재생 스윕이 잡는지
 *   연습 4: 로그 크기 예산 — 1 kHz 루프에서 바이너리/텍스트 로그가 초당 몇 바이트인가
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GIVEN __attribute__((unused))   /* 주어진 도우미 — starter 의 스텁 상태에서는 아직 안 불릴 수 있음 */

/* ================= 0. 시뮬레이션 시계, 텍스트 버퍼, CRC-32 ================= */
static uint32_t g_now_us;                                   /* 모든 버스·지연이 이 시계를 민다 */
static void plat_delay_us(uint32_t us) { g_now_us += us; }

typedef struct { char s[32768]; size_t len; bool overflow; } Text;
static void text_clear(Text *t) { t->len = 0; t->s[0] = '\0'; t->overflow = false; }
__attribute__((format(printf, 2, 3)))
static void text_printf(Text *t, const char *fmt, ...) {
    size_t room = sizeof t->s - t->len;
    va_list ap; va_start(ap, fmt);
    int k = vsnprintf(t->s + t->len, room, fmt, ap);
    va_end(ap);
    if (k < 0) return;
    if ((size_t)k >= room) { t->len = sizeof t->s - 1; t->overflow = true; } else t->len += (size_t)k;
}

/* CRC-32/ISO-HDLC: poly 0x04C11DB7 반사형(0xEDB88320), init/xorout 0xFFFFFFFF. 여러 조각을 이어서 계산 가능 */
static uint32_t crc32_update(uint32_t c, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c;
}

/* ================= 1. bus_ops (COMM-M12 모델 축약판, 반환값만 "바이트 수"로 바꿈) ================= */
enum { BUS_OK = 0, BUS_EINVAL = -1, BUS_ENODEV = -2, BUS_EIO = -3, BUS_ETIMEDOUT = -4, BUS_EAGAIN = -5 };
#define BUS_MAX_XFER 32u
/* 계약: 성공 = n, 짧은 전송 = 0..n-1 (버퍼 뒤쪽은 정의되지 않음), 실패 = BUS_E*. 재시도 안 함.
 *       인자 오류(buf NULL, n 0 또는 > 32)는 BUS_EINVAL 이고 선에 아무것도 안 나간다. */
typedef struct bus_ops {
    const char *name;
    int (*read_regs)(void *ctx, uint8_t reg, uint8_t *buf, size_t n);
    int (*write_regs)(void *ctx, uint8_t reg, const uint8_t *buf, size_t n);
} bus_ops;
typedef struct { const bus_ops *ops; void *ctx; } bus_t;
static bool bus_args_ok(const void *buf, size_t n) { return buf != NULL && n >= 1 && n <= BUS_MAX_XFER; }

/* ================= 2. "보드": 가상 칩 2개 + mock 포트 (주어짐) ================= */
enum { CHIP_IMU = 1, CHIP_TMP = 2 };
enum { R_WHO = 0x0F, R_CTRL1 = 0x10, R_CTRL3 = 0x12, R_STATUS = 0x27, R_OUTX_L = 0x28 };   /* MIMU-6D */
enum { T_TEMP = 0x00, T_CONFIG = 0x02, T_ID = 0x0F };                                    /* MTMP-1  */
#define IMU_ID        0xB5u
#define TMP_ID        0x5Au
#define STATUS_XLDA   0x01u
#define IMU_BOOT_US   800u    /* SW_RESET 뒤 이 시간 동안 칩은 쓰기를 무시한다 (이 mock 의 "데이터시트") */
#define TMP_I2C_ADDR  0x48u
#define IMU_SPI_CS    0u

typedef struct { int kind; uint8_t regs[64]; uint32_t boot_until; unsigned ignored, resets; } Chip;
static void chip_init(Chip *c, int kind) {
    memset(c, 0, sizeof *c);
    c->kind = kind;
    if (kind == CHIP_IMU) c->regs[R_WHO] = IMU_ID; else c->regs[T_ID] = TMP_ID;
}
static bool chip_writable(const Chip *c, uint8_t a) {
    return c->kind == CHIP_IMU ? (a >= R_CTRL1 && a <= R_CTRL3) : a == T_CONFIG;
}
static uint8_t chip_read(Chip *c, uint8_t a) {
    a &= 0x3Fu;
    uint8_t v = c->regs[a];
    if (c->kind == CHIP_IMU && a == R_OUTX_L + 5) c->regs[R_STATUS] &= (uint8_t)~STATUS_XLDA;  /* Z_H 읽기 → XLDA 클리어 */
    return v;
}
static void chip_write(Chip *c, uint8_t a, uint8_t v) {
    a &= 0x3Fu;
    if (g_now_us < c->boot_until || !chip_writable(c, a)) { c->ignored++; return; }
    if (c->kind == CHIP_IMU && a == R_CTRL3) {                 /* bit0 SW_RESET: 기본값 + 부팅 중 */
        if (v & 1u) { chip_init(c, CHIP_IMU); c->boot_until = g_now_us + IMU_BOOT_US; c->resets = 1; }
        return;
    }
    c->regs[a] = v;
}
static void chip_set_imu(Chip *c, int16_t x, int16_t y, int16_t z) {
    int16_t v[3] = { x, y, z };
    for (int k = 0; k < 3; k++) {
        uint16_t u = (uint16_t)v[k];
        c->regs[R_OUTX_L + 2 * k] = (uint8_t)(u & 0xFFu);
        c->regs[R_OUTX_L + 2 * k + 1] = (uint8_t)(u >> 8);
    }
    c->regs[R_STATUS] |= STATUS_XLDA;
}
static void chip_set_tmp(Chip *c, int16_t raw) {
    uint16_t u = (uint16_t)raw;
    c->regs[T_TEMP] = (uint8_t)(u >> 8);                       /* big-endian, 1/128 °C per LSB (mock 정의) */
    c->regs[T_TEMP + 1] = (uint8_t)(u & 0xFFu);
}

static uint32_t i2c_dur_us(size_t bytes) { return (uint32_t)((bytes * 45u + 1u) / 2u); }   /* 9비트 × 2.5 µs @400 kHz */
static uint32_t spi_dur_us(size_t bytes) { return (uint32_t)bytes; }                       /* 8비트 × 125 ns @8 MHz */

typedef struct { Chip *chip; unsigned txns; } I2cPort;   /* chip NULL = 그 주소에 아무도 없음 */
typedef struct { Chip *chip; unsigned frames; } SpiPort;  /* chip NULL = MISO 풀업 → 0xFF */

static int i2c_read_regs(void *ctx, uint8_t reg, uint8_t *buf, size_t n) {    /* S a+W reg Sr a+R d.. P */
    I2cPort *p = (I2cPort *)ctx;
    if (!p || !bus_args_ok(buf, n)) return BUS_EINVAL;
    p->txns++;
    if (!p->chip) { g_now_us += i2c_dur_us(1); return BUS_ENODEV; }
    uint8_t a = (uint8_t)(reg & 0x3Fu);
    for (size_t i = 0; i < n; i++) { buf[i] = chip_read(p->chip, a); a = (uint8_t)((a + 1u) & 0x3Fu); }
    g_now_us += i2c_dur_us(3 + n);
    return (int)n;
}
static int i2c_write_regs(void *ctx, uint8_t reg, const uint8_t *buf, size_t n) {
    I2cPort *p = (I2cPort *)ctx;
    if (!p || !bus_args_ok(buf, n)) return BUS_EINVAL;
    p->txns++;
    if (!p->chip) { g_now_us += i2c_dur_us(1); return BUS_ENODEV; }
    uint8_t a = (uint8_t)(reg & 0x3Fu);
    for (size_t i = 0; i < n; i++) { chip_write(p->chip, a, buf[i]); a = (uint8_t)((a + 1u) & 0x3Fu); }
    g_now_us += i2c_dur_us(2 + n);
    return (int)n;
}
static int spi_read_regs(void *ctx, uint8_t reg, uint8_t *buf, size_t n) {    /* [0x80|0x40|reg d..] */
    SpiPort *p = (SpiPort *)ctx;
    if (!p || !bus_args_ok(buf, n) || reg > 0x3Fu || reg + n > 0x40u) return BUS_EINVAL;
    p->frames++;
    for (size_t i = 0; i < n; i++) buf[i] = p->chip ? chip_read(p->chip, (uint8_t)(reg + i)) : 0xFFu;
    g_now_us += spi_dur_us(1 + n);
    return (int)n;
}
static int spi_write_regs(void *ctx, uint8_t reg, const uint8_t *buf, size_t n) {
    SpiPort *p = (SpiPort *)ctx;
    if (!p || !bus_args_ok(buf, n) || reg > 0x3Fu || reg + n > 0x40u) return BUS_EINVAL;
    p->frames++;
    for (size_t i = 0; i < n; i++) if (p->chip) chip_write(p->chip, (uint8_t)(reg + i), buf[i]);
    g_now_us += spi_dur_us(1 + n);
    return (int)n;
}
static const bus_ops I2C_OPS = { "i2c", i2c_read_regs, i2c_write_regs };
static const bus_ops SPI_OPS = { "spi", spi_read_regs, spi_write_regs };

/* ================= 3. 트랜잭션 레코드와 로그 ================= */
enum { OP_READ = 'R', OP_WRITE = 'W' };
enum { BUSID_I2C = 1, BUSID_SPI = 2 };
#define REC_MAX_DATA BUS_MAX_XFER
#define LOG_MAX_RECS 128u
typedef struct {
    uint16_t seq;        /* 로그 안 순번 0,1,2.. */
    uint32_t t_us;       /* 시작 시각 (녹화 시작 기준) */
    uint16_t dur_us;     /* 걸린 시간 (65535 에서 포화) */
    uint8_t  bus, op, addr, reg, len;   /* addr = I2C 7비트 주소 또는 SPI CS 번호 */
    int8_t   result;     /* n / 짧은 전송 바이트 수 / BUS_E* */
    uint8_t  data[REC_MAX_DATA];        /* WRITE: 보낸 바이트, READ: 받은 바이트 (result 뒤는 0) */
} TxRec;
typedef struct { TxRec r[LOG_MAX_RECS]; size_t n; unsigned dropped; uint32_t t0; } TxLog;

static void log_reset(TxLog *l) { memset(l, 0, sizeof *l); l->t0 = g_now_us; }
static const char *bus_name(uint8_t b) { return b == BUSID_I2C ? "i2c" : b == BUSID_SPI ? "spi" : "?"; }
static bool rec_equal(const TxRec *a, const TxRec *b) {
    return a->seq == b->seq && a->t_us == b->t_us && a->dur_us == b->dur_us && a->bus == b->bus && a->op == b->op
        && a->addr == b->addr && a->reg == b->reg && a->len == b->len && a->result == b->result
        && a->len <= REC_MAX_DATA && memcmp(a->data, b->data, a->len) == 0;
}
static bool log_equal(const TxLog *a, const TxLog *b) {
    if (a->n != b->n) return false;
    for (size_t i = 0; i < a->n; i++) if (!rec_equal(&a->r[i], &b->r[i])) return false;
    return true;
}
/* 정규형 검사: 디코더·파서가 받아들이는 레코드의 정의 */
static bool rec_valid(const TxRec *r, size_t i) {
    if (r->seq != i || (r->op != OP_READ && r->op != OP_WRITE)) return false;
    if (r->bus != BUSID_I2C && r->bus != BUSID_SPI) return false;
    if (r->addr > 0x7Fu || r->len < 1u || r->len > REC_MAX_DATA) return false;
    if (r->result < BUS_EAGAIN || r->result > (int)r->len || r->result == BUS_EINVAL) return false;
    if (r->op == OP_READ) {
        size_t ok = r->result > 0 ? (size_t)r->result : 0u;
        for (size_t k = ok; k < r->len; k++) if (r->data[k]) return false;
    }
    return true;
}

/* ---- Part A: 녹화기. 진짜 버스로 넘기고 결과를 그대로 돌려주며, 그 사이에 레코드 하나를 남긴다 ---- */
typedef struct { bus_t inner; TxLog *log; uint8_t bus, addr; } RecPort;
static int rec_xfer(RecPort *p, uint8_t op, uint8_t reg, uint8_t *rbuf, const uint8_t *wbuf, size_t n) {
    uint32_t t = g_now_us;
    int rc = op == OP_READ ? p->inner.ops->read_regs(p->inner.ctx, reg, rbuf, n)
                           : p->inner.ops->write_regs(p->inner.ctx, reg, wbuf, n);
    if (!bus_args_ok(op == OP_READ ? (const void *)rbuf : (const void *)wbuf, n) || rc == BUS_EINVAL)
        return rc;                                   /* 인자 오류는 선에 안 나감 → 기록하지 않음 */
    TxLog *l = p->log;
    if (l->n >= LOG_MAX_RECS) { l->dropped++; return rc; }   /* 가득 참: 버스 동작은 그대로, 버린 수만 */
    TxRec *r = &l->r[l->n];
    memset(r, 0, sizeof *r);
    uint32_t d = g_now_us - t;
    r->seq = (uint16_t)l->n;
    r->t_us = t - l->t0;
    r->dur_us = (uint16_t)(d > 0xFFFFu ? 0xFFFFu : d);
    r->bus = p->bus; r->op = op; r->addr = p->addr; r->reg = reg; r->len = (uint8_t)n;
    r->result = (int8_t)(rc > (int)n ? (int)n : rc);
    if (op == OP_WRITE) memcpy(r->data, wbuf, n);
    else if (r->result > 0) memcpy(r->data, rbuf, (size_t)r->result);
    l->n++;
    return rc;
}
GIVEN static int rec_read_regs(void *ctx, uint8_t reg, uint8_t *buf, size_t n) { return rec_xfer((RecPort *)ctx, OP_READ, reg, buf, NULL, n); }
GIVEN static int rec_write_regs(void *ctx, uint8_t reg, const uint8_t *buf, size_t n) { return rec_xfer((RecPort *)ctx, OP_WRITE, reg, NULL, buf, n); }
static const bus_ops REC_OPS = { "rec", rec_read_regs, rec_write_regs };

/* ================= 4. 바이너리 형식 BTXL v1 ================= */
enum { LOG_OK = 0, LOG_ENOSPC = -201, LOG_EMAGIC = -202, LOG_EVERSION = -203, LOG_ETRUNC = -204,
       LOG_ECRC = -205, LOG_EFORMAT = -206, LOG_ESYNTAX = -207 };
#define BTXL_VERSION   1u
#define BTXL_HDR       16u
#define BTXL_REC_FIXED 14u
#define BTXL_MAX_BYTES (BTXL_HDR + LOG_MAX_RECS * (BTXL_REC_FIXED + REC_MAX_DATA))
GIVEN static const char *log_strerror(int rc) {
    switch (rc) {
    case LOG_OK: return "LOG_OK"; case LOG_ENOSPC: return "LOG_ENOSPC"; case LOG_EMAGIC: return "LOG_EMAGIC";
    case LOG_EVERSION: return "LOG_EVERSION"; case LOG_ETRUNC: return "LOG_ETRUNC"; case LOG_ECRC: return "LOG_ECRC";
    case LOG_EFORMAT: return "LOG_EFORMAT"; case LOG_ESYNTAX: return "LOG_ESYNTAX"; default: return "LOG_?";
    }
}
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFFu); put16(p + 2, v >> 16); }
static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | get16(p + 2) << 16; }
/* 헤더: 0 "BTXL" | 4 version | 5 hdr_len | 6 count u16 | 8 payload_len u32 | 12 crc32 u32  (모두 little-endian)
 * CRC 범위 = 바이트 [0,12) ++ [16, hdr_len + payload_len)  — CRC 필드 자신만 빠진다 */
static uint32_t btxl_crc(const uint8_t *file, size_t hdr_len, size_t payload_len) {
    uint32_t c = crc32_update(0xFFFFFFFFu, file, 12);
    c = crc32_update(c, file + 16, hdr_len - 16u + payload_len);
    return ~c;
}
/* 레코드: 0 seq u16 | 2 t_us u32 | 6 dur_us u16 | 8 bus | 9 op | 10 addr | 11 reg | 12 len | 13 result(i8) | 14 data[len] */
GIVEN static size_t rec_put(const TxRec *r, uint8_t *p) {
    put16(p, r->seq); put32(p + 2, r->t_us); put16(p + 6, r->dur_us);
    p[8] = r->bus; p[9] = r->op; p[10] = r->addr; p[11] = r->reg; p[12] = r->len; p[13] = (uint8_t)r->result;
    memcpy(p + BTXL_REC_FIXED, r->data, r->len);
    return BTXL_REC_FIXED + r->len;
}
GIVEN static void rec_get_fixed(TxRec *r, const uint8_t *p) {
    r->seq = (uint16_t)get16(p); r->t_us = get32(p + 2); r->dur_us = (uint16_t)get16(p + 6);
    r->bus = p[8]; r->op = p[9]; r->addr = p[10]; r->reg = p[11]; r->len = p[12];
    int v = p[13]; if (v >= 128) v -= 256;
    r->result = (int8_t)v;
}

/* ---- Part A: 인코더. 용량이 모자라면 아무것도 안 쓰고 LOG_ENOSPC ---- */
static int bin_encode(const TxLog *l, uint8_t *out, size_t cap, size_t *out_len) {
    size_t need = BTXL_HDR;
    for (size_t i = 0; i < l->n; i++) need += BTXL_REC_FIXED + l->r[i].len;
    if (out_len) *out_len = 0;
    if (!out || need > cap || l->n > LOG_MAX_RECS) return LOG_ENOSPC;
    memcpy(out, "BTXL", 4);
    out[4] = BTXL_VERSION; out[5] = BTXL_HDR;
    put16(out + 6, (uint32_t)l->n);
    put32(out + 8, (uint32_t)(need - BTXL_HDR));
    size_t off = BTXL_HDR;
    for (size_t i = 0; i < l->n; i++) off += rec_put(&l->r[i], out + off);
    put32(out + 12, btxl_crc(out, BTXL_HDR, need - BTXL_HDR));
    if (out_len) *out_len = need;
    return LOG_OK;
}

/* ---- Part A: 디코더. 모든 길이를 len 안에서 검사, CRC 먼저, 그다음 구조. 실패하면 l->n == 0 ---- */
static int bin_decode(const uint8_t *in, size_t len, TxLog *l) {
    l->n = 0; l->dropped = 0;
    if (!in) return LOG_EFORMAT;
    if (len < BTXL_HDR) return LOG_ETRUNC;
    if (memcmp(in, "BTXL", 4) != 0) return LOG_EMAGIC;
    if (in[4] != BTXL_VERSION) return LOG_EVERSION;
    size_t hdr = in[5], count = get16(in + 6), pay = get32(in + 8);
    if (hdr < BTXL_HDR) return LOG_EFORMAT;
    if (hdr > len || pay > len - hdr) return LOG_ETRUNC;
    if (hdr + pay != len) return LOG_EFORMAT;                  /* 뒤에 붙은 쓰레기 */
    if (btxl_crc(in, hdr, pay) != get32(in + 12)) return LOG_ECRC;
    if (count > LOG_MAX_RECS) return LOG_ENOSPC;
    size_t off = hdr;
    for (size_t i = 0; i < count; i++) {
        if (len - off < BTXL_REC_FIXED) return LOG_EFORMAT;
        TxRec *r = &l->r[i];
        memset(r, 0, sizeof *r);
        rec_get_fixed(r, in + off);
        if (r->len > REC_MAX_DATA || len - off - BTXL_REC_FIXED < r->len) return LOG_EFORMAT;
        memcpy(r->data, in + off + BTXL_REC_FIXED, r->len);
        if (!rec_valid(r, i)) return LOG_EFORMAT;
        off += BTXL_REC_FIXED + r->len;
    }
    if (off != len) return LOG_EFORMAT;
    l->n = count;
    return LOG_OK;
}

/* ================= 5. 텍스트 형식 (사람이 읽고 코드 리뷰에서 diff 하는 골든 파일) ================= */
/* # btxl v1 n=<개수>
 * <seq> t=<µs> d=<µs> <i2c|spi>:<addr hex2> <R|W> <reg hex2> n=<len> rc=<result> : <hex>×len
 * # crc32=0x<8 hex>      ← 선택. 있으면 같은 레코드를 BTXL 로 인코딩한 헤더 CRC 와 같아야 한다 */
static void txt_format(const TxLog *l, Text *t) {
    text_printf(t, "# btxl v%u n=%u\n", BTXL_VERSION, (unsigned)l->n);
    for (size_t i = 0; i < l->n; i++) {
        const TxRec *r = &l->r[i];
        text_printf(t, "%04u t=%u d=%u %s:%02x %c %02x n=%u rc=%d :", (unsigned)r->seq, (unsigned)r->t_us,
                    (unsigned)r->dur_us, bus_name(r->bus), r->addr, r->op, r->reg, r->len, r->result);
        for (size_t k = 0; k < r->len; k++) text_printf(t, " %02x", r->data[k]);
        text_printf(t, "\n");
    }
    static uint8_t bin[BTXL_MAX_BYTES];
    size_t blen = 0;
    if (bin_encode(l, bin, sizeof bin, &blen) == LOG_OK) text_printf(t, "# crc32=0x%08x\n", (unsigned)get32(bin + 12));
}
/* 파서 도우미: 모두 *p 를 성공했을 때만 전진시킨다 */
static bool lit(const char **p, const char *s) {
    size_t k = strlen(s);
    if (strncmp(*p, s, k) != 0) return false;
    *p += k; return true;
}
static int digit_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 99;
}
static bool num(const char **p, unsigned base, uint32_t max, uint32_t *out) {   /* 1자리 이상, 넘침 검사 */
    const char *s = *p; uint64_t v = 0; size_t nd = 0;
    for (int d; (d = digit_val(*s)) < (int)base; s++, nd++) { v = v * base + (unsigned)d; if (v > max) return false; }
    if (nd == 0) return false;
    *out = (uint32_t)v; *p = s; return true;
}
GIVEN static bool snum(const char **p, int32_t min, int32_t max, int32_t *out) {      /* 선택적 '-' + 10진 */
    const char *s = *p; bool neg = lit(&s, "-"); uint32_t u;
    if (!num(&s, 10, 0x7FFFFFFFu, &u)) return false;
    int64_t v = neg ? -(int64_t)u : (int64_t)u;
    if (v < min || v > max) return false;
    *out = (int32_t)v; *p = s; return true;
}
GIVEN static bool sp(const char **p) { if (**p != ' ') return false; while (**p == ' ') (*p)++; return true; }
GIVEN static bool eol(const char **p) {                                                /* \n, \r\n, 또는 문자열 끝 */
    if (**p == '\0') return true;
    if ((*p)[0] == '\r' && (*p)[1] == '\n') { *p += 2; return true; }
    if (**p == '\n') { (*p)++; return true; }
    return false;
}

/* ---- Part A: 텍스트 파서. 에러면 *err_line = 줄 번호(1부터), l->n == 0 ---- */
static int txt_parse(const char *s, TxLog *l, int *err_line) {
    int line = 1, rc = LOG_OK;
    uint32_t v = 0, crc = 0;
    int32_t sv = 0;
    bool have_crc = false;
    const char *p = s;
    l->n = 0; l->dropped = 0;
    if (err_line) *err_line = 0;
#define BAD(code) do { rc = (code); goto fail; } while (0)
    if (!p || !lit(&p, "# btxl v")) BAD(LOG_EMAGIC);
    if (!num(&p, 10, 255, &v)) BAD(LOG_ESYNTAX);
    if (v != BTXL_VERSION) BAD(LOG_EVERSION);
    if (!lit(&p, " n=") || !num(&p, 10, 65535, &v) || !eol(&p)) BAD(LOG_ESYNTAX);
    size_t declared = v;
    while (*p) {
        line++;
        if (lit(&p, "# crc32=0x")) {
            if (have_crc || !num(&p, 16, 0xFFFFFFFFu, &crc) || !eol(&p)) BAD(LOG_ESYNTAX);
            have_crc = true; continue;
        }
        if (*p == '#') { while (*p && *p != '\n') p++; if (*p) p++; continue; }    /* 주석 */
        if (eol(&p)) continue;                                                      /* 빈 줄 */
        if (have_crc) BAD(LOG_EFORMAT);                                             /* crc 줄 뒤에 레코드 */
        if (l->n >= LOG_MAX_RECS) BAD(LOG_ENOSPC);
        TxRec *r = &l->r[l->n];
        memset(r, 0, sizeof *r);
        if (!num(&p, 10, 65535, &v)) BAD(LOG_ESYNTAX);
        r->seq = (uint16_t)v;
        if (!sp(&p) || !lit(&p, "t=") || !num(&p, 10, 0xFFFFFFFFu, &v)) BAD(LOG_ESYNTAX);
        r->t_us = v;
        if (!sp(&p) || !lit(&p, "d=") || !num(&p, 10, 65535, &v)) BAD(LOG_ESYNTAX);
        r->dur_us = (uint16_t)v;
        if (!sp(&p)) BAD(LOG_ESYNTAX);
        if (lit(&p, "i2c:")) r->bus = BUSID_I2C; else if (lit(&p, "spi:")) r->bus = BUSID_SPI; else BAD(LOG_ESYNTAX);
        if (!num(&p, 16, 0x7F, &v)) BAD(LOG_ESYNTAX);
        r->addr = (uint8_t)v;
        if (!sp(&p) || (*p != 'R' && *p != 'W')) BAD(LOG_ESYNTAX);
        r->op = (uint8_t)*p++;
        if (!sp(&p) || !num(&p, 16, 0xFF, &v)) BAD(LOG_ESYNTAX);
        r->reg = (uint8_t)v;
        if (!sp(&p) || !lit(&p, "n=") || !num(&p, 10, REC_MAX_DATA, &v)) BAD(LOG_ESYNTAX);
        r->len = (uint8_t)v;
        if (!sp(&p) || !lit(&p, "rc=") || !snum(&p, -128, 127, &sv)) BAD(LOG_ESYNTAX);
        r->result = (int8_t)sv;
        if (!sp(&p) || !lit(&p, ":")) BAD(LOG_ESYNTAX);
        for (size_t k = 0; k < r->len; k++) {
            if (!sp(&p) || !num(&p, 16, 0xFF, &v)) BAD(LOG_ESYNTAX);
            r->data[k] = (uint8_t)v;
        }
        if (!eol(&p)) BAD(LOG_ESYNTAX);
        if (!rec_valid(r, l->n)) BAD(LOG_EFORMAT);
        l->n++;
    }
    if (l->n != declared) BAD(LOG_EFORMAT);
    if (have_crc) {
        static uint8_t bin[BTXL_MAX_BYTES];
        size_t blen = 0;
        if (bin_encode(l, bin, sizeof bin, &blen) != LOG_OK || get32(bin + 12) != crc) BAD(LOG_ECRC);
    }
    return LOG_OK;
fail:
    if (err_line) *err_line = line;
    l->n = 0;
    return rc;
#undef BAD
}

/* ================= 6. 재생기 (replay) ================= */
enum { RP_STRICT = 0, RP_RELAXED = 1 };
enum { MUT_NONE = 0, MUT_NODEV, MUT_NACK, MUT_TIMEOUT, MUT_ARLO, MUT_SHORT, MUT_FLIP, MUT_SLOW, MUT_KINDS };
#define RP_TIMEOUT_US 25000u   /* 변형 TIMEOUT: 버스가 이만큼 붙잡혔다가 실패 (SMBus t_TIMEOUT 25 ms 를 대표값으로) */
#define RP_SLOW_US    5000u    /* 변형 SLOW: 성공하지만 이만큼 늦게 */
#define RP_GAP_MIN_US 500u     /* 타이밍 검사: 녹화된 간격이 이 이상일 때만 본다 */
#define RP_GAP_PCT    90u      /*            실제 간격 < 녹화 간격의 90 % 면 "너무 빨리" */
#define RP_MAX_KEYS   8u
typedef struct { int kind; int seq; uint8_t byte, bit; bool fired; } Mutation;
typedef struct { uint8_t bus, addr; bool seen; uint32_t end_actual, end_rec; } RpKey;
typedef struct {
    const TxLog *log;
    int  mode;
    bool timing;
    bool used[LOG_MAX_RECS];
    size_t next;                       /* strict: 다음에 맞춰야 할 레코드 */
    Mutation mut;
    unsigned issued, served, mismatches, timing_errs;
    bool derailed;                     /* 한 번 어긋나면 이후 op 는 모두 실패 (첫 diff 만 보고) */
    RpKey keys[RP_MAX_KEYS]; size_t nkeys;
    char diff[200];                    /* 첫 문제의 사람이 읽는 설명 */
} Replayer;
typedef struct { Replayer *rp; uint8_t bus, addr; } RpPort;

static void rp_init(Replayer *rp, const TxLog *log, int mode, bool timing) {
    memset(rp, 0, sizeof *rp);
    rp->log = log; rp->mode = mode; rp->timing = timing; rp->mut.seq = -1;
}
__attribute__((format(printf, 2, 3)))
GIVEN static void rp_note(Replayer *rp, const char *fmt, ...) {
    if (rp->diff[0]) return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(rp->diff, sizeof rp->diff, fmt, ap);
    va_end(ap);
}
/* "spi:00 READ reg 0x27 len 7" / "spi:00 WRITE reg 0x10 len 1 [44]" */
GIVEN static void op_desc(char *b, size_t cap, uint8_t bus, uint8_t addr, uint8_t op, uint8_t reg, size_t n, const uint8_t *w) {
    int k = snprintf(b, cap, "%s:%02x %s reg 0x%02x len %u", bus_name(bus), addr, op == OP_READ ? "READ" : "WRITE", reg, (unsigned)n);
    if (k < 0 || (size_t)k >= cap || op != OP_WRITE || !w) return;
    size_t o = (size_t)k;
    for (size_t i = 0; i < n && o + 4 < cap; i++) o += (size_t)snprintf(b + o, cap - o, "%s%02x", i ? " " : " [", w[i]);
    if (o + 2 <= cap) snprintf(b + o, cap - o, "]");
}
GIVEN static RpKey *rp_key(Replayer *rp, uint8_t bus, uint8_t addr) {
    for (size_t i = 0; i < rp->nkeys; i++) if (rp->keys[i].bus == bus && rp->keys[i].addr == addr) return &rp->keys[i];
    if (rp->nkeys >= RP_MAX_KEYS) return NULL;
    RpKey *k = &rp->keys[rp->nkeys++];
    memset(k, 0, sizeof *k); k->bus = bus; k->addr = addr;
    return k;
}

/* ---- Part B: 이번 op 에 대응하는 레코드 찾기 ----
 * strict : 로그의 다음 레코드 하나와 같아야 한다.
 * relaxed: 같은 장치 (bus, addr) 의 아직 안 쓴 가장 이른 레코드와 같아야 한다 (장치 사이 순서는 자유, 장치 안 순서는 고정).
 * "같다" = bus, addr, op, reg, len 일치 + WRITE 면 데이터까지. 아니면 diff 를 남기고 -1, 이후로는 탈선 상태. */
GIVEN static int rp_lookup(Replayer *rp, const RpPort *pt, uint8_t op, uint8_t reg, const uint8_t *wbuf, size_t n) {
    char got[112], exp[112];
    if (rp->derailed) { rp->mismatches++; return -1; }
    op_desc(got, sizeof got, pt->bus, pt->addr, op, reg, n, wbuf);
    size_t idx = rp->log->n;
    if (rp->mode == RP_STRICT) {
        if (rp->next < rp->log->n) idx = rp->next;
    } else {
        for (size_t i = 0; i < rp->log->n; i++)
            if (!rp->used[i] && rp->log->r[i].bus == pt->bus && rp->log->r[i].addr == pt->addr) { idx = i; break; }
    }
    if (idx >= rp->log->n) {
        rp->mismatches++; rp->derailed = true;
        rp_note(rp, "op %u: unexpected %s (no more records%s)", rp->issued, got, rp->mode == RP_RELAXED ? " for this device" : "");
        return -1;
    }
    const TxRec *r = &rp->log->r[idx];
    bool same = r->bus == pt->bus && r->addr == pt->addr && r->op == op && r->reg == reg && r->len == n
             && (op == OP_READ || memcmp(r->data, wbuf, n) == 0);
    if (!same) {
        op_desc(exp, sizeof exp, r->bus, r->addr, r->op, r->reg, r->len, r->op == OP_WRITE ? r->data : NULL);
        rp->mismatches++; rp->derailed = true;
        rp_note(rp, "seq %u: expected %s, got %s", (unsigned)r->seq, exp, got);
        return -1;
    }
    return (int)idx;
}

/* ---- Part B: 재생 bus_ops 의 몸통. 찾고 → 타이밍 확인 → 변형 적용 → 녹화된 응답을 돌려준다 ---- */
static int rp_xfer(RpPort *pt, uint8_t op, uint8_t reg, uint8_t *rbuf, const uint8_t *wbuf, size_t n) {
    Replayer *rp = pt->rp;
    if (!bus_args_ok(op == OP_READ ? (const void *)rbuf : (const void *)wbuf, n)) return BUS_EINVAL;
    rp->issued++;
    int idx = rp_lookup(rp, pt, op, reg, wbuf, n);
    if (idx < 0) return BUS_EIO;
    const TxRec *r = &rp->log->r[idx];
    RpKey *k = rp_key(rp, pt->bus, pt->addr);
    if (rp->timing && k && k->seen) {                 /* 같은 장치의 이전 op 끝 → 이번 op 시작 */
        uint32_t rec_gap = r->t_us - k->end_rec, act_gap = g_now_us - k->end_actual;
        if (rec_gap >= RP_GAP_MIN_US && (uint64_t)act_gap * 100u < (uint64_t)rec_gap * RP_GAP_PCT) {
            char got[112];
            op_desc(got, sizeof got, pt->bus, pt->addr, op, reg, n, wbuf);
            rp->timing_errs++;
            rp_note(rp, "seq %u: %s issued too early: gap %u us < %u%% of recorded %u us",
                    (unsigned)r->seq, got, (unsigned)act_gap, RP_GAP_PCT, (unsigned)rec_gap);
        }
    }
    Mutation *m = &rp->mut;
    int kind = (!m->fired && m->seq == (int)r->seq) ? m->kind : MUT_NONE;
    if (kind != MUT_NONE) m->fired = true;
    if (kind == MUT_ARLO) return BUS_EAGAIN;         /* 이번 시도는 버스를 못 얻음: 레코드는 그대로 남긴다 */
    rp->used[idx] = true;
    if (rp->mode == RP_STRICT) rp->next = (size_t)idx + 1u;
    rp->served++;
    g_now_us += r->dur_us + (kind == MUT_TIMEOUT ? RP_TIMEOUT_US : kind == MUT_SLOW ? RP_SLOW_US : 0u);
    if (k) { k->seen = true; k->end_actual = g_now_us; k->end_rec = r->t_us + r->dur_us; }
    if (kind == MUT_NODEV) return BUS_ENODEV;
    if (kind == MUT_NACK) return BUS_EIO;
    if (kind == MUT_TIMEOUT) return BUS_ETIMEDOUT;
    int rc = r->result;
    if (kind == MUT_SHORT && rc > 0) rc--;            /* 마지막 바이트를 못 주고받음 */
    if (op == OP_READ && rc > 0) {
        memcpy(rbuf, r->data, (size_t)rc);
        if (kind == MUT_FLIP && m->byte < (unsigned)rc) rbuf[m->byte] ^= (uint8_t)(1u << (m->bit & 7u));
    }
    return rc;
}
GIVEN static int rp_read_regs(void *ctx, uint8_t reg, uint8_t *buf, size_t n) { return rp_xfer((RpPort *)ctx, OP_READ, reg, buf, NULL, n); }
GIVEN static int rp_write_regs(void *ctx, uint8_t reg, const uint8_t *buf, size_t n) { return rp_xfer((RpPort *)ctx, OP_WRITE, reg, NULL, buf, n); }
static const bus_ops RP_OPS = { "replay", rp_read_regs, rp_write_regs };

/* ---- Part B: 재생 끝. 문제 수 (어긋남 + 타이밍 위반 + 안 쓰인 레코드) 를 돌려준다. 0 = 통과 ---- */
static int rp_finish(Replayer *rp) {
    unsigned missing = 0;
    for (size_t i = 0; i < rp->log->n; i++) {
        if (rp->used[i]) continue;
        if (missing++ == 0) {
            const TxRec *r = &rp->log->r[i];
            char exp[112];
            op_desc(exp, sizeof exp, r->bus, r->addr, r->op, r->reg, r->len, r->op == OP_WRITE ? r->data : NULL);
            rp_note(rp, "seq %u: expected %s, never issued", (unsigned)r->seq, exp);
        }
    }
    return (int)(rp->mismatches + rp->timing_errs + missing);
}

/* ================= 7. 센서 드라이버 (주어짐 — 테스트 대상. bus_t 만 안다) ================= */
enum { SNS_OK = 0, SNS_EINVAL = -101, SNS_ENOTFOUND = -102, SNS_EBADID = -103, SNS_EIO = -104,
       SNS_ETIMEDOUT = -105, SNS_EBUSY = -106, SNS_ENOTREADY = -107 };
#define SNS_MAX_ATTEMPTS   3u      /* BUS_EAGAIN 만, 첫 시도 포함 */
#define IMU_RESET_DELAY_US 1000u   /* mock 데이터시트: 리셋 뒤 800 µs 접근 금지 → 여유 두고 1 ms */
static const char *sns_strerror(int rc) {
    switch (rc) {
    case SNS_OK: return "OK"; case SNS_EINVAL: return "EINVAL"; case SNS_ENOTFOUND: return "ENOTFOUND";
    case SNS_EBADID: return "EBADID"; case SNS_EIO: return "EIO"; case SNS_ETIMEDOUT: return "ETIMEDOUT";
    case SNS_EBUSY: return "EBUSY"; case SNS_ENOTREADY: return "ENOTREADY"; default: return "?";
    }
}
typedef struct { bus_t bus; bool ready; uint8_t fs; unsigned attempts, retries; int last_bus; } sns_t;
static int sns_map(int bus_rc) {
    switch (bus_rc) {
    case BUS_EINVAL: return SNS_EINVAL; case BUS_ENODEV: return SNS_ENOTFOUND; case BUS_ETIMEDOUT: return SNS_ETIMEDOUT;
    case BUS_EAGAIN: return SNS_EBUSY; default: return SNS_EIO;
    }
}
static int g_drv_bug;   /* 연습 3: 0 = 문서대로, 1 = 짧은 전송을 성공으로, 2 = 모든 에러를 재시도 */
static int sns_xfer(sns_t *d, uint8_t reg, uint8_t *r, const uint8_t *w, size_t n) {   /* 재시도는 여기 한 곳 */
    int rc = BUS_EIO;
    for (unsigned a = 0; a < SNS_MAX_ATTEMPTS; a++) {
        if (a) d->retries++;
        d->attempts++;
        rc = w ? d->bus.ops->write_regs(d->bus.ctx, reg, w, n) : d->bus.ops->read_regs(d->bus.ctx, reg, r, n);
        if (rc != BUS_EAGAIN && !(g_drv_bug == 2 && rc < 0)) break;
    }
    d->last_bus = rc;
    if (rc < 0) return sns_map(rc);
    return ((size_t)rc == n || g_drv_bug == 1) ? SNS_OK : SNS_EIO;
}
static int imu_init_ex(sns_t *d, bus_t bus, uint8_t fs, uint32_t reset_delay_us) {
    d->bus = bus; d->ready = false; d->fs = fs;
    if (fs > 3u) return SNS_EINVAL;
    uint8_t id = 0, v = 0x01u, back = 0;
    int rc = sns_xfer(d, R_WHO, &id, NULL, 1);
    if (rc) return rc;
    if (id == 0x00u || id == 0xFFu) return SNS_ENOTFOUND;
    if (id != IMU_ID) return SNS_EBADID;
    if ((rc = sns_xfer(d, R_CTRL3, NULL, &v, 1))) return rc;     /* SW_RESET */
    plat_delay_us(reset_delay_us);
    v = (uint8_t)(4u << 4 | (unsigned)fs << 2);                 /* ODR 4, FS */
    if ((rc = sns_xfer(d, R_CTRL1, NULL, &v, 1))) return rc;
    if ((rc = sns_xfer(d, R_CTRL1, &back, NULL, 1))) return rc;
    if (back != v) return SNS_EIO;                               /* 설정이 안 먹음 */
    d->ready = true;
    return SNS_OK;
}
static int imu_mg_from(const sns_t *d, const uint8_t *b, int32_t out[3]) {
    for (int k = 0; k < 3; k++) {
        int32_t raw = (int32_t)((uint32_t)b[2 * k] | (uint32_t)b[2 * k + 1] << 8);
        if (raw & 0x8000) raw -= 65536;
        out[k] = raw * (int32_t)(2000 << d->fs) / 32768;          /* 0 쪽으로 버림 */
    }
    return SNS_OK;
}
static int imu_read_mg(sns_t *d, int32_t mg[3]) {                /* STATUS + XYZ 7바이트 burst 한 번 */
    if (!d->ready) return SNS_EINVAL;
    uint8_t b[7]; int32_t out[3];
    int rc = sns_xfer(d, R_STATUS, b, NULL, sizeof b);
    if (rc) return rc;
    if (b[0] == 0xFFu) return SNS_ENOTFOUND;                     /* MISO 떠 있음 */
    if (b[0] & 0xFEu) return SNS_EIO;                            /* 예약 비트 = 0 이어야 함 */
    if (!(b[0] & STATUS_XLDA)) return SNS_ENOTREADY;
    imu_mg_from(d, b + 1, out);
    memcpy(mg, out, sizeof out);                                 /* 성공했을 때만 한꺼번에 커밋 */
    return SNS_OK;
}
static int tmp_init(sns_t *d, bus_t bus) {
    d->bus = bus; d->ready = false;
    uint8_t id = 0, cfg = 0x60u;                                 /* 연속 변환 (mock 정의) */
    int rc = sns_xfer(d, T_ID, &id, NULL, 1);
    if (rc) return rc;
    if (id == 0x00u || id == 0xFFu) return SNS_ENOTFOUND;
    if (id != TMP_ID) return SNS_EBADID;
    if ((rc = sns_xfer(d, T_CONFIG, NULL, &cfg, 1))) return rc;
    d->ready = true;
    return SNS_OK;
}
static int tmp_read_mc(sns_t *d, int32_t *mc) {                  /* m°C = raw × 1000 / 128 = raw × 125 / 16 */
    if (!d->ready) return SNS_EINVAL;
    uint8_t b[2];
    int rc = sns_xfer(d, T_TEMP, b, NULL, 2);
    if (rc) return rc;
    int32_t raw = (int32_t)((uint32_t)b[0] << 8 | b[1]);
    if (raw & 0x8000) raw -= 65536;
    *mc = raw * 125 / 16;
    return SNS_OK;
}

/* ---- "리팩터" 변형들 (회귀 데모의 대상, 주어짐) ---- */
static int imu_read_mg_per_axis(sns_t *d, int32_t mg[3]) {      /* burst 를 축별 읽기로 "정리" */
    if (!d->ready) return SNS_EINVAL;
    uint8_t st = 0, b[6]; int32_t out[3];
    int rc = sns_xfer(d, R_STATUS, &st, NULL, 1);
    if (rc) return rc;
    if (st == 0xFFu) return SNS_ENOTFOUND;
    if (st & 0xFEu) return SNS_EIO;
    if (!(st & STATUS_XLDA)) return SNS_ENOTREADY;
    for (int k = 0; k < 3; k++) if ((rc = sns_xfer(d, (uint8_t)(R_OUTX_L + 2 * k), b + 2 * k, NULL, 2))) return rc;
    imu_mg_from(d, b, out);
    memcpy(mg, out, sizeof out);
    return SNS_OK;
}
static int imu_read_mg_shift(sns_t *d, int32_t mg[3]) {         /* "/32768 은 느리니 >>15 로" — 음수 반올림이 바뀐다 */
    if (!d->ready) return SNS_EINVAL;
    uint8_t b[7]; int32_t out[3];
    int rc = sns_xfer(d, R_STATUS, b, NULL, sizeof b);
    if (rc) return rc;
    if (b[0] == 0xFFu) return SNS_ENOTFOUND;
    if (b[0] & 0xFEu) return SNS_EIO;
    if (!(b[0] & STATUS_XLDA)) return SNS_ENOTREADY;
    for (int k = 0; k < 3; k++) {
        int32_t raw = (int32_t)((uint32_t)b[1 + 2 * k] | (uint32_t)b[2 + 2 * k] << 8);
        if (raw & 0x8000) raw -= 65536;
        out[k] = (raw * (int32_t)(2000 << d->fs)) >> 15;          /* 음수 >> 는 구현 정의 (clang/gcc: 산술 시프트 = 내림) */
    }
    memcpy(mg, out, sizeof out);
    return SNS_OK;
}

/* ================= 8. 세션 (앱이 하는 일의 대본, 주어짐) ================= */
#define SESS_MAX_TICKS 16u
#define SESS_CALLS     (2u + 2u * SESS_MAX_TICKS)
#define SESS_PERIOD_US 1000u
typedef struct { unsigned ticks; bool swap_order, per_axis, shift_math, stop_on_error; uint32_t reset_delay_us; } SessOpt;
typedef struct {
    bool done[SESS_CALLS]; int rc[SESS_CALLS]; int32_t v[SESS_CALLS][3];   /* call id: 0 imu_init 1 tmp_init 2+2k imu[k] 3+2k tmp[k] */
    size_t rec_after[SESS_CALLS];                                           /* 그 call 뒤의 녹화 로그 길이 */
    int fail_call, fail_rc;
    unsigned calls_run, partial_commits, retries;
    Text out;                                                               /* call id 순서로 정렬한 결과 */
} SessResult;
typedef void (*TickHook)(unsigned k, void *arg);
static const SessOpt SESS_DEFAULT = { 6, false, false, false, false, IMU_RESET_DELAY_US };

static void run_session(bus_t ib, bus_t tb, const SessOpt *o, TickHook hook, void *arg, const TxLog *watch, SessResult *sr) {
    static sns_t imu, tmp;
    memset(&imu, 0, sizeof imu); memset(&tmp, 0, sizeof tmp);
    memset(sr, 0, sizeof *sr); text_clear(&sr->out);
    sr->fail_call = -1;
    int32_t mg[3] = { 0, 0, 0 }, mc = 0;
    unsigned ticks = o->ticks > SESS_MAX_TICKS ? SESS_MAX_TICKS : o->ticks;
    for (unsigned step = 0; step < 2u + 2u * ticks; step++) {
        unsigned cid = step;
        if (step >= 2u) {
            unsigned k = (step - 2u) / 2u, j = (step - 2u) % 2u;
            if (j == 0u && hook) hook(k, arg);
            if (o->swap_order) j ^= 1u;
            cid = 2u + 2u * k + j;
        }
        int32_t before[3] = { mg[0], mg[1], mg[2] }, mc_before = mc;
        int rc;
        if (cid == 0u) rc = imu_init_ex(&imu, ib, 1, o->reset_delay_us);
        else if (cid == 1u) rc = tmp_init(&tmp, tb);
        else if (cid % 2u == 0u) rc = o->per_axis ? imu_read_mg_per_axis(&imu, mg) : o->shift_math ? imu_read_mg_shift(&imu, mg) : imu_read_mg(&imu, mg);
        else rc = tmp_read_mc(&tmp, &mc);
        sr->done[cid] = true; sr->rc[cid] = rc;
        if (cid >= 2u && cid % 2u == 0u) memcpy(sr->v[cid], mg, sizeof mg); else sr->v[cid][0] = mc;
        if (rc != SNS_OK && (memcmp(before, mg, sizeof mg) != 0 || mc != mc_before)) sr->partial_commits++;
        sr->calls_run++;
        if (watch) sr->rec_after[cid] = watch->n;
        if (step >= 2u && (step - 2u) % 2u == 1u) plat_delay_us(SESS_PERIOD_US);   /* 1 kHz 루프 */
        if (o->stop_on_error && rc != SNS_OK && rc != SNS_ENOTREADY) { sr->fail_call = (int)cid; sr->fail_rc = rc; break; }
    }
    sr->retries = imu.retries + tmp.retries;
    for (unsigned c = 0; c < 2u + 2u * ticks; c++) {
        if (!sr->done[c]) continue;
        if (c < 2u) text_printf(&sr->out, "%s=%s\n", c ? "tmp_init" : "imu_init", sns_strerror(sr->rc[c]));
        else if (c % 2u == 0u) text_printf(&sr->out, "imu[%u]=%s %d %d %d\n", (c - 2u) / 2u, sns_strerror(sr->rc[c]), (int)sr->v[c][0], (int)sr->v[c][1], (int)sr->v[c][2]);
        else text_printf(&sr->out, "tmp[%u]=%s %d\n", (c - 3u) / 2u, sns_strerror(sr->rc[c]), (int)sr->v[c][0]);
    }
}

/* 보드 + 녹화 / 재생 배선 */
typedef struct { Chip imu, tmp; I2cPort i2c; SpiPort spi; uint32_t seed; } Board;
static int16_t s16(uint32_t u) { int32_t v = (int32_t)(u & 0xFFFFu); return (int16_t)(v >= 32768 ? v - 65536 : v); }
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static void board_init(Board *b, uint32_t seed) {
    memset(b, 0, sizeof *b);
    chip_init(&b->imu, CHIP_IMU); chip_init(&b->tmp, CHIP_TMP);
    b->i2c.chip = &b->tmp; b->spi.chip = &b->imu; b->seed = seed;
}
static void board_hook(unsigned k, void *arg) {                  /* 매 틱 새 샘플 (k == 3 은 IMU 새 샘플 없음) */
    Board *b = (Board *)arg;
    if (b->seed) {
        if (xs32(&b->seed) % 5u) chip_set_imu(&b->imu, s16(xs32(&b->seed)), s16(xs32(&b->seed)), s16(xs32(&b->seed)));
        chip_set_tmp(&b->tmp, s16(xs32(&b->seed)));
        return;
    }
    if (k != 3u) chip_set_imu(&b->imu, s16(7919u * k + 35536u), s16(1u - 4099u * k), (k & 1u) ? -1 : 16384);
    chip_set_tmp(&b->tmp, s16(3200u + 37u * k - (k % 3u == 2u ? 4000u : 0u)));
}
static void record_session(Board *b, TxLog *log, const SessOpt *o, SessResult *sr) {
    static RecPort ri, rt;
    log_reset(log);
    ri = (RecPort){ { &SPI_OPS, &b->spi }, log, BUSID_SPI, IMU_SPI_CS };
    rt = (RecPort){ { &I2C_OPS, &b->i2c }, log, BUSID_I2C, TMP_I2C_ADDR };
    bus_t bi = { &REC_OPS, &ri }, bt = { &REC_OPS, &rt };
    run_session(bi, bt, o, board_hook, b, log, sr);
}
GIVEN static void board_session(Board *b, const SessOpt *o, SessResult *sr) {   /* 녹화기 없이 진짜 버스 직결 */
    bus_t bi = { &SPI_OPS, &b->spi }, bt = { &I2C_OPS, &b->i2c };
    run_session(bi, bt, o, board_hook, b, NULL, sr);
}
static void replay_session(Replayer *rp, const SessOpt *o, SessResult *sr) {
    static RpPort pi, pt;
    pi = (RpPort){ rp, BUSID_SPI, IMU_SPI_CS };
    pt = (RpPort){ rp, BUSID_I2C, TMP_I2C_ADDR };
    bus_t bi = { &RP_OPS, &pi }, bt = { &RP_OPS, &pt };
    run_session(bi, bt, o, NULL, NULL, NULL, sr);
}

/* ================= 9. Part C: 실제 로그 가져오기 — Linux i2c 트레이스 이벤트 ================= */
/* include/trace/events/i2c.h 의 TP_printk:
 *   i2c_write : "i2c-%d #%u a=%03x f=%04x l=%u [%*phD]"     (요청 메시지, 데이터 포함)
 *   i2c_read  : "i2c-%d #%u a=%03x f=%04x l=%u"             (읽기 요청, 데이터 없음)
 *   i2c_reply : "i2c-%d #%u a=%03x f=%04x l=%u [%*phD]"     (읽기 결과 데이터)
 *   i2c_result: "i2c-%d n=%u ret=%d"                        (전송 끝, ret = 처리한 메시지 수 또는 -errno)
 * 줄 앞부분은 ftrace 형식 "<task>-<pid> [cpu] <flags> <sec>.<usec>: <event>: ..." */
enum { EV_NONE = 0, EV_WRITE, EV_READ, EV_REPLY, EV_RESULT };
GIVEN static int tr_head(const char **pp, uint64_t *t_us) {          /* 이벤트 이름 뒤로 *pp 를 옮기고 종류를 돌려준다 */
    static const char *const NAMES[] = { "", "i2c_write: ", "i2c_read: ", "i2c_reply: ", "i2c_result: " };
    const char *line = *pp;
    for (const char *s = line; *s && *s != '\n'; s++) {
        if (s == line || s[-1] != ' ') continue;
        for (int e = EV_WRITE; e <= EV_RESULT; e++) {
            size_t k = strlen(NAMES[e]);
            if (strncmp(s, NAMES[e], k) != 0) continue;
            if (s - line < 10 || s[-2] != ':') return EV_NONE;
            const char *b = s - 2;                              /* 시각 토큰 = "<sec>.<6자리>:" */
            while (b > line && b[-1] != ' ') b--;
            const char *q = b;
            uint32_t sec, us;
            if (!num(&q, 10, 0xFFFFFFFFu, &sec) || !lit(&q, ".") || s - 2 - q != 6 || !num(&q, 10, 999999u, &us) || q != s - 2) return EV_NONE;
            *t_us = (uint64_t)sec * 1000000u + us;
            *pp = s + k;
            return e;
        }
    }
    return EV_NONE;
}
GIVEN static int errno_to_bus(int32_t e) {                            /* Documentation/i2c/fault-codes */
    switch (e) { case -6: return BUS_ENODEV; case -11: return BUS_EAGAIN; case -110: return BUS_ETIMEDOUT; default: return BUS_EIO; }
}
typedef struct { bool used, have_data; uint16_t addr, flags, len; uint8_t data[1 + REC_MAX_DATA]; } TrMsg;
#define I2C_M_RD 0x0001u

/* ---- Part C: 트레이스 텍스트 → TxLog. 지원하는 전송 모양:
 *   [write l=1 (reg)] + [read l=n]  → READ  레코드 (repeated START 레지스터 읽기)
 *   [write l>=2 (reg, data..)]      → WRITE 레코드
 * 다른 어댑터 번호·다른 이벤트·주석은 무시. 그 밖의 모양 / 앞뒤가 안 맞는 이벤트 → LOG_EFORMAT, 문법 오류 → LOG_ESYNTAX ---- */
GIVEN static int trace_import(const char *text, unsigned adapter, TxLog *l, int *err_line) {
    static TrMsg m[2];
    char lb[384];
    int line = 0, rc = LOG_OK;
    bool in_xfer = false, have_base = false;
    uint64_t t_base = 0, t_first = 0;
    const char *p = text;
    l->n = 0; l->dropped = 0;
    if (err_line) *err_line = 0;
    memset(m, 0, sizeof m);
#define TBAD(code) do { rc = (code); goto fail; } while (0)
    while (p && *p) {
        line++;
        const char *q = p;
        while (*p && *p != '\n') p++;
        size_t ll = (size_t)(p - q);
        if (*p) p++;
        if (ll >= sizeof lb) TBAD(LOG_ESYNTAX);
        memcpy(lb, q, ll); lb[ll] = '\0';
        const char *s = lb;
        uint64_t t = 0;
        int ev = tr_head(&s, &t);
        if (ev == EV_NONE) continue;
        uint32_t adap, nr, addr, flags, len, v;
        if (!lit(&s, "i2c-") || !num(&s, 10, 0xFFFF, &adap)) TBAD(LOG_ESYNTAX);
        if (adap != adapter) continue;
        if (ev == EV_RESULT) {
            int32_t ret;
            if (!lit(&s, " n=") || !num(&s, 10, 0xFFFF, &nr) || !lit(&s, " ret=") || !snum(&s, -4095, 0xFFFF, &ret) || !eol(&s)) TBAD(LOG_ESYNTAX);
            unsigned used = (unsigned)m[0].used + (unsigned)m[1].used;
            if (!in_xfer || nr != used || !m[0].used) TBAD(LOG_EFORMAT);
            if (l->n >= LOG_MAX_RECS) TBAD(LOG_ENOSPC);
            TxRec *r = &l->r[l->n];
            memset(r, 0, sizeof *r);
            r->seq = (uint16_t)l->n; r->bus = BUSID_I2C; r->addr = (uint8_t)m[0].addr;
            r->t_us = (uint32_t)(t_first - t_base);
            r->dur_us = (uint16_t)(t - t_first > 0xFFFFu ? 0xFFFFu : t - t_first);
            if (nr == 2 && !(m[0].flags & I2C_M_RD) && m[0].len == 1 && (m[1].flags & I2C_M_RD)
                && m[1].addr == m[0].addr && m[1].len >= 1 && m[1].len <= REC_MAX_DATA) {
                r->op = OP_READ; r->reg = m[0].data[0]; r->len = (uint8_t)m[1].len;
                if (ret == 2) {
                    if (!m[1].have_data) TBAD(LOG_EFORMAT);
                    r->result = (int8_t)r->len; memcpy(r->data, m[1].data, r->len);
                } else if (ret < 0) r->result = (int8_t)errno_to_bus(ret);
                else TBAD(LOG_EFORMAT);                         /* 일부 메시지만 처리됨: 이 importer 는 거부 */
            } else if (nr == 1 && !(m[0].flags & I2C_M_RD) && m[0].len >= 2 && m[0].len <= 1u + REC_MAX_DATA) {
                r->op = OP_WRITE; r->reg = m[0].data[0]; r->len = (uint8_t)(m[0].len - 1u);
                memcpy(r->data, m[0].data + 1, r->len);
                if (ret == 1) r->result = (int8_t)r->len;
                else if (ret < 0) r->result = (int8_t)errno_to_bus(ret);
                else TBAD(LOG_EFORMAT);
            } else TBAD(LOG_EFORMAT);                           /* 모르는 모양 (읽기만, 포인터만 쓰기, 3메시지...) */
            l->n++;
            in_xfer = false; memset(m, 0, sizeof m);
            continue;
        }
        if (!lit(&s, " #") || !num(&s, 10, 0xFFFF, &nr) || !lit(&s, " a=") || !num(&s, 16, 0xFFFF, &addr)
            || !lit(&s, " f=") || !num(&s, 16, 0xFFFF, &flags) || !lit(&s, " l=") || !num(&s, 10, 0xFFFF, &len)) TBAD(LOG_ESYNTAX);
        if (nr >= 2u || addr > 0x7Fu || len > 1u + REC_MAX_DATA) TBAD(LOG_EFORMAT);
        TrMsg *mm = &m[nr];
        if (ev == EV_WRITE || ev == EV_READ) {
            if ((ev == EV_READ) != ((flags & I2C_M_RD) != 0) || mm->used) TBAD(LOG_EFORMAT);
            if (!in_xfer) { in_xfer = true; t_first = t; if (!have_base) { t_base = t; have_base = true; } }
            mm->used = true; mm->addr = (uint16_t)addr; mm->flags = (uint16_t)flags; mm->len = (uint16_t)len;
        } else if (!in_xfer || !mm->used || !(mm->flags & I2C_M_RD) || mm->len != len || mm->have_data) TBAD(LOG_EFORMAT);
        if (ev == EV_WRITE || ev == EV_REPLY) {
            if (!lit(&s, " [")) TBAD(LOG_ESYNTAX);
            for (uint32_t k = 0; k < len; k++) {
                if ((k && !lit(&s, "-")) || !num(&s, 16, 0xFF, &v)) TBAD(LOG_ESYNTAX);
                mm->data[k] = (uint8_t)v;
            }
            if (!lit(&s, "]")) TBAD(LOG_ESYNTAX);
            mm->have_data = true;
        }
        if (!eol(&s)) TBAD(LOG_ESYNTAX);
    }
    if (in_xfer) { line++; TBAD(LOG_EFORMAT); }                 /* i2c_result 없이 끝남 = 잘린 캡처 */
    return LOG_OK;
fail:
    if (err_line) *err_line = line;
    l->n = 0;
    return rc;
#undef TBAD
}

/* ================= 10. Part C: CI 골든 파일 판정 ================= */
enum { GOLD_PASS_STRICT = 0, GOLD_PASS_RELAXED = 1, GOLD_FAIL = 2, GOLD_EBADLOG = 3 };
GIVEN static void first_line_diff(const char *exp, const char *got, Text *rep) {   /* 첫 번째로 다른 줄 */
    int line = 1;
    while (*exp || *got) {
        const char *e = exp, *g = got;
        while (*e && *e != '\n') e++;
        while (*g && *g != '\n') g++;
        if (e - exp != g - got || strncmp(exp, got, (size_t)(e - exp)) != 0) {
            text_printf(rep, "output line %d: expected '%.*s', got '%.*s'\n", line, (int)(e - exp), exp, (int)(g - got), got);
            return;
        }
        exp = *e ? e + 1 : e; got = *g ? g + 1 : g; line++;
    }
}
/* ---- Part C: 골든 = (버스 로그 텍스트, 기대 출력 텍스트). 판정:
 * strict+timing 재생이 깨끗하고 출력이 같으면 PASS_STRICT. 버스는 맞는데 출력이 다르면 FAIL (relaxed 로 구제 안 함).
 * strict 가 어긋나면 relaxed+timing 으로 다시: 깨끗 + 출력 같음 → PASS_RELAXED, 아니면 FAIL.
 * 보고서에는 strict 의 첫 diff (가장 정확한 위치) 와 판정 한 줄을 남긴다. ---- */
GIVEN static int golden_check(const char *log_txt, const char *out_txt, const SessOpt *o, Text *rep) {
    static TxLog gl;
    static Replayer rp;
    static SessResult sr;
    int line = 0;
    int rc = txt_parse(log_txt, &gl, &line);
    if (rc != LOG_OK) { text_printf(rep, "golden log unreadable: %s at line %d\n", log_strerror(rc), line); return GOLD_EBADLOG; }
    for (int mode = RP_STRICT; mode <= RP_RELAXED; mode++) {
        rp_init(&rp, &gl, mode, true);
        replay_session(&rp, o, &sr);
        int prob = rp_finish(&rp);
        bool same = strcmp(sr.out.s, out_txt) == 0;
        if (prob == 0 && same) {
            text_printf(rep, mode == RP_STRICT ? "PASS strict\n" : "PASS relaxed (cross-device order changed; re-record golden to make it strict)\n");
            return mode == RP_STRICT ? GOLD_PASS_STRICT : GOLD_PASS_RELAXED;
        }
        if (prob == 0) { first_line_diff(out_txt, sr.out.s, rep); text_printf(rep, "FAIL (bus traffic matches, driver output differs)\n"); return GOLD_FAIL; }
        text_printf(rep, "%s: %s\n", mode == RP_STRICT ? "strict " : "relaxed", rp.diff);
    }
    text_printf(rep, "FAIL\n");
    return GOLD_FAIL;
}


/* ================= 연습 1: 블랙박스 링 레코더 ================= */
/* 현장 펌웨어는 전체 로그를 못 담는다 → 마지막 RING_N 개만 덮어쓰며 보관하고, 사고(워치독, 에러) 때 덤프한다. */
#define RING_N 16u
typedef struct { TxRec r[RING_N]; uint32_t total; } RingLog;            /* total = 지금까지 본 op 수 = 다음 절대 seq */
typedef struct { bus_t inner; RingLog *ring; uint8_t bus, addr; } RingPort;
static int ring_xfer(RingPort *p, uint8_t op, uint8_t reg, uint8_t *rbuf, const uint8_t *wbuf, size_t n) {
    static TxLog one;                                                     /* 레코드 하나짜리 임시 로그로 rec_xfer 재사용 */
    one.n = 0; one.t0 = 0;
    RecPort rp = { p->inner, &one, p->bus, p->addr };
    int rc = rec_xfer(&rp, op, reg, rbuf, wbuf, n);
    if (one.n == 1) {
        TxRec *slot = &p->ring->r[p->ring->total % RING_N];
        *slot = one.r[0];
        slot->seq = (uint16_t)p->ring->total;                             /* 링 안에서는 절대 seq (하위 16비트) */
        p->ring->total++;
    }
    return rc;
}
static int ring_read_regs(void *ctx, uint8_t reg, uint8_t *buf, size_t n) { return ring_xfer((RingPort *)ctx, OP_READ, reg, buf, NULL, n); }
static int ring_write_regs(void *ctx, uint8_t reg, const uint8_t *buf, size_t n) { return ring_xfer((RingPort *)ctx, OP_WRITE, reg, NULL, buf, n); }
static const bus_ops RING_OPS = { "ring", ring_read_regs, ring_write_regs };
/* 오래된 것 → 최신 순으로 TxLog 에 펼친다. seq 는 0.. 으로 다시 매기고 첫 절대 seq 를 돌려준다 */
static uint32_t ring_dump(const RingLog *g, TxLog *out) {
    uint32_t cnt = g->total < RING_N ? g->total : RING_N, first = g->total - cnt;
    memset(out, 0, sizeof *out);
    for (uint32_t i = 0; i < cnt; i++) { out->r[i] = g->r[(first + i) % RING_N]; out->r[i].seq = (uint16_t)i; }
    out->n = cnt;
    return first;
}
/* 98번째 op (절대 seq 97) 에서 한 번 주소 NACK 을 내는 버스 */
typedef struct { bus_t inner; unsigned calls, fail_at; } FlakyPort;
static int flaky_read(void *ctx, uint8_t reg, uint8_t *buf, size_t n) {
    FlakyPort *f = (FlakyPort *)ctx;
    if (f->calls++ == f->fail_at) { g_now_us += i2c_dur_us(1); return BUS_ENODEV; }
    return f->inner.ops->read_regs(f->inner.ctx, reg, buf, n);
}
static int flaky_write(void *ctx, uint8_t reg, const uint8_t *buf, size_t n) {
    FlakyPort *f = (FlakyPort *)ctx;
    f->calls++;
    return f->inner.ops->write_regs(f->inner.ctx, reg, buf, n);
}
static const bus_ops FLAKY_OPS = { "flaky", flaky_read, flaky_write };

/* ================= 연습 2: LCS diff (ALGO-M08) ================= */
static bool op_same(const TxRec *a, const TxRec *b) {                    /* 재생의 "같다" 와 같은 정의 */
    return a->bus == b->bus && a->addr == b->addr && a->op == b->op && a->reg == b->reg && a->len == b->len
        && (a->op == OP_READ || memcmp(a->data, b->data, a->len) == 0);
}
static void log_diff(const TxLog *a, const TxLog *b, Text *t, unsigned *same, unsigned *del, unsigned *ins) {
    static uint16_t L[LOG_MAX_RECS + 1][LOG_MAX_RECS + 1];                /* L[i][j] = LCS(a[i..], b[j..]) — 33 KB, 정적 */
    for (size_t i = a->n + 1; i-- > 0;)
        for (size_t j = b->n + 1; j-- > 0;)
            L[i][j] = (i == a->n || j == b->n) ? 0
                    : op_same(&a->r[i], &b->r[j]) ? (uint16_t)(L[i + 1][j + 1] + 1u)
                    : (L[i + 1][j] >= L[i][j + 1] ? L[i + 1][j] : L[i][j + 1]);
    size_t i = 0, j = 0;
    *same = *del = *ins = 0;
    while (i < a->n || j < b->n) {
        const TxRec *r; char c;
        if (i < a->n && j < b->n && op_same(&a->r[i], &b->r[j])) { r = &a->r[i]; c = ' '; i++; j++; (*same)++; }
        else if (j == b->n || (i < a->n && L[i + 1][j] >= L[i][j + 1])) { r = &a->r[i]; c = '-'; i++; (*del)++; }
        else { r = &b->r[j]; c = '+'; j++; (*ins)++; }
        text_printf(t, "%c %04u %s:%02x %c %02x n=%u\n", c, (unsigned)r->seq, bus_name(r->bus), r->addr, r->op, r->reg, r->len);
    }
}

/* ================= 연습 3: 드라이버 버그를 변형 재생 스윕이 잡는가 ================= */
static int mut_expect(int kind) {
    switch (kind) { case MUT_NODEV: return SNS_ENOTFOUND; case MUT_NACK: return SNS_EIO; case MUT_TIMEOUT: return SNS_ETIMEDOUT;
                    case MUT_SHORT: return SNS_EIO; default: return SNS_OK; }
}
/* 레코드 k × 실패 변형 4종 → 문서화된 결과와 다른 경우 수. first 에는 첫 번째 어긋남 설명 */
static unsigned sweep_deviations(const TxLog *g, const SessResult *gold, unsigned per_kind[MUT_KINDS], char *first, size_t cap) {
    static Replayer rp;
    static SessResult sr;
    unsigned dev = 0;
    first[0] = '\0';
    for (size_t k = 0; k < g->n; k++) {
        int owner = 0;
        while (owner < (int)SESS_CALLS - 1 && gold->rec_after[owner] <= k) owner++;
        for (int kind = MUT_NODEV; kind <= MUT_SHORT; kind++) {
            if (kind == MUT_ARLO) continue;
            SessOpt o = SESS_DEFAULT; o.stop_on_error = true;
            rp_init(&rp, g, RP_STRICT, true);
            rp.mut.kind = kind; rp.mut.seq = (int)k;
            replay_session(&rp, &o, &sr);
            rp_finish(&rp);
            bool ok = sr.fail_call == owner && sr.fail_rc == mut_expect(kind) && rp.issued == k + 1u
                   && rp.mismatches == 0 && sr.partial_commits == 0;
            if (!ok) {
                dev++; per_kind[kind]++;
                if (!first[0]) snprintf(first, cap, "seq %u %s: driver %s, replay says: %s", (unsigned)k,
                                        kind == MUT_NODEV ? "NODEV" : kind == MUT_NACK ? "NACK" : kind == MUT_TIMEOUT ? "TIMEOUT" : "SHORT",
                                        sr.fail_call < 0 ? "reported no error" : sns_strerror(sr.fail_rc), rp.diff[0] ? rp.diff : "(clean)");
            }
        }
    }
    return dev;
}

/* ================= 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    static Board board;
    static TxLog glog, alt, dump;
    static SessResult gold, sr;
    static Text t;

    board_init(&board, 0);
    record_session(&board, &glog, &SESS_DEFAULT, &gold);
    CHECK(glog.n == 18);

    /* ---- 연습 1 ---- */
    {
        static RingLog ring;
        memset(&ring, 0, sizeof ring);
        board_init(&board, 0);
        FlakyPort fp = { { &I2C_OPS, &board.i2c }, 0, 97 };
        RingPort rp = { { &FLAKY_OPS, &fp }, &ring, BUSID_I2C, TMP_I2C_ADDR };
        bus_t bus = { &RING_OPS, &rp };                                   /* 드라이버 입장에선 그냥 bus_t */
        int ok = 0, nodev = 0; uint8_t b = 0;
        for (int i = 0; i < 100; i++) {
            int rc = bus.ops->read_regs(bus.ctx, T_ID, &b, 1);
            if (rc == 1 && b == TMP_ID) ok++; else if (rc == BUS_ENODEV) nodev++;
        }
        uint32_t first = ring_dump(&ring, &dump);
        CHECK(ok == 99 && nodev == 1 && ring.total == 100);
        CHECK(first == 84 && dump.n == RING_N);
        CHECK(dump.r[13].result == BUS_ENODEV && dump.r[12].result == 1 && dump.r[15].result == 1);
        bool mono = true;
        for (size_t i = 1; i < dump.n; i++) mono = mono && dump.r[i].t_us > dump.r[i - 1].t_us;
        CHECK(mono);
        text_clear(&t); txt_format(&dump, &t);
        static TxLog back; int line = 0;
        CHECK(txt_parse(t.s, &back, &line) == LOG_OK && log_equal(&back, &dump));   /* 덤프도 같은 형식으로 재생 가능 */
        static uint8_t bin[BTXL_MAX_BYTES]; size_t blen = 0;
        CHECK(bin_encode(&dump, bin, sizeof bin, &blen) == LOG_OK && bin_decode(bin, blen, &back) == LOG_OK && log_equal(&back, &dump));
        printf("ex1 black box: %u ops seen, dump = abs seq %u..%u, failing op at dump index 13:\n", (unsigned)ring.total,
               (unsigned)first, (unsigned)(first + dump.n - 1u));
        const char *p = t.s;
        for (int ln = 0; *p && ln < 17; ln++) { const char *e = strchr(p, '\n'); if (!e) break; if (ln >= 13) printf("    %.*s\n", (int)(e - p), p); p = e + 1; }
    }

    /* ---- 연습 2 ---- */
    {
        SessOpt o = SESS_DEFAULT; o.per_axis = true;
        board_init(&board, 0);
        record_session(&board, &alt, &o, &sr);
        unsigned same = 0, del = 0, ins = 0;
        text_clear(&t);
        log_diff(&glog, &alt, &t, &same, &del, &ins);
        CHECK(alt.n == 33 && same == 12 && del == 6 && ins == 21);
        printf("ex2 diff golden -> per-axis: %u same, %u removed, %u added; first lines:\n", same, del, ins);
        const char *p = t.s;
        for (int ln = 0; *p && ln < 12; ln++) { const char *e = strchr(p, '\n'); if (!e) break; printf("    %.*s\n", (int)(e - p), p); p = e + 1; }
        /* 순서만 바꾼 리팩터: LCS 는 순서를 보므로 이동한 op 를 -/+ 쌍으로 본다 */
        o = SESS_DEFAULT; o.swap_order = true;
        board_init(&board, 0);
        record_session(&board, &alt, &o, &sr);
        text_clear(&t);
        log_diff(&glog, &alt, &t, &same, &del, &ins);
        CHECK(alt.n == 18 && same == 17 && del == 1 && ins == 1);              /* 읽기 키가 매 틱 같아서 "한 칸 밀림" 으로 보인다 */
        printf("ex2 diff golden -> swapped order: %u same, %u removed, %u added\n", same, del, ins);
        log_diff(&glog, &glog, &t, &same, &del, &ins);
        CHECK(same == 18 && del == 0 && ins == 0);
    }

    /* ---- 연습 3 ---- */
    {
        char first[256];
        unsigned dev[3];
        for (int bug = 0; bug < 3; bug++) {
            unsigned per[MUT_KINDS] = { 0 };
            g_drv_bug = bug;
            dev[bug] = sweep_deviations(&glog, &gold, per, first, sizeof first);
            printf("ex3 driver bug %d (%s): %u/72 deviations (NODEV %u, NACK %u, TIMEOUT %u, SHORT %u)\n", bug,
                   bug == 0 ? "documented driver" : bug == 1 ? "short transfer treated as OK" : "retry every error",
                   dev[bug], per[MUT_NODEV], per[MUT_NACK], per[MUT_TIMEOUT], per[MUT_SHORT]);
            if (first[0]) printf("    first: %s\n", first);
            if (bug == 1) {                                               /* 어느 SHORT 하나가 안 잡혔나 */
                static Replayer rp; static SessResult s2;
                SessOpt o = SESS_DEFAULT; o.stop_on_error = true;
                rp_init(&rp, &glog, RP_STRICT, true); rp.mut.kind = MUT_SHORT; rp.mut.seq = 3;
                replay_session(&rp, &o, &s2);
                printf("    SHORT at seq 3 (CTRL1 read-back): driver %s -> same as documented, by accident\n", sns_strerror(s2.fail_rc));
                CHECK(s2.fail_rc == SNS_EIO && s2.fail_call == 0);
            }
            if (bug == 1) CHECK(per[MUT_SHORT] == 17 && dev[1] == 17);   /* seq 3 (CTRL1 되읽기) 은 되읽기 비교가 우연히 EIO 로 막는다 */
            if (bug == 2) CHECK(per[MUT_SHORT] == 0 && dev[2] == 54);
        }
        g_drv_bug = 0;
        CHECK(dev[0] == 0);
    }

    /* ---- 연습 4 ---- */
    {
        unsigned bin_tick = 0, txt_tick = 0;
        text_clear(&t); txt_format(&glog, &t);
        const char *p = t.s;
        for (size_t i = 0; i < glog.n; i++) {
            if (i >= 6 && i < 8) bin_tick += BTXL_REC_FIXED + glog.r[i].len;   /* 틱 0 의 두 레코드 */
            p = strchr(p, '\n') + 1;                                          /* 첫 줄 = 헤더 */
            const char *e = strchr(p, '\n');
            if (i >= 6 && i < 8) txt_tick += (unsigned)(e - p + 1);
        }
        CHECK(bin_tick == 37);
        unsigned ring_bytes = 4096;
        printf("ex4 per 1 ms tick: binary %u B, text %u B -> %u B/s vs %u B/s at 1 kHz; a 4 KiB RAM ring holds %u ms (binary) / %u ms (text)\n",
               bin_tick, txt_tick, bin_tick * 1000u, txt_tick * 1000u, ring_bytes / bin_tick, ring_bytes / txt_tick);
        CHECK(txt_tick > 2u * bin_tick);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
