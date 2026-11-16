/* ENV: cross — check.sh 가 arm-none-eabi-gcc 로 링크만 한다 (실행/QEMU 없음, 부팅은 BM-B08). */
/* TOOL-M01 데모 펌웨어: 링커 스크립트가 무엇을 어디에 두는지 보기 위한 최소 Cortex-M4 이미지.
 *   .isr_vector : g_vectors (벡터 표 16칸 — [0] 초기 SP = _estack, [1] Reset_Handler)
 *   .text       : Reset_Handler / main / 핸들러 / 도우미 함수
 *   .rodata     : k_crc_table, k_banner (const → FLASH 에만)
 *   .data       : g_boot_count, g_cal_offset, g_fw_tag (초기값 → FLASH 사본(LMA) + RAM(VMA))
 *   .bss        : g_tick, g_rx_buf (0 → RAM 만, 파일에 바이트 없음)
 * -DPRACTICE   : 9. 추가 연습용 .noinit / .ccmram 변수 추가
 */
#include <stddef.h>
#include <stdint.h>

/* 링커 스크립트가 만드는 심볼. "값" 이 아니라 "주소" 만 의미가 있다 → 배열로 선언해 &를 안 쓴다. */
extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];

/* ---- .data : 0 이 아닌 초기값 ---- */
char g_fw_tag[] = "fw-1.4.2"; /* 9 바이트: 4 의 배수가 아니다 */
uint32_t g_boot_count = 7;
int32_t g_cal_offset = -42;

/* ---- .bss : 0 / 초기값 없음 ---- */
uint8_t g_rx_buf[37]; /* 37 바이트: 4 의 배수가 아니다 */
uint32_t g_tick;

/* ---- .rodata : const ---- */
const uint16_t k_crc_table[16] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
};
const char k_banner[] = "TOOL-M01 linker script demo";

#ifdef PRACTICE
/* 9. 추가 연습: 리셋을 넘어 살아남는 RAM (0 으로 지우지 않는다) + CCM RAM 버퍼 */
__attribute__((section(".noinit"))) uint32_t g_reset_reason;
__attribute__((section(".noinit"))) uint32_t g_crash_pc;
__attribute__((section(".ccmram"))) uint8_t g_dsp_buf[4096];
#endif

static uint16_t crc16_nibble(uint16_t crc, uint8_t b) {
    crc = (uint16_t)((crc << 4) ^ k_crc_table[((crc >> 12) ^ (b >> 4)) & 0xF]);
    crc = (uint16_t)((crc << 4) ^ k_crc_table[((crc >> 12) ^ (b & 0xF)) & 0xF]);
    return crc;
}

/* 아무도 부르지 않는 함수: --gc-sections 가 지워야 한다 (--print-gc-sections 로 확인) */
uint32_t unused_debug_dump(void) {
    uint32_t s = 0;
    for (size_t i = 0; i < sizeof g_rx_buf; i++) s += g_rx_buf[i];
    return s;
}

int main(void) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; k_banner[i] != '\0'; i++) crc = crc16_nibble(crc, (uint8_t)k_banner[i]);
    for (size_t i = 0; g_fw_tag[i] != '\0'; i++) crc = crc16_nibble(crc, (uint8_t)g_fw_tag[i]);
    g_boot_count++;
    for (;;) {
        g_tick++;
        g_rx_buf[g_tick % sizeof g_rx_buf] = (uint8_t)(crc + (uint16_t)g_cal_offset + g_boot_count);
#ifdef PRACTICE
        g_dsp_buf[g_tick % sizeof g_dsp_buf] = (uint8_t)(g_reset_reason + g_crash_pc);
#endif
    }
}

/* C 로 쓴 스타트업 (BM-B08 과 같은 일): .data 복사(LMA → VMA) → .bss 0 → main */
void Reset_Handler(void) {
    const uint32_t *src = _sidata; /* FLASH 에 있는 초기값 사본 = LOADADDR(.data) */
    uint32_t *dst = _sdata;        /* RAM 에서의 실행 주소       = ADDR(.data)     */
    while (dst < _edata) *dst++ = *src++;
    for (dst = _sbss; dst < _ebss;) *dst++ = 0u;
    (void)main();
    for (;;) {
    }
}

void Default_Handler(void) {
    for (;;) {
    }
}
void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));

/* 벡터 표: 코어가 리셋 때 [0] → MSP, [1] → PC 로 읽는다. 아무 코드도 이 배열을 참조하지 않는다! */
__attribute__((section(".isr_vector"), used)) const uintptr_t g_vectors[16] = {
    (uintptr_t)_estack,           /* 0  초기 MSP            */
    (uintptr_t)Reset_Handler,     /* 1  Reset (Thumb bit=1) */
    (uintptr_t)NMI_Handler,       /* 2  NMI                 */
    (uintptr_t)HardFault_Handler, /* 3  HardFault           */
    0, 0, 0, 0, 0, 0, 0,          /* 4..10                  */
    (uintptr_t)Default_Handler,   /* 11 SVCall              */
    0, 0,                         /* 12..13                 */
    (uintptr_t)Default_Handler,   /* 14 PendSV              */
    (uintptr_t)SysTick_Handler,   /* 15 SysTick             */
};
