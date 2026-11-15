/* ARM-M01 AAPCS64 심화: 많은 인자·구조체 반환·callee-saved — AArch64 asm (Apple Mach-O + Linux ELF 겸용)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_violators.S -o t && ./t
 *
 * 규칙 요약 (AAPCS64 2026Q3 + Apple "Writing ARM64 code for Apple platforms"):
 *  - 정수/포인터 인자 x0..x7 (NGRN), FP/HFA 인자 v0..v7 (NSRN) — 두 카운터는 따로 센다.
 *  - 그 다음은 스택 (NSAA = 호출 순간 sp). 표준: 8 B 미만도 8 B 슬롯. Apple: 자연 크기·정렬로 채움.
 *  - 16 B 이하 구조체는 x0/x1 로 반환, 그보다 크면 호출자가 버퍼 주소를 x8 로 준다 (x8 보존 의무 없음).
 *  - HFA (같은 FP 타입 1~4개) 는 s/d 레지스터 하나씩. 16 B 넘는 비-HFA 인자는 복사본 포인터로.
 *  - callee-saved: x19..x28, x29, sp, d8..d15 (v8..v15 의 하위 64비트만). x18 은 Apple 예약 → 안 쓴다.
 */
#ifdef __APPLE__
#define SYM(x) _##x
#define FTYPE(x)
#define FSIZE(x)
#define MS_C 0          /* mixed_stack 의 스택 인자 오프셋: Apple = 자연 정렬로 압축 */
#define MS_S 2
#define MS_I 4
#define MS_L 8
#else
#define SYM(x) x
#define FTYPE(x) .type x, %function
#define FSIZE(x) .size x, .-x
#define MS_C 0          /* 표준 AAPCS64 = 인자마다 8 B 슬롯 (C.16) */
#define MS_S 8
#define MS_I 16
#define MS_L 24
#endif

    .text

/* ---------------- Part A: long sum10(long a0, …, long a9) ----------------
 * a0..a7 = x0..x7, a8 = [sp], a9 = [sp+8]  (진입 순간의 sp = 호출자가 채운 NSAA)
 * leaf 라 프레임 없음 → sp 를 안 건드리므로 [sp] 가 그대로 인자 영역이다.
 */
    .globl  SYM(sum10)
    FTYPE(SYM(sum10))
    .p2align 2
SYM(sum10):
    // TODO: a0..a7 = x0..x7 더하고, a8 = [sp], a9 = [sp+8] 을 ldp 로 읽어 더한다.
    mov     x0, #0                      // 스텁: 틀린 값, 안전하게 복귀
    ret
    FSIZE(SYM(sum10))

/* ---------------- Part A: long mixed_stack(long a0..a7, signed char c, short s, int i, long l) ----------------
 * 반환 a7 + c + s + i + l. c, s, i, l 은 전부 스택. 오프셋은 위 MS_* (Apple 0/2/4/8, 표준 0/8/16/24).
 * 스택의 char/short/int 는 그 크기만큼만 써 있다 → 반드시 ldrsb/ldrsh/ldrsw 로 부호 확장해서 읽는다.
 */
    .globl  SYM(mixed_stack)
    FTYPE(SYM(mixed_stack))
    .p2align 2
SYM(mixed_stack):
    // TODO: c/s/i/l 을 [sp, #MS_C/S/I/L] 에서 ldrsb/ldrsh/ldrsw/ldr 로 읽고 a7 + c + s + i + l.
    mov     x0, #0                      // 스텁
    ret
    FSIZE(SYM(mixed_stack))

/* ---------------- Part A: qr16 divmod_pair(int64_t a, int64_t b) — 16 B 구조체 → x0 = q, x1 = r ----------------
 * SDIV 는 0 쪽으로 자르고(C 와 같음), b == 0 → 0, INT64_MIN / -1 → INT64_MIN (트랩 없음, ARM-B07).
 */
    .globl  SYM(divmod_pair)
    FTYPE(SYM(divmod_pair))
    .p2align 2
SYM(divmod_pair):
    // TODO: sdiv 로 q, msub 로 r = a - q*b. x0 = q, x1 = r.
    mov     x0, #0                      // 스텁: {0, 0}
    mov     x1, #0
    ret
    FSIZE(SYM(divmod_pair))

/* ---------------- Part A/C: imu_sample make_imu(uint64_t ts, int32_t ax, int32_t ay, int32_t az) ----------------
 * 32 B 구조체 → 호출자가 버퍼를 잡고 주소를 x8 로 준다. 우리는 [x8] 에 쓰기만 한다.
 * 레이아웃: +0 ts | +8 ax | +12 ay | +16 az | +20 flags (bit k = 축 k < 0) | +24 sum = ts+ax+ay+az
 * int32 인자는 w1..w3 만 믿는다 (x 의 윗 32비트는 unspecified) → 더할 땐 sxtw.
 */
    .globl  SYM(make_imu)
    FTYPE(SYM(make_imu))
    .p2align 2
SYM(make_imu):
    // TODO: [x8] 에 ts, ax, ay, az, flags(부호 비트 3개), sum(sxtw 로 더하기) 를 쓴다.
    stp     xzr, xzr, [x8]              // 스텁: 버퍼를 0 으로 (틀리지만 안전한 값)
    stp     xzr, xzr, [x8, #16]
    ret
    FSIZE(SYM(make_imu))

/* ---------------- Part A/C: quat quat_mul(quat a, quat b) — HFA 두 개 in, HFA 하나 out ----------------
 * a = {s0,s1,s2,s3} (w,x,y,z), b = {s4,s5,s6,s7}, 결과 → s0..s3.
 * 임시는 v16..v19 (caller-saved) → d8..d15 를 안 쓰므로 저장할 것도 없다.
 * FMADD Sd,Sn,Sm,Sa = Sa + Sn*Sm,  FMSUB Sd,Sn,Sm,Sa = Sa - Sn*Sm.
 */
    .globl  SYM(quat_mul)
    FTYPE(SYM(quat_mul))
    .p2align 2
SYM(quat_mul):
    // TODO: 해밀턴 곱. a = s0..s3, b = s4..s7, 임시는 s16..s19, 결과를 s0..s3 로.
    movi    d0, #0                      // 스텁: {0,0,0,0}
    movi    d1, #0
    movi    d2, #0
    movi    d3, #0
    ret
    FSIZE(SYM(quat_mul))

/* ---------------- Part A/C: float f5_sum(f5 v) — float 5개 = HFA 아님 + 20 B > 16 → 복사본 포인터 x0 ----------------
 * 반환 float 은 s0. C 의 ((((v0+v1)+v2)+v3)+v4) 순서 그대로 더한다.
 */
    .globl  SYM(f5_sum)
    FTYPE(SYM(f5_sum))
    .p2align 2
SYM(f5_sum):
    // TODO: x0 이 가리키는 복사본에서 v[0..4] 를 순서대로 더해 s0 으로.
    movi    d0, #0                      // 스텁: 0.0f
    ret
    FSIZE(SYM(f5_sum))

/* ---------------- Part B: uint32_t abi_call_check(const void *fn, const abi_args *a, abi_ret *r) ----------------
 * x19..x28, d8..d15 에 센티널을 심고 a 의 값으로 x0..x8, d0..d7, 스택 인자 16 B 를 채워 fn 을 부른다.
 * 돌아오면 r = {x0, x1, d0..d3} 를 적고, 바뀐 callee-saved 를 마스크로 돌려준다:
 *   bit 0..9 = x19..x28,  bit 10..17 = d8..d15 (하위 64비트만 비교),  bit 18 = sp,  bit 19 = x29
 * 센티널: x(19+k) = 0xC5C5_0000_0000_00nn (nn = 레지스터 번호),  d(8+k) = 0x5EED_0000_0000_00nn
 *
 * 프레임 176 B (+ 나가는 스택 인자 16 B):
 *   [x29+0] 호출자 x29 | +8 LR | +16..+95 호출자 x19..x28 | +96..+159 호출자 d8..d15
 *   +160 r 포인터 | +168 자기 주소(= x29) "표지" — sp/x29 중 어느 쪽이 깨졌는지 가려낼 때 쓴다
 *   [x29-16], [x29-8] = fn 이 보는 [sp], [sp+8]
 */
    .macro SETX reg, n
    movz    \reg, #0xC5C5, lsl #48
    movk    \reg, #\n
    .endm
    .macro SETD n
    movz    x9, #0x5EED, lsl #48
    movk    x9, #\n
    fmov    d\n, x9                     // d 쓰기 (상위 64비트는 0 이 된다 — 상관없음)
    .endm
    .macro CHKX reg, n
    movz    x10, #0xC5C5, lsl #48
    movk    x10, #\n
    cmp     \reg, x10
    cset    x11, ne                     // 바뀌었으면 1
    orr     x12, x12, x11, lsl #(\n - 19)
    .endm
    .macro CHKD n
    movz    x10, #0x5EED, lsl #48
    movk    x10, #\n
    fmov    x13, d\n                    // 하위 64비트만 꺼낸다 (상위는 caller 몫)
    cmp     x13, x10
    cset    x11, ne
    orr     x12, x12, x11, lsl #(\n - 8 + 10)
    .endm

    .globl  SYM(abi_call_check)
    FTYPE(SYM(abi_call_check))
    .p2align 2
SYM(abi_call_check):
    // TODO: 프레임 176 B (+16 B 나가는 인자), x19..x28 / d8..d15 저장, 센티널(SETX/SETD),
    //       a 에서 x0..x8, d0..d7, [sp]/[sp+8] 채우기, blr, sp/x29 확인·복구, r 에 결과, CHKX/CHKD, 복원.
    movn    w0, #0                      // 스텁: 0xFFFFFFFF ("전부 깨짐") — r 은 안 건드림
    ret
    FSIZE(SYM(abi_call_check))
