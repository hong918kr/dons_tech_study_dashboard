// ARM-B01 demo_intbug.S — int 인자를 x 레지스터로 읽는 "버그 판" + 실제 x0/x1 엿보기
// 빌드: cc -std=c11 -O2 demo_intbug.c demo_intbug.S shared_hostile.S -o /tmp/ARM-B01_intbug && /tmp/ARM-B01_intbug
    .text

// void raw_regs(int a, int b, uint64_t out[2])  — 들어온 x0/x1 64비트 전체를 그대로 기록
    .globl  _raw_regs
    .p2align 2
_raw_regs:
    str     x0, [x2]                // out[0] = x0 (윗 32비트 포함)
    str     x1, [x2, #8]            // out[1] = x1
    ret

// int max_int_bad(int a, int b)   버그: 64비트로 비교 → 윗 32비트를 본다
    .globl  _max_int_bad
    .p2align 2
_max_int_bad:
    cmp     x0, x1                  // BUG: cmp w0, w1 이어야 한다
    csel    w0, w0, w1, gt
    ret

// long add_wide_bad(long a, int b)  버그: b 를 확장하지 않고 64비트로 더한다
    .globl  _add_wide_bad
    .p2align 2
_add_wide_bad:
    add     x0, x0, x1              // BUG: add x0, x0, w1, sxtw 이어야 한다
    ret
