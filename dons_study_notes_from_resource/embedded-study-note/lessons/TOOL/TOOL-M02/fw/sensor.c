/* TOOL-M02 demo firmware: ADC filter with a calibration table. */
#include "fw.h"

#define C4(b) (b), (b) + 3, (b) + 7, (b) + 12
#define C16(b) C4(b), C4((b) + 20), C4((b) + 40), C4((b) + 60)
#define C64(b) C16(b), C16((b) + 100), C16((b) + 200), C16((b) + 300)
const uint16_t sensor_calibration_table[256] = { C64(0), C64(400), C64(800), C64(1200) };

static int32_t s_filter_acc;

int32_t sensor_filter_update(int32_t raw)
{
	s_filter_acc += (raw - s_filter_acc) >> 3;
	return s_filter_acc + sensor_calibration_table[raw & 0xff];
}

/* Never called: --gc-sections drops it ("Discarded input sections"). */
int sensor_selftest_unused(void)
{
	int bad = 0;
	for (int i = 1; i < 256; i++)
		bad += sensor_calibration_table[i] < sensor_calibration_table[i - 1];
	return bad;
}

/* Short name, also never called: its discarded line is NOT wrapped in the map. */
int bist(void)
{
	return sensor_calibration_table[255] == 1272 ? 0 : -1;
}
