#ifndef HWWDT_H
#define HWWDT_H

#include <stdint.h>

/* hwwdt.h — SoC hardware-watchdog vendor header (GIVEN; do not modify).
 *
 * The real part has a 2 s window: if hwwdt_kick() is not called at least once
 * every 2 s the watchdog resets the board. The harness SCALES THE WHOLE PROBLEM
 * DOWN 20x (see question_note): HWWDT_TIMEOUT_US below is 100 ms, so the test
 * finishes in a few seconds instead of minutes. Nothing else changes.
 */

/* Deadline of the hardware watchdog. Real hardware: 2 s. Harness: 100 ms. */
#define HWWDT_TIMEOUT_US 100000ull

/* Pet the dog. Very cheap (a single MMIO write on real hardware).
 * Callable from any thread, but the register is NOT a synchronisation point:
 * you are expected to serialise the calls yourself. */
void hwwdt_kick(void);

/* Tell the platform that `worker_id` stopped checking in. On a real gateway
 * this ends up in a crash log / cloud event and may trigger a graceful reboot.
 * The harness simply records every call (time + id). */
void hwwdt_report_fault(uint32_t worker_id);

/* Microsecond clock shared by the vendor code, your code and the harness
 * (gettimeofday based, same on macOS and Linux). */
uint64_t hwwdt_now_us(void);

#endif /* HWWDT_H */
