/* ENV: qemu */
/* ARM-M09 starter.c — C 로 쓰는 TODO 세 개 + 테스트 하네스(harness.c) 포함.
 * 빌드: run_qemu.sh starter  →  starter.c + starter.S + shared_startup.c  (an385, 그리고 -DHAS_FPU 로 an386)
 */
#include "harness.c"

/* ---- TODO 1: EXC_RETURN 디코더 (ARMv7-M, 보안 확장 없음) ----
 * 유효: [31:5] 전부 1, [1:0] = 01. bit4 FType(0 = 확장 FP 프레임), bit3 Mode(1 = Thread), bit2 SPSEL(1 = PSP).
 * Handler 로 돌아가면서 PSP 는 불가능, FPU 없는 코어에서 FType = 0 도 불가능. 유효하면 1, 아니면 0.
 */
int decode_exc_return(uint32_t exc_return, int has_fpu, struct exc_info *out) {
    /* TODO: 고정 비트 검사 */
    (void)exc_return;
    (void)has_fpu;
    /* TODO: Handler + PSP 거부 */
    /* TODO: FPU 없는데 FP 프레임 거부 */
    (void)out;
    /* TODO: out 채우기 (handler_mode, use_psp, fp_frame, frame_words) */


    return 0;
}

/* ---- TODO 2: 예외 직전의 sp 를 프레임에서 되살린다 ----
 * sp_before = 프레임 주소 + (8 또는 26) 워드 + (스택된 xPSR bit9 ? 4 : 0)
 */
uint32_t pre_exception_sp(const uint32_t *frame, uint32_t exc_return) {
    (void)exc_return;
    (void)frame;
    /* TODO: xPSR bit9 패딩 */
    return 0u;
}

/* ---- TODO 3: SVC 디스패처 ----
 * 번호 = 스택된 PC 바로 앞 16비트 명령(0xDFnn)의 하위 바이트. 결과는 스택된 r0 (frame[0]) 에 쓴다.
 * 0 ADD: r0+r1 / 1 XOR4: r0^r1^r2^r3 / 2 TICKS: g_ticks / 3 WAIT: wait_tick_change(r0) / 그 밖: 0xFFFFFFFF.
 * 반환값 = SVC 번호.
 */
uint32_t svc_dispatch(uint32_t *frame) {
    (void)frame;
    uint32_t num = 0xFFFFFFFFu;                                /* TODO: 스택된 PC 에서 번호 꺼내기 */
    switch (num) {
    /* TODO: case SVC_ADD / SVC_XOR4 / SVC_TICKS / SVC_WAIT */



    default:        break;                                     /* TODO: frame[0] = SVC_ENOSYS */
    }
    return num;
}
