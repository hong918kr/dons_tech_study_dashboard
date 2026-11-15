/* TOOL-B12 — firmware helper module checked with UBSan.
 * Freestanding on purpose: only <stdint.h>/<stddef.h>/<stdbool.h>, no libc,
 * so the same file also compiles for --target=thumbv7em-none-eabi (Part C). */
#ifndef FWUB_H
#define FWUB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Part A: arithmetic ------------------------------------------------ */
/* (raw * num) / den computed exactly, result saturated to int32_t, den == 0 -> 0 */
int32_t adc_scale(int32_t raw, int32_t num, int32_t den);
/* saturating integrator step: acc + x clamped to [INT32_MIN, INT32_MAX] */
int32_t accum_add(int32_t acc, int32_t x);
/* |x| as unsigned, exact for every int32_t (mag(INT32_MIN) == 2147483648u) */
uint32_t sample_mag(int32_t x);
/* register field mask: `width` ones starting at bit `lsb`
 * (1 <= width <= 32, lsb + width <= 32) */
uint32_t reg_mask(unsigned lsb, unsigned width);
/* left-align an `adc_bits`-bit sample (1..24) into 16 bits:
 * 12-bit 0xFFF -> 0xFFF0, 18-bit 0x3FFFF -> 0xFFFF (extra LSBs dropped) */
uint16_t sample_to_u16(uint32_t raw, int adc_bits);

/* ---- Part B: memory and representation -------------------------------- */
struct pkt_hdr {
    uint8_t  type;
    uint32_t seq;  /* wire bytes [1..4], little-endian, no alignment */
    bool     ack;  /* wire byte  [5]: must be 0 or 1                 */
    uint16_t len;  /* wire bytes [6..7], little-endian               */
};
#define PKT_HDR_WIRE 8u
/* 0 = ok, -1 = n < PKT_HDR_WIRE, -2 = ack byte not 0/1 */
int pkt_parse(const uint8_t *buf, size_t n, struct pkt_hdr *out);

enum pwr_mode { PWR_RUN = 0, PWR_SLEEP = 1, PWR_STOP = 2 };
/* name for a mode byte from the wire; unknown values -> "?" */
const char *pwr_mode_name(uint8_t wire);

struct cal { int32_t offset; };
/* apply calibration; c == NULL means "not loaded yet" -> identity */
int32_t cal_apply(const struct cal *c, int32_t x);

/* 16-entry thermistor table indexed by adc12 >> 8; glitch values
 * above 12 bits use the last entry */
uint16_t therm_lookup(uint16_t adc12);

/* ---- Part C: float -> int (control loop / telemetry) -------------------- */
/* degrees C -> centi-degrees, saturated to int16_t, NaN -> 0 */
int16_t temp_to_centi(float deg_c);
/* duty 0.0..1.0 -> timer compare value in [0, arr]; out of range clamps, NaN -> 0 */
uint16_t duty_to_ccr(float duty, uint16_t arr);

#endif
