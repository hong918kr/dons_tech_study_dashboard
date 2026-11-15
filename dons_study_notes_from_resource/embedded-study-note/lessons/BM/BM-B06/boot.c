/* ENV: qemu */
/* BM-B06 부트로더 (빌드만 — 실행·점프는 BM-A03). arm-none-eabi-gcc -T boot.ld 로 BOOT 영역에 링크.
 * 하는 일: 공유 .noinit 에 부트 사유 기록 → SLOT_A 헤더 확인 → (BM-A03: VTOR/MSP 설정 후 점프)
 */
#include <stdint.h>
#include "shared.h"

volatile boot_share_t g_boot_share SHARED_NOINIT;       /* 앱의 같은 이름 변수와 같은 주소 (ld ASSERT) */

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
int main(void);
void Reset_Handler(void);
void Default_Handler(void);

__attribute__((section(".isr_vector"), used))
static void (*const g_vectors[16])(void) = {
    (void (*)(void))(uintptr_t)_estack,                  /* [0] 초기 MSP */
    Reset_Handler,                                       /* [1] 리셋 (Thumb 비트는 링커가 붙인다) */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    0, 0, 0, 0,
    Default_Handler, Default_Handler, 0, Default_Handler, Default_Handler,
};

#ifdef BLOAT   /* 일부러 BOOT(32 KB) 를 넘기는 빌드 — run_vm.sh bad 에서 ld 오류를 보인다 */
const uint8_t g_bloat[40 * 1024] = { 1 };
#endif

uint32_t g_boot_count_seed = 7u;                         /* .data 한 개 (LMA 는 BOOT, VMA 는 RAM) */

void Reset_Handler(void)
{
    uint32_t *s = _sidata, *d = _sdata;                  /* 최소 crt0 — 자세한 건 BM-B07/B08 */
    while (d < _edata) *d++ = *s++;
    for (d = _sbss; d < _ebss; ) *d++ = 0u;
    (void)main();
    for (;;) { }
}

void Default_Handler(void) { for (;;) { } }

static int slot_header_ok(const img_hdr_t *h, uint32_t slot)
{
    return h->magic == IMG_MAGIC && h->load_addr == slot &&
           h->vector_addr == slot + (uint32_t)(uintptr_t)__img_hdr_size &&
           h->img_size <= (uint32_t)(uintptr_t)__slot_size;
}

int main(void)
{
    volatile boot_share_t *sh = &g_boot_share;
    if (sh->magic != SHARE_MAGIC || sh->inv_magic != ~SHARE_MAGIC) {   /* 콜드 부트: RAM 은 쓰레기 */
        sh->magic = SHARE_MAGIC; sh->inv_magic = ~SHARE_MAGIC;
        sh->reason = REASON_COLD; sh->boot_count = g_boot_count_seed; sh->app_request = 0u;
    }
    sh->boot_count++;
    const uint32_t slot_a = (uint32_t)(uintptr_t)__slot_a_start;
    const img_hdr_t *h = (const img_hdr_t *)(uintptr_t)slot_a;
    if (!slot_header_ok(h, slot_a)) {
        sh->reason = REASON_BOOT_FAILED_B;
        for (;;) { }                                     /* 실제로는 복구 모드 (BM-A05) */
    }
    sh->active_slot = 0u;
    /* BM-A03: 인터럽트 끄기, SysTick 정지, VTOR = h->vector_addr, MSP = vec[0], PC = vec[1] */
    return 0;
}
