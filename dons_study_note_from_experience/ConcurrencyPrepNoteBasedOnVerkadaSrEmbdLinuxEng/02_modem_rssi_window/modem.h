#ifndef MODEM_H
#define MODEM_H

#include <stdint.h>

/* Status returned by modem_poll_rssi(). */
enum {
    MODEM_ERROR = 0,   /* AT command failed / timed out: rssi_dbm, sinr_db and
                        * timestamp are GARBAGE. Do not publish them.        */
    MODEM_BUSY  = 1,   /* radio had nothing new to report. The value you got
                        * last time is still the current one. The payload
                        * fields are GARBAGE in this case too.               */
    MODEM_OK    = 2,   /* fresh measurement in rssi_dbm / sinr_db / timestamp */
};

/* One signal-quality measurement from the LTE modem. */
typedef struct RssiSample {
    int      status;     /* MODEM_ERROR / MODEM_BUSY / MODEM_OK          */
    int      rssi_dbm;   /* received power, dBm. Typically -115 .. -55   */
    int      sinr_db;    /* signal to interference+noise, dB. -10 .. 30  */
    uint64_t timestamp;  /* microseconds, same clock as modem_now_us()   */
} RssiSample;

/* Poll the modem for signal quality.
 *
 *  - BLOCKS between 0 and 100 ms (the real AT+CSQ round trip on this module is
 *    up to ~1 s; scaled down here so the test finishes quickly).
 *  - Returns MODEM_BUSY fairly often (roughly half the polls): the radio has
 *    not produced a new measurement yet, the previous one is still valid.
 *  - Returns MODEM_ERROR occasionally, immediately, with garbage payload.
 *  - NOT thread-safe: it keeps static state and its own RNG. Exactly ONE
 *    thread may ever call it.
 *  - The FIRST call returns MODEM_OK immediately, without blocking.
 */
struct RssiSample modem_poll_rssi(void);

/* Monotonic-ish wall clock in microseconds (gettimeofday based, like the
 * vendor's own timestamps). Cheap, callable from any thread. */
uint64_t modem_now_us(void);

#endif /* MODEM_H */
