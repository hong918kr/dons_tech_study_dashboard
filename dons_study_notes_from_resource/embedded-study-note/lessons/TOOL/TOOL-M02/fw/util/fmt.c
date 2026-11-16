/* libutil.a member: tiny unsigned decimal formatter (no printf). */
#include "../fw.h"

size_t fmt_u32(char *out, uint32_t v)
{
	char tmp[10];
	size_t n = 0;
	do {
		tmp[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	for (size_t i = 0; i < n; i++)
		out[i] = tmp[n - 1 - i];
	out[n] = '\0';
	return n;
}

/* Same member, never called: pulled in with the member, then gc'd. */
size_t fmt_hex32_unused(char *out, uint32_t v)
{
	for (int i = 0; i < 8; i++)
		out[i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 0xf];
	out[8] = '\0';
	return 8;
}
