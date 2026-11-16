/* TOOL-M02 demo firmware: v2 feature - telemetry frames. */
#include "fw.h"

uint8_t telemetry_frame_buffer[2048];
static uint32_t s_tm_pos;
const char *const telemetry_channel_names[] = { "vbat", "temp", "adc0", "adc1", "rssi", "uptime" };

void telemetry_sample_and_pack(int32_t value, uint32_t uptime_s)
{
	char tmp[12];
	size_t n = fmt_u32(tmp, (uint32_t)value);
	if (s_tm_pos + n + 8 > sizeof telemetry_frame_buffer)
		s_tm_pos = 0;
	memcpy(&telemetry_frame_buffer[s_tm_pos], tmp, n);
	s_tm_pos += (uint32_t)n;
	n = fmt_u32(tmp, uptime_s);
	memcpy(&telemetry_frame_buffer[s_tm_pos], tmp, n);
	s_tm_pos += (uint32_t)n;
	telemetry_frame_buffer[s_tm_pos++] = (uint8_t)telemetry_channel_names[uptime_s % 6u][0];
}
