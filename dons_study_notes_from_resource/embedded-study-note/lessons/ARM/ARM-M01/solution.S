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
    add     x0, x0, x1                  // a0 + a1
    add     x2, x2, x3
    add     x4, x4, x5
    add     x6, x6, x7
    ldp     x9, x10, [sp]               // x9 = a8, x10 = a9 (스택 인자 둘)
    add     x0, x0, x2
    add     x4, x4, x6
    add     x9, x9, x10
    add     x0, x0, x4
    add     x0, x0, x9                  // 합 (mod 2^64)
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
    ldrsb   x9, [sp, #MS_C]             // c: 1 B → 64비트 부호 확장
    ldrsh   x10, [sp, #MS_S]            // s: 2 B
    ldrsw   x11, [sp, #MS_I]            // i: 4 B
    ldr     x12, [sp, #MS_L]            // l: 8 B
    add     x0, x7, x9                  // a7 + c
    add     x10, x10, x11               // s + i
    add     x0, x0, x12                 // + l
    add     x0, x0, x10
    ret
    FSIZE(SYM(mixed_stack))

/* ---------------- Part A: qr16 divmod_pair(int64_t a, int64_t b) — 16 B 구조체 → x0 = q, x1 = r ----------------
 * SDIV 는 0 쪽으로 자르고(C 와 같음), b == 0 → 0, INT64_MIN / -1 → INT64_MIN (트랩 없음, ARM-B07).
 */
    .globl  SYM(divmod_pair)
    FTYPE(SYM(divmod_pair))
    .p2align 2
SYM(divmod_pair):
    sdiv    x2, x0, x1                  // q = a / b
    msub    x1, x2, x1, x0              // r = a - q*b   (구조체 두 번째 8 B → x1)
    mov     x0, x2                      // 첫 번째 8 B → x0
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
    str     x0, [x8]                    // ts
    stp     w1, w2, [x8, #8]            // ax, ay
    lsr     w9, w1, #31                 // bit0 = ax 의 부호 비트
    lsr     w10, w2, #31
    orr     w9, w9, w10, lsl #1         // bit1 = ay < 0
    lsr     w10, w3, #31
    orr     w9, w9, w10, lsl #2         // bit2 = az < 0
    stp     w3, w9, [x8, #16]           // az, flags
    add     x10, x0, w1, sxtw           // ts + ax (부호 확장)
    add     x10, x10, w2, sxtw
    add     x10, x10, w3, sxtw
    str     x10, [x8, #24]              // sum
    ret                                 // x0 에 아무것도 안 돌려줘도 된다 (호출자는 버퍼를 읽는다)
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
    fmul    s16, s0, s4                 // w = aw*bw
    fmsub   s16, s1, s5, s16            //     - ax*bx
    fmsub   s16, s2, s6, s16            //     - ay*by
    fmsub   s16, s3, s7, s16            //     - az*bz
    fmul    s17, s0, s5                 // x = aw*bx
    fmadd   s17, s1, s4, s17            //     + ax*bw
    fmadd   s17, s2, s7, s17            //     + ay*bz
    fmsub   s17, s3, s6, s17            //     - az*by
    fmul    s18, s0, s6                 // y = aw*by
    fmsub   s18, s1, s7, s18            //     - ax*bz
    fmadd   s18, s2, s4, s18            //     + ay*bw
    fmadd   s18, s3, s5, s18            //     + az*bx
    fmul    s19, s0, s7                 // z = aw*bz
    fmadd   s19, s1, s6, s19            //     + ax*by
    fmsub   s19, s2, s5, s19            //     - ay*bx
    fmadd   s19, s3, s4, s19            //     + az*bw
    fmov    s0, s16                     // HFA 반환: 멤버 하나에 레지스터 하나
    fmov    s1, s17
    fmov    s2, s18
    fmov    s3, s19
    ret
    FSIZE(SYM(quat_mul))

/* ---------------- Part A/C: float f5_sum(f5 v) — float 5개 = HFA 아님 + 20 B > 16 → 복사본 포인터 x0 ----------------
 * 반환 float 은 s0. C 의 ((((v0+v1)+v2)+v3)+v4) 순서 그대로 더한다.
 */
    .globl  SYM(f5_sum)
    FTYPE(SYM(f5_sum))
    .p2align 2
SYM(f5_sum):
    ldp     s0, s1, [x0]                // v[0], v[1]  (x0 = 호출자가 만든 복사본)
    fadd    s0, s0, s1
    ldp     s1, s2, [x0, #8]            // v[2], v[3]
    fadd    s0, s0, s1
    fadd    s0, s0, s2
    ldr     s1, [x0, #16]               // v[4]
    fadd    s0, s0, s1
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
    stp     x29, x30, [sp, #-176]!      // 프레임 확보 + 프레임 레코드
    mov     x29, sp
    stp     x19, x20, [sp, #16]         // 우리도 피호출자: 호출자의 callee-saved 보관
    stp     x21, x22, [sp, #32]
    stp     x23, x24, [sp, #48]
    stp     x25, x26, [sp, #64]
    stp     x27, x28, [sp, #80]
    stp     d8, d9, [sp, #96]
    stp     d10, d11, [sp, #112]
    stp     d12, d13, [sp, #128]
    stp     d14, d15, [sp, #144]
    stp     x2, x29, [sp, #160]         // r 포인터, 자기 주소 표지
    sub     sp, sp, #16                 // 나가는 스택 인자 16 B (sp 는 계속 16 정렬)
    mov     x16, x0                     // x16 = fn
    mov     x17, x1                     // x17 = a
    ldp     x9, x10, [x17, #72]
    stp     x9, x10, [sp]               // fn 이 보는 [sp], [sp+8]
    ldp     d0, d1, [x17, #88]          // FP 인자 d0..d7
    ldp     d2, d3, [x17, #104]
    ldp     d4, d5, [x17, #120]
    ldp     d6, d7, [x17, #136]
    SETX    x19, 19
    SETX    x20, 20
    SETX    x21, 21
    SETX    x22, 22
    SETX    x23, 23
    SETX    x24, 24
    SETX    x25, 25
    SETX    x26, 26
    SETX    x27, 27
    SETX    x28, 28
    SETD    8
    SETD    9
    SETD    10
    SETD    11
    SETD    12
    SETD    13
    SETD    14
    SETD    15
    ldr     x8, [x17, #64]              // 간접 결과 주소
    ldp     x6, x7, [x17, #48]          // 정수 인자 (x17 을 마지막까지 쓰므로 x0/x1 은 맨 끝)
    ldp     x4, x5, [x17, #32]
    ldp     x2, x3, [x17, #16]
    ldp     x0, x1, [x17]
    blr     x16                         // fn(...)
    mov     x12, #0                     // 결과 마스크
    add     x9, sp, #16                 // sp 가 맞다면 여기가 우리 프레임
    cmp     x9, x29
    b.eq    3f                          // sp, x29 일치 → 둘 다 정상
    ldr     x10, [x9, #168]             // sp 기준으로 표지 확인
    cmp     x10, x9
    b.ne    2f
    orr     x12, x12, #(1 << 19)        // sp 는 맞고 x29 가 깨짐
    mov     x29, x9                     // x29 복구
    b       3f
2:  ldr     x10, [x29, #168]            // x29 기준으로 표지 확인
    cmp     x10, x29
    b.ne    9f                          // 둘 다 깨짐 → 복구 불가
    orr     x12, x12, #(1 << 18)        // x29 는 맞고 sp 가 깨짐
    sub     sp, x29, #16                // sp 복구
3:  ldr     x9, [x29, #160]             // r
    stp     x0, x1, [x9]                // r->x0, r->x1
    stp     d0, d1, [x9, #16]           // r->d[0..3]
    stp     d2, d3, [x9, #32]
    CHKX    x19, 19
    CHKX    x20, 20
    CHKX    x21, 21
    CHKX    x22, 22
    CHKX    x23, 23
    CHKX    x24, 24
    CHKX    x25, 25
    CHKX    x26, 26
    CHKX    x27, 27
    CHKX    x28, 28
    CHKD    8
    CHKD    9
    CHKD    10
    CHKD    11
    CHKD    12
    CHKD    13
    CHKD    14
    CHKD    15
    mov     w0, w12                     // 반환 = 마스크
    mov     sp, x29                     // 나가는 인자 영역 해제
    ldp     d8, d9, [sp, #96]           // 호출자의 callee-saved 복원
    ldp     d10, d11, [sp, #112]
    ldp     d12, d13, [sp, #128]
    ldp     d14, d15, [sp, #144]
    ldp     x19, x20, [sp, #16]
    ldp     x21, x22, [sp, #32]
    ldp     x23, x24, [sp, #48]
    ldp     x25, x26, [sp, #64]
    ldp     x27, x28, [sp, #80]
    ldp     x29, x30, [sp], #176
    ret
9:  brk     #0xabc                      // sp 와 x29 가 둘 다 깨졌다: 계속하면 더 큰 사고
    FSIZE(SYM(abi_call_check))
