/* ENV: qemu */
/* BM-B06 앱 (빌드만). arm-none-eabi-gcc -T app.ld 로 SLOT_A 에 링크.
 * 슬롯 시작에 이미지 헤더, +0x200 에 벡터 테이블, 공유 .noinit 에서 부트 사유를 읽는다.
 */
#include <stdint.h>
#include "shared.h"

#ifdef TYPO_NOINIT   /* 섹션 이름 오타 → 링커 규칙에 안 걸리는 orphan 섹션 (run_vm.sh bad) */
volatile boot_share_t g_boot_share __attribute__((section(".noinit_shared")));
#else
volatile boot_share_t g_boot_share SHARED_NOINIT;        /* 부트로더의 같은 변수와 같은 주소 */
#endif

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
extern const uint8_t __app_image_size[];                 /* app.ld 가 계산한 절대 심볼 */
extern const uint8_t __slot_a_vectors[];                 /* layout.ld: 슬롯 A + 0x200 */
int main(void);
void Reset_Handler(void);
void Default_Handler(void);

#ifdef HDR_NOT_FIRST /* 헤더 섹션에 다른 데이터가 먼저 들어가는 실수 (run_vm.sh bad) */
__attribute__((section(".img_hdr"), used)) const uint32_t g_build_tag = 0x7A67u;
#endif

/* 이미지 헤더 — 필드 대부분을 링커가 채운다 (주소를 손으로 적지 않는다) */
__attribute__((section(".img_hdr"), used))
const img_hdr_t g_img_hdr = {
    .magic       = IMG_MAGIC,
    .hdr_size    = 0x200u,
    .hdr_version = IMG_HDR_VERSION,
    .load_addr   = (uint32_t)(uintptr_t)__slot_a_start,
    .vector_addr = (uint32_t)(uintptr_t)__slot_a_vectors,
    .img_size    = (uint32_t)(uintptr_t)__app_image_size,
    .fw_version  = (1u << 24) | (4u << 16) | 2u,          /* 1.4.2 */
    .crc32       = 0u,
};

__attribute__((section(".isr_vector"), used))
static void (*const g_vectors[16])(void) = {
    (void (*)(void))(uintptr_t)_estack,
    Reset_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    0, 0, 0, 0,
    Default_Handler, Default_Handler, 0, Default_Handler, Default_Handler,
};

const uint16_t g_sine_q15[64] = {                        /* .rodata (플래시에만) */
    0, 3212, 6393, 9512, 12540, 15447, 18205, 20788, 23170, 25330, 27246, 28899, 30274, 31357, 32138, 32610,
    32767, 32610, 32138, 31357, 30274, 28899, 27246, 25330, 23170, 20788, 18205, 15447, 12540, 9512, 6393, 3212,
};                                                       /* 뒤 32 개는 0 (음수 반주기는 부호 반전으로) */
uint32_t g_led_period_ms = 500u;                         /* .data (플래시 LMA + RAM VMA) */
uint32_t g_ticks;                                        /* .bss  (RAM 만) */

void Reset_Handler(void)
{
    uint32_t *s = _sidata, *d = _sdata;                  /* .noinit 은 여기서 건드리지 않는다 */
    while (d < _edata) *d++ = *s++;
    for (d = _sbss; d < _ebss; ) *d++ = 0u;
    (void)main();
    for (;;) { }
}

void Default_Handler(void) { for (;;) { } }

int main(void)
{
    volatile boot_share_t *sh = &g_boot_share;
    uint32_t why = (sh->magic == SHARE_MAGIC && sh->inv_magic == ~SHARE_MAGIC) ? sh->reason : 0xFFu;
    if (why == REASON_WDT) g_led_period_ms = 100u;       /* 워치독 리셋이었으면 빠르게 깜빡 */
    for (uint32_t i = 0; i < 64u; i++) g_ticks += g_sine_q15[(i + sh->boot_count) & 63u];
    sh->app_request = 0u;                                /* 업데이트가 필요하면 1 쓰고 리셋 (BM-A05/A06) */
    return (int)(g_ticks + g_led_period_ms);
}
