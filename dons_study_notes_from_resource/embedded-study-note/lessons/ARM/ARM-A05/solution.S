/* ARM-A05 int8 GEMM 마이크로커널 — 손 asm: SDOT 4x4 타일, SMMLA 8x8 타일 (맥 M-칩 네이티브, ELF 도 같은 파일)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c solution.S shared_bugs.S -o /tmp/ARM-A05_v && /tmp/ARM-A05_v
 *       (starter: 같은 명령에 starter.c starter.S)
 * 공통 시그니처 (AAPCS64: x0..x4):
 *   void kern(const int8_t *Ap, const int8_t *Bp, size_t kb, int32_t *C, size_t ldc)
 *   Ap = 패킹된 A 패널 (MR 행), Bp = 패킹된 B 패널 (NR 열), kb = k-블록 수 (0 가능 → C 타일 = 0),
 *   C = 타일 왼쪽 위, ldc = C 의 행 간격 (원소 수). 타일 전체 MRxNR 를 덮어쓴다 (누적 아님).
 * 레지스터: v0..v7, v16..v31 만 쓴다. v8..v15 하위 64비트는 callee-saved → 안 쓰면 저장할 필요가 없다.
 * SDOT = FEAT_DotProd, SMMLA = FEAT_I8MM. 플래그 없이 빌드되도록 이 파일에서만 켠다.
 */
#ifdef __APPLE__
#define SYM(x) _##x
#define FTYPE(x)
#define FSIZE(x)
#else
#define SYM(x) x
#define FTYPE(x) .type x, %function
#define FSIZE(x) .size x, .-x
#endif

    .arch_extension dotprod
    .arch_extension i8mm
    .text

/* ============ Part A-1 — SDOT 4x4: 패킹 깊이 KU=4, 블록당 A 16 B + B 16 B, SDOT 4번 = MAC 64 ============
 * A 블록 v4.16b = [r0k0..r0k3 | r1k0..r1k3 | r2k0..r2k3 | r3k0..r3k3]
 * B 블록 v5.16b = [c0k0..c0k3 | c1k0..c1k3 | c2k0..c2k3 | c3k0..c3k3]
 * sdot vR.4s, v5.16b, v4.4b[R]  →  vR.s[j] += Σ_t B[c_j, k_t] · A[r_R, k_t]   (= C 의 R 행, 열 0..3)
 */
    .globl SYM(sdot_4x4_kernel)
    FTYPE(sdot_4x4_kernel)
    .p2align 4
SYM(sdot_4x4_kernel):
    movi    v0.16b, #0                  // C 행 0 누산기 (int32 x4) = 0
    movi    v1.16b, #0                  // 행 1
    movi    v2.16b, #0                  // 행 2
    movi    v3.16b, #0                  // 행 3
    cbz     x2, 2f                      // kb == 0 (K == 0) → 0 타일 저장
1:  ld1     {v4.16b}, [x0], #16         // A: 4행 x 4깊이, Ap += 16
    ld1     {v5.16b}, [x1], #16         // B: 4열 x 4깊이, Bp += 16
    sdot    v0.4s, v5.16b, v4.4b[0]     // 행 0 += B · A[행0, k..k+3] (by-element: A 4바이트를 4 lane 에 방송)
    sdot    v1.4s, v5.16b, v4.4b[1]     // 행 1
    sdot    v2.4s, v5.16b, v4.4b[2]     // 행 2
    sdot    v3.4s, v5.16b, v4.4b[3]     // 행 3
    subs    x2, x2, #1
    b.ne    1b
2:  lsl     x4, x4, #2                  // ldc (원소) → 바이트
    st1     {v0.4s}, [x3], x4           // C[0][0..3], x3 += ldc*4
    st1     {v1.4s}, [x3], x4           // C[1][0..3]
    st1     {v2.4s}, [x3], x4           // C[2][0..3]
    st1     {v3.4s}, [x3]               // C[3][0..3]
    ret
    FSIZE(sdot_4x4_kernel)

/* ============ Part A-2 — SMMLA 8x8: 패킹 깊이 KU=8, 블록당 A 64 B + B 64 B, SMMLA 16번 = MAC 512 ============
 * A 블록 v0..v3: v(p) = [행 2p 의 k0..k7 | 행 2p+1 의 k0..k7]   (2x8 행렬, 행 우선)
 * B 블록 v4..v7: v(4+q) = [열 2q 의 k0..k7 | 열 2q+1 의 k0..k7] (2x8 = B 의 전치 조각)
 * smmla acc.4s, A.16b, B.16b → acc += A(2x8) · B(2x8)^T = 2x2 int32, lane 순서 [r0c0, r0c1, r1c0, r1c1]
 * 누산기 acc[p][q] = v(16 + 4p + q) = [C(2p,2q) C(2p,2q+1) C(2p+1,2q) C(2p+1,2q+1)]
 * 저장: zip1/zip2 .2d 로 2x2 조각 두 개를 "행 2p 의 4열", "행 2p+1 의 4열" 로 풀어낸다.
 */
    .globl SYM(smmla_8x8_kernel)
    FTYPE(smmla_8x8_kernel)
    .p2align 4
SYM(smmla_8x8_kernel):
    movi    v16.16b, #0                 // acc[0][0]
    movi    v17.16b, #0                 // acc[0][1]
    movi    v18.16b, #0                 // acc[0][2]
    movi    v19.16b, #0                 // acc[0][3]
    movi    v20.16b, #0                 // acc[1][0]
    movi    v21.16b, #0
    movi    v22.16b, #0
    movi    v23.16b, #0
    movi    v24.16b, #0                 // acc[2][0]
    movi    v25.16b, #0
    movi    v26.16b, #0
    movi    v27.16b, #0
    movi    v28.16b, #0                 // acc[3][0]
    movi    v29.16b, #0
    movi    v30.16b, #0
    movi    v31.16b, #0                 // acc[3][3]
    cbz     x2, 2f
1:  ld1     {v0.16b-v3.16b}, [x0], #64  // A: 행쌍 4개 x 8깊이
    ld1     {v4.16b-v7.16b}, [x1], #64  // B: 열쌍 4개 x 8깊이
    smmla   v16.4s, v0.16b, v4.16b      // 행쌍 0 x 열쌍 0
    smmla   v17.4s, v0.16b, v5.16b      // 행쌍 0 x 열쌍 1
    smmla   v18.4s, v0.16b, v6.16b
    smmla   v19.4s, v0.16b, v7.16b
    smmla   v20.4s, v1.16b, v4.16b      // 행쌍 1
    smmla   v21.4s, v1.16b, v5.16b
    smmla   v22.4s, v1.16b, v6.16b
    smmla   v23.4s, v1.16b, v7.16b
    smmla   v24.4s, v2.16b, v4.16b      // 행쌍 2
    smmla   v25.4s, v2.16b, v5.16b
    smmla   v26.4s, v2.16b, v6.16b
    smmla   v27.4s, v2.16b, v7.16b
    smmla   v28.4s, v3.16b, v4.16b      // 행쌍 3
    smmla   v29.4s, v3.16b, v5.16b
    smmla   v30.4s, v3.16b, v6.16b
    smmla   v31.4s, v3.16b, v7.16b
    subs    x2, x2, #1
    b.ne    1b
2:  lsl     x4, x4, #2                  // ldc → 바이트
    zip1    v0.2d, v16.2d, v17.2d       // 행 0: C(0,0..3)
    zip1    v1.2d, v18.2d, v19.2d       // 행 0: C(0,4..7)
    zip2    v2.2d, v16.2d, v17.2d       // 행 1: C(1,0..3)
    zip2    v3.2d, v18.2d, v19.2d       // 행 1: C(1,4..7)
    st1     {v0.4s, v1.4s}, [x3], x4
    st1     {v2.4s, v3.4s}, [x3], x4
    zip1    v0.2d, v20.2d, v21.2d       // 행 2
    zip1    v1.2d, v22.2d, v23.2d
    zip2    v2.2d, v20.2d, v21.2d       // 행 3
    zip2    v3.2d, v22.2d, v23.2d
    st1     {v0.4s, v1.4s}, [x3], x4
    st1     {v2.4s, v3.4s}, [x3], x4
    zip1    v0.2d, v24.2d, v25.2d       // 행 4
    zip1    v1.2d, v26.2d, v27.2d
    zip2    v2.2d, v24.2d, v25.2d       // 행 5
    zip2    v3.2d, v26.2d, v27.2d
    st1     {v0.4s, v1.4s}, [x3], x4
    st1     {v2.4s, v3.4s}, [x3], x4
    zip1    v0.2d, v28.2d, v29.2d       // 행 6
    zip1    v1.2d, v30.2d, v31.2d
    zip2    v2.2d, v28.2d, v29.2d       // 행 7
    zip2    v3.2d, v30.2d, v31.2d
    st1     {v0.4s, v1.4s}, [x3], x4
    st1     {v2.4s, v3.4s}, [x3]
    ret
    FSIZE(smmla_8x8_kernel)
