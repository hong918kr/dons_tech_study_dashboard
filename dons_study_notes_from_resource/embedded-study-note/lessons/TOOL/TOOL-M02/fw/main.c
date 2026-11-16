/* TOOL-M02 demo firmware: main loop. */
#include "fw.h"

volatile uint64_t g_uptime_ms; /* SysTick would advance this */
volatile int32_t g_adc_raw;
char g_banner[24] = "fw demo";

static uint32_t uptime_seconds(void)
{
	return (uint32_t)(g_uptime_ms / 1000u); /* 64-bit divide -> libgcc __aeabi_uldivmod */
}

int main(void)
{
	char line[16];
	legacy_board_init();
	for (;;) {
		int32_t v = sensor_filter_update(g_adc_raw);
		uint32_t up = uptime_seconds();
		fmt_u32(line, up);
		g_banner[8] = line[0];
		g_banner[9] = comm_cmd_names[(uint32_t)v % 5u][0];
		if (comm_parse_frame(g_rx_buf, sizeof g_rx_buf) == 0)
			comm_rx_isr_push((uint8_t)v);
#ifdef FEATURE_TELEMETRY
		telemetry_sample_and_pack(v, up);
#endif
		if (board_rev == 0)
			break;
	}
	return 0;
}
