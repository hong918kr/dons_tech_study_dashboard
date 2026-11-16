// ARM-A10 demo_bti_pads.S — 착지 패드(landing pad) 종류별 함수 6개 + 간접 분기 트램펄린 3개.
// 각 패드 함수는 w0 = 고유 번호를 돌려준다. 어느 분기 종류(BTYPE)로 들어왔을 때 BTI 예외(SIGILL)가 나는지 본다.
#ifdef __APPLE__
#define SYM(x) _##x
#define FTYPE(x)
#else
#define SYM(x) x
#define FTYPE(x) .type x, %function
#endif
        .text
        .arch_extension pauth
        .globl  SYM(pad_none)
        FTYPE(pad_none)
        .p2align 2
SYM(pad_none):                   // 패드 없음: 첫 명령이 평범한 mov
        mov     w0, #10
        ret
        .globl  SYM(pad_bti)
        FTYPE(pad_bti)
        .p2align 2
SYM(pad_bti):                    // bti (타깃 없음) — 어떤 간접 분기도 못 받는다
        hint    #32              // = bti
        mov     w0, #11
        ret
        .globl  SYM(pad_c)
        FTYPE(pad_c)
        .p2align 2
SYM(pad_c):                      // bti c — 호출(call)용
        hint    #34              // = bti c
        mov     w0, #12
        ret
        .globl  SYM(pad_j)
        FTYPE(pad_j)
        .p2align 2
SYM(pad_j):                      // bti j — 점프(jump)용
        hint    #36              // = bti j
        mov     w0, #13
        ret
        .globl  SYM(pad_jc)
        FTYPE(pad_jc)
        .p2align 2
SYM(pad_jc):                     // bti jc — 둘 다
        hint    #38              // = bti jc
        mov     w0, #14
        ret
        .globl  SYM(pad_pac)
        FTYPE(pad_pac)
        .p2align 2
SYM(pad_pac):                    // paciasp 로 시작 (컴파일러가 bti c 대신 쓰는 모양)
        hint    #25              // = paciasp
        mov     w0, #15
        hint    #29              // = autiasp
        ret

// 트램펄린: x0 = 목적지. 각각 다른 간접 분기로 들어가고, 목적지는 ret 로 돌아온다(ret = BTYPE 00, 검사 없음).
        .globl  SYM(via_blr)
        FTYPE(via_blr)
        .p2align 2
SYM(via_blr):                    // blr      → BTYPE 10
        hint    #34
        stp     x29, x30, [sp, #-16]!
        mov     x29, sp
        blr     x0
        ldp     x29, x30, [sp], #16
        ret
        .globl  SYM(via_br16)
        FTYPE(via_br16)
        .p2align 2
SYM(via_br16):                   // br x16   → BTYPE 01
        hint    #34
        stp     x29, x30, [sp, #-16]!
        mov     x29, sp
        mov     x16, x0
        adr     x30, 1f
        br      x16
1:      ldp     x29, x30, [sp], #16
        ret
        .globl  SYM(via_br9)
        FTYPE(via_br9)
        .p2align 2
SYM(via_br9):                    // br x9    → BTYPE 11 (가드된 페이지에서, x16/x17 이 아닌 레지스터)
        hint    #34
        stp     x29, x30, [sp, #-16]!
        mov     x29, sp
        mov     x9, x0
        adr     x30, 2f
        br      x9
2:      ldp     x29, x30, [sp], #16
        ret

#if !defined(__APPLE__) && defined(WITH_NOTE)
// GNU property note: 이 오브젝트도 BTI(1)|PAC(2) 를 지원한다고 링커에게 알린다.
// 없으면 링커가 실행 파일 전체의 BTI 속성을 AND 로 떨어뜨린다.
        .section .note.gnu.property, "a"
        .p2align 3
        .word   4                // n_namesz ("GNU\0")
        .word   16               // n_descsz
        .word   5                // NT_GNU_PROPERTY_TYPE_0
        .asciz  "GNU"
        .word   0xc0000000       // GNU_PROPERTY_AARCH64_FEATURE_1_AND
        .word   4                // pr_datasz
        .word   3                // BTI | PAC
        .word   0                // 8바이트 정렬 패딩
#endif
#ifndef __APPLE__
        .section .note.GNU-stack, "", %progbits
#endif
