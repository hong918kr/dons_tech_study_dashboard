// ARCH-B01 demo: AArch64 B/BL 의 imm26 필드를 실제 어셈블러가 어떻게 채우나
// build: cc -c demo_branch.s -o /tmp/esn_ARCH-B01/br.o
//        xcrun llvm-objdump -d --stop-address=0x20 /tmp/esn_ARCH-B01/br.o
//        xcrun llvm-objdump -d --start-address=0x100018 /tmp/esn_ARCH-B01/br.o | tail -3
// 오프셋 = target - (이 명령의 주소). AArch64 는 PC 자체 기준 (Thumb 의 PC+4 와 다르다).
        .text
        .p2align 2
        .globl _branches
_branches:
back:   nop                     // 0x00
        b       back            // 0x04: 오프셋 -4
        bl      back            // 0x08: 오프셋 -8
        b       fwd             // 0x0c: 오프셋 +8
        nop                     // 0x10
fwd:    b       fwd             // 0x14: 오프셋 0 (자기 자신)
        b       far             // 0x18: 오프셋 +0x100000 (far = 0x100018)
        b       back            // 0x1c: 오프셋 -0x1c
        .space  0x100000 - 0x20 + 0x18   // far 가 0x18 + 0x100000 에 오도록
far:    ret
