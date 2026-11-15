/* ENV: qemu */
/* BM-B05 예제 펌웨어 — map/size 출력을 캡처하려고 Lima VM 에서 빌드한다 (run_vm.sh).
 * Cortex-M4, STM32F411RE 급 메모리 (fw.ld: Flash 512 KB @0x08000000, RAM 128 KB @0x20000000).
 *   -DBAD : 실수 버전 — 큰 룩업 테이블과 포인터 표에서 const 를 빠뜨렸다.
 * lut_gen.h 는 run_vm.sh 가 awk 로 만든다 (4096 개 12-bit 사인 값 = DAC 파형 테이블).
 * 실행도 된다: QEMU -M netduinoplus2 (STM32F405, 같은 0x08000000/0x20000000 주소) + semihosting 종료 코드.
 */
#include <stdint.h>

#ifdef BAD
#define LUT_CONST                       /* ← 실수: const 없음 → .data (Flash 이미지 + RAM 사본) */
#define PTR_CONST                       /* ← 실수: 포인터 배열 자체가 쓰기 가능 → .data */
#else
#define LUT_CONST const                 /* → .rodata (Flash 만) */
#define PTR_CONST const
#endif

#define LUT_N 4096u
LUT_CONST uint16_t sine_lut[LUT_N] = {  /* 4096 × 2 B = 8192 B */
#include "lut_gen.h"
};

/* "const 가 두 개 필요한" 고전: 문자열은 .rodata 에 있어도, 포인터 배열은 PTR_CONST 가 있어야 .rodata */
const char *PTR_CONST cmd_names[4] = { "help", "led", "baud", "reset" };

/* 초기값이 있는 전역 → .data */
typedef struct { uint32_t baud; uint16_t dac_rate_hz; uint8_t led_pin; uint8_t flags; } cfg_t;
cfg_t g_cfg = { 115200u, 48000u, 5u, 0x01u };
char g_fw_name[16] = "BM-B05 demo";
uint32_t g_boot_count = 1u;

/* 0 으로 초기화되는 전역 → .bss (명시적 "= 0" 도 .bss) */
#ifndef RX_BUF_SIZE
#define RX_BUF_SIZE 1024u                /* run_vm.sh overflow: 120 KB 로 키워 ld 에러를 본다 */
#endif
uint8_t  g_rx_buf[RX_BUF_SIZE];
uint16_t g_dac_dma_buf[1024];           /* 2 KB 더블 버퍼 */
uint32_t g_ticks;
uint32_t g_err_count = 0;

/* ---- 최소 startup (B07/B08 에서 자세히) ---- */
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
int main(void);
void Reset_Handler(void);
void Default_Handler(void);

void Default_Handler(void) { for (;;) { } }

__attribute__((section(".isr_vector"), used))
const uintptr_t g_vectors[16] = {
    (uintptr_t)&_estack,                /* [0] 초기 MSP */
    (uintptr_t)Reset_Handler,           /* [1] Reset (Thumb 비트는 링커가 붙인다) */
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, (uintptr_t)Default_Handler,
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, 0, 0, 0, 0,
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler, 0,
    (uintptr_t)Default_Handler, (uintptr_t)Default_Handler,
};

void Reset_Handler(void) {
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;      /* .data: Flash(LMA) → RAM(VMA) 복사 */
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0u; /* .bss: 0 으로 */
    int rc = main();
    /* semihosting SYS_EXIT_EXTENDED (0x20): QEMU 가 rc 를 프로세스 종료 코드로 돌려준다 */
    uint32_t blk[2] = { 0x20026u, (uint32_t)rc };
    register uint32_t r0 __asm__("r0") = 0x20u;
    register uint32_t *r1 __asm__("r1") = blk;
    __asm__ volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
    for (;;) { }
}

static uint32_t sum_lut(void) {
    uint32_t s = 0u;
    for (uint32_t i = 0u; i < LUT_N; i++) s += sine_lut[i];
    return s;
}

int main(void) {
    uint32_t s = sum_lut();
    for (uint32_t i = 0u; i < 1024u; i++) g_dac_dma_buf[i] = sine_lut[i * 4u];
    g_rx_buf[0] = (uint8_t)cmd_names[g_boot_count][0];
    g_ticks += g_cfg.baud;
    /* 기대값: 사인 4096 점 합 = 2048 × 4096 (대칭이라 반올림 오차가 상쇄) */
    if (s != 2048u * LUT_N) g_err_count++;
    if (g_fw_name[0] != 'B' || g_rx_buf[0] != 'l' || g_ticks != 115200u) g_err_count++;
    return (int)g_err_count;
}
