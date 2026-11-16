/* TOOL-M06 the APP's own helper. Same name as libfoo's internal helper (a
 * copy of an old company util), different rounding: truncates. */
#include <stdint.h>

int32_t round_div(int64_t num, uint32_t den)
{
	return (int32_t)(num / (int64_t)den);
}
