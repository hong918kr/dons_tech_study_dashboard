/* ENV: board */
/* BM-A10 DWT 사이클 카운터 — 보드 펌웨어(latency_fw.c)와 QEMU 확인 펌웨어(qemu_cyccnt_probe.c)가
 * 똑같이 쓰는 코드. 주소·비트는 ARMv7-M ARM C1.6 (DWT), C1.8 (DEMCR) 기준 대표값 — 문서 확인 권장.
 *
 *   DEMCR   0xE000EDFC  bit24 TRCENA   : DWT/ITM 전원(클럭) 켜기. 이게 0 이면 DWT 레지스터는 동작 안 함
 *   DWT_CTRL 0xE0001000 bit0 CYCCNTENA : 사이클 카운터 동작
 *                       bit25 NOCYCCNT : 1 = 이 구현엔 CYCCNT 가 없음 (읽기 전용)
 *   DWT_CYCCNT 0xE0001004              : 32 비트, 코어 클럭마다 +1, 2^32 에서 0 으로 감김
 *   DWT_LAR 0xE0001FB0 / DWT_LSR 0xE0001FB4 : CoreSight 소프트웨어 잠금. Cortex-M7 (일부 부품) 은
 *            0xC5ACCE55 를 LAR 에 써야 DWT 쓰기가 먹는다. M3/M4 에선 대개 구현 안 됨(LSR=0, 쓰기 무시) — 확인 권장.
 */
#ifndef BM_A10_DWT_CYCCNT_H
#define BM_A10_DWT_CYCCNT_H
#include <stdint.h>

#define A10_REG(a)      (*(volatile uint32_t *)(uintptr_t)(a))
#define DEMCR           A10_REG(0xE000EDFCu)
#define DWT_CTRL        A10_REG(0xE0001000u)
#define DWT_CYCCNT      A10_REG(0xE0001004u)
#define DWT_LAR         A10_REG(0xE0001FB0u)
#define DWT_LSR         A10_REG(0xE0001FB4u)
#define DEMCR_TRCENA    (1u << 24)
#define DWT_CYCCNTENA   (1u << 0)
#define DWT_NOCYCCNT    (1u << 25)
#define DWT_LAR_KEY     0xC5ACCE55u

/* 반환: 0 = CYCCNT 가 실제로 센다, 1 = 구현 없음(NOCYCCNT), 2 = 켰는데 값이 안 변함(멈춤 — QEMU 가 이 경우) */
static inline int dwt_cyccnt_init(void)
{
    DEMCR |= DEMCR_TRCENA;                 /* 1) 추적 블록 켜기 — 빠뜨리면 CYCCNT 는 0 그대로 */
    DWT_LAR = DWT_LAR_KEY;                 /* 2) M7 잠금 해제 (M3/M4 에선 무해한 쓰기로 가정 — 확인 권장) */
    if (DWT_CTRL & DWT_NOCYCCNT) return 1;
    DWT_CYCCNT = 0;
    DWT_CTRL |= DWT_CYCCNTENA;             /* 3) 카운터 시작 */
    uint32_t a = DWT_CYCCNT;
    for (volatile int i = 0; i < 100; ++i) { }
    uint32_t b = DWT_CYCCNT;
    return (a == b) ? 2 : 0;               /* 4) 자기검사: 100 번 도는 동안 안 변하면 믿지 않는다 */
}

/* 감김 안전 차이: 부호 없는 뺄셈은 2^32 를 법으로 정확 (구간 < 2^32 사이클일 때) */
static inline uint32_t cyc_delta(uint32_t t0, uint32_t t1) { return t1 - t0; }

#endif
