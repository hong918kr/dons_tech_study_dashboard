/* ENV: qemu */
/* ARM-M09 solution.c — C 로 쓰는 TODO 세 개 + 테스트 하네스(harness.c) 포함.
 * 빌드: run_qemu.sh solution  →  solution.c + solution.S + shared_startup.c  (an385, 그리고 -DHAS_FPU 로 an386)
 */
#include "harness.c"

/* ---- TODO 1: EXC_RETURN 디코더 (ARMv7-M, 보안 확장 없음) ----
 * 유효: [31:5] 전부 1, [1:0] = 01. bit4 FType(0 = 확장 FP 프레임), bit3 Mode(1 = Thread), bit2 SPSEL(1 = PSP).
 * Handler 로 돌아가면서 PSP 는 불가능, FPU 없는 코어에서 FType = 0 도 불가능. 유효하면 1, 아니면 0.
 */
int decode_exc_return(uint32_t exc_return, int has_fpu, struct exc_info *out) {
    if ((exc_return & 0xFFFFFFE3u) != 0xFFFFFFE1u) return 0;   /* 고정 비트 검사 (bit4..2 만 자유) */
    uint32_t thread = (exc_return >> 3) & 1u;
    uint32_t psp = (exc_return >> 2) & 1u;
    uint32_t fp = ((exc_return >> 4) & 1u) ^ 1u;
    if (!thread && psp) return 0;                              /* Handler 모드는 항상 MSP */
    if (fp && !has_fpu) return 0;                              /* FP 확장 없는 코어 */
    out->handler_mode = (uint8_t)!thread;
    out->use_psp = (uint8_t)psp;
    out->fp_frame = (uint8_t)fp;
    out->frame_words = (uint8_t)(fp ? 26u : 8u);
    return 1;
}

/* ---- TODO 2: 예외 직전의 sp 를 프레임에서 되살린다 ----
 * sp_before = 프레임 주소 + (8 또는 26) 워드 + (스택된 xPSR bit9 ? 4 : 0)
 */
uint32_t pre_exception_sp(const uint32_t *frame, uint32_t exc_return) {
    uint32_t words = (exc_return & 0x10u) ? 8u : 26u;
    uint32_t sp = (uint32_t)(uintptr_t)frame + words * 4u;
    if (frame[7] & (1u << 9)) sp += 4u;                        /* 진입 때 8 정렬하느라 넣은 패딩 */
    return sp;
}

/* ---- TODO 3: SVC 디스패처 ----
 * 번호 = 스택된 PC 바로 앞 16비트 명령(0xDFnn)의 하위 바이트. 결과는 스택된 r0 (frame[0]) 에 쓴다.
 * 0 ADD: r0+r1 / 1 XOR4: r0^r1^r2^r3 / 2 TICKS: g_ticks / 3 WAIT: wait_tick_change(r0) / 그 밖: 0xFFFFFFFF.
 * 반환값 = SVC 번호.
 */
uint32_t svc_dispatch(uint32_t *frame) {
    const uint8_t *pc = (const uint8_t *)(uintptr_t)frame[6];
    uint32_t num = pc[-2];                                     /* 리틀엔디언: 하위 바이트가 먼저 */
    switch (num) {
    case SVC_ADD:   frame[0] = frame[0] + frame[1]; break;
    case SVC_XOR4:  frame[0] = frame[0] ^ frame[1] ^ frame[2] ^ frame[3]; break;
    case SVC_TICKS: frame[0] = g_ticks; break;
    case SVC_WAIT:  frame[0] = wait_tick_change(frame[0]); break;
    default:        frame[0] = SVC_ENOSYS; break;
    }
    return num;
}
