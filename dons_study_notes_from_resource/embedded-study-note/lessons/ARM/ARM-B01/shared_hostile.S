// ARM-B01 shared_hostile.S — "적대적 호출자(hostile caller)" 트램펄린 (starter/solution/practice 공용)
//
//   int  call2_dirty_i(void (*fn)(void), uint64_t r0, uint64_t r1);
//   long call2_dirty_l(void (*fn)(void), uint64_t r0, uint64_t r1);
//
// x0/x1 을 64비트 "통째로" 채운 뒤 fn 으로 점프(꼬리 호출, tail call)한다.
// int 인자 자리의 윗 32비트에 쓰레기를 넣어서, ABI 가 "unspecified" 라고 한 비트를
// 실제로 쓰레기로 만든다 → asm 이 그 비트를 읽으면 테스트가 잡는다.
// Mach-O(macOS) 와 ELF(Linux) 둘 다에서 조립되도록 SYM() 매크로를 쓴다.
#ifdef __APPLE__
#define SYM(n) _##n
#else
#define SYM(n) n
#endif

    .text
    .globl SYM(call2_dirty_i)
    .globl SYM(call2_dirty_l)
#ifndef __APPLE__
    .type  SYM(call2_dirty_i), %function
    .type  SYM(call2_dirty_l), %function
#endif
    .p2align 2
SYM(call2_dirty_i):                 // 두 이름이 같은 코드를 가리킨다 (반환 타입만 C 쪽에서 다름)
SYM(call2_dirty_l):
    mov     x16, x0                 // x16 (IP0): 호출 경계에서 망가져도 되는 임시 레지스터
    mov     x0, x1                  // r0 → x0 (64비트 전체, 윗 32비트 쓰레기 포함)
    mov     x1, x2                  // r1 → x1
    br      x16                     // fn 으로 점프. x30(lr) 은 그대로 → fn 의 ret 이 우리 호출자로 바로 복귀

#ifndef __APPLE__
    .size  SYM(call2_dirty_i), . - SYM(call2_dirty_i)
    .section .note.GNU-stack,"",%progbits
#endif
