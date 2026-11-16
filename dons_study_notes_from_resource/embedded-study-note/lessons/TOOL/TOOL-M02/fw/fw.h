/* TOOL-M02 demo firmware: shared declarations (Cortex-M4, -nostdlib). */
#ifndef FW_H
#define FW_H
#include <stddef.h>
#include <stdint.h>

#ifndef RX_BUF_SIZE
#define RX_BUF_SIZE 1024 /* v2 builds with -DRX_BUF_SIZE=4096 */
#endif

struct comm_stats {
	uint32_t baud;
	uint32_t frames_ok;
	uint32_t frames_bad;
	uint32_t overruns;
};

/* comm.c */
extern uint8_t g_rx_buf[RX_BUF_SIZE];
extern struct comm_stats g_comm_stats;
extern const char *const comm_cmd_names[];
void comm_rx_isr_push(uint8_t b);
int comm_parse_frame(const uint8_t *p, size_t n);

/* sensor.c */
int32_t sensor_filter_update(int32_t raw);

/* legacy_board.c (built with -fcommon -> COMMON symbols) */
extern int board_rev;
extern uint32_t board_scratch[64];
void legacy_board_init(void);

/* telemetry.c (v2 only) */
void telemetry_sample_and_pack(int32_t value, uint32_t uptime_s);

/* libutil.a */
uint16_t crc16_ccitt(const uint8_t *p, size_t n);
size_t fmt_u32(char *out, uint32_t v);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);

#endif
