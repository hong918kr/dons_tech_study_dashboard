/* ENV: qemu */
// ARM-B13 demo — IT 블록 규칙 위반을 어셈블러가 잡는가? 케이스마다 -DCASE=n 으로 따로 어셈블.
//   run_qemu.sh itrules   (arm-none-eabi-gcc 14.2 = GNU as)
        .syntax unified
        .thumb
        .text
        .global case
        .thumb_func
case:
#if CASE == 1           // IT 없이 조건 명령
        cmp     r0, r1
        movgt   r0, r1
#elif CASE == 2         // 조건이 T/E 패턴과 다름: ite gt 의 두 번째는 le 여야 한다
        cmp     r0, r1
        ite     gt
        movgt   r0, r1
        movgt   r0, r2
#elif CASE == 3         // IT 는 1개만 덮는데 조건 명령이 2개
        cmp     r0, r1
        it      gt
        movgt   r0, r1
        addgt   r0, r0, #1
#elif CASE == 4         // 분기가 IT 블록의 마지막이 아님
        cmp     r0, #0
        itt     eq
        beq     1f
        moveq   r0, #1
1:
#elif CASE == 5         // CBZ 는 IT 블록 안에 둘 수 없다
        cmp     r1, #0
        it      eq
        cbz     r0, 1f
1:
#elif CASE == 6         // IT 는 최대 4개 (it + T/E 글자 3개까지)
        cmp     r0, #0
        ittttt  eq
        moveq   r0, #1
        moveq   r0, #1
        moveq   r0, #1
        moveq   r0, #1
        moveq   r0, #1
#elif CASE == 7         // 합법 (v7-M) — 그러나 ARMv8-A AArch32 에서는 폐기(deprecated) 형식
        cmp     r0, r1
        itt     gt
        movgt   r0, r1                  // 2개 이상의 명령
        addgt   r0, r0, #300            // 32비트 명령
#endif
        bx      lr
