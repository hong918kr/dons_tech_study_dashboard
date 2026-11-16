/* ARM-A04 골든 테이블 — "진짜 어셈블러" 가 만든 명령어 워드 (모든 변형에 링크된다)
 *
 * 이 파일의 명령은 실행하지 않는다. C 테스트가 ref_insns[] 를 uint32_t 배열로 읽어서
 * 손으로 만든 인코더 결과와 비교한다 (코드 섹션 읽기는 허용된다: __TEXT 는 r-x).
 * 분기 목표는 ". + off" (현재 위치 기준 상대값) 로 써서 재배치(relocation)가 생기지 않게 했다.
 * 같은 내용을 /usr/bin/objdump -d 로 본 결과가 solution.c 의 golden 표 word 열이다.
 */
#ifdef __APPLE__
#define SYM(x) _##x
#else
#define SYM(x) x
#endif

    .text
    .p2align 2
    .globl SYM(ref_insns)
SYM(ref_insns):
    add  x0, x0, #1                // 0  ADD (immediate)
    add  x1, x2, #4095             // 1
    add  x3, x4, #1, lsl #12       // 2  sh=1 → 0x1000
    add  x5, x6, #4095, lsl #12    // 3
    add  sp, sp, #16               // 4  Rd/Rn = 31 은 ADD imm 에서 SP
    add  x0, sp, #0                // 5  = mov x0, sp (별칭)
    movz x9, #0xbeef               // 6  MOVZ
    movz x9, #0xdead, lsl #16      // 7
    movk x9, #0x1234, lsl #48      // 8  MOVK
    movk x17, #0xffff, lsl #32     // 9
    ret                            // 10 RET (x30)
    ret  x16                       // 11
    b    . + 0x40                  // 12 B: imm26 × 4
    b    . - 8                     // 13
    b    . + 0x7fffffc             // 14 최대 +128 MB − 4
    b    . - 0x8000000             // 15 최소 −128 MB
    bl   . + 0x1000                // 16 BL
    bl   . - 4                     // 17
    cbz  x3, . + 0x20              // 18 CBZ (64-bit): imm19 × 4
    cbz  x0, . - 0x100000          // 19 최소 −1 MB
    cbz  x17, . + 0xffffc          // 20 최대 +1 MB − 4
    mul  x0, x0, x9                // 21 = MADD x0, x0, x9, xzr  (Part C)
    add  x0, x0, x10               // 22 ADD (shifted register)  (Part C)
    asr  x0, x0, #5                // 23 = SBFM x0, x0, #5, #63  (Part C)
    asr  x7, x3, #63               // 24
    // ---- 디코더가 "모름" 으로 거절해야 하는 이웃 인코딩 ----
    b.ne . + 8                     // 25 B.cond (연습 1)
    cbnz x1, . - 4                 // 26 CBNZ  (연습 1)
    movn x2, #0                    // 27 MOVN  (MOVZ 와 opc 만 다름)
    .arch_extension memtag
    addg x0, x1, #16, #2           // 28 ADDG: ADD imm 과 bit 23 만 다름 (FEAT_MTE)
    sub  x0, x0, #1                // 29 SUB imm: bit 30 만 다름
    add  w0, w0, #1                // 30 32-bit ADD: sf(bit 31) = 0
    .arch_extension pauth
    retaa                          // 31 PAC 리턴 (ARM-A10)
    br   x16                       // 32 BR (연습 2 veneer)
    .globl SYM(ref_insns_end)
SYM(ref_insns_end):
