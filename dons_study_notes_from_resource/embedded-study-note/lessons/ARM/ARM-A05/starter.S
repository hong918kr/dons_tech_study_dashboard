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
    // TODO(Part A-1): 누산기 v0..v3 = 0, kb 번 { A 16B, B 16B 로드, sdot by-element x4 }, 4행 저장 (ldc*4 바이트 간격)
    // 스텁: C 를 건드리지 않고 돌아간다 (ABI 안전, 테스트는 FAIL)
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
    // TODO(Part A-2): 누산기 v16..v31 = 0, kb 번 { A 64B, B 64B 로드, smmla x16 }, zip1/zip2 .2d 로 8행 저장
    // 스텁: C 를 건드리지 않고 돌아간다
    ret
    FSIZE(smmla_8x8_kernel)
