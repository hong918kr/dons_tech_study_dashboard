// ARM-A10 shared_ref.S — 디코더 검증용 "진짜 어셈블러가 만든" 명령 워드 표.
// 실행하지 않는다: C 하네스가 ref_words 부터 ref_words_end 까지를 데이터로 읽어 decode() 결과를 기대값과 비교한다.
// (ARM-B03 shared_ref.S 와 같은 방식: 텍스트 섹션의 워드를 런타임에 읽기)
#ifdef __APPLE__
#define SYM(x) _##x
#else
#define SYM(x) x
#endif
        .text
        .arch_extension pauth
        .globl  SYM(ref_words)
        .globl  SYM(ref_words_end)
        .p2align 2
SYM(ref_words):
        paciasp                         //  0 PACIASP  (= hint #25)
        pacibsp                         //  1 PACIBSP  (= hint #27)
        autiasp                         //  2 AUTIASP  (= hint #29)
        autibsp                         //  3 AUTIBSP  (= hint #31)
        bti                             //  4 BTI      (= hint #32)
        bti     c                       //  5 BTI c    (= hint #34)
        bti     j                       //  6 BTI j    (= hint #36)
        bti     jc                      //  7 BTI jc   (= hint #38)
        hint    #39                     //  8 PACM (FEAT_PAuth_LR)
        retaa                           //  9
        retab                           // 10
        ret                             // 11 ret x30
        ret     x1                      // 12
        br      x16                     // 13
        br      x9                      // 14
        blr     x20                     // 15
        braa    x16, x17                // 16
        braaz   x9                      // 17
        blraaz  x20                     // 18
        blrab   x3, sp                  // 19
        bl      1f                      // 20 +4 words 앞으로
        b       1f                      // 21 +3
        b.ne    1f                      // 22 +2
        cbz     x0, 1f                  // 23 +1
1:      tbnz    w0, #31, 0f             // 24 +2 (0: 은 아래 26 번)
        stp     x29, x30, [sp, #-16]!   // 25 LR 저장 (pre-index)
0:      ldp     x29, x30, [sp], #16     // 26 LR 복원 (post-index)
        str     x30, [sp, #-16]!        // 27 LR 저장 (str pre)
        ldr     x30, [sp], #16          // 28 LR 복원 (ldr post)
        str     x30, [sp, #8]           // 29 LR 저장 (unsigned offset)
        ldr     x30, [x29, #8]          // 30 LR 복원
        stp     x19, x20, [sp, #16]     // 31 LR 아님
        stp     x30, x19, [sp, #-32]!   // 32 LR 저장 (Rt = x30)
        paciaz                          // 33 다른 변형(제로 modifier): 이 린터는 OTHER 로 본다
        xpaclri                         // 34 OTHER
        nop                             // 35 OTHER
        eret                            // 36 OTHER (bit23=1)
        add     x0, x0, #1              // 37 OTHER
        b       .                       // 38 자기 자신으로 (off 0)
SYM(ref_words_end):
        nop
#ifndef __APPLE__
        .section .note.GNU-stack, "", %progbits
#endif
