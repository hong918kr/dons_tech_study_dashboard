/* ARM-B11 demo: FMOV 즉시값은 8비트(부호 1 + 지수 3 + 가수 4) → ±(16..31)/16 × 2^(-3..4) 만 된다
 *   cc -c demo_fmov_imm.S -o /tmp/ARM-B11_fi.o && objdump -d /tmp/ARM-B11_fi.o
 *   cc -c -DBAD demo_fmov_imm.S -o /dev/null          # 0.1, 32.0, 0.1171875 → 어셈블러 에러
 *   (#0.0 은 LLVM 이 fmov s0, wzr 로 바꿔 준다 — imm8 로는 0 을 못 만든다)
 */
    .text
    .p2align 2
fmov_ok:
    fmov    s0, #1.0            // imm8 = 0x70
    fmov    s0, #0.125          // 가장 작은 양수 = 2^-3
    fmov    s0, #31.0           // 가장 큰 값 = 31/16 * 2^4
    fmov    s0, #-1.5
    fmov    d0, #2.5            // double 도 같은 imm8 (확장 규칙만 다름)
    fmov    s0, wzr             // 0.0 은 즉시값이 아니라 wzr 로
    fmov    s0, #0.0            // LLVM 별칭: 위와 같은 명령(1e2703e0)으로 어셈블됨
    ret
#ifdef BAD
    fmov    s0, #0.1            // 2진 유한소수가 아님
    fmov    s0, #32.0           // 지수 범위 밖
    fmov    s0, #0.1171875      // 15/128: 가수 4비트로는 되지만 지수 2^-4 가 범위 밖
#endif
