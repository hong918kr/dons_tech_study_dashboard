/* ARM-B06 추가 연습 — AArch64 asm 답 (모두 leaf)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o /tmp/ARM-B06_p && /tmp/ARM-B06_p
 */
#ifdef __APPLE__
    .macro FUNC name
    .text
    .globl _\name
    .p2align 2
_\name:
    .endm
    .macro ENDF name
    .endm
#else
    .macro FUNC name
    .text
    .global \name
    .type \name, %function
    .p2align 2
\name:
    .endm
    .macro ENDF name
    .size \name, . - \name
    .endm
#endif

// 연습 1. int ctz64(uint64_t x)   가장 낮은 1비트 위치, x==0 → 64.  CTZ 명령 없이: RBIT + CLZ
FUNC ctz64
    rbit    x0, x0                  // 가장 낮은 비트가 가장 높은 자리로
    clz     x0, x0                  // 앞쪽 0의 개수 = 원래 뒤쪽 0의 개수 (x==0 → 64)
    ret
ENDF ctz64

// 연습 1b. 같은 일을 FEAT_CSSC 의 CTZ 한 개로 (M6 는 지원, 지원 안 하는 CPU 에선 SIGILL → 하네스가 확인 후 호출)
    .arch_extension cssc
FUNC ctz64_cssc
    ctz     x0, x0
    ret
ENDF ctz64_cssc

// 연습 2. uint64_t field_insert_var(uint64_t reg, unsigned lsb, unsigned width, uint64_t val)
//         실행 중 정해지는 위치/폭의 FIELD_REPLACE (BFI 는 즉시값만 받는다)
FUNC field_insert_var
    neg     w4, w2                  // -width (mod 64 = 64-width)
    mov     x5, #-1
    lsr     x5, x5, x4              // m = 하위 width 비트 1 (width=64 → 전부 1)
    and     x3, x3, x5              // val 을 폭에 맞게 자름
    lsl     x5, x5, x1              // m << lsb  (필드 자리 마스크)
    lsl     x3, x3, x1              // val << lsb
    bic     x0, x0, x5              // 필드 지우고
    orr     x0, x0, x3              // 새 값 넣기
    ret
ENDF field_insert_var

// 연습 3. uint32_t rotl32(uint32_t x, unsigned n)   AArch64 에는 ROL 이 없다 → ROR by -n
FUNC rotl32
    neg     w1, w1                  // 왼쪽 n = 오른쪽 (32-n) mod 32
    ror     w0, w0, w1              // RORV: 회전 양 = w1 mod 32
    ret
ENDF rotl32

// 연습 4. uint64_t nibrev64(uint64_t x)   0x0123456789ABCDEF → 0xFEDCBA9876543210
FUNC nibrev64
    rev     x0, x0                  // 8바이트 역순
    and     x1, x0, #0x0f0f0f0f0f0f0f0f
    lsr     x0, x0, #4
    and     x0, x0, #0x0f0f0f0f0f0f0f0f
    orr     x0, x0, x1, lsl #4      // 바이트마다 니블 교환
    ret
ENDF nibrev64

// 연습 5. int cls32(int32_t x)   부호 비트 바로 아래부터 부호와 같은 비트 수 (정규화 시프트 양)
FUNC cls32
    cls     w0, w0                  // cls(0) = cls(-1) = 31, cls(INT32_MIN) = 0
    ret
ENDF cls32

// 연습 6a. uint32_t log_pack(uint32_t word, uint32_t stat)   word[7:0] ← stat[15:8] (ERRCNT), 나머지 보존
FUNC log_pack
    bfxil   w0, w1, #8, #8          // BFXIL: 원본의 [lsb+w-1:lsb] 를 목적지 하위 w 비트에
    ret
ENDF log_pack

// 연습 6b. uint32_t irq_mask_all_except(uint32_t imr, uint32_t keep)   imr | ~keep  (1 = masked)
FUNC irq_mask_all_except
    orn     w0, w0, w1              // OR NOT 한 명령
    ret
ENDF irq_mask_all_except
