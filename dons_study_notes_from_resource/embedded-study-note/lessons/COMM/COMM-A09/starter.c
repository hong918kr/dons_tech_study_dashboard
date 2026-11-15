/* COMM-A09 버스 트랜잭션 녹화·재생으로 드라이버 회귀 테스트
 *
 * 구성 (COMM-M12 bus_ops 모델 축약판 위에 쌓는다):
 *   driver  : imu_* (SPI CS0 의 MIMU-6D), tmp_* (I2C 0x48 의 MTMP-1) — bus_t 만 안다 (주어짐)
 *   bus_ops : read_regs / write_regs. 반환 = n (성공), 0..n-1 (짧은 전송), BUS_E* (< 0)
 *   board   : 가상 칩 2개 + mock I2C/SPI 포트 + 시뮬레이션 시계 g_now_us (주어짐) = "실보드" 역할
 *   recorder: RecPort — 진짜 버스로 넘기고 {seq,t,dur,bus,op,addr,reg,len,result,data} 를 TxLog 에 남김
 *   formats : 바이너리 BTXL v1 (헤더 16 B + 레코드 14 B + data, CRC-32/ISO-HDLC) / 텍스트 (diff 가능)
 *   replay  : RpPort — 로그에서 응답을 주고 드라이버가 같은 op 를 같은 순서로 내는지 검사
 *             (strict / relaxed) + 타이밍 검사 + 변형 주입 (NODEV/NACK/TIMEOUT/ARLO/SHORT/FLIP/SLOW)
 * Part A: rec_xfer, bin_encode, bin_decode, txt_parse
 * Part B: rp_lookup, rp_xfer, rp_finish
 * Part C: trace_import (Linux i2c 트레이스 이벤트 → TxLog), golden_check (CI 골든 파일 판정)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
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
    /* TODO(Part A): 진짜 버스로 넘기고 (반환값 그대로), 선에 나간 op 면 TxRec 하나를 덧붙인다.
     *   t_us = 시작 시각 - log->t0, dur_us = 걸린 시간 (65535 포화), READ 데이터는 result 바이트만, 나머지 0.
     *   인자 오류(BUS_EINVAL)는 기록하지 않는다. 로그가 가득 차면 dropped++ 만 (버스 동작은 그대로). */
    return op == OP_READ ? p->inner.ops->read_regs(p->inner.ctx, reg, rbuf, n)
                         : p->inner.ops->write_regs(p->inner.ctx, reg, wbuf, n);
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
    /* TODO(Part A): 필요한 크기 계산 → 모자라면 LOG_ENOSPC (*out_len = 0). 헤더 16 B + rec_put() 들 + btxl_crc() */
    (void)l; (void)out; (void)cap;
    if (out_len) *out_len = 0;
    return LOG_ENOSPC;
}

/* ---- Part A: 디코더. 모든 길이를 len 안에서 검사, CRC 먼저, 그다음 구조. 실패하면 l->n == 0 ---- */
static int bin_decode(const uint8_t *in, size_t len, TxLog *l) {
    /* TODO(Part A): 길이 → magic → version → hdr_len → payload 길이 → CRC → 개수 → 레코드마다 rec_valid().
     *   어떤 실패든 l->n == 0 으로 돌아간다. 입력 밖을 읽으면 안 된다 (ASan 이 본다). */
    (void)in; (void)len;
    l->n = 0; l->dropped = 0;
    return LOG_EFORMAT;
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
    /* TODO(Part A): 헤더 "# btxl v1 n=N" → 레코드 줄들 (주석·빈 줄·CRLF 허용) → 개수 확인 → 선택적 crc 줄 검증.
     *   도우미 lit/num/snum/sp/eol 를 쓰고, 에러면 *err_line = 줄 번호, l->n = 0. */
    (void)s;
    l->n = 0; l->dropped = 0;
    if (err_line) *err_line = 0;
    return LOG_ESYNTAX;
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
    /* TODO(Part B): strict = rp->next 의 레코드, relaxed = 같은 (bus, addr) 의 안 쓴 가장 이른 레코드.
     *   bus/addr/op/reg/len (+ WRITE 데이터) 비교, 다르면 rp_note("seq %u: expected %s, got %s") + derailed. */
    (void)pt; (void)op; (void)reg; (void)wbuf; (void)n;
    rp->mismatches++;
    return -1;
}

/* ---- Part B: 재생 bus_ops 의 몸통. 찾고 → 타이밍 확인 → 변형 적용 → 녹화된 응답을 돌려준다 ---- */
static int rp_xfer(RpPort *pt, uint8_t op, uint8_t reg, uint8_t *rbuf, const uint8_t *wbuf, size_t n) {
    /* TODO(Part B): 인자 검사 → issued++ → rp_lookup → 타이밍 검사 → 변형 → 소비 + 시계 전진 → 녹화된 응답 */
    (void)pt; (void)op; (void)reg; (void)rbuf; (void)wbuf; (void)n;
    return BUS_EIO;
}
GIVEN static int rp_read_regs(void *ctx, uint8_t reg, uint8_t *buf, size_t n) { return rp_xfer((RpPort *)ctx, OP_READ, reg, buf, NULL, n); }
GIVEN static int rp_write_regs(void *ctx, uint8_t reg, const uint8_t *buf, size_t n) { return rp_xfer((RpPort *)ctx, OP_WRITE, reg, NULL, buf, n); }
static const bus_ops RP_OPS = { "replay", rp_read_regs, rp_write_regs };

/* ---- Part B: 재생 끝. 문제 수 (어긋남 + 타이밍 위반 + 안 쓰인 레코드) 를 돌려준다. 0 = 통과 ---- */
static int rp_finish(Replayer *rp) {
    /* TODO(Part B): 안 쓰인 레코드를 세고 첫 번째를 "seq %u: expected %s, never issued" 로 보고.
     *   반환 = mismatches + timing_errs + 안 쓰인 수 */
    (void)rp;
    return 1;
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
static int sns_xfer(sns_t *d, uint8_t reg, uint8_t *r, const uint8_t *w, size_t n) {   /* 재시도는 여기 한 곳 */
    int rc = BUS_EIO;
    for (unsigned a = 0; a < SNS_MAX_ATTEMPTS; a++) {
        if (a) d->retries++;
        d->attempts++;
        rc = w ? d->bus.ops->write_regs(d->bus.ctx, reg, w, n) : d->bus.ops->read_regs(d->bus.ctx, reg, r, n);
        if (rc != BUS_EAGAIN) break;
    }
    d->last_bus = rc;
    if (rc < 0) return sns_map(rc);
    return (size_t)rc == n ? SNS_OK : SNS_EIO;      /* 짧은 전송 = 실패 (버퍼 뒤쪽은 쓰레기) */
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
static void board_session(Board *b, const SessOpt *o, SessResult *sr) {   /* 녹화기 없이 진짜 버스 직결 */
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
static int trace_import(const char *text, unsigned adapter, TxLog *l, int *err_line) {
    /* TODO(Part C): 줄마다 tr_head() → "i2c-N ..." 파싱 → 메시지 #0/#1 모으기 → i2c_result 에서 레코드 하나.
     *   [write l=1]+[read l=n] → READ, [write l>=2] → WRITE, ret<0 → errno_to_bus(). 끝에 미완 전송 → LOG_EFORMAT. */
    (void)text; (void)adapter;
    l->n = 0; l->dropped = 0;
    if (err_line) *err_line = 0;
    return LOG_ESYNTAX;
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
static int golden_check(const char *log_txt, const char *out_txt, const SessOpt *o, Text *rep) {
    /* TODO(Part C): txt_parse → strict+timing 재생 → (어긋나면) relaxed+timing 재생, 출력 비교, 보고서 작성 */
    (void)log_txt; (void)out_txt; (void)o;
    text_printf(rep, "TODO\n");
    return GOLD_EBADLOG;
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static bool is_sns_code(int rc) { return rc == SNS_OK || (rc <= SNS_EINVAL && rc >= SNS_ENOTREADY); }
static void random_rec(TxRec *r, size_t i, uint32_t *seed) {     /* rec_valid 를 만족하는 임의 레코드 */
    memset(r, 0, sizeof *r);
    r->seq = (uint16_t)i; r->t_us = xs32(seed); r->dur_us = (uint16_t)xs32(seed);
    r->bus = (xs32(seed) & 1u) ? BUSID_I2C : BUSID_SPI; r->op = (xs32(seed) & 1u) ? OP_READ : OP_WRITE;
    r->addr = (uint8_t)(xs32(seed) & 0x7Fu); r->reg = (uint8_t)xs32(seed); r->len = (uint8_t)(1u + xs32(seed) % REC_MAX_DATA);
    static const int8_t ERRS[] = { BUS_ENODEV, BUS_EIO, BUS_ETIMEDOUT, BUS_EAGAIN };
    uint32_t pick = xs32(seed) % 8u;
    r->result = pick < 4u ? ERRS[pick] : pick < 6u ? (int8_t)r->len : (int8_t)(xs32(seed) % (r->len + 1u));
    size_t nd = r->op == OP_WRITE ? r->len : r->result > 0 ? (size_t)r->result : 0u;
    for (size_t k = 0; k < nd; k++) r->data[k] = (uint8_t)xs32(seed);
}
/* 비트 반전의 "문서화된" 결과 (드라이버 계약에서 유도): 어떤 레지스터 / 몇 번째 바이트냐에 따라 */
static int flip_expect(const TxRec *r, unsigned byte, unsigned bit) {
    uint8_t v = (uint8_t)(r->data[byte] ^ (1u << bit));
    if (r->bus == BUSID_SPI && r->reg == R_WHO) return (v == 0 || v == 0xFF) ? SNS_ENOTFOUND : v == IMU_ID ? SNS_OK : SNS_EBADID;
    if (r->bus == BUSID_SPI && r->reg == R_CTRL1) return SNS_EIO;          /* 되읽기 ≠ 쓴 값 */
    if (r->bus == BUSID_SPI && r->reg == R_STATUS) {                       /* 결과는 STATUS 바이트가 정한다 */
        uint8_t st = byte == 0 ? v : r->data[0];
        return st == 0xFF ? SNS_ENOTFOUND : (st & 0xFEu) ? SNS_EIO : (st & 1u) ? SNS_OK : SNS_ENOTREADY;
    }
    if (r->bus == BUSID_I2C && r->reg == T_ID) return (v == 0 || v == 0xFF) ? SNS_ENOTFOUND : v == TMP_ID ? SNS_OK : SNS_EBADID;
    return SNS_OK;                                                         /* 데이터 바이트: CRC 가 없으니 못 잡는다 */
}
static int mut_expect(int kind) {
    switch (kind) { case MUT_NODEV: return SNS_ENOTFOUND; case MUT_NACK: return SNS_EIO; case MUT_TIMEOUT: return SNS_ETIMEDOUT;
                    case MUT_SHORT: return SNS_EIO; default: return SNS_OK; }
}

/* Part C 용 트레이스 (이 레슨이 만든 예 — 형식은 커널 TP_printk 그대로, 값은 MTMP-1 mock) */
static const char TRACE_TMP[] =
    "# tracer: nop\n"
    "#           TASK-PID     CPU#  |||||  TIMESTAMP  FUNCTION\n"
    "   sensord-412     [001] .....   812.000100: i2c_write: i2c-1 #0 a=048 f=0000 l=1 [0f]\n"
    "   sensord-412     [001] .....   812.000101: i2c_read: i2c-1 #1 a=048 f=0001 l=1\n"
    "   sensord-412     [001] .....   812.000160: i2c_reply: i2c-1 #1 a=048 f=0001 l=1 [5a]\n"
    "   sensord-412     [001] .....   812.000161: i2c_result: i2c-1 n=2 ret=2\n"
    "   sensord-412     [001] .....   812.000170: sched_switch: prev_comm=sensord prev_pid=412\n"
    "   sensord-412     [001] .....   812.000200: i2c_write: i2c-1 #0 a=048 f=0000 l=2 [02-60]\n"
    "   sensord-412     [001] .....   812.000262: i2c_result: i2c-1 n=1 ret=1\n"
    "     other-77      [000] .....   812.000300: i2c_write: i2c-2 #0 a=050 f=0000 l=2 [00-11]\n"
    "     other-77      [000] .....   812.000340: i2c_result: i2c-2 n=1 ret=1\n"
    "   sensord-412     [001] .....   813.000100: i2c_write: i2c-1 #0 a=048 f=0000 l=1 [00]\n"
    "   sensord-412     [001] .....   813.000101: i2c_read: i2c-1 #1 a=048 f=0001 l=2\n"
    "   sensord-412     [001] .....   813.000183: i2c_reply: i2c-1 #1 a=048 f=0001 l=2 [0c-80]\n"
    "   sensord-412     [001] .....   813.000184: i2c_result: i2c-1 n=2 ret=2\n"
    "   sensord-412     [001] .....   814.000100: i2c_write: i2c-1 #0 a=048 f=0000 l=1 [00]\n"
    "   sensord-412     [001] .....   814.000101: i2c_read: i2c-1 #1 a=048 f=0001 l=2\n"
    "   sensord-412     [001] .....   814.000183: i2c_reply: i2c-1 #1 a=048 f=0001 l=2 [f3-5b]\n"
    "   sensord-412     [001] .....   814.000184: i2c_result: i2c-1 n=2 ret=2\n";

int main(void) {
    static Board board;
    static TxLog glog, log2;
    static SessResult gold, sr;
    static Replayer rp;
    static Text txt, rep;
    static uint8_t bin[BTXL_MAX_BYTES], bin2[BTXL_MAX_BYTES + 64];
    size_t blen = 0, blen2 = 0;
    uint32_t seed = 0xA09C0DE5u;
    int line = 0;

    /* ---------- 0. 주어진 모델 자체 점검 ---------- */
    {
        const uint8_t chk[] = "123456789";
        CHECK(~crc32_update(0xFFFFFFFFu, chk, 9) == 0xCBF43926u);          /* CRC-32/ISO-HDLC check value */
        uint8_t b[2] = { 0, 0 };
        board_init(&board, 0);
        CHECK(i2c_read_regs(&board.i2c, T_ID, b, 1) == 1 && b[0] == TMP_ID);
        CHECK(spi_read_regs(&board.spi, R_WHO, b, 1) == 1 && b[0] == IMU_ID);
        CHECK(spi_read_regs(&board.spi, 0x3F, b, 2) == BUS_EINVAL && i2c_read_regs(&board.i2c, 0, b, 0) == BUS_EINVAL);
        board.i2c.chip = NULL;
        CHECK(i2c_read_regs(&board.i2c, T_ID, b, 1) == BUS_ENODEV);
    }

    /* ---------- Part A: 녹화 — 골든 세션 ---------- */
    board_init(&board, 0);
    record_session(&board, &glog, &SESS_DEFAULT, &gold);
    printf("golden session output:\n%s", gold.out.s);
    CHECK(glog.n == 18 && glog.dropped == 0);
    CHECK(strstr(gold.out.s, "imu_init=OK\ntmp_init=OK\nimu[0]=OK -3662 0 2000\ntmp[0]=OK 25000\n") != NULL);
    CHECK(strstr(gold.out.s, "imu[3]=ENOTREADY") != NULL);
    {
        const TxRec *r = glog.r;
        CHECK(r[0].bus == BUSID_SPI && r[0].op == OP_READ && r[0].reg == R_WHO && r[0].len == 1 && r[0].result == 1 && r[0].data[0] == IMU_ID && r[0].t_us == 0);
        CHECK(r[1].op == OP_WRITE && r[1].reg == R_CTRL3 && r[1].data[0] == 0x01);
        CHECK(r[2].op == OP_WRITE && r[2].reg == R_CTRL1 && r[2].data[0] == 0x44 && r[2].t_us - (r[1].t_us + r[1].dur_us) == IMU_RESET_DELAY_US);
        CHECK(r[4].bus == BUSID_I2C && r[4].addr == TMP_I2C_ADDR && r[4].reg == T_ID && r[4].dur_us == i2c_dur_us(4));
        CHECK(r[6].reg == R_STATUS && r[6].len == 7 && r[7].reg == T_TEMP && r[7].len == 2);
        bool seq_ok = true;
        for (size_t i = 0; i < glog.n; i++) seq_ok = seq_ok && glog.r[i].seq == i && rec_valid(&glog.r[i], i);
        CHECK(seq_ok);
    }
    /* 녹화기는 투명해야 한다: 녹화 없이 돌린 결과와 같음 */
    board_init(&board, 0);
    board_session(&board, &SESS_DEFAULT, &sr);
    CHECK(strcmp(sr.out.s, gold.out.s) == 0);

    /* ---------- Part A: 형식 왕복 ---------- */
    text_clear(&txt);
    txt_format(&glog, &txt);
    printf("golden log (text):\n%s", txt.s);
    CHECK(bin_encode(&glog, bin, sizeof bin, &blen) == LOG_OK);
    {
        size_t want = BTXL_HDR;
        for (size_t i = 0; i < glog.n; i++) want += BTXL_REC_FIXED + glog.r[i].len;
        CHECK(blen == want);
        printf("binary: %u bytes (16 header + 18 x 14 fixed + %u data), text: %u bytes, crc32=0x%08x\n",
               (unsigned)blen, (unsigned)(want - 16u - 18u * 14u), (unsigned)txt.len, (unsigned)get32(bin + 12));
        char crcline[32];
        snprintf(crcline, sizeof crcline, "# crc32=0x%08x\n", (unsigned)get32(bin + 12));
        CHECK(strstr(txt.s, crcline) != NULL);                              /* 두 형식이 같은 CRC */
    }
    CHECK(bin_decode(bin, blen, &log2) == LOG_OK && log_equal(&log2, &glog));
    CHECK(txt_parse(txt.s, &log2, &line) == LOG_OK && log_equal(&log2, &glog));
    CHECK(bin_encode(&glog, bin2, blen ? blen - 1u : 0u, &blen2) == LOG_ENOSPC && blen2 == 0);
    /* 무작위 로그 2000개: bin / text 왕복 */
    {
        int bad = 0;
        static TxLog rl, back;
        static Text t2;
        for (int it = 0; it < 2000; it++) {
            memset(&rl, 0, sizeof rl);
            rl.n = it % 50 == 0 ? (it % 100 == 0 ? 0u : LOG_MAX_RECS) : xs32(&seed) % 24u;
            for (size_t i = 0; i < rl.n; i++) random_rec(&rl.r[i], i, &seed);
            if (bin_encode(&rl, bin2, sizeof bin2, &blen2) != LOG_OK) { bad++; continue; }
            if (bin_decode(bin2, blen2, &back) != LOG_OK || !log_equal(&back, &rl)) bad++;
            text_clear(&t2); txt_format(&rl, &t2);
            if (t2.overflow || txt_parse(t2.s, &back, &line) != LOG_OK || !log_equal(&back, &rl)) bad++;
        }
        CHECK(bad == 0);
    }
    /* 손상: 모든 바이트 위치 × 3가지 XOR, 모든 잘린 길이 → 절대 LOG_OK 아님 */
    {
        int accepted = 0, crc_hits = 0;
        for (size_t i = 0; i < blen; i++)
            for (int x = 0; x < 3; x++) {
                static const uint8_t X[3] = { 0x01, 0x80, 0xFF };
                memcpy(bin2, bin, blen); bin2[i] ^= X[x];
                int rc = bin_decode(bin2, blen, &log2);
                if (rc == LOG_OK) accepted++;
                if (rc == LOG_ECRC) crc_hits++;
            }
        for (size_t L = 0; L < blen; L++) if (bin_decode(bin, L, &log2) == LOG_OK) accepted++;
        CHECK(accepted == 0);
        CHECK(log2.n == 0);
        printf("corruption: %u single-byte flips, %d caught by CRC, rest by header checks; %u truncations rejected\n",
               (unsigned)(blen * 3u), crc_hits, (unsigned)blen);
    }
    /* 헤더 규칙 하나씩 (인코더가 아직 없으면 건너뛰고 FAIL 하나) */
    CHECK(blen >= BTXL_HDR);
    if (blen >= BTXL_HDR) {
        memcpy(bin2, bin, blen); bin2[0] = 'X';
        CHECK(bin_decode(bin2, blen, &log2) == LOG_EMAGIC);
        memcpy(bin2, bin, blen); bin2[4] = 2;
        CHECK(bin_decode(bin2, blen, &log2) == LOG_EVERSION);
        memcpy(bin2, bin, blen); bin2[blen] = 0;                          /* 꼬리 쓰레기 1바이트 */
        CHECK(bin_decode(bin2, blen + 1u, &log2) == LOG_EFORMAT);
        /* 앞으로 호환: hdr_len 20 (미래 필드 4바이트) + CRC 다시 계산 → 읽힘 */
        memcpy(bin2, bin, 16); memset(bin2 + 16, 0xAB, 4); memcpy(bin2 + 20, bin + 16, blen - 16u);
        bin2[5] = 20; put32(bin2 + 12, btxl_crc(bin2, 20, blen - 16u));
        CHECK(bin_decode(bin2, blen + 4u, &log2) == LOG_OK && log_equal(&log2, &glog));
        memcpy(bin2, bin, blen); put16(bin2 + 6, 200); put32(bin2 + 12, btxl_crc(bin2, 16, blen - 16u));
        CHECK(bin_decode(bin2, blen, &log2) == LOG_ENOSPC);
        memcpy(bin2, bin, blen); put16(bin2 + 6, 17); put32(bin2 + 12, btxl_crc(bin2, 16, blen - 16u));
        CHECK(bin_decode(bin2, blen, &log2) == LOG_EFORMAT);              /* 개수와 payload 길이가 안 맞음 */
        memcpy(bin2, bin, blen); bin2[16 + 13] = 0x7F; put32(bin2 + 12, btxl_crc(bin2, 16, blen - 16u));
        CHECK(bin_decode(bin2, blen, &log2) == LOG_EFORMAT);              /* result 127 > len (CRC 는 맞아도) */
    }
    /* 텍스트 파서의 에러 */
    {
        static char ed[32768];
        CHECK(txt_parse("btxl v1 n=0\n", &log2, &line) == LOG_EMAGIC);
        CHECK(txt_parse("# btxl v2 n=0\n", &log2, &line) == LOG_EVERSION);
        CHECK(txt_parse("# btxl v1 n=0\n", &log2, &line) == LOG_OK && log2.n == 0);
        CHECK(txt_parse("# btxl v1 n=1\n0000 t=0 d=2 spi:00 R 0f n=1 rc=1 : b5\n", &log2, &line) == LOG_OK && log2.n == 1);
        CHECK(txt_parse("# btxl v1 n=1\r\n# hand-written\r\n\r\n0000 t=0 d=2 spi:00 R 0f n=1 rc=1 : b5\r\n", &log2, &line) == LOG_OK && log2.n == 1);
        CHECK(txt_parse("# btxl v1 n=2\n0000 t=0 d=2 spi:00 R 0f n=1 rc=1 : b5\n0001 t=0 d=2 spi:00 X 0f n=1 rc=1 : b5\n", &log2, &line) == LOG_ESYNTAX && line == 3 && log2.n == 0);
        CHECK(txt_parse("# btxl v1 n=1\n0000 t=99999999999 d=2 spi:00 R 0f n=1 rc=1 : b5\n", &log2, &line) == LOG_ESYNTAX && line == 2);
        CHECK(txt_parse("# btxl v1 n=1\n0000 t=0 d=2 spi:00 R 0f n=1 rc=9 : b5\n", &log2, &line) == LOG_EFORMAT);
        CHECK(txt_parse("# btxl v1 n=1\n0000 t=0 d=2 spi:00 R 0f n=2 rc=1 : b5 07\n", &log2, &line) == LOG_EFORMAT);   /* rc 뒤 바이트는 0 이어야 */
        CHECK(txt_parse("# btxl v1 n=1\n0003 t=0 d=2 spi:00 R 0f n=1 rc=1 : b5\n", &log2, &line) == LOG_EFORMAT);      /* seq 불연속 */
        CHECK(txt_parse("# btxl v1 n=2\n0000 t=0 d=2 spi:00 R 0f n=1 rc=1 : b5\n", &log2, &line) == LOG_EFORMAT);      /* 개수 */
        CHECK(txt_parse("# btxl v1 n=1\n0000 t=0 d=2 spi:00 R 0f n=1 rc=1 : b5 \n", &log2, &line) == LOG_ESYNTAX);
        /* 손으로 고친 골든: crc 줄을 남긴 채 데이터 수정 → ECRC, crc 줄을 지우면 받아들인다 */
        snprintf(ed, sizeof ed, "%s", txt.s);
        char *pb5 = strstr(ed, ": b5"); if (pb5) pb5[3] = '4';
        CHECK(txt_parse(ed, &log2, &line) == LOG_ECRC);
        char *pc = strstr(ed, "# crc32="); if (pc) *pc = '\0';
        CHECK(txt_parse(ed, &log2, &line) == LOG_OK && log2.r[0].data[0] == 0xB4);
    }
    /* 녹화 로그가 가득 차면: 버스 동작은 그대로, dropped 만 센다 */
    {
        static TxLog small;
        board_init(&board, 0);
        log_reset(&small);
        RecPort rp_t = { { &I2C_OPS, &board.i2c }, &small, BUSID_I2C, TMP_I2C_ADDR };
        int ok = 0; uint8_t b = 0;
        for (int i = 0; i < 200; i++) if (rec_read_regs(&rp_t, T_ID, &b, 1) == 1 && b == TMP_ID) ok++;
        CHECK(ok == 200 && small.n == LOG_MAX_RECS && small.dropped == 72);
        CHECK(rec_read_regs(&rp_t, T_ID, &b, 0) == BUS_EINVAL && small.dropped == 72);   /* 인자 오류는 기록 대상 아님 */
        CHECK(board.i2c.txns == 200);
    }

    /* ---------- Part B: 재생 — 같은 드라이버면 깨끗 ---------- */
    for (int mode = RP_STRICT; mode <= RP_RELAXED; mode++) {
        rp_init(&rp, &glog, mode, true);
        replay_session(&rp, &SESS_DEFAULT, &sr);
        CHECK(rp_finish(&rp) == 0 && rp.issued == 18 && rp.served == 18 && rp.diff[0] == '\0');
        CHECK(strcmp(sr.out.s, gold.out.s) == 0);
    }
    /* 인자 오류는 재생기도 선(로그)에 안 닿는다 */
    {
        RpPort pt = { &rp, BUSID_I2C, TMP_I2C_ADDR };
        uint8_t b = 0;
        rp_init(&rp, &glog, RP_STRICT, true);
        CHECK(rp_read_regs(&pt, T_ID, &b, 0) == BUS_EINVAL && rp.issued == 0);
        CHECK(rp_read_regs(&pt, T_ID, NULL, 1) == BUS_EINVAL && rp.issued == 0);
    }
    /* 회귀 1: burst → 축별 읽기 (보드에선 값이 같아 기능 테스트로는 안 잡힘) */
    {
        SessOpt o = SESS_DEFAULT; o.per_axis = true;
        board_init(&board, 0);
        board_session(&board, &o, &sr);
        CHECK(strcmp(sr.out.s, gold.out.s) == 0);
        for (int mode = RP_STRICT; mode <= RP_RELAXED; mode++) {
            rp_init(&rp, &glog, mode, true);
            replay_session(&rp, &o, &sr);
            CHECK(rp_finish(&rp) > 0);
            CHECK(strcmp(rp.diff, "seq 6: expected spi:00 READ reg 0x27 len 7, got spi:00 READ reg 0x27 len 1") == 0);
            printf("per-axis refactor, %s: %s\n", mode ? "relaxed" : "strict ", rp.diff);
        }
    }
    /* 회귀 2: 리셋 지연 삭제 — op 순서는 똑같다. 타이밍 검사만 잡는다. 보드에선 설정이 무시된다 */
    {
        SessOpt o = SESS_DEFAULT; o.reset_delay_us = 0;
        rp_init(&rp, &glog, RP_STRICT, false);
        replay_session(&rp, &o, &sr);
        CHECK(rp_finish(&rp) == 0 && strcmp(sr.out.s, gold.out.s) == 0);                 /* 함정: 순서만 보면 통과 */
        rp_init(&rp, &glog, RP_STRICT, true);
        replay_session(&rp, &o, &sr);
        CHECK(rp_finish(&rp) == 1 && rp.timing_errs == 1);
        CHECK(strcmp(rp.diff, "seq 2: spi:00 WRITE reg 0x10 len 1 [44] issued too early: gap 0 us < 90% of recorded 1000 us") == 0);
        printf("no-reset-delay refactor, strict+timing: %s\n", rp.diff);
        board_init(&board, 0);
        board_session(&board, &o, &sr);
        CHECK(sr.rc[0] == SNS_EIO && board.imu.ignored == 1);
        printf("no-reset-delay refactor on the board: imu_init=%s (chip ignored %u write)\n", sns_strerror(sr.rc[0]), board.imu.ignored);
    }
    /* 무해한 리팩터: 틱 안에서 온도 → IMU 순서로 — strict 는 거부, relaxed 는 통과 */
    {
        SessOpt o = SESS_DEFAULT; o.swap_order = true;
        rp_init(&rp, &glog, RP_STRICT, true);
        replay_session(&rp, &o, &sr);
        CHECK(rp_finish(&rp) > 0);
        CHECK(strcmp(rp.diff, "seq 6: expected spi:00 READ reg 0x27 len 7, got i2c:48 READ reg 0x00 len 2") == 0);
        printf("swapped order, strict : %s\n", rp.diff);
        rp_init(&rp, &glog, RP_RELAXED, true);
        replay_session(&rp, &o, &sr);
        CHECK(rp_finish(&rp) == 0 && strcmp(sr.out.s, gold.out.s) == 0);
        printf("swapped order, relaxed: %s\n", rp.diff[0] ? rp.diff : "clean");
    }
    /* 같은 레지스터에 다른 값을 쓰면 (FS ±4 g → ±8 g) 쓰기 데이터까지 비교해 잡는다 */
    {
        RpPort pi = { &rp, BUSID_SPI, IMU_SPI_CS };
        bus_t bi = { &RP_OPS, &pi };
        sns_t d; memset(&d, 0, sizeof d);
        rp_init(&rp, &glog, RP_STRICT, true);
        CHECK(imu_init_ex(&d, bi, 2, IMU_RESET_DELAY_US) == SNS_EIO && rp_finish(&rp) > 0);
        CHECK(strcmp(rp.diff, "seq 2: expected spi:00 WRITE reg 0x10 len 1 [44], got spi:00 WRITE reg 0x10 len 1 [48]") == 0);
        printf("config change fs=2: %s\n", rp.diff);
    }
    /* 드라이버가 일찍 멈춤 / 더 많이 함 */
    {
        SessOpt o = SESS_DEFAULT; o.ticks = 5;
        rp_init(&rp, &glog, RP_STRICT, true);
        replay_session(&rp, &o, &sr);
        CHECK(rp_finish(&rp) == 2 && strcmp(rp.diff, "seq 16: expected spi:00 READ reg 0x27 len 7, never issued") == 0);
        o.ticks = 7;
        rp_init(&rp, &glog, RP_STRICT, true);
        replay_session(&rp, &o, &sr);
        CHECK(rp_finish(&rp) == 2 && strcmp(rp.diff, "op 19: unexpected spi:00 READ reg 0x27 len 7 (no more records)") == 0);
        rp_init(&rp, &glog, RP_RELAXED, true);
        replay_session(&rp, &o, &sr);
        CHECK(rp_finish(&rp) == 2 && strcmp(rp.diff, "op 19: unexpected spi:00 READ reg 0x27 len 7 (no more records for this device)") == 0);
    }

    /* ---------- Part B: 변형 재생 — 모든 레코드 k × 모든 변형 ---------- */
    {
        int bad = 0, cases = 0;
        Text *mx = &rep; text_clear(mx);
        text_printf(mx, "seq  record             NODEV       NACK       TIMEOUT    ARLO       SHORT      SLOW\n");
        for (size_t k = 0; k < glog.n; k++) {
            const TxRec *r = &glog.r[k];
            int owner = 0;
            while (owner < (int)SESS_CALLS - 1 && gold.rec_after[owner] <= k) owner++;
            text_printf(mx, "%4u %s:%02x %c %02x n=%u   ", (unsigned)k, bus_name(r->bus), r->addr, r->op, r->reg, r->len);
            for (int kind = MUT_NODEV; kind <= MUT_SLOW; kind++) {
                if (kind == MUT_FLIP) continue;
                SessOpt o = SESS_DEFAULT; o.stop_on_error = true;
                rp_init(&rp, &glog, RP_STRICT, true);
                rp.mut.kind = kind; rp.mut.seq = (int)k;
                replay_session(&rp, &o, &sr);
                int prob = rp_finish(&rp);
                cases++;
                bool ok = rp.mut.fired && sr.partial_commits == 0 && rp.mismatches == 0 && rp.timing_errs == 0;
                if (kind == MUT_ARLO || kind == MUT_SLOW) {
                    ok = ok && prob == 0 && strcmp(sr.out.s, gold.out.s) == 0 && sr.retries == (kind == MUT_ARLO ? 1u : 0u)
                            && rp.issued == glog.n + (kind == MUT_ARLO ? 1u : 0u);
                    text_printf(mx, " %-10s", ok ? (kind == MUT_ARLO ? "retry-OK" : "OK") : "BAD");
                } else {
                    int exp = mut_expect(kind);
                    ok = ok && sr.fail_call == owner && sr.fail_rc == exp && rp.issued == k + 1u;
                    for (int c = 0; c < owner; c++) ok = ok && sr.done[c] && sr.rc[c] == gold.rc[c] && memcmp(sr.v[c], gold.v[c], sizeof sr.v[c]) == 0;
                    text_printf(mx, " %-10s", ok ? sns_strerror(sr.fail_rc) : "BAD");
                }
                if (!ok) bad++;
            }
            text_printf(mx, "\n");
        }
        CHECK(cases == 18 * 6 && bad == 0);
        printf("mutation sweep (driver result per injected fault):\n%s", mx->s);
    }
    /* 비트 반전: 모든 READ 레코드의 모든 비트 */
    {
        int bad = 0, cases = 0, detected = 0, notready = 0, changed = 0, masked = 0;
        int cls[4][4] = { { 0 } };   /* [ID·되읽기 / STATUS / IMU 데이터 / 온도 데이터][detected, notready, changed, masked] */
        for (size_t k = 0; k < glog.n; k++) {
            const TxRec *r = &glog.r[k];
            if (r->op != OP_READ) continue;
            int owner = 0;
            while (owner < (int)SESS_CALLS - 1 && gold.rec_after[owner] <= k) owner++;
            for (unsigned byte = 0; byte < r->len; byte++)
                for (unsigned bit = 0; bit < 8; bit++) {
                    SessOpt o = SESS_DEFAULT; o.stop_on_error = true;
                    rp_init(&rp, &glog, RP_STRICT, true);
                    rp.mut = (Mutation){ MUT_FLIP, (int)k, (uint8_t)byte, (uint8_t)bit, false };
                    replay_session(&rp, &o, &sr);
                    int prob = rp_finish(&rp), exp = flip_expect(r, byte, bit);
                    bool ok = rp.mut.fired && sr.partial_commits == 0 && is_sns_code(sr.rc[owner]) && sr.rc[owner] == exp;
                    cases++;
                    int c = (r->reg == R_STATUS && r->bus == BUSID_SPI) ? (byte == 0 ? 1 : 2) : (r->reg == T_TEMP && r->bus == BUSID_I2C) ? 3 : 0, out;
                    if (exp != SNS_OK && exp != SNS_ENOTREADY) { ok = ok && sr.fail_call == owner && rp.issued == k + 1u; detected++; out = 0; }
                    else {
                        ok = ok && prob == 0 && sr.fail_call == -1;
                        if (exp == SNS_ENOTREADY) { notready++; out = 1; }
                        else if (strcmp(sr.out.s, gold.out.s) != 0) { changed++; out = 2; } else { masked++; out = 3; }
                    }
                    cls[c][out]++;
                    if (!ok) bad++;
                }
        }
        CHECK(cases == 8 * (1 + 1 + 1 + 6 * 7 + 6 * 2) && bad == 0);
        printf("bit-flip sweep: %d flips -> detected %d, became NOTREADY %d, silently changed output %d, masked %d\n",
               cases, detected, notready, changed, masked);
        static const char *const CN[4] = { "WHO/ID + CTRL1 read-back", "IMU STATUS byte", "IMU X/Y/Z bytes", "temperature bytes" };
        printf("  %-26s %8s %8s %8s %8s\n", "flipped byte", "detected", "NOTREADY", "changed", "masked");
        for (int c = 0; c < 4; c++) printf("  %-26s %8d %8d %8d %8d\n", CN[c], cls[c][0], cls[c][1], cls[c][2], cls[c][3]);
    }
    /* 무작위 세션 300개: 녹화 → bin → 디코드 → strict 재생 = 같은 출력, 깨끗 */
    {
        int bad = 0;
        static TxLog rl;
        for (int it = 0; it < 300; it++) {
            SessOpt o = SESS_DEFAULT; o.ticks = 1u + xs32(&seed) % SESS_MAX_TICKS;
            board_init(&board, xs32(&seed) | 1u);
            record_session(&board, &rl, &o, &gold);
            if (bin_encode(&rl, bin2, sizeof bin2, &blen2) != LOG_OK || bin_decode(bin2, blen2, &log2) != LOG_OK) { bad++; continue; }
            rp_init(&rp, &log2, (it & 1) ? RP_RELAXED : RP_STRICT, true);
            replay_session(&rp, &o, &sr);
            if (rp_finish(&rp) != 0 || strcmp(sr.out.s, gold.out.s) != 0) bad++;
        }
        CHECK(bad == 0);
    }
    /* 골든 세션 다시 (위 무작위 세션이 gold 를 덮어썼다) */
    board_init(&board, 0);
    record_session(&board, &glog, &SESS_DEFAULT, &gold);

    /* ---------- Part C: Linux i2c 트레이스 가져오기 → 온도 드라이버 재생 ---------- */
    {
        static TxLog tl;
        static Text t3;
        CHECK(trace_import(TRACE_TMP, 1, &tl, &line) == LOG_OK && tl.n == 4);
        text_clear(&t3); txt_format(&tl, &t3);
        printf("imported from i2c trace events (adapter 1):\n%s", t3.s);
        CHECK(tl.r[0].op == OP_READ && tl.r[0].reg == T_ID && tl.r[0].data[0] == TMP_ID && tl.r[0].t_us == 0 && tl.r[0].dur_us == 61);
        CHECK(tl.r[1].op == OP_WRITE && tl.r[1].reg == T_CONFIG && tl.r[1].len == 1 && tl.r[1].data[0] == 0x60);
        CHECK(tl.r[2].t_us == 1000000u && tl.r[3].data[0] == 0xF3 && tl.r[3].data[1] == 0x5B);
        rp_init(&rp, &tl, RP_STRICT, true);
        RpPort pt = { &rp, BUSID_I2C, TMP_I2C_ADDR };
        bus_t bt = { &RP_OPS, &pt };
        sns_t d; memset(&d, 0, sizeof d);
        int32_t mc1 = 0, mc2 = 0;
        int r1 = tmp_init(&d, bt);
        plat_delay_us(1000000u);                                       /* 트레이스의 1 s 주기를 지킨다 (타이밍 검사) */
        int r2 = tmp_read_mc(&d, &mc1);
        plat_delay_us(1000000u);
        int r3 = tmp_read_mc(&d, &mc2);
        CHECK(r1 == SNS_OK && r2 == SNS_OK && r3 == SNS_OK && mc1 == 25000 && mc2 == -25289 && rp_finish(&rp) == 0);
        printf("replayed MTMP-1 driver on imported trace: init=%s, %d m°C, %d m°C\n", sns_strerror(r1), (int)mc1, (int)mc2);
        /* 실패한 전송 (ret=-6 = ENXIO, 주소 NACK) → BUS_ENODEV 레코드 → 드라이버는 ENOTFOUND */
        CHECK(trace_import(" x-1 [000] ..... 5.000000: i2c_write: i2c-1 #0 a=048 f=0000 l=1 [0f]\n"
                           " x-1 [000] ..... 5.000001: i2c_read: i2c-1 #1 a=048 f=0001 l=1\n"
                           " x-1 [000] ..... 5.000030: i2c_result: i2c-1 n=2 ret=-6\n", 1, &tl, &line) == LOG_OK
              && tl.n == 1 && tl.r[0].result == BUS_ENODEV);
        rp_init(&rp, &tl, RP_STRICT, true);
        memset(&d, 0, sizeof d);
        CHECK(tmp_init(&d, bt) == SNS_ENOTFOUND && rp_finish(&rp) == 0);
        /* 잘린 캡처 / 문법 오류 / 모르는 모양 */
        CHECK(trace_import(" x-1 [000] ..... 5.000000: i2c_write: i2c-1 #0 a=048 f=0000 l=1 [0f]\n", 1, &tl, &line) == LOG_EFORMAT && line == 2);
        CHECK(trace_import(" x-1 [000] ..... 5.000000: i2c_write: i2c-1 #0 a=0zz f=0000 l=1 [0f]\n", 1, &tl, &line) == LOG_ESYNTAX && line == 1);
        CHECK(trace_import(" x-1 [000] ..... 5.000000: i2c_write: i2c-1 #0 a=048 f=0000 l=2 [0f]\n", 1, &tl, &line) == LOG_ESYNTAX);
        CHECK(trace_import(" x-1 [000] ..... 5.000000: i2c_read: i2c-1 #0 a=048 f=0001 l=2\n"
                           " x-1 [000] ..... 5.000040: i2c_reply: i2c-1 #0 a=048 f=0001 l=2 [01-02]\n"
                           " x-1 [000] ..... 5.000041: i2c_result: i2c-1 n=1 ret=1\n", 1, &tl, &line) == LOG_EFORMAT && line == 3);
        CHECK(trace_import(" x-1 [000] ..... 5.000000: i2c_write: i2c-1 #0 a=048 f=0000 l=1 [0f]\n"
                           " x-1 [000] ..... 5.000041: i2c_result: i2c-1 n=2 ret=1\n", 1, &tl, &line) == LOG_EFORMAT && tl.n == 0);
        CHECK(trace_import(TRACE_TMP, 2, &tl, &line) == LOG_OK && tl.n == 1 && tl.r[0].op == OP_WRITE && tl.r[0].addr == 0x50);
    }

    /* ---------- Part C: CI 골든 판정 ---------- */
    {
        static char gold_log[32768], gold_out[4096];
        text_clear(&txt); txt_format(&glog, &txt);
        snprintf(gold_log, sizeof gold_log, "%s", txt.s);
        snprintf(gold_out, sizeof gold_out, "%s", gold.out.s);
        static const struct { const char *name; SessOpt o; int expect; } CASES[] = {
            { "unchanged driver",            { 6, false, false, false, false, IMU_RESET_DELAY_US }, GOLD_PASS_STRICT },
            { "tmp read moved before imu",   { 6, true,  false, false, false, IMU_RESET_DELAY_US }, GOLD_PASS_RELAXED },
            { "burst split per axis",        { 6, false, true,  false, false, IMU_RESET_DELAY_US }, GOLD_FAIL },
            { "reset delay removed",         { 6, false, false, false, false, 0 },                  GOLD_FAIL },
            { "/32768 replaced by >>15",     { 6, false, false, true,  false, IMU_RESET_DELAY_US }, GOLD_FAIL },
        };
        for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
            text_clear(&rep);
            int g = golden_check(gold_log, gold_out, &CASES[i].o, &rep);
            CHECK(g == CASES[i].expect);
            printf("[golden] %s ->\n%s", CASES[i].name, rep.s);
        }
        text_clear(&rep);
        golden_check(gold_log, gold_out, &CASES[4].o, &rep);
        CHECK(strstr(rep.s, "output line 3: expected 'imu[0]=OK -3662 0 2000', got 'imu[0]=OK -3663 0 2000'") != NULL);
        /* 깨진 골든 → 판정 불가 */
        char *pb5 = strstr(gold_log, ": b5"); if (pb5) pb5[3] = '4';
        text_clear(&rep);
        CHECK(golden_check(gold_log, gold_out, &CASES[0].o, &rep) == GOLD_EBADLOG && strstr(rep.s, "LOG_ECRC") != NULL);
        /* 의도된 변경 → 다시 녹화 (UPDATE_GOLDEN) → 새 골든에서는 strict 통과 */
        board_init(&board, 0);
        record_session(&board, &log2, &CASES[1].o, &sr);
        text_clear(&txt); txt_format(&log2, &txt);
        snprintf(gold_log, sizeof gold_log, "%s", txt.s);
        text_clear(&rep);
        CHECK(golden_check(gold_log, sr.out.s, &CASES[1].o, &rep) == GOLD_PASS_STRICT && strcmp(sr.out.s, gold_out) == 0);
        text_clear(&rep);
        CHECK(golden_check(gold_log, gold_out, &CASES[0].o, &rep) == GOLD_PASS_RELAXED);   /* 옛 순서는 이제 relaxed */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
