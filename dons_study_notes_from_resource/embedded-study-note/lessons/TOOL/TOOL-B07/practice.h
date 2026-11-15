/* board_config.h — TOOL-B07 practice: value-style board selection (-DBOARD=1|2).
 *
 * Inputs (from the build system, -D on the command line):
 *   BOARD=1|2 (or BOARD_ID_A/B)  required           which PCB (old -DBOARD_A/-DBOARD_B rejected)
 *   BUILD_DEBUG | BUILD_RELEASE  exactly one        which build type
 *   FEATURE_USB=0|1              default 0          BOARD 2 only (BOARD 1 has no USB PHY)
 *   FEATURE_SHELL=0|1            default 0          never in BUILD_RELEASE
 *   LOG_LEVEL=0..4               default 4 (debug) / 2 (release)
 *   RX_BUF_SIZE=n                default 64 (1) / 256 (2), power of two >= 16, fits rx_idx_t
 * Outputs (what app code may use):
 *   IS_ENABLED(X) BOARD_NAME BUILD_NAME LED_PIN UART_BAUD LOG_LEVEL HAS_USB HAS_SHELL RX_BUF_SIZE rx_idx_t ASSERT()
 * Rule: every illegal combination must stop the build with a clear message.
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <stdint.h>

/* ---- 0. ids + IS_ENABLED(): 1 only if X is defined as 1 (Linux kconfig.h trick) */
#define BOARD_ID_A 1
#define BOARD_ID_B 2
#define BC_PLACEHOLDER_1 0,
#define BC_SECOND(ignored, val, ...) val
#define IS_ENABLED(x) BC_IS_ENABLED1(x)
#define BC_IS_ENABLED1(val) BC_IS_ENABLED2(BC_PLACEHOLDER_##val)
#define BC_IS_ENABLED2(arg1_or_junk) BC_SECOND(arg1_or_junk 1, 0, 0)

/* ---- 1. board: one value macro --------------------------------------------- */
#if defined(BOARD_A) || defined(BOARD_B)
#error "board_config: old flag -DBOARD_A/-DBOARD_B; use -DBOARD=1 (A) or -DBOARD=2 (B)"
#elif !defined(BOARD)
#error "board_config: BOARD is not set; use -DBOARD=1 (A) or -DBOARD=2 (B)"
#elif BOARD != BOARD_ID_A && BOARD != BOARD_ID_B
#error "board_config: BOARD must be 1 (A) or 2 (B)"
#endif
/* BOARD 2 is a Cortex-M4F: when compiling for 32-bit ARM, insist on the hard-float ABI */
#if BOARD == BOARD_ID_B && defined(__arm__) && !defined(__ARM_PCS_VFP)
#error "board_config: BOARD 2 (Cortex-M4F) needs -mfloat-abi=hard -mfpu=fpv4-sp-d16"
#endif

#if BOARD == BOARD_ID_A
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
#if FEATURE_USB && BOARD != BOARD_ID_B
#error "board_config: FEATURE_USB requires BOARD 2 (BOARD 1 has no USB PHY)"
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
#if LOG_LEVEL < 0 || LOG_LEVEL > 4
#error "board_config: LOG_LEVEL must be 0..4"
#endif

/* ---- 5. ASSERT: active in debug, compiled out in release ------------------ */
void assert_fail(const char *file, int line, const char *expr);
#if defined(BUILD_DEBUG)
#define ASSERT(c) do { if (!(c)) assert_fail(__FILE__, __LINE__, #c); } while (0)
#else
#define ASSERT(c) do { (void)sizeof(c); } while (0)
#endif

/* ---- 6. RX ring buffer size ------------------------------------------------ */
#ifndef RX_BUF_SIZE
#if BOARD == BOARD_ID_A
#define RX_BUF_SIZE 64
#else
#define RX_BUF_SIZE 256
#endif
#endif
#if RX_BUF_SIZE < 16 || (RX_BUF_SIZE & (RX_BUF_SIZE - 1)) != 0
#error "board_config: RX_BUF_SIZE must be a power of two >= 16"
#endif
_Static_assert((rx_idx_t)(RX_BUF_SIZE - 1) == RX_BUF_SIZE - 1,
               "board_config: RX_BUF_SIZE - 1 does not fit rx_idx_t");

#endif /* BOARD_CONFIG_H */
