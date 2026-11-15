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
    // TODO: x0 = a + b + c  (add 두 번)
    mov     x0, #0                  // 스텁: 틀리지만 안전한 값
    ret                             // br x30 → 호출자의 bl 다음 명령으로

// long max_long(long a, long b)              부호 있는(signed) 비교
    .globl  _max_long
    .p2align 2
_max_long:
    // TODO: cmp + csel (부호 있는 조건 코드)
    mov     x0, #0                  // 스텁
    ret

// long sum_array(const long *p, size_t n)    p=x0  n=x1  →  x0
    .globl  _sum_array
    .p2align 2
_sum_array:
    // TODO: acc = 0, n == 0 이면 바로 끝, 아니면 ldr x, [x0], #8 로 읽으며 n 번 더하기
    mov     x0, #0                  // 스텁: 메모리를 읽지 않는다
    ret

// ======================= Part B =======================

// int max_int(int a, int b)                  a=w0  b=w1  (x0/x1 의 [63:32] 는 unspecified)
    .globl  _max_int
    .p2align 2
_max_int:
    // TODO: 32비트(w) 레지스터로 비교·선택
    mov     w0, #0                  // 스텁
    ret

// long add_wide(long a, int b)               = a + (long)b    a=x0  b=w1
    .globl  _add_wide
    .p2align 2
_add_wide:
    // TODO: b 를 64비트로 부호 확장해서 더하기 (확장 레지스터 피연산자 , sxtw)
    ret                             // 스텁: x0 = a 그대로

// ======================= Part C =======================

// void regdump_capture(long a0, ..., long a6, struct regdump *d)
//   a0..a6 = x0..x6,  d = x7 (8번째 정수 인자 = 마지막 인자 레지스터)
//   struct regdump { uint64_t x[7]; uint64_t fp, lr, sp; }  → 오프셋 0,8,..,48 / 56 / 64 / 72
    .globl  _regdump_capture
    .p2align 2
_regdump_capture:
    // TODO: x0..x6, x29, x30, sp 를 d (= x7) 의 오프셋 0..72 에 저장
    ret                             // 스텁: 아무것도 안 씀