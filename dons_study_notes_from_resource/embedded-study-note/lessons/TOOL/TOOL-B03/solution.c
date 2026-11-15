/* TOOL-B03 — "debug build works, release build breaks"
 *
 * Four small firmware helpers. Each one hides undefined behaviour (UB), so the
 * result depends on the optimization level. check.sh builds this file at
 * -O0 -O1 -O2 -O3 -Os -Oz and demands the SAME, CORRECT output at every level,
 * plus a clean -fsanitize=address,undefined run.
 *
 *   bash check.sh starter     # some levels FAIL
 *   bash check.sh solution    # 6 levels identical + sanitizers clean
 *
 * TODO: rewrite the four function bodies UB-free. Keep the signatures.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NOINL __attribute__((noinline))

/* ---- Part B1: packet parser bounds check ---------------------------------
 * true  <=> bytes [off, off+len) fit inside a buffer of cap bytes.
 * off, len, cap come from a received header: any int32_t value is possible. */
NOINL bool range_ok(int32_t off, int32_t len, int32_t cap)
{
	/* all three are >= 0 after the first test, so cap - off cannot overflow */
	if (off < 0 || len < 0 || cap < 0) return false;
	if (off > cap) return false;
	return len <= cap - off;
}

/* ---- Part B2: frames left before the int32 sequence number wraps ---------
 * seq > 0. Count seq, seq+1, ..., INT32_MAX (stop after max frames). */
NOINL int frames_until_wrap(int32_t seq, int max)
{
	/* test for the wrap BEFORE the increment: s++ never overflows */
	int n = 0;
	for (int32_t s = seq; s > 0 && n < max; s++) {
		n++;
		if (s == INT32_MAX) break;
	}
	return n;
}

/* ---- Part B3: put a float sample into a 32-bit TX slot (DMA buffer) -----
 * Legacy driver: one slot seen through two typed pointers (word and sample
 * point to the SAME uint32_t). Returns the word that goes on the wire. */
struct tx_view {
	uint32_t *word;
	float *sample;
};

NOINL uint32_t pack_float_word(struct tx_view tx, float v)
{
	/* copy the bytes: memcpy may alias anything, compiles to one str */
	*tx.word = 0;
	memcpy(tx.word, &v, sizeof v);
	return *tx.word;
}

/* ---- Part B4: is this code in the calibration table? -------------------- */
static int16_t cal_codes[4] = { 100, 200, 300, 400 };

NOINL bool cal_has_code(int16_t code)
{
	/* bound from the array itself: i < 4 */
	for (size_t i = 0; i < sizeof cal_codes / sizeof cal_codes[0]; i++)
		if (cal_codes[i] == code)
			return true;
	return false;
}

/* ---- harness (same in starter / solution) -------------------------------- */
static int g_pass, g_total;

#define CHECK_EQ(expr, want)                                                   \
	do {                                                                   \
		long long got_ = (long long)(expr), want_ = (long long)(want); \
		g_total++;                                                     \
		if (got_ == want_) {                                           \
			g_pass++;                                              \
			printf("PASS %-44s = %lld\n", #expr, got_);            \
		} else {                                                       \
			printf("FAIL %-44s = %lld (want %lld)\n", #expr, got_, \
			       want_);                                         \
		}                                                              \
	} while (0)

int main(void)
{
	/* B1 */
	CHECK_EQ(range_ok(10, 100, 1000), true);
	CHECK_EQ(range_ok(0, 1000, 1000), true);
	CHECK_EQ(range_ok(950, 100, 1000), false);
	CHECK_EQ(range_ok(-1, 10, 1000), false);
	CHECK_EQ(range_ok(INT32_MAX - 10, 100, 1000), false);
	CHECK_EQ(range_ok(100, INT32_MAX, 1000), false);
	/* B2 */
	CHECK_EQ(frames_until_wrap(1, 3), 3);
	CHECK_EQ(frames_until_wrap(INT32_MAX, 10), 1);
	CHECK_EQ(frames_until_wrap(INT32_MAX - 5, 100), 6);
	/* B3 */
	uint32_t slot;
	struct tx_view tx = { &slot, (float *)&slot };
	CHECK_EQ(pack_float_word(tx, 1.5f), 0x3FC00000u);
	CHECK_EQ(pack_float_word(tx, -2.0f), 0xC0000000u);
	CHECK_EQ(slot, 0xC0000000u);
	/* B4 */
	CHECK_EQ(cal_has_code(300), true);
	CHECK_EQ(cal_has_code(400), true);
	CHECK_EQ(cal_has_code(0x5A5A), false);
	CHECK_EQ(cal_has_code(12345), false);

	printf("%d/%d passed\n", g_pass, g_total);
	return g_pass == g_total ? 0 : 1;
}
