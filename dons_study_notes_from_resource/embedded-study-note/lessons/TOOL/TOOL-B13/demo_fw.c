/* TOOL-B13 demo_fw.c — 펌웨어 모듈 하나(UART + Flash + 센서)에 로그 13개 (E 3 · W 3 · I 3 · D 4).
 * demo_size.sh 가 LOG_LEVEL 0..4, 스타일(if0 / void0 / runtime) 별로 -c 빌드해서 크기를 잰다.
 * freestanding: <stdint.h> 만 쓴다. log_emit 은 다른 파일(log.c)에 있다고 가정 → extern.
 *
 *   -DLOG_STYLE_IF0     (기본) 꺼진 레벨 = do { if (0) log_emit(...); } while (0)
 *   -DLOG_STYLE_VOID0          꺼진 레벨 = ((void)0)   — 크기는 같지만 포맷 검사·변수 사용이 사라짐
 *   -DLOG_STYLE_RUNTIME        컴파일 타임 필터 없음, 런타임 변수로만 거름 (= 전부 바이너리에 남음)
 *   -DLOG_STYLE_BOTH           컴파일 타임 상한 && 런타임 레벨 (solution.c 의 방식)
 *   -DLOG_FILE_FULL            __FILE__ (전체 경로) 사용 — 기본은 __FILE_NAME__ (basename)
 */
#include <stdint.h>

enum { LOG_LVL_NONE = 0, LOG_LVL_ERR = 1, LOG_LVL_WRN = 2, LOG_LVL_INF = 3, LOG_LVL_DBG = 4 };
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LVL_INF
#endif

void log_emit(int lvl, const char *file, int line, const char *func, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));
extern volatile int g_log_runtime_level;

#if defined(LOG_FILE_FULL) || !defined(__FILE_NAME__)
#define LOG_FILE_ __FILE__
#else
#define LOG_FILE_ __FILE_NAME__
#endif

#define LOG_CALL_(lvl, ...) log_emit((lvl), LOG_FILE_, __LINE__, __func__, __VA_ARGS__)

#if defined(LOG_STYLE_RUNTIME)
#define LOG_AT_(lvl, ...) do { if ((lvl) <= g_log_runtime_level) LOG_CALL_(lvl, __VA_ARGS__); } while (0)
#elif defined(LOG_STYLE_BOTH)
#define LOG_AT_(lvl, ...) do { if ((lvl) <= LOG_LEVEL && (lvl) <= g_log_runtime_level) LOG_CALL_(lvl, __VA_ARGS__); } while (0)
#elif defined(LOG_STYLE_VOID0)
#define LOG_ON_(lvl, ...) do { LOG_CALL_(lvl, __VA_ARGS__); } while (0)
#else /* LOG_STYLE_IF0 */
#define LOG_AT_(lvl, ...) do { if ((lvl) <= LOG_LEVEL) LOG_CALL_(lvl, __VA_ARGS__); } while (0)
#endif

#if defined(LOG_STYLE_VOID0)   /* 옛날 스타일: 레벨마다 #if 로 통째로 지운다 */
#  if LOG_LEVEL >= 1
#    define LOG_E(...) LOG_ON_(LOG_LVL_ERR, __VA_ARGS__)
#  else
#    define LOG_E(...) ((void)0)
#  endif
#  if LOG_LEVEL >= 2
#    define LOG_W(...) LOG_ON_(LOG_LVL_WRN, __VA_ARGS__)
#  else
#    define LOG_W(...) ((void)0)
#  endif
#  if LOG_LEVEL >= 3
#    define LOG_I(...) LOG_ON_(LOG_LVL_INF, __VA_ARGS__)
#  else
#    define LOG_I(...) ((void)0)
#  endif
#  if LOG_LEVEL >= 4
#    define LOG_D(...) LOG_ON_(LOG_LVL_DBG, __VA_ARGS__)
#  else
#    define LOG_D(...) ((void)0)
#  endif
#else
#  define LOG_E(...) LOG_AT_(LOG_LVL_ERR, __VA_ARGS__)
#  define LOG_W(...) LOG_AT_(LOG_LVL_WRN, __VA_ARGS__)
#  define LOG_I(...) LOG_AT_(LOG_LVL_INF, __VA_ARGS__)
#  define LOG_D(...) LOG_AT_(LOG_LVL_DBG, __VA_ARGS__)
#endif

/* ---- 가짜 하드웨어 ---- */
#define UART1_SR  (*(volatile uint32_t *)0x40011000u)
#define UART1_DR  (*(volatile uint32_t *)0x40011004u)
#define FLASH_SR  (*(volatile uint32_t *)0x40023C0Cu)
#define FLASH_CR  (*(volatile uint32_t *)0x40023C10u)
#define ADC1_DR   (*(volatile uint32_t *)0x4001204Cu)
extern uint32_t now_ms(void);

int uart_send(const uint8_t *p, unsigned n) {
    LOG_D("uart_send n=%u first=0x%02x", n, n ? p[0] : 0u);
    for (unsigned i = 0; i < n; i++) {
        unsigned spin = 0;
        while ((UART1_SR & 0x80u) == 0u) {
            if (++spin > 100000u) {
                LOG_E("uart tx timeout at byte %u/%u", i, n);
                return -1;
            }
        }
        UART1_DR = p[i];
    }
    LOG_D("uart_send done");
    return 0;
}

int flash_erase(unsigned sector) {
    uint32_t t0 = now_ms();
    LOG_D("erase sector %u start, SR=0x%08lx", sector, (unsigned long)FLASH_SR);
    if (sector >= 8u) {
        LOG_E("bad sector %u (max 7)", sector);
        return -1;
    }
    if (sector == 0u)
        LOG_W("erasing boot sector 0");
    FLASH_CR = (sector << 3) | 0x2u | 0x10000u;
    while (FLASH_SR & 0x10000u) { }
    if (FLASH_SR & 0xF0u) {
        LOG_E("erase error SR=0x%08lx", (unsigned long)FLASH_SR);
        return -2;
    }
    LOG_I("sector %u erased in %lu ms", sector, (unsigned long)(now_ms() - t0));
    return 0;
}

int sensor_read(int *out_mc) {
    uint32_t raw = ADC1_DR & 0xFFFu;
    int mc = (int)(raw * 3300u / 4095u) - 500;
    LOG_D("adc raw=%lu -> %d mC", (unsigned long)raw, mc);
    if (raw == 0u || raw == 0xFFFu) {
        LOG_W("adc rail value %lu, sensor open/short?", (unsigned long)raw);
        return -1;
    }
    if (mc > 85000)
        LOG_W("over temperature %d mC", mc);
    *out_mc = mc;
    LOG_I("temp %d mC", mc);
    return 0;
}

void fw_boot_banner(void) {
    LOG_I("fw v1.4.2 boot, reset cause 0x%02x", 0x08u);
}

#ifdef LOG_DEMO_FMT_BUG        /* demo_fmt.sh: 꺼진 레벨의 포맷 오류가 보이나? */
void fmt_bug(void) {
    LOG_D("%s", 42);
}
#endif
