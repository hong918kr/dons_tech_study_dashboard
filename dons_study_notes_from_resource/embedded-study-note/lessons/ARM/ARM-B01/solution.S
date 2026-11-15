// ARM-B01 첫 AArch64 함수: C에서 호출하는 .S 파일 — Apple arm64 (Mach-O)
//
// 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 harness.c <starter|solution>.S shared_hostile.S -o /tmp/ARM-B01_x
//
// 호출 규약 한 줄 요약 (AAPCS64 + Apple):
//   정수/포인터 인자 → x0..x7 (int 는 w0..w7: 아래 32비트만 의미 있음)
//   반환값 → x0 (int 는 w0),  복귀 주소 → x30 (lr),  ret = x30 으로 점프
//   x9..x15 는 마음대로 써도 되는 임시(caller-saved), x19..x28 은 쓰면 복원 의무(callee-saved)
//   Apple: C 이름 foo 의 심볼은 _foo, x18 은 플랫폼 예약 → 절대 쓰지 않는다.
// 이 파일의 함수는 전부 leaf (다른 함수를 안 부름) → 스택 프레임 불필요 (프레임은 ARM-B08).

    .text

// ======================= Part A =======================

// long add3(long a, long b, long c)          a=x0  b=x1  c=x2  →  x0
    .globl  _add3
    .p2align 2
_add3:
    add     x0, x0, x1              // x0 = a + b      (64비트, 넘치면 wrap. 플래그는 안 바꿈)
    add     x0, x0, x2              // x0 = a + b + c
    ret                             // br x30 → 호출자의 bl 다음 명령으로

// long max_long(long a, long b)              부호 있는(signed) 비교
    .globl  _max_long
    .p2align 2
_max_long:
    cmp     x0, x1                  // a - b 를 계산해 NZCV 만 설정 (결과는 버림)
    csel    x0, x0, x1, gt          // signed a > b 면 a, 아니면 b   (gt = 부호 있는 '>')
    ret

// long sum_array(const long *p, size_t n)    p=x0  n=x1  →  x0
    .globl  _sum_array
    .p2align 2
_sum_array:
    mov     x2, #0                  // acc = 0   (x2 는 인자로 안 쓰였으니 임시로 사용)
    cbz     x1, 2f                  // n == 0 → 메모리를 한 번도 읽지 않고 끝
1:  ldr     x3, [x0], #8            // x3 = *p,  그 다음 p += 8  (post-index, 자세히는 ARM-B04)
    add     x2, x2, x3              // acc += x3
    subs    x1, x1, #1              // n -= 1, 결과가 0 이면 Z=1
    b.ne    1b                      // Z == 0 (n != 0) 이면 다시
2:  mov     x0, x2                  // 반환값은 x0 에
    ret

// ======================= Part B =======================

// int max_int(int a, int b)                  a=w0  b=w1  (x0/x1 의 [63:32] 는 unspecified)
    .globl  _max_int
    .p2align 2
_max_int:
    cmp     w0, w1                  // 32비트 비교 → 윗 32비트 쓰레기는 아예 안 본다
    csel    w0, w0, w1, gt          // w 레지스터에 쓰면 x0[63:32] 는 0 이 된다
    ret

// long add_wide(long a, int b)               = a + (long)b    a=x0  b=w1
    .globl  _add_wide
    .p2align 2
_add_wide:
    add     x0, x0, w1, sxtw        // w1 을 64비트로 부호 확장(sxtw)한 뒤 더한다
    ret

// ======================= Part C =======================

// void regdump_capture(long a0, ..., long a6, struct regdump *d)
//   a0..a6 = x0..x6,  d = x7 (8번째 정수 인자 = 마지막 인자 레지스터)
//   struct regdump { uint64_t x[7]; uint64_t fp, lr, sp; }  → 오프셋 0,8,..,48 / 56 / 64 / 72
    .globl  _regdump_capture
    .p2align 2
_regdump_capture:
    str     x0, [x7]                // d->x[0] = a0
    str     x1, [x7, #8]            // d->x[1] = a1
    str     x2, [x7, #16]           // d->x[2]
    str     x3, [x7, #24]           // d->x[3]
    str     x4, [x7, #32]           // d->x[4]
    str     x5, [x7, #40]           // d->x[5]
    str     x6, [x7, #48]           // d->x[6]
    str     x29, [x7, #56]          // d->fp: 우리는 프레임을 안 만들었으니 x29 = 호출자의 frame record
    str     x30, [x7, #64]          // d->lr: 호출자의 bl 바로 다음 주소
    mov     x9, sp                  // str 의 Rt 자리에서 31번은 xzr 이라 sp 를 직접 못 쓴다 → 복사
    str     x9, [x7, #72]           // d->sp
    ret
