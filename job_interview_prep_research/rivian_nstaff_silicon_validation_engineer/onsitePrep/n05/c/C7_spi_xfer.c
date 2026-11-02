/* C7 - SPI full-duplex transfer: feed TX FIFO, drain RX FIFO, cap bytes in flight at FIFO depth. */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/* ---- HW model (test only) ----
 * 0x00 DATA : write = push TX FIFO; read = pop RX FIFO
 * 0x04 STAT : bit0 TXFULL, bit1 RXEMPTY
 * TX and RX FIFO depth 4. Fast SPI clock: on EVERY bus access the shifter moves one
 * byte TX -> wire -> RX (slave answers ~tx). If RX is full the byte is LOST (overrun),
 * like most real SPI blocks - the shifter does not wait for software.             */
#define SPI_DATA 0x00u
#define SPI_STAT 0x04u
#define ST_TXFULL  (1u << 0)
#define ST_RXEMPTY (1u << 1)
#define SPI_FIFO_DEPTH 4u
#define SPI_IDLE_MAX   1000u
static uint8_t m_tx[SPI_FIFO_DEPTH], m_rx[SPI_FIFO_DEPTH];
static unsigned m_txn, m_rxn, m_ovr;
static bool m_stalled;
static void shifter(void)
{
    if (!m_txn || m_stalled) return;
    uint8_t in = (uint8_t)~m_tx[0];
    memmove(m_tx, m_tx + 1, --m_txn);
    if (m_rxn == SPI_FIFO_DEPTH) m_ovr++; else m_rx[m_rxn++] = in;
}
uint32_t reg_read(uint32_t off)
{
    uint32_t v;
    if (off == SPI_STAT) {
        v = (m_txn == SPI_FIFO_DEPTH ? ST_TXFULL : 0) | (m_rxn == 0 ? ST_RXEMPTY : 0);
    } else {
        v = m_rx[0];
        if (m_rxn) memmove(m_rx, m_rx + 1, --m_rxn);
    }
    shifter();
    return v;
}
void reg_write(uint32_t off, uint32_t val)
{
    if (off == SPI_DATA && m_txn < SPI_FIFO_DEPTH) m_tx[m_txn++] = (uint8_t)val;
    shifter();
}
static void model_reset(void) { m_txn = m_rxn = m_ovr = 0; m_stalled = false; }

/* ==== ANSWER BEGIN ==== */
uint32_t reg_read(uint32_t off);
void     reg_write(uint32_t off, uint32_t val);

/* tx == NULL: send 0xFF dummies (read-only). rx == NULL: discard (write-only).
 * Returns 0, or -1 if the engine makes no progress for SPI_IDLE_MAX rounds. */
int spi_xfer(const uint8_t *tx, uint8_t *rx, size_t n)
{
    size_t sent = 0, recvd = 0;
    uint32_t idle = 0;
    while (recvd < n) {
        size_t before = sent + recvd;
        /* fill: TX has room AND in-flight (sent - recvd) < depth, else RX overruns */
        while (sent < n && sent - recvd < SPI_FIFO_DEPTH &&
               !(reg_read(SPI_STAT) & ST_TXFULL)) {
            reg_write(SPI_DATA, tx ? tx[sent] : 0xFFu);
            sent++;
        }
        /* drain: every TX byte clocks exactly one RX byte back */
        while (recvd < sent && !(reg_read(SPI_STAT) & ST_RXEMPTY)) {
            uint8_t b = (uint8_t)reg_read(SPI_DATA);
            if (rx)
                rx[recvd] = b;
            recvd++;
        }
        idle = (sent + recvd == before) ? idle + 1 : 0;
        if (idle > SPI_IDLE_MAX)
            return -1;                          /* clock gated? block in reset? */
    }
    return 0;
}
/* ==== ANSWER END ==== */

/* Naive: push everything first, then read -> RX overrun once n > depth,
 * and the read loop would then wait forever for the lost bytes (bounded here). */
static size_t spi_xfer_naive(const uint8_t *tx, uint8_t *rx, size_t n)
{
    size_t got = 0;
    for (size_t i = 0; i < n; i++) {
        while (reg_read(SPI_STAT) & ST_TXFULL) {}
        reg_write(SPI_DATA, tx[i]);
    }
    for (unsigned spins = 0; got < n && spins < 100; spins++)
        if (!(reg_read(SPI_STAT) & ST_RXEMPTY))
            rx[got++] = (uint8_t)reg_read(SPI_DATA);
    return got;
}

int main(void)
{
    uint8_t tx[10], rx[10];
    for (int i = 0; i < 10; i++) tx[i] = (uint8_t)(0x10 + i);

    /* n = 10 > depth 4: all bytes, in order, no overrun, in-flight capped */
    model_reset(); memset(rx, 0, sizeof rx);
    assert(spi_xfer(tx, rx, 10) == 0);
    for (int i = 0; i < 10; i++) assert(rx[i] == (uint8_t)~tx[i]);
    assert(m_ovr == 0);

    /* n = 0: no bus access needed, returns 0 */
    model_reset();
    assert(spi_xfer(tx, rx, 0) == 0);

    /* n = 1 and n = depth exactly */
    model_reset(); assert(spi_xfer(tx, rx, 1) == 0 && rx[0] == (uint8_t)~0x10);
    model_reset(); assert(spi_xfer(tx, rx, 4) == 0 && rx[3] == (uint8_t)~0x13 && m_ovr == 0);

    /* read-only (tx NULL -> 0xFF dummies -> slave answers 0x00) and write-only (rx NULL) */
    model_reset(); memset(rx, 0xAA, sizeof rx);
    assert(spi_xfer(NULL, rx, 6) == 0);
    for (int i = 0; i < 6; i++) assert(rx[i] == 0x00);
    model_reset(); assert(spi_xfer(tx, NULL, 7) == 0 && m_ovr == 0 && m_rxn == 0);

    /* stalled engine: bounded, returns -1 */
    model_reset(); m_stalled = true;
    assert(spi_xfer(tx, rx, 3) == -1);

    /* naive version loses bytes */
    model_reset();
    assert(spi_xfer_naive(tx, rx, 6) < 6);
    assert(m_ovr > 0);

    printf("C7 PASS\n");
    return 0;
}
