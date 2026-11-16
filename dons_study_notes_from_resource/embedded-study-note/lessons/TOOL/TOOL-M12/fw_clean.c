/* fw_clean.c - the same UART link module as fw_bugs.c with the 10 bugs fixed
 * (each fix is marked FIX01..FIX10).  This is the CI baseline: the analyzer
 * configs must report ZERO findings here.  selftest.c #includes this file and
 * checks the fixed behaviour at run time.
 *
 * On the target, UART1 is ((uart_regs_t *)0x40011000u); on the host it is a
 * plain struct so the module can be unit-tested and analyzed with host tools. */
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fw_util.h"
#include "vendor_hal.h"

/* ---------------- register block ---------------- */
typedef struct {
    volatile uint32_t SR;  /* status: bit5 RXNE, bit3 ORE           */
    volatile uint32_t DR;  /* data                                  */
    volatile uint32_t BRR; /* baud-rate divider                     */
    volatile uint32_t CR1; /* control: bit13 UE, bit3 TE, bit2 RE   */
} uart_regs_t;

static uart_regs_t uart1_sim;
#define UART1 (&uart1_sim)
#define UART_PCLK_HZ 84000000u
#define UART_CR1_UE (1u << 13)
#define UART_CR1_TE (1u << 3)
#define UART_CR1_RE (1u << 2)

int fw_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

/* baud_code comes from the host link-config command: 0..3 */
int uart_init(int baud_code)
{
    uint32_t baud;
    switch (baud_code) {
    case 0: baud = 9600u; break;
    case 1: baud = 57600u; break;
    case 2: baud = 115200u; break;
    case 3: baud = 921600u; break;
    default: fw_log("uart: bad baud code %d\n", baud_code); return -1; /* FIX04 */
    }
    UART1->BRR = __HAL_UART_BRR(UART_PCLK_HZ, baud);
    UART1->CR1 = UART_CR1_UE | UART_CR1_TE | UART_CR1_RE;
    return 0;
}

/* ---------------- RX ring buffer (RX poll task producer, link task consumer) ---- */
#define RB_SIZE 64u
typedef struct {
    uint8_t buf[RB_SIZE];
    uint32_t head; /* next write */
    uint32_t tail; /* next read  */
    fw_mutex_t lock;
} ringbuf_t;

static ringbuf_t rx_rb = {.lock = FW_MUTEX_INIT};

int rb_push(ringbuf_t *rb, uint8_t b)
{
    fw_mutex_lock(&rb->lock);
    if (rb->head - rb->tail == RB_SIZE) {
        fw_mutex_unlock(&rb->lock); /* FIX07 */
        return -1;
    }
    rb->buf[rb->head % RB_SIZE] = b;
    rb->head++;
    fw_mutex_unlock(&rb->lock);
    return 0;
}

int rb_pop(ringbuf_t *rb, uint8_t *out)
{
    int rc = -1;
    fw_mutex_lock(&rb->lock);
    if (rb->head != rb->tail) {
        *out = rb->buf[rb->tail % RB_SIZE];
        rb->tail++;
        rc = 0;
    }
    fw_mutex_unlock(&rb->lock);
    return rc;
}

/* RX poll task: move received bytes from the data register into the ring */
#define UART_SR_ORE (1u << 3)
static uint32_t rx_overruns;

void uart1_rx_poll(void)
{
    uint32_t sr = UART1->SR;
    if ((sr & UART_SR_ORE) != 0u) {
        /* overrun: ORE is cleared by reading SR and then DR (vendor HAL idiom).
         * Both reads are the point; the stores to tmp are dead on purpose. */
        uint32_t tmp = UART1->SR; // NOLINT(clang-analyzer-deadcode.DeadStores)
        tmp = UART1->DR;          // cppcheck-suppress redundantInitialization
        (void)tmp;
        rx_overruns++;
        return;
    }
    if (__HAL_UART_FLAG(sr, HAL_UART_SR_RXNE_POS) != 0u) {
        (void)rb_push(&rx_rb, (uint8_t)UART1->DR);
    }
}

uint32_t uart1_overruns(void) { return rx_overruns; }

/* ---------------- frame parser ----------------
 * frame = [0xA5][type][crc8][rsv][n_rec: u32 LE][n_rec * 6-byte records]       */
#define FRAME_SOF 0xA5u
#define REC_SIZE 6u
#define FRAME_HDR 8u

typedef struct {
    uint8_t type;
    uint32_t n_rec;
    uint8_t *recs;
} packet_t;

static uint8_t crc8(const uint8_t *p, size_t n)
{
    uint8_t c = 0;
    for (size_t i = 0; i < n; i++) {
        c = (uint8_t)(c ^ p[i]);
        for (int k = 0; k < 8; k++) {
            uint32_t w = (uint32_t)c << 1;
            c = (uint8_t)(((c & 0x80u) != 0u) ? (w ^ 0x07u) : w);
        }
    }
    return c;
}

void packet_free(packet_t *pkt);

packet_t *parse_frame(const uint8_t *buf, size_t len)
{
    if (len < FRAME_HDR || buf[0] != FRAME_SOF) {
        return NULL;
    }
    uint32_t n_rec = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                     ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
    if (n_rec > (len - FRAME_HDR) / REC_SIZE) { /* FIX08: divide, never multiply-then-compare */
        return NULL;
    }
    size_t body = (size_t)n_rec * REC_SIZE;
    packet_t *pkt = malloc(sizeof *pkt);
    if (pkt == NULL) {
        return NULL;
    }
    pkt->recs = NULL; /* zero-record frames carry no body: never malloc(0) */
    if (body > 0u) {
        pkt->recs = malloc(body);
        if (pkt->recs == NULL) {
            free(pkt);
            return NULL;
        }
        memcpy(pkt->recs, buf + FRAME_HDR, body);
    }
    if (crc8(buf + FRAME_HDR, body) != buf[2]) {
        packet_free(pkt); /* FIX02 */
        return NULL;
    }
    pkt->type = buf[1];
    pkt->n_rec = n_rec;
    return pkt;
}

void packet_free(packet_t *pkt)
{
    if (pkt != NULL) {
        free(pkt->recs);
        free(pkt);
    }
}

/* ---------------- command table ---------------- */
typedef struct {
    const char *name;
    int (*fn)(const packet_t *pkt);
} cmd_t;

static int cmd_ping(const packet_t *pkt) { return (int)pkt->n_rec; }
static int cmd_reset(const packet_t *pkt) { return pkt->n_rec == 0u ? 0 : -1; }
static int cmd_stats(const packet_t *pkt) { return pkt->n_rec > 0u ? (int)pkt->recs[0] : -1; }

#define CMD_COUNT 3u
static const cmd_t cmd_table[CMD_COUNT] = {
    {"ping", cmd_ping},
    {"reset", cmd_reset},
    {"stats", cmd_stats},
};

static uint32_t cmd_hits[CMD_COUNT];

static const cmd_t *cmd_lookup(uint8_t id)
{
    if (id >= CMD_COUNT) { /* FIX03 */
        return NULL;
    }
    cmd_hits[id]++;
    return &cmd_table[id];
}

uint32_t cmd_hit_count(uint8_t id) { return id < CMD_COUNT ? cmd_hits[id] : 0u; }

int dispatch(packet_t *pkt)
{
    const cmd_t *cmd = cmd_lookup(pkt->type);
    if (cmd == NULL) {
        fw_log("cmd %u unknown\n", pkt->type); /* FIX01 */
        packet_free(pkt);
        return -1;
    }
    int rc = cmd->fn(pkt);
    if (rc < 0) {
        fw_log("cmd %s failed, type %u\n", cmd->name, pkt->type); /* FIX09: log first */
        packet_free(pkt);
        return rc;
    }
    packet_free(pkt);
    return rc;
}

/* ---------------- link state machine ---------------- */
typedef enum { LINK_DOWN, LINK_UP, LINK_DEGRADED } link_state_t;

extern int flash_write(uint32_t addr, const void *src, size_t n);
extern int flash_verify(uint32_t addr, const void *src, size_t n);

static link_state_t link_state = LINK_DOWN;
static uint64_t uptime_ms;
static const int16_t RSSI_FLOOR_DBM = -90; /* FIX05: signed, like the RSSI */

void link_tick_1ms(void) { uptime_ms++; }

link_state_t link_step(int16_t rssi_dbm)
{
    switch (link_state) {
    case LINK_DOWN:
        if (rssi_dbm > -80) {
            link_state = LINK_UP;
        }
        break;
    case LINK_UP:
        if (rssi_dbm < RSSI_FLOOR_DBM) {
            link_state = LINK_DEGRADED;
        }
        break;
    case LINK_DEGRADED:
        if (rssi_dbm > -70) {
            link_state = LINK_UP;
        }
        break;
    }
    return link_state;
}

int link_save_stats(uint32_t addr)
{
    char line[48];
    int n = snprintf(line, sizeof line, "up %" PRIu64 " ms state %d\n", uptime_ms, (int)link_state); /* FIX10 */
    if (n < 0 || (size_t)n >= sizeof line) {
        return -1;
    }
    int err = flash_write(addr, line, (size_t)n);
    if (err == 0) { /* FIX06 */
        err = flash_verify(addr, line, (size_t)n);
    }
    return err;
}

/* ---------------- task entry: drain RX bytes into a frame ---------------- */
int link_poll(void)
{
    uint8_t frame[FRAME_HDR + 255u * REC_SIZE];
    size_t len = 0;
    uint8_t b;
    while (len < sizeof frame && rb_pop(&rx_rb, &b) == 0) {
        frame[len++] = b;
    }
    if (len == 0) {
        return 0;
    }
    packet_t *pkt = parse_frame(frame, len);
    if (pkt == NULL) {
        return -1;
    }
    return dispatch(pkt);
}
