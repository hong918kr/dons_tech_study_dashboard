/* legacy_driver.c — C-A10 분석 대상: "LS200" 온도 + FIFO 센서 허브 드라이버 (가상의 레거시 코드)
 *
 * 10년 된 제품 코드라고 가정한다. 팀의 기존 빌드 옵션(-Wall -Wextra)으로는 경고 0개 — "빌드는 깨끗한" 레거시.
 * 하지만 암시적 변환, 부작용 있는 매크로 인자, switch fall-through, 재귀, 8진수 상수, 뒤로 가는 goto,
 * malloc/printf 가 섞여 있다. 이 파일은 고치지 않는다 — 분석 도구의 입력이자 starter.c 의 old_* 원본이다.
 *
 *   cc -std=c11 -Wall -Wextra -c legacy_driver.c -o /tmp/legacy.o                       # 경고 0
 *   cc -std=c11 -Weverything -c legacy_driver.c -o /tmp/legacy.o                        # 경고 다수
 *   clang-tidy legacy_driver.c --checks='-*,bugprone-*,cert-*,misc-*,readability-*' -- -std=c11 \
 *       -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---- register map ---- */
#define REG_ID          0x00
#define REG_CTRL        0x01
#define REG_STATUS      0x02
#define REG_ODR_DIV     0x03
#define REG_TEMP_H      0x10
#define REG_TEMP_L      0x11
#define REG_FIFO_CNT    0x20
#define REG_FIFO_DATA   0x21

#define CTRL_EN         0x01
#define CTRL_RST        0x80
#define STATUS_RDY      0x01
#define LS200_ID        0x5A
#define FIFO_DEPTH      32
#define BASE_CLK_HZ     3200

/* ---- helpers ---- */
#define MAX(a, b)       ((a) > (b) ? (a) : (b))
#define SCALE_MC(x)     x * 625 / 10            /* 1 LSB = 0.0625 C = 62.5 mC */
#define SET_BIT(r, b)   r |= 1 << b
#define DBG(...)        printf(__VA_ARGS__)

enum { CMD_RESET = 1, CMD_START, CMD_SET_RATE, CMD_STOP, CMD_CLEAR };

typedef struct {
    char name[8];
    uint8_t addr;
    int running;
    int rate;
    int err;
    short last_temp;
    uint8_t *log;
    int log_n;
} ls_dev;

/* ODR table, zero-padded so the columns line up */
static const int k_odr_hz[] = { 001, 002, 005, 010, 020, 050, 100, 200 };

int g_temp_offset;      /* calibration offset in LSB, set by factory tool */

/* ---------------------------------------------------------------- mock bus */
static uint8_t g_regs[256];
static uint8_t g_fifo[FIFO_DEPTH];
static int g_fifo_head, g_fifo_n;
static int g_bus_reads;

uint8_t bus_read(uint8_t reg)
{
    g_bus_reads++;
    if (reg == REG_FIFO_DATA) {
        uint8_t v;
        if (g_fifo_n == 0)
            return 0;
        v = g_fifo[g_fifo_head];
        g_fifo_head = (g_fifo_head + 1) % FIFO_DEPTH;
        g_fifo_n--;
        g_regs[REG_FIFO_CNT] = g_fifo_n;
        return v;
    }
    return g_regs[reg];
}

void bus_write(uint8_t reg, uint8_t val)
{
    g_regs[reg] = val;
    if (reg == REG_CTRL && (val & CTRL_RST))
        g_regs[REG_STATUS] = STATUS_RDY;
}

void fifo_push(uint8_t v)
{
    if (g_fifo_n < FIFO_DEPTH) {
        g_fifo[(g_fifo_head + g_fifo_n) % FIFO_DEPTH] = v;
        g_fifo_n++;
        g_regs[REG_FIFO_CNT] = g_fifo_n;
    }
}

/* ---------------------------------------------------------------- conversions */
/* 12-bit two's complement temperature -> milli-Celsius */
int ls_temp_mc(unsigned short raw)
{
    short v = raw << 4;
    v = v >> 4;
    return SCALE_MC(v);
}

int ls_temp_cal_mc(unsigned short raw)
{
    short v = raw << 4;
    v = v >> 4;
    return SCALE_MC(v + g_temp_offset);
}

int ls_read_temp(ls_dev *d)
{
    unsigned short raw = bus_read(REG_TEMP_H) << 8 | bus_read(REG_TEMP_L);
    d->last_temp = ls_temp_mc(raw);
    return d->last_temp;
}

/* average of n samples */
short ls_avg(const short *s, unsigned n)
{
    int sum = 0;
    unsigned i;
    for (i = 0; i < n; i++)
        sum += s[i];
    return sum / n;
}

/* frame = payload bytes + 1 checksum byte (8-bit sum of payload) */
int ls_frame_ok(const uint8_t *buf, uint8_t n)
{
    uint8_t sum = 0;
    int i;
    for (i = 0; i < n - 1; i++)
        sum += buf[i];
    return sum == buf[n - 1];
}

/* ---------------------------------------------------------------- FIFO */
/* pop n samples, return the peak (samples are 0..255) */
int ls_fifo_peak(int n)
{
    int peak = 0;
    while (n--)
        peak = MAX(bus_read(REG_FIFO_DATA), peak);
    return peak;
}

int ls_fifo_drain(ls_dev *d)
{
    int cnt = bus_read(REG_FIFO_CNT);
    int i;
    if (d->log)
        free(d->log);
    d->log = malloc(cnt);
    d->log_n = 0;
    if (!d->log)
        return -1;
    for (i = 0; i < cnt; i++)
        d->log[d->log_n++] = bus_read(REG_FIFO_DATA);
    return d->log_n;
}

/* ---------------------------------------------------------------- commands */
int ls_cmd(ls_dev *d, int cmd, int arg)
{
    switch (cmd) {
    case CMD_RESET:
        d->rate = 1;
        d->err = 0;
        /* and start again */
    case CMD_START:
        d->running = 1;
        break;
    case CMD_SET_RATE:
        if (arg > 0 && arg <= 100)
            d->rate = arg;
        else
            d->err++;
    case CMD_STOP:
        d->running = 0;
        break;
    case CMD_CLEAR:
        d->err = 0;
    }
    return d->running;
}

/* ---------------------------------------------------------------- config TLV */
/* element = [type][len][payload]; type bit7 = container (payload is more elements).
 * returns the byte-sum of all leaf payloads, or -1 if malformed. */
int ls_tlv_sum(const uint8_t *p, int len)
{
    int sum = 0;
    while (len > 0) {
        int t, l, i;
        if (len < 2)
            return -1;
        t = p[0];
        l = p[1];
        if (l > len - 2)
            return -1;
        if (t & 0x80) {
            int s = ls_tlv_sum(p + 2, l);
            if (s < 0)
                return s;
            sum += s;
        } else {
            for (i = 0; i < l; i++)
                sum += p[2 + i];
        }
        p += 2 + l;
        len -= 2 + l;
    }
    return sum;
}

/* ---------------------------------------------------------------- ODR */
static int gcd(int a, int b)
{
    return b ? gcd(b, a % b) : a;
}

int ls_set_odr(ls_dev *d, int idx)
{
    int hz, div, g;
    if (idx < 0 || idx > 7)
        return -1;
    hz = k_odr_hz[idx];
    g = gcd(BASE_CLK_HZ, hz);
    div = BASE_CLK_HZ / g;
    bus_write(REG_ODR_DIV, div);
    d->rate = hz;
    DBG("odr %d Hz div %d\n", hz, div);
    return div;
}

/* ---------------------------------------------------------------- lifecycle */
ls_dev *ls_open(const char *name, uint8_t addr)
{
    ls_dev *d;
    if ((d = malloc(sizeof(ls_dev))) == 0)
        return 0;
    memset(d, 0, sizeof(ls_dev));
    strcpy(d->name, name);
    d->addr = addr;
    return d;
}

void ls_close(ls_dev *d)
{
    free(d->log);
    free(d);
}

int ls_init(ls_dev *d)
{
    int tries = 3;
    uint8_t ctrl;
retry:
    bus_write(REG_CTRL, CTRL_RST);
    while (!(bus_read(REG_STATUS) & STATUS_RDY))
        ;
    if (bus_read(REG_ID) != LS200_ID) {
        if (--tries)
            goto retry;
        d->err++;
        return -1;
    }
    ctrl = bus_read(REG_CTRL);
    SET_BIT(ctrl, 0);
    bus_write(REG_CTRL, ctrl);
    d->running = 0;
    DBG("%s @0x%02x up\n", d->name, d->addr);
    return 0;
}

void ls_dump(ls_dev *d)
{
    int r;
    printf("%s: run=%d rate=%d err=%d temp=%d\n", d->name, d->running, d->rate, d->err, d->last_temp);
    for (r = 0; r < 0x22; r++)
        if (g_regs[r])
            printf("  reg %02x = %02x\n", r, g_regs[r]);
}

/* ---------------------------------------------------------------- self test */
int ls_selftest(void)
{
    static const uint8_t cfg[] = { 0x81, 6, 0x01, 1, 7, 0x02, 1, 9, 0x03, 2, 1, 2 };
    ls_dev *d = ls_open("ls0", 0x48);
    int ok = 1;
    if (!d)
        return 0;
    g_regs[REG_ID] = LS200_ID;
    if (ls_init(d))
        ok = 0;
    g_regs[REG_TEMP_H] = 0x01;
    g_regs[REG_TEMP_L] = 0x90;
    if (ls_read_temp(d) != 25000)
        ok = 0;
    if (ls_tlv_sum(cfg, sizeof cfg) != 19)
        ok = 0;
    fifo_push(3); fifo_push(9); fifo_push(4);
    if (ls_fifo_peak(3) != 9)
        ok = 0;
    ls_set_odr(d, 3);
    ls_dump(d);
    ls_close(d);
    return ok;
}
