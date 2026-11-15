/* ENV: qemu */
// ARM-B13 demo — 같은 명령이 IT 블록 밖/안에서 16비트(.n)/32비트(.w) 중 무엇이 되는가.
//   run_qemu.sh dis  → arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -c demo_encodings.S + objdump -d
// 규칙 (ARMv7-M ARM A6.2/A7.7): 16비트 데이터처리 명령은 IT 밖에서는 플래그를 쓰고(…s), IT 안에서는 안 쓴다.
// 그래서 "IT 밖의 add(플래그 X)" 와 "IT 안의 adds(플래그 O)" 는 32비트 인코딩이 필요하다.
        .syntax unified
        .thumb
        .text
        .global enc_outside
        .thumb_func
enc_outside:                            // IT 블록 밖
        adds    r0, r0, r1              // 플래그 O → 16비트
        add     r0, r1, r2              // 플래그 X, 3-레지스터 → 32비트 add.w
        add     r0, r0, r1              // 플래그 X, Rd==Rn → 16비트 T2 (high-reg 형식)
        rsbs    r0, r0, #0              // 16비트 (objdump: negs)
        rsb     r0, r0, #0              // 32비트 rsb.w
        movs    r0, #1                  // 16비트
        mov     r0, #1                  // 32비트 mov.w
        ldr     r3, [r0, #4]            // 16비트 (오프셋 0..124, 4의 배수)
        ldr     r3, [r0], #4            // post-index → 32비트만 존재 (ldr.w)
        adds.w  r0, r0, r1              // .w 로 32비트 강제
        bx      lr

        .global enc_inside
        .thumb_func
enc_inside:                             // IT 블록 안
        it      gt
        addgt   r0, r1, r2              // 플래그 X → 16비트 (밖에서는 adds 였을 인코딩)
        it      gt
        addsgt  r0, r1, r2              // 플래그 O → 32비트 adds.w
        it      gt
        rsbgt   r0, r0, #0              // 16비트 (밖에서는 negs 였을 인코딩)
        it      gt
        movgt   r0, #1                  // 16비트
        it      gt
        movsgt  r0, #1                  // 32비트
        it      gt
        movgt   r0, #300                // 즉시값 > 255 → 32비트
        bx      lr
