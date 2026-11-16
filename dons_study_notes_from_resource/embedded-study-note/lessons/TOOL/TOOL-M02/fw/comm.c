/* TOOL-M02 demo firmware: UART framing (rx buffer grows in v2). */
#include "fw.h"

uint8_t g_rx_buf[RX_BUF_SIZE];
static uint32_t s_rx_head;
struct comm_stats g_comm_stats = { .baud = 115200 };
const char *const comm_cmd_names[] = { "PING", "READ", "WRITE", "RESET", "BOOTLOADER" };

void comm_rx_isr_push(uint8_t b)
{
	if (s_rx_head >= RX_BUF_SIZE) {
		g_comm_stats.overruns++;
		return;
	}
	g_rx_buf[s_rx_head++] = b;
}

int comm_parse_frame(const uint8_t *p, size_t n)
{
	if (n < 4 || p[0] != 0x7e) {
		g_comm_stats.frames_bad++;
		return -1;
	}
	uint16_t want = (uint16_t)(p[n - 2] | (p[n - 1] << 8));
	if (crc16_ccitt(p + 1, n - 3) != want) {
		g_comm_stats.frames_bad++;
		return -2;
	}
	g_comm_stats.frames_ok++;
	s_rx_head = 0;
	return p[1] < 5 ? (int)p[1] : -3;
}
