/* board_config.h — TOOL-B07: one header, many build variants (copied as board_config.h by check.sh).
 *
 * Inputs (from the build system, -D on the command line):
 *   BOARD_A | BOARD_B            exactly one        which PCB
 *   BUILD_DEBUG | BUILD_RELEASE  exactly one        which build type
 *   FEATURE_USB=0|1              default 0          BOARD_B only (BOARD_A has no USB PHY)
 *   FEATURE_SHELL=0|1            default 0          never in BUILD_RELEASE
 *   LOG_LEVEL=0..4               default 4 (debug) / 2 (release)
 *   RX_BUF_SIZE=n                default 64 (A) / 256 (B), power of two >= 16, fits rx_idx_t
 * Outputs (what app code may use):
 *   BOARD_NAME BUILD_NAME LED_PIN UART_BAUD LOG_LEVEL HAS_USB HAS_SHELL RX_BUF_SIZE rx_idx_t ASSERT()
 * Rule: every illegal combination must stop the build with a clear message.
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <stdint.h>

/* ---- 1. board: exactly one ------------------------------------------------ */
/* TODO 1: both or none must be an #error (not a silent default) */
#if defined(BOARD_A) && defined(BOARD_B)
#error "board_config: define exactly one of BOARD_A / BOARD_B (got both)"
#elif !defined(BOARD_A) && !defined(BOARD_B)
#error "board_config: define exactly one of BOARD_A / BOARD_B (got none)"
#endif

#if defined(BOARD_A)
#define BOARD_NAME "A"
#define LED_PIN 5            /* PA5 */
#define UART_BAUD 115200     /* ST-LINK virtual COM port */
typedef uint8_t rx_idx_t;    /* small MCU: 1-byte ring index */
#else
#define BOARD_NAME "B"
#define LED_PIN 13           /* PC13 */
#define UART_BAUD 921600     /* FTDI bridge */
typedef uint16_t rx_idx_t;
#endif

/* ---- 2. build type: exactly one ------------------------------------------- */
/* TODO 2: both or none must be an #error */
#if defined(BUILD_DEBUG) && defined(BUILD_RELEASE)
#error "board_config: BUILD_DEBUG and BUILD_RELEASE are mutually exclusive"
#elif !defined(BUILD_DEBUG) && !defined(BUILD_RELEASE)
#error "board_config: define BUILD_DEBUG or BUILD_RELEASE"
#endif

#if defined(BUILD_DEBUG)
#define BUILD_NAME "debug"
#else
#define BUILD_NAME "release"
#endif

/* ---- 3. feature flags: input FEATURE_X = 0|1, output HAS_X = 0|1 ---------- */
/* TODO 3: -DFEATURE_USB=0 must mean OFF; only 0/1 allowed;
 *         USB only on BOARD_B; shell never in release (#error each) */
#ifndef FEATURE_USB
#define FEATURE_USB 0
#endif
#ifndef FEATURE_SHELL
#define FEATURE_SHELL 0
#endif
#if FEATURE_USB != 0 && FEATURE_USB != 1
#error "board_config: FEATURE_USB must be 0 or 1"
#endif
#if FEATURE_SHELL != 0 && FEATURE_SHELL != 1
#error "board_config: FEATURE_SHELL must be 0 or 1"
#endif
#if FEATURE_USB && !defined(BOARD_B)
#error "board_config: FEATURE_USB requires BOARD_B (BOARD_A has no USB PHY)"
#endif
#if FEATURE_SHELL && defined(BUILD_RELEASE)
#error "board_config: FEATURE_SHELL is not allowed in BUILD_RELEASE"
#endif

#if FEATURE_USB
#define HAS_USB 1
#else
#define HAS_USB 0
#endif
#if FEATURE_SHELL
#define HAS_SHELL 1
#else
#define HAS_SHELL 0
#endif

/* ---- 4. log level ---------------------------------------------------------- */
#ifndef LOG_LEVEL
#if defined(BUILD_DEBUG)
#define LOG_LEVEL 4
#else
#define LOG_LEVEL 2
#endif
#endif
/* TODO 4: LOG_LEVEL outside 0..4 must be an #error */
#if LOG_LEVEL < 0 || LOG_LEVEL > 4
#error "board_config: LOG_LEVEL must be 0..4"
#endif

/* ---- 5. ASSERT: active in debug, compiled out in release ------------------ */
void assert_fail(const char *file, int line, const char *expr);
/* TODO 5: in BUILD_RELEASE, ASSERT(c) must leave no call and must not evaluate c */
#if defined(BUILD_DEBUG)
#define ASSERT(c) do { if (!(c)) assert_fail(__FILE__, __LINE__, #c); } while (0)
#else
#define ASSERT(c) do { (void)sizeof(c); } while (0)
#endif

/* ---- 6. RX ring buffer size ------------------------------------------------ */
#ifndef RX_BUF_SIZE
#if defined(BOARD_A)
#define RX_BUF_SIZE 64
#else
#define RX_BUF_SIZE 256
#endif
#endif
/* TODO 6: power of two >= 16 -> #error (the preprocessor can test numbers);
 *         RX_BUF_SIZE - 1 must fit rx_idx_t -> _Static_assert (it needs a type) */
#if RX_BUF_SIZE < 16 || (RX_BUF_SIZE & (RX_BUF_SIZE - 1)) != 0
#error "board_config: RX_BUF_SIZE must be a power of two >= 16"
#endif
_Static_assert((rx_idx_t)(RX_BUF_SIZE - 1) == RX_BUF_SIZE - 1,
               "board_config: RX_BUF_SIZE - 1 does not fit rx_idx_t");

#endif /* BOARD_CONFIG_H */
