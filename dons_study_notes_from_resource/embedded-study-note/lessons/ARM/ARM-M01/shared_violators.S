/* ARM-M01 shared_violators.S — 검사기의 시험 대상 (주어진 코드, TODO 아님). starter/solution/practice 모두에 링크.
 * 착한 함수 3개 (마스크 0 이어야 함) + 일부러 규약을 어긴 함수 7개 + Apple 에서 틀리는 "표준 배치" asm 1개.
 * 모든 함수는 x18 을 건드리지 않는다. 위반 함수도 "돌아올 수는 있게" 만들었다 (검사기가 복구한다).
 */
#ifdef __APPLE__
#define SYM(x) _##x
#define FTYPE(x)
#define FSIZE(x)
#else
#define SYM(x) x
#define FTYPE(x) .type x, %function
#define FSIZE(x) .size x, .-x
#endif
    .macro FUNC name
    .globl  SYM(\name)
    FTYPE(SYM(\name))
    .p2align 2
SYM(\name):
    .endm

    .text

// 착함: x0 = x0 + x1. 그 대신 caller-saved 를 전부 망가뜨린다 (x1..x17, v0..v7, v16..v31, v8..v15 의 상위 64비트)
FUNC vio_good_leaf
    add     x0, x0, x1
    mov     x9, #0xdead
    .irp r, 1,2,3,4,5,6,7,8,10,11,12,13,14,15,16,17
    mov     x\r, x9                     // x1..x17 (x18 제외) — 호출자가 기대하면 안 되는 값들
    .endr
    .irp r, 0,1,2,3,4,5,6,7,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    movi    v\r\().16b, #0xa5           // v0..v7, v16..v31 전체 128비트 — 전부 caller-saved
    .endr
    .irp r, 8,9,10,11,12,13,14,15
    mov     v\r\().d[1], x9             // 상위 64비트만 덮음: d8..d15 (하위) 는 그대로 → 합법
    .endr
    ret
    FSIZE(SYM(vio_good_leaf))

// 착함: v8 의 상위 64비트만 바꾼다 → 위반 아님 (AAPCS64: 하위 64비트만 callee-saved)
FUNC vio_v8_upper
    mov     x9, #0x1234
    mov     v8.d[1], x9
    ret
    FSIZE(SYM(vio_v8_upper))

// 착함: x19, x20, d8 을 쓰지만 저장/복원한다. 반환 x0 = x0 * 19 + x1
FUNC vio_good_saver
    stp     x29, x30, [sp, #-48]!
    mov     x29, sp
    stp     x19, x20, [sp, #16]
    str     d8, [sp, #32]
    mov     x19, #19
    mov     x20, x1
    fmov    d8, x19                     // d8 을 임시로 사용
    fmov    x9, d8
    madd    x0, x0, x9, x20             // x0*19 + x1
    ldr     d8, [sp, #32]
    ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp], #48
    ret
    FSIZE(SYM(vio_good_saver))

// 위반: x19 를 저장 없이 덮음
FUNC vio_x19
    mov     x19, #0
    ret
    FSIZE(SYM(vio_x19))

// 위반: d9 (하위 64비트) 를 덮음
FUNC vio_d9
    fmov    d9, xzr
    ret
    FSIZE(SYM(vio_d9))

// 위반: s12 만 썼다 — 하지만 스칼라 FP 쓰기는 레지스터 나머지 비트를 0 으로 만든다 → d12 도 바뀜
FUNC vio_s12
    fmov    s12, #1.0
    ret
    FSIZE(SYM(vio_s12))

// 위반: 경계 두 개 (마지막 x, 마지막 d)
FUNC vio_x28_d15
    add     x28, x28, #1
    fmov    d15, #2.0
    ret
    FSIZE(SYM(vio_x28_d15))

// 위반: 저장은 했는데 복원 순서를 바꿈 (stp x19, x20 / ldp x20, x19) — 리뷰에서 잘 안 보이는 버그
FUNC vio_swap_restore
    stp     x29, x30, [sp, #-32]!
    mov     x29, sp
    stp     x19, x20, [sp, #16]
    mov     x19, #1
    mov     x20, #2
    ldp     x20, x19, [sp, #16]         // 버그: 순서 뒤바뀜
    ldp     x29, x30, [sp], #32
    ret
    FSIZE(SYM(vio_swap_restore))

// 위반: sp 를 16 내려놓고 복귀 (에필로그에서 add 를 빠뜨린 꼴)
FUNC vio_sp
    sub     sp, sp, #16
    ret
    FSIZE(SYM(vio_sp))

// 위반: x29 를 망가뜨리고 복귀 (프레임 레코드 체인이 끊긴다)
FUNC vio_fp
    mov     x29, #0x1230
    ret
    FSIZE(SYM(vio_fp))

// 규약 보존은 지키지만 "표준 AAPCS64 배치(8 B 슬롯)" 로 스택 인자를 읽는 mixed_stack — Apple 에선 틀린 값
// long vio_mixed_aapcs(long a0..a7, signed char c, short s, int i, long l)
FUNC vio_mixed_aapcs
    ldrsb   x9, [sp]                    // c  (표준: [sp+0])
    ldrsh   x10, [sp, #8]               // s  (표준: [sp+8])  Apple 은 [sp+2]
    ldrsw   x11, [sp, #16]              // i  (표준: [sp+16]) Apple 은 [sp+4]
    ldr     x12, [sp, #24]              // l  (표준: [sp+24]) Apple 은 [sp+8]
    add     x0, x7, x9
    add     x10, x10, x11
    add     x0, x0, x12
    add     x0, x0, x10
    ret
    FSIZE(SYM(vio_mixed_aapcs))
