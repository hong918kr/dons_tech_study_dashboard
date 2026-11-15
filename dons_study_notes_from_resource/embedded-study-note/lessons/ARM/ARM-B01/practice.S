// ARM-B01 추가 연습 답 — Apple arm64 (Mach-O)
// 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_hostile.S -o /tmp/ARM-B01_practice

    .text

// 연습 1: long add8(long a0, ..., long a7)    인자 레지스터 8개 x0..x7 전부 사용
    .globl  _add8
    .p2align 2
_add8:
    add     x0, x0, x1              // 덧셈은 교환·결합 → 순서는 자유
    add     x2, x2, x3              // 두 줄기로 나눠 더하면 의존성 사슬이 짧아진다 (ILP)
    add     x4, x4, x5
    add     x6, x6, x7
    add     x0, x0, x2
    add     x4, x4, x6
    add     x0, x0, x4
    ret

// 연습 2: unsigned long min_ulong(unsigned long a, unsigned long b)   부호 없는 비교
    .globl  _min_ulong
    .p2align 2
_min_ulong:
    cmp     x0, x1                  // a - b → C 플래그 = (a >= b, 부호 없이)
    csel    x0, x0, x1, lo          // lo (= cc, C==0): a < b (unsigned) 이면 a
    ret

// 연습 3: uint32_t add_u8(uint8_t a, uint8_t b)   AAPCS64(Linux) 에서도 안전한 판
//   Apple: 호출자가 8비트 인자를 32비트로 확장해 준다 → add w0, w0, w1 만으로 충분
//   AAPCS64: w0[31:8] 이 unspecified → 피호출자가 직접 확장해야 한다
    .globl  _add_u8
    .p2align 2
_add_u8:
    and     w0, w0, #0xff           // a 를 0 확장 (uxtb w0, w0 와 같음)
    add     w0, w0, w1, uxtb        // b 는 확장 레지스터 피연산자로 0 확장하면서 더한다
    ret

// 연습 4: size_t count_eq(const long *p, size_t n, long key)
    .globl  _count_eq
    .p2align 2
_count_eq:
    mov     x3, #0                  // cnt = 0
    cbz     x1, 2f                  // n == 0 → 읽지 않음
1:  ldr     x4, [x0], #8            // x4 = *p++
    cmp     x4, x2                  // *p == key ?
    cinc    x3, x3, eq              // 같으면 cnt + 1 (분기 없음, 조건 명령은 ARM-B05)
    subs    x1, x1, #1
    b.ne    1b
2:  mov     x0, x3
    ret

// 연습 5: long sext_s32(int v) / uint64_t zext_u32(uint32_t v)
    .globl  _sext_s32
    .p2align 2
_sext_s32:
    sxtw    x0, w0                  // w0 의 비트 31 을 [63:32] 로 복사 (= sbfm x0, x0, #0, #31)
    ret

    .globl  _zext_u32
    .p2align 2
_zext_u32:
    mov     w0, w0                  // w 레지스터 쓰기 → [63:32] = 0. 이게 "공짜 0 확장"
    ret
