// ARM-B01 demo_printf.S — asm 에서 printf(가변 인자) 부르기: Apple 방식 vs AAPCS64(Linux) 방식
// 빌드: cc -std=c11 -O2 demo_printf.c demo_printf.S -o /tmp/ARM-B01_printf && /tmp/ARM-B01_printf
// (printf 를 부르므로 leaf 가 아니다 → lr 을 저장하는 최소 프레임이 필요. 프레임 자체는 ARM-B08)
    .text

// void print2_apple(long a, long b)   Apple: 가변 인자는 전부 스택 [sp], [sp+8] ...
    .globl  _print2_apple
    .p2align 2
_print2_apple:
    sub     sp, sp, #32             // 32B: [sp+0..15] 가변 인자 2개, [sp+16..31] fp/lr 저장 (16B 정렬 유지)
    stp     x29, x30, [sp, #16]     // 호출하면 x30 이 덮이므로 먼저 저장
    add     x29, sp, #16            // frame record 연결 (Apple: x29 는 항상 유효한 frame record)
    stp     x0, x1, [sp]            // 가변 인자 a, b → [sp], [sp+8]  ← Apple 규칙의 핵심
    adrp    x0, Lfmt@PAGE           // x0 = 포맷 문자열 주소 (고정 인자는 여전히 레지스터)
    add     x0, x0, Lfmt@PAGEOFF    //      (ADRP/@PAGE 상세는 ARM-A09)
    bl      _printf
    ldp     x29, x30, [sp, #16]     // 복원
    add     sp, sp, #32
    ret

// void print2_linux_style(long a, long b)   AAPCS64 방식: 가변 인자도 x1, x2 → Apple 에선 틀림
    .globl  _print2_linux_style
    .p2align 2
_print2_linux_style:
    sub     sp, sp, #32
    stp     x29, x30, [sp, #16]
    add     x29, sp, #16
    mov     x2, x1                  // b → x2
    mov     x1, x0                  // a → x1   (Apple 의 printf 는 여기를 안 본다)
    mov     x9, #0x5A5A
    stp     x9, x9, [sp]            // 스택 슬롯엔 표지값 0x5A5A(=23130) → printf 가 이걸 읽는지 확인
    adrp    x0, Lfmt@PAGE
    add     x0, x0, Lfmt@PAGEOFF
    bl      _printf
    ldp     x29, x30, [sp, #16]
    add     sp, sp, #32
    ret

    .section __TEXT,__cstring,cstring_literals
Lfmt:
    .asciz  "  a=%ld b=%ld\n"
