/* UART RX via circular DMA (simulated): NDTR remaining count -> new data range, HT/TC/IDLE events. */
/*
 * On STM32 the DMA writes into rx_buf in circular mode and only exposes NDTR (items remaining
 * until wrap). Write position = SIZE - NDTR. The consumer keeps its own read position and, on
 * every Half-Transfer, Transfer-Complete or IDLE-line interrupt, processes [read_pos, write_pos)
 * with at most two chunks. All three events call the same function, so it doesn't matter which fired.
 * Constraint: process before the DMA laps the reader (more than SIZE bytes between events = data loss).
 * On cores with D-cache (e.g. Cortex-M7), invalidate the range before reading or place rx_buf in
 * non-cacheable memory, otherwise the CPU reads stale bytes.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RX_SIZE 32u

static uint8_t  rx_buf[RX_SIZE];         /* DMA target */
static uint32_t ndtr = RX_SIZE;          /* simulated DMA counter register */
static uint32_t read_pos;                /* owned by consumer */

static uint8_t  app[256];                /* where processed bytes go */
static uint32_t app_len;

static void consume(const uint8_t *p, uint32_t n)
{
    memcpy(&app[app_len], p, n);
    app_len += n;
}

/* Called from HT, TC and IDLE interrupts alike. */
static void uart_rx_dma_event(void)
{
    uint32_t write_pos = RX_SIZE - ndtr;
    if (write_pos == RX_SIZE)            /* NDTR==0 momentarily means "at the end" */
        write_pos = 0;
    if (write_pos == read_pos)
        return;
    if (write_pos > read_pos) {
        consume(&rx_buf[read_pos], write_pos - read_pos);
    } else {                             /* wrapped: tail of buffer, then the start */
        consume(&rx_buf[read_pos], RX_SIZE - read_pos);
        consume(&rx_buf[0], write_pos);
    }
    read_pos = write_pos;
}

/* Simulated hardware: DMA stores n bytes and decrements NDTR, reloading at wrap. */
static uint8_t g_next;
static void dma_receive(uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        rx_buf[RX_SIZE - ndtr] = g_next++;
        if (--ndtr == 0)
            ndtr = RX_SIZE;              /* circular reload */
    }
}

static void check_stream(void)
{
    for (uint32_t i = 0; i < app_len; i++)
        assert(app[i] == (uint8_t)i);
}

int main(void)
{
    dma_receive(5);   uart_rx_dma_event();               /* IDLE after a short frame */
    assert(app_len == 5);

    dma_receive(11);  uart_rx_dma_event();               /* reaches 16 = half transfer */
    assert(app_len == 16);

    dma_receive(16);  uart_rx_dma_event();               /* exactly at end: TC, NDTR reloads */
    assert(app_len == 32 && read_pos == 0);

    dma_receive(20);  uart_rx_dma_event();               /* plain progress */
    dma_receive(20);  uart_rx_dma_event();               /* crosses the wrap point */
    assert(app_len == 72);

    uart_rx_dma_event();                                 /* spurious event: no new data */
    assert(app_len == 72);

    for (int i = 0; i < 20; i++) {                       /* many small frames, multiple laps */
        dma_receive(7);
        uart_rx_dma_event();
    }
    assert(app_len == 212);
    check_stream();

    puts("dma_circular_rx: PASS");
    return 0;
}
