@ ARCH-B01 demo (추가 연습 3): Thumb 16비트 B (T2 인코딩, imm11) 를 실제 어셈블러가 어떻게 채우나
@ build: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -c demo_thumb_branch.s -o /tmp/esn_ARCH-B01/t.o
@        xcrun llvm-objdump -d /tmp/esn_ARCH-B01/t.o
@ 오프셋 = target - (이 명령의 주소 + 4). Thumb 은 PC 가 "현재 명령 + 4" 로 읽힌다.
        .syntax unified
        .thumb
        .text
back:   nop                     @ 0x0
        b.n     back            @ 0x2: target 0x0 → 0x0 - (0x2+4) = -6
        b.n     fwd             @ 0x4: target 0xa → 0xa - (0x4+4) = +2
        nop                     @ 0x6
        nop                     @ 0x8
fwd:    b.n     fwd             @ 0xa: 자기 자신 → -4
